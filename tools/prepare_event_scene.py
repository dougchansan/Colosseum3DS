#!/usr/bin/env python3
"""Prepare the original subway event scenes as script-driven floors.

The train between the Under (0x3C/0x3D) and the Shadow Pokemon Lab garage
(0x49) rides through four event-scene sets: 0x8A (Under departure), 0x8B
(Under arrival), 0x8C (Lab departure) and 0x8D (Lab arrival). Each archive
holds the floor's script bank and one map scene: model 0 is the tunnel set,
model 1 the train, one camera and four lights. None has a floor REL,
collision, entrance or people table.

Each scene becomes the generic f%03x packs the field scene loads:

  .c3f   no entrance and no walk triangle (an event scene; the scripts place
         the hero), the scene's rest camera and Wes's movement constants
  .c3a   the original lifecycle scripts, no surfaces or links, and one
         scene-model row per scripted map model (aux3 'C3SM'): natives
         72/73/75/46 address it by its original resource ID, aux2 holds the
         GSmodelGetPart index the scripts attach to and +156 the sampled bone
         that retains that part's matrix
  .c3s   the floor's script bank with the common bank, messages and names
  _map%u.c3t   the unscripted map models (looping clock)
  _model%u.c3t the scripted models, every source clip kept and none looping
  _wes.c3t     Wes (the scripts show him riding the train)
  _camera.c3c  the scene camera, header resource = its original ID

Animated lights keep their first values (natives BE/BF/C1 stay recorded as
unrendered). Everything is written to a private directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import prepare_title as title

SCENE_MODEL_MARKER = 0x4333534D  # 'C3SM'
PIN = "51bfe4314a218dfaf9592fc80723c50498d0ee55"

# Archive identities (GC6E01 rev 0), script/map members and the scripted
# models: {scene model index: (original resource ID, attached part)}.
EVENT_SCENES = {
    0x8A: {"archive": "M4_subway_B3_ev_1.fsys", "bytes": 551424,
           "sha256": "c36c4af42d4b11b6fc3a9874399d1cbe57e0acfbb226a4b901114f6f3593978a",
           "script": 0x10A80E00, "map": 0x10850200, "models": {1: (0x10851001, 12)}},
    0x8B: {"archive": "M4_subway_B3_ev_2.fsys", "bytes": 552000,
           "sha256": "7975512dcb6474a4ccdda9d54d69f8f37a91f351c42b8d24fa7131187c9161bf",
           "script": 0x10A90E00, "map": 0x10860200, "models": {1: (0x10861001, 12)}},
    0x8C: {"archive": "D1_garage_B2_2_ev_1.fsys", "bytes": 430080,
           "sha256": "9f566d0d0ec33785176056fd227e6b21f9ea7c08abd15502cb9c9535312de595",
           "script": 0x10AE0E00, "map": 0x10840200, "models": {1: (0x10841001, 12)}},
    0x8D: {"archive": "D1_garage_B2_2_ev_2.fsys", "bytes": 433120,
           "sha256": "7f02e02ed2c40b551ca7dc913e3cdc9a2db849a2a7cb0b2bdb51e5562df9906b",
           "script": 0x10AF0E00, "map": 0x10830200, "models": {1: (0x10831001, 12)}},
}


def prefix(floor):
    return "f%03x" % floor


def admit(path, profile):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size != profile["bytes"]:
        raise ValueError("Unexpected original archive path or size: " + profile["archive"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != profile["sha256"]:
        raise ValueError("Archive differs from admitted GC6E01 revision 0: " + profile["archive"])
    return data


def lifecycle_row(rel, floor):
    """The floor's REL125 lifecycle row (floor_bios.c): resource group and
    the load/init/pre/main/post scripts, with no native hooks."""
    from prepare_story import extract_area_metadata, COMMON_REL
    metadata = extract_area_metadata({COMMON_REL: rel}, {floor: None}, {})
    rows = [row for row in metadata["area-lifecycle.json"] if row["floor"] == floor]
    events = [row for row in metadata["area-events.json"] if row["floor"] == floor]
    if len(rows) != 1 or events:
        raise ValueError("Event scene lifecycle or event table changed")
    row = rows[0]
    if row["pre_native"] or row["main_native"] or row["post_native"]:
        raise ValueError("Event scene lifecycle uses an unported native hook")
    words = row["raw_words"]
    return row["resource_group"], (words[10], row["init_script"], row["pre_script"],
                                   row["main_script"], row["post_script"]), row["index"]


def story_pack(floor, archive, profile, rel):
    from prepare_outskirt import member
    from prepare_story import be16, encode_pack, inspect_bank, u32
    base = u32(rel, u32(rel, 16) + 4*8) & ~3
    common = rel[base+0x89CFC:base+0x8BCB4]
    common_messages = rel[base+0x647B4:base+0x647B4+0x124DD]
    common_names = rel[base+0x58044:base+0x647B4]
    if be16(common, 0) != 0x596 or be16(common_messages, 4) != 684 or be16(common_names, 4) != 2018:
        raise ValueError("Unexpected original common script bank")
    bank = member(archive, profile["script"])
    if be16(bank, 0) != 0x100:
        raise ValueError("Unexpected original field script bank")
    inspect_bank(bank, allow_null_entries=True)
    return encode_pack(floor, [bank, common], [common_messages, common_names])


def field_pack(floor, map_resource, camera, actor_vertices, map_packs):
    """C3F1 with no entrance and no walk/wall triangle. Movement constants
    are Wes's (prepare_outskirt.field_metadata); nothing walkable is made."""
    scale = 0.1
    header = bytearray(256)
    struct.pack_into("<4s15I", header, 0, b"C3F1", 1, 256, 256, floor, map_resource,
                     0, 0, 0, 256, 256, 256, 56, 24, 0, 0)
    struct.pack_into("<7fI", header, 64, scale, 4*scale, 9*scale, 13*scale, 31, 18, 31, 1)
    struct.pack_into("<11f", header, 96, *camera)
    struct.pack_into("<4f", header, 140, camera[1]-camera[4], camera[2]-camera[5], 0, 1.4)
    struct.pack_into("<6I", header, 156, 0, actor_vertices, 0xFFFFFFFF, 0, 1, 2)
    struct.pack_into("<2I", header, 180, map_packs, 0)
    return bytes(header)


def area_pack(floor, group, scripts, rows):
    actors = bytearray()
    for row in rows:
        record = bytearray(160)
        struct.pack_into("<8I7f", record, 0, row["index"], row["resource"], 0, 0, 0, 0,
                         row["part"], SCENE_MODEL_MARKER, 0., 0., 0., 0., 0.01, 0., 0.)
        name = row["file"].encode("ascii")
        record[72:72+len(name)] = name
        struct.pack_into("<I", record, 156, row["bone"] + 1)
        actors.extend(record)
    size = 128 + len(actors)
    header = bytearray(128)
    struct.pack_into("<4s15I", header, 0, b"C3A1", 1, size, floor, group, len(rows), 0, 0,
                     128, size, size, 160, 56, 0, 0, size)
    header[64:84] = bytes.fromhex(PIN)
    struct.pack_into("<5I", header, 84, *scripts)
    return bytes(header + actors)


def prepare_scene(disc, floor, output, field_common, rel):
    import prepare_field_geometry as geometry
    from prepare_area import sampled_camera
    from prepare_floor import runtime_meshes, MAX_MAP_PACKS
    profile = EVENT_SCENES[floor]
    stem = prefix(floor)
    archive = admit(Path(disc) / "files" / profile["archive"], profile)
    data, raw, scene = geometry.load_scene(archive, profile["map"])
    if len(scene.models) != 2 or len(scene.cameras) != 1 or scene.fogs:
        raise ValueError("Event scene topology changed")
    options = {"allow_instances": True, "allow_paths": True, "frozen_material_colors": True}
    models = [geometry.Model(model, raw_model, **options) for model, raw_model in zip(scene.models, raw.models)]
    camera = scene.cameras[0]
    fixed = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
             camera.near, camera.far, camera.aspect, 0.)
    # Animated lights keep their first values (recorded approximation).
    geometry.APPROXIMATE["enabled"] = True
    geometry.APPROXIMATIONS.clear()
    lights = geometry.fixed_lights(raw, data)
    reports, omitted, packs = [], [], 0
    scripted = profile["models"]
    # Unscripted map models: one group on the longest first clip.
    grouped = [i for i in range(len(models)) if i not in scripted]
    end = max([models[i].ends[0] for i in grouped if models[i].tracks and models[i].ends] or [0])
    frames = geometry.sample_sequence(end) if end else [0.]
    meshes = []
    for index in grouped:
        own = runtime_meshes([geometry.build_mesh(models[index], i) for i in range(len(models[index].meshes))],
                             index, True, omitted)
        own += geometry.instance_meshes(models[index], own)
        meshes += own
    static, animated = geometry.split_static(meshes, 0, frames)
    static = [geometry.bake_mesh(mesh, 0, frames[0]) or mesh for mesh in static]
    for label, items, sampled in (("static", static, frames[:1]), ("animated", animated, frames)):
        if not items:
            continue
        group = geometry.prepare_pack_set(items, [(0, sampled, True)], 60, [fixed] * len(sampled), lights,
                                          profile["sha256"], output, "%s_group%s_" % (stem, label[0]),
                                          format_version=3, gpu_textures=True)
        for report in group:
            name = "%s_map%d.c3t" % (stem, packs)
            (output / report["file"]).rename(output / name)
            report.update(file=name, source_models=grouped, clock_end=end, sampling=label)
            packs += 1
        reports += group
    if not 1 <= packs <= MAX_MAP_PACKS:
        raise ValueError("Event scene map pack count exceeds the loader bound")
    rows, used = [], set()
    for index, (resource, part) in sorted(scripted.items()):
        model = models[index]
        if resource != (profile["map"] & 0xFFFF0000) | 0x1000 | index:
            raise ValueError("Scripted model resource does not name its scene model")
        if not model.ends or not 0 <= part < len(model.joints):
            raise ValueError("Scripted model has no clip or no such part")
        own = runtime_meshes([geometry.build_mesh(model, i) for i in range(len(model.meshes))], index, True, omitted)
        own += geometry.instance_meshes(model, own)
        # natives 73/75 choose and wait for the clip: none loops here.
        schedules = [(clip, geometry.sample_sequence(e) if e else [0.], False) for clip, e in enumerate(model.ends)]
        samples = sum(len(sequence) for _, sequence, _ in schedules)
        # GSmodelGetPart(model, part): the part-th joint in HSD order.
        group = geometry.prepare_pack_set(own, schedules, 60, [fixed] * samples, lights, profile["sha256"], output,
                                          "%s_model%d" % (stem, index), expected_count=1, format_version=3,
                                          retained_parts=((model, part, part),), gpu_textures=True)
        bone = group[0]["retained_parts"][0]["sampled_bone"]
        slot = next(i for i in range(255, -1, -1) if i not in used)
        used.add(slot)
        group[0].update(source_models=[index], resource="%08x" % resource, attached_part=part,
                        source_clip_ends=list(model.ends))
        rows.append({"index": slot, "resource": resource, "part": part, "bone": bone, "file": group[0]["file"]})
        reports += group
    wes, actor_vertices = geometry.prepare_wes(field_common, output, stem + "_wes", fixed, lights, format_version=3)
    reports += wes
    files = {stem + ".c3f": field_pack(floor, profile["map"], fixed[:11], actor_vertices, packs)}
    group_id, scripts, row_index = lifecycle_row(rel, floor)
    files[stem + ".c3a"] = area_pack(floor, group_id, scripts, rows)
    files[stem + ".c3s"] = story_pack(floor, archive, profile, rel)
    files[stem + "_camera.c3c"] = sampled_camera(camera, profile["sha256"], (profile["map"] & 0xFFFF0000) | 0x1800)
    for name, blob in files.items():
        (output / name).write_bytes(blob)
        reports.append({"file": name, "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})
    return {"floor": floor, "archive": profile["archive"], "lifecycle_row": row_index, "resource_group": group_id,
            "scripts": ["%08x" % s for s in scripts], "scene_models": rows, "map_packs": packs,
            "omitted_meshes": omitted, "approximations": sorted(set(geometry.APPROXIMATIONS)), "packs": reports}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--floor", action="append", type=lambda text: int(text, 0),
                        help="event scene floor (repeatable; default: all four)")
    parser.add_argument("--parser", type=Path, default=title.ROOT / "vendor/hsd-importer")
    args = parser.parse_args()
    from prepare_boot_media import verify_disc
    from prepare_model import check_output
    from prepare_outskirt import admit_archive
    from prepare_story import admit_common_rel
    import prepare_field_geometry as geometry
    try:
        floors = args.floor or sorted(EVENT_SCENES)
        if any(floor not in EVENT_SCENES for floor in floors):
            raise ValueError("Not an event scene floor")
        disc, output = verify_disc(args.disc), check_output(args.output)
        if output.exists():
            raise ValueError("Choose a new private output directory")
        geometry.import_parser(title.verify_parser(args.parser))
        field_common = admit_archive(disc / "files" / "field_common.fsys")
        rel = admit_common_rel(disc / "files")
        output.mkdir(parents=True)
        report = {"scenes": [prepare_scene(disc, floor, output, field_common, rel) for floor in floors]}
        (output / "event-scenes.json").write_text(json.dumps(report, indent=2) + "\n", newline="\n")
        print(json.dumps([{k: s[k] for k in ("floor", "map_packs", "scene_models", "approximations")}
                          for s in report["scenes"]], indent=2))
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
