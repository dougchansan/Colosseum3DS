#!/usr/bin/env python3
"""Prepare a DEVELOPER-ONLY field floor: original map, collision and Wes.

This generalises prepare_outskirt.py to further floors selected by an exact
profile. Its outputs contain original game data and belong in a private
directory (ignored ``private/`` or outside the repository). The packs are for
the labelled developer floor loader only: they carry no NPCs, scripts,
dialogue, doorways, story flags or save data, and they do not make the floor
reachable through the real story route.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

import prepare_title as title
from floor_profiles import geometry_floors
from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import PROFILES as COMMON_PROFILES, admit_archive, field_metadata, member, u32

# Exact admitted profiles live in floor_profiles.py, one row per floor. This
# converter takes the rows that carry the map geometry keys.
FLOORS = geometry_floors()
MAX_MAP_PACKS = 15  # C3_STAGE_MAX_MODELS (16) minus Wes.


def admit_floor(disc, profile):
    path = Path(disc) / "files" / profile["archive"]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != profile["bytes"]:
        raise ValueError("Unexpected original floor archive path or size: " + profile["archive"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != profile["sha256"]:
        raise ValueError("Floor archive differs from admitted GC6E01 revision 0: " + profile["archive"])
    return data


def schedule_frames(kind, end):
    if kind == "short":
        from prepare_field_geometry import sample_sequence
        return sample_sequence(end), 60
    # Sparse sampling of a slow linear sky; its loop is `end` source frames,
    # not GSmodelGetFrameCount's inclusive query (same rule as Outskirt).
    return [i * end / 1000 for i in range(1001)], 30000 / end


def prepare_floor_geometry(archive, field_common, profile, output):
    from prepare_field_geometry import (load_scene, Model, build_mesh, instance_meshes,
                                        prepare_pack_set, prepare_wes, fixed_lights, split_static,
                                        bake_mesh)
    data, raw, scene = load_scene(archive, profile["map"])
    if len(scene.models) != profile["models"] or len(scene.cameras) != 1 or scene.fogs:
        raise ValueError("Original floor scene topology changed")
    models = [Model(model, raw_model, allow_instances=True)
              for model, raw_model in zip(scene.models, raw.models)]
    camera = scene.cameras[0]
    fixed = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
             camera.near, camera.far, camera.aspect, 0.)
    lights = fixed_lights(raw, data)
    grouped = [index for indices, _ in profile["groups"] for index in indices]
    if sorted(grouped) != list(range(len(models))):
        raise ValueError("Floor pack groups must cover each original model once")
    reports, packs = [], 0
    for number, (indices, (kind, end)) in enumerate(profile["groups"]):
        frames, fps = schedule_frames(kind, end)
        meshes = []
        for index in indices:
            model = models[index]
            if model.tracks and any(e > end for e in model.ends[:1]):
                raise ValueError("Original map clip exceeds its reviewed clock")
            own = [build_mesh(model, i) for i in range(len(model.meshes))]
            frozen = profile.get("frozen_instance_targets", {}).get(index, ())
            for item in instance_meshes(model, own):
                target = item["descriptors"][0][2]
                if target in frozen:
                    baked = bake_mesh(item, 0, frames[0])
                    if baked is None:
                        raise ValueError("Frozen instance copy has a singular transform")
                    item = baked
                own.append(item)
            meshes += own
        static, animated = split_static(meshes, 0, frames)
        static = [bake_mesh(mesh, 0, frames[0]) or mesh for mesh in static]
        # Static meshes need one sample; animated ones keep the whole clock.
        # Draw order changes only between these two sets of one group.
        for label, items, sampled, rate in (("static", static, frames[:1], 60),
                                            ("animated", animated, frames, fps)):
            if not items:
                continue
            group = prepare_pack_set(items, [(0, sampled, True)], rate, [fixed] * len(sampled), lights,
                                     profile["sha256"], output,
                                     "%s_group%d%s_" % (profile["prefix"], number, label[0]))
            for report in group:
                name = "%s_map%d.c3t" % (profile["prefix"], packs)
                (output / report["file"]).rename(output / name)
                report.update(file=name, source_models=list(indices), clock=kind, clock_end=end,
                              sampling=label)
                packs += 1
            reports += group
    if not 1 <= packs <= MAX_MAP_PACKS:
        raise ValueError("Floor map pack count exceeds the developer loader bound")
    wes_reports, actor_vertices = prepare_wes(field_common, output, profile["prefix"] + "_wes", fixed, lights)
    instances = {str(i): [list(pair) for pair in model.instances]
                 for i, model in enumerate(models) if model.instances}
    return reports + wes_reports, fixed[:11], actor_vertices, packs, instances


def common_rel(archive):
    table = u32(archive, u32(archive, 24))
    found = None
    for index in range(u32(archive, 12)):
        identity = u32(archive, u32(archive, table + index * 4))
        if (identity >> 8) & 255 == 0x1C and u32(member(archive, identity), 0) == 125:
            found = identity
    if found is None:
        raise ValueError("Required common REL is missing")
    return found


def prepare(disc, output, parser, floor="phenac"):
    from prepare_field_geometry import import_parser
    profile = FLOORS[floor]
    disc = verify_disc(disc)
    output = check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    parser = title.verify_parser(parser)
    import_parser(parser)
    archive = admit_floor(disc, profile)
    commons = {name: admit_archive(disc / "files" / name)
               for name in ("field_common.fsys", "common.fsys")}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".floor-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        packs, camera, actor_vertices, map_packs, instances = prepare_floor_geometry(
            archive, commons["field_common.fsys"], profile, stage)
        rel_id = common_rel(commons["common.fsys"])
        field, metadata = field_metadata(
            member(archive, profile["rel"]), member(commons["common.fsys"], rel_id),
            member(archive, profile["collision"]), camera, actor_vertices,
            module=profile["module"], spawn_table=profile["entrance_table"],
            spawn_count=profile["entrance_count"], floor=profile["floor"],
            map_resource=profile["map"], map_pack_count=map_packs, arrival_pack_count=0)
        name = profile["prefix"] + ".c3f"
        (stage / name).write_bytes(field)
        source_members = []
        for container, data, identities in (
                (profile["archive"], archive, (profile["map"], profile["collision"], profile["rel"])),
                ("field_common.fsys", commons["field_common.fsys"], (0x00F70400,)),
                ("common.fsys", commons["common.fsys"], (rel_id,))):
            for identity in identities:
                blob = member(data, identity)
                source_members.append({"archive": container, "resource_id": "%08x" % identity,
                                       "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})
        archives = {profile["archive"]: {"bytes": profile["bytes"], "sha256": profile["sha256"]}}
        archives.update({n: {"bytes": COMMON_PROFILES[n][0], "sha256": COMMON_PROFILES[n][1]}
                         for n in commons})
        manifest = {
            "developer_only": True,
            "floor_profile": floor,
            "floor": profile["floor"], "lifecycle_row": profile["lifecycle_row"],
            "resource_group": profile["resource_group"],
            "source_commit": title.SOURCE_COMMIT, "parser_commit": title.PARSER_COMMIT,
            "archives": archives, "source_members": source_members,
            "field": {"file": name, "sha256": hashlib.sha256(field).hexdigest(),
                      "bytes": len(field), **metadata},
            "hsd_instances": instances,
            "packs": packs,
            "limitations": [
                "Developer floor loader only: no story route reaches this floor in the native port.",
                "Only the original map, collision, camera defaults and Wes are included; NPCs, field scripts, dialogue, doorways, battles, particles and audio are not.",
                "Map models 0-5 play original clip0 of two authored clips; clip1 is not used.",
                "HSD instance joints are expanded into additional sampled draws of their shared subtrees.",
                "Targetless HSD IK chains keep FK joint matrices and place only the effector, as resolveIKJoint1/2 do without an effector JObj reference.",
                "Geometry is sampled at 60 Hz; the slow sky uses 1001 samples across its original loop.",
                "GX filtering, lighting and blending use the native PICA material subset."]}
        (stage / (profile["prefix"] + "-manifest.json")).write_bytes(
            (json.dumps(manifest, indent=2) + "\n").encode())
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--floor", choices=sorted(FLOORS), default="phenac")
    parser.add_argument("--parser", type=Path, default=title.ROOT / "vendor/hsd-importer")
    args = parser.parse_args()
    try:
        result = prepare(args.disc, args.output, args.parser, args.floor)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, "Floor preparation failed: %s\n" % error)
    print(json.dumps({k: v for k, v in result.items() if k != "packs"}, indent=2))
    for pack in result["packs"]:
        print("%s %d bytes %s" % (pack["file"], pack["bytes"], pack["sha256"]))


if __name__ == "__main__":
    main()
