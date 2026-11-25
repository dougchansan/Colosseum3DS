#!/usr/bin/env python3
"""Prepare one profile floor's original map, collision and Wes packs.

This generalises prepare_outskirt.py to further floors selected by an exact
profile. Its outputs contain original game data and belong in a private
directory (ignored ``private/`` or outside the repository). These packs carry
no NPCs, scripts, dialogue, doorways, story flags or save data; the actor,
event and script packs come from prepare_area.py, prepare_story.py and
prepare_area_runtime.py. The exterior profile's packs are also what the
labelled developer floor loader reads.

Interior rows ("scene": "interior") additionally write one object pack per
multi-clip map model (<prefix>_obj<model>.c3t, every source clip, one-shot).
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
import floor_profiles as _profiles
# Reviewed geometry profiles, plus surveyed floors (C3_FLOOR_SURVEY) that
# get auto_geometry for the warp tour.
FLOORS = {**{name: row for name, row in _profiles.FLOORS.items() if row.get("auto") and row.get("map")},
          **geometry_floors()}
MAX_MAP_PACKS = 40  # C3_FIELD_MAX_MODEL_PACKS is 47; leave room for Wes and NPCs (C3_STAGE_MAX_MODELS 48).


def admit_floor(disc, profile):
    path = Path(disc) / "files" / profile["archive"]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != profile["bytes"]:
        raise ValueError("Unexpected original floor archive path or size: " + profile["archive"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != profile["sha256"]:
        raise ValueError("Floor archive differs from admitted GC6E01 revision 0: " + profile["archive"])
    return data


# Texture selectors src/title.c admits (raw HSD colour and alpha map modes).
RUNTIME_COLOR_MODES, RUNTIME_ALPHA_MODES = (0, 3, 4), (0, 2, 3)


def runtime_meshes(meshes, model_index, omit, omitted):
    """Keep the meshes whose texture modes the pack runtime can load.

    The native reader rejects a whole pack for one other selector. Without
    the profile's explicit omit_unsupported_texture_modes such a mesh is an
    error; with it the mesh is left out and listed in the manifest.
    """
    kept = []
    for mesh in meshes:
        color, alpha = mesh["batch"][4], mesh["batch"][5]
        if color in RUNTIME_COLOR_MODES and alpha in RUNTIME_ALPHA_MODES:
            kept.append(mesh)
        elif not omit:
            raise ValueError("Map mesh uses a texture mode the pack runtime rejects")
        else:
            omitted.append({"model": model_index, "mesh_index": mesh["index"], "owner_joint": mesh["bone"],
                            "vertices": len(mesh["vertices"]), "color_mode": color, "alpha_mode": alpha})
    return kept


def schedule_frames(kind, end):
    if kind == "static":
        # No authored clock: every mesh is baked at its rest sample.
        return [0.], 60
    if kind == "short":
        from prepare_field_geometry import sample_sequence
        return sample_sequence(end), 60
    # Sparse sampling of a slow linear sky; its loop is `end` source frames,
    # not GSmodelGetFrameCount's inclusive query (same rule as Outskirt).
    return [i * end / 1000 for i in range(1001)], 30000 / end


# Clocks above this many source frames are sampled as a slow loop (sky).
AUTO_SHORT_END = 300


def auto_geometry(scene, raw, Model, forced=()):
    """Default geometry for a surveyed floor without a reviewed profile
    (the warp tour): multi-clip models are object packs (doors, machines)
    whose clip the scripts choose, as are the profile's map_objects (forced:
    a one-clip model the bank plays and stops); every other model shares one group whose
    clock is the longest first clip. Unsupported texture modes are omitted
    and material colour animation is frozen, as on reviewed interiors. The
    first scene camera is the fixed camera; fog is not rendered."""
    options = {"allow_instances": True, "allow_paths": True, "frozen_material_colors": True}
    models = [Model(model, raw_model, **options) for model, raw_model in zip(scene.models, raw.models)]
    objects = tuple(i for i, model in enumerate(models) if len(model.ends) > 1 or i in forced)
    grouped = tuple(i for i in range(len(models)) if i not in objects)
    end = max([model.ends[0] for i, model in enumerate(models) if i in grouped and model.tracks and model.ends] or [0])
    kind = "static" if not end else "short" if end <= AUTO_SHORT_END else "sky"
    return {"models": len(models), "objects": objects, "groups": ((grouped, (kind, end)),) if grouped else (),
            "scene": "interior", "pack_format": 3, "path_joints": True, "material_colors": "frozen",
            "omit_unsupported_texture_modes": True}


def prepare_floor_geometry(archive, field_common, profile, output):
    from prepare_field_geometry import (load_scene, Model, build_mesh, instance_meshes,
                                        prepare_pack_set, prepare_wes, fixed_lights, split_static,
                                        bake_mesh, sample_sequence)
    data, raw, scene = load_scene(archive, profile["map"])
    if profile.get("auto") and "groups" not in profile:
        # Every door-row model is an object pack, which the door table names.
        doors = tuple(profile.get("door_objects", {}).values())
        if any(model >= len(scene.models) for model in doors):
            raise ValueError("Door row names a model the original floor scene lacks")
        profile = dict(profile, **auto_geometry(scene, raw, Model, (*profile.get("map_objects", ()), *doors)))
    elif len(scene.models) != profile["models"] or len(scene.cameras) != 1 or scene.fogs:
        raise ValueError("Original floor scene topology changed")
    if not scene.cameras:
        raise ValueError("Original floor scene has no camera")
    colors = profile.get("material_colors")
    options = {"allow_instances": True}
    if profile.get("path_joints"):
        options["allow_paths"] = True
    if colors:
        options["constant_material_colors" if colors == "constant" else "frozen_material_colors"] = True
    models = [Model(model, raw_model, **options)
              for model, raw_model in zip(scene.models, raw.models)]
    version = profile.get("pack_format", 2)
    objects = tuple(profile.get("objects", ()))
    interior, omit, omitted = profile.get("scene") == "interior", bool(profile.get("omit_unsupported_texture_modes")), []
    camera = scene.cameras[0]
    fixed = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
             camera.near, camera.far, camera.aspect, 0.)
    lights = fixed_lights(raw, data)
    grouped = [index for indices, _ in profile["groups"] for index in indices]
    if sorted([*grouped, *objects]) != list(range(len(models))):
        raise ValueError("Floor pack groups must cover each original model once")
    if any(len(models[index].ends) > 1 for index in grouped) and profile.get("scene") == "interior":
        raise ValueError("A multi-clip interior map model must be an object pack")
    reports, packs = [], 0
    for number, (indices, (kind, end)) in enumerate(profile["groups"]):
        frames, fps = schedule_frames(kind, end)
        meshes = []
        for index in indices:
            model = models[index]
            if model.tracks and any(e > end for e in model.ends[:1]):
                raise ValueError("Original map clip exceeds its reviewed clock")
            own = [build_mesh(model, i) for i in range(len(model.meshes))]
            if interior:
                own = runtime_meshes(own, index, omit, omitted)
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
                                     "%s_group%d%s_" % (profile["prefix"], number, label[0]),
                                     format_version=version, gpu_textures=True)
            for report in group:
                name = "%s_map%d.c3t" % (profile["prefix"], packs)
                (output / report["file"]).rename(output / name)
                report.update(file=name, source_models=list(indices), clock=kind, clock_end=end,
                              sampling=label)
                packs += 1
            reports += group
    if not 1 <= packs <= MAX_MAP_PACKS:
        raise ValueError("Floor map pack count exceeds the developer loader bound")
    object_reports = []
    for index in objects:
        # A door or machine: the scripts choose its clip, so every source clip
        # is kept at its own index and none loops (as for NPC packs).
        model = models[index]
        if not model.ends:
            raise ValueError("Floor object model has no authored clip")
        own = runtime_meshes([build_mesh(model, i) for i in range(len(model.meshes))], index, omit, omitted)
        own += instance_meshes(model, own)
        schedules = [(clip, sample_sequence(end) if end else [0.], False)
                     for clip, end in enumerate(model.ends)]
        samples = sum(len(sequence) for _, sequence, _ in schedules)
        if samples > 4000:
            # C3T holds at most 4096 sampled frames. Surveyed floors keep every
            # clip at a coarser rate (approximation mode); the last frame stays.
            from prepare_field_geometry import approximate
            if approximate("object_clips_decimated"):
                step = -(-samples // 4000)
                schedules = [(clip, sequence[::step] + ([sequence[-1]] if (len(sequence) - 1) % step else []), loop)
                             for clip, sequence, loop in schedules]
                samples = sum(len(sequence) for _, sequence, _ in schedules)
        # fn_801D0DB0 places the balls at the machine's parts 9-14 (joints
        # in HSD order, none with a mesh): kept as the pack's last six bones.
        retained = ()
        if index == profile.get("healing_machine"):
            if len(model.joints) <= HEAL_PARTS[-1]:
                raise ValueError("Healing machine lacks its ball slot parts")
            retained = tuple((model, part, part) for part in HEAL_PARTS)
        elevator_part = profile.get("elevator_parts", {}).get(index)
        if elevator_part is not None:
            if retained or not 0 <= elevator_part < len(model.joints):
                raise ValueError("Elevator part is not a joint of its object model")
            retained = ((model, elevator_part, elevator_part),)
        group = prepare_pack_set(own, schedules, 60, [fixed] * samples, lights, profile["sha256"], output,
                                 "%s_obj%d" % (profile["prefix"], index), expected_count=1,
                                 format_version=version, gpu_textures=True, crop_textures=True,
                                 retained_parts=retained)
        group[0].update(source_models=[index], sampling="object", source_clip_ends=list(model.ends))
        if index == profile.get("healing_machine") and [part["sampled_bone"] for part in group[0]["retained_parts"]] != list(
                range(group[0]["sampled_bones"] - len(HEAL_PARTS), group[0]["sampled_bones"])):
            raise ValueError("Healing machine ball slots are not the pack's last bones")
        if elevator_part is not None and [part["sampled_bone"] for part in group[0]["retained_parts"]] != [
                group[0]["sampled_bones"] - 1]:
            raise ValueError("Elevator part is not the pack's last bone")
        object_reports += group
    ball_reports = (prepare_heal_balls(archive, output, profile["prefix"], fixed, lights, profile["sha256"], version)
                    if "healing_machine" in profile else [])
    wes_reports, actor_vertices = prepare_wes(field_common, output, profile["prefix"] + "_wes", fixed, lights,
                                              **({"format_version": version} if version != 2 else {}))
    instances = {str(i): [list(pair) for pair in model.instances]
                 for i, model in enumerate(models) if model.instances}
    frozen = {str(i): model.frozen_material_tracks for i, model in enumerate(models)
              if model.frozen_material_tracks}
    paths = {str(i): sorted({joint for clip in model.paths for joint in clip})
             for i, model in enumerate(models) if any(model.paths)}
    extras = {"objects": object_reports, "frozen_material_tracks": frozen, "path_joints": paths,
              "omitted_meshes": omitted, "heal_balls": [report["file"] for report in ball_reports]}
    return reports + object_reports + ball_reports + wes_reports, fixed[:11], actor_vertices, packs, instances, extras


# battleIntroPartIds and battleIntroObjectIds (fn_801D0DB0, 0x8036E030):
# the machine's ball slot parts and the Poke Ball model of each catch ball id.
HEAL_PARTS = (9, 10, 11, 12, 13, 14)
HEAL_BALLS = (0x03640400, 0x03650400, 0x03640400, 0x036B0400, 0x03360400, 0x036A0400, 0x03660400,
              0x03400400, 0x03670400, 0x03690400, 0x036C0400, 0x03630400, 0x03680400)


def heal_ball_file(prefix, resource):
    """<prefix>_ball_<resource group>.c3t: one pack per distinct ball model
    (src/field_story.c c3_field_heal_ball). Each floor's archive holds the
    same models, but a pack carries that floor's lights."""
    return "%s_ball_%04x.c3t" % (prefix, resource >> 16)


def prepare_heal_balls(archive, output, prefix, fixed, lights, archive_hash, version):
    """The Poke Ball models fn_801D0DB0 opens with floorOpenObject (static,
    one mesh; placed and scaled 1.5 at run time)."""
    from prepare_field_geometry import load_scene, Model, build_mesh, prepare_pack_set
    reports = []
    for resource in sorted(set(HEAL_BALLS)):
        _data, raw, scene = load_scene(archive, resource)
        if len(scene.models) != 1:
            raise ValueError("A Poke Ball resource must hold one model")
        model = Model(scene.models[0], raw.models[0])
        meshes = [build_mesh(model, i) for i in range(len(model.meshes))]
        schedules = [(clip, sample_sequence_or_rest(end), False) for clip, end in enumerate(model.ends or [0.])]
        samples = sum(len(sequence) for _, sequence, _ in schedules)
        stem = heal_ball_file(prefix, resource)[:-len(".c3t")]
        group = prepare_pack_set(meshes, schedules, 60, [fixed] * samples, lights, archive_hash, output, stem,
                                 expected_count=1, format_version=version, gpu_textures=True)
        group[0].update(source_resource="%08x" % resource, sampling="heal_ball")
        reports += group
    return reports


def sample_sequence_or_rest(end):
    from prepare_field_geometry import sample_sequence
    return sample_sequence(end) if end else [0.]


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


def archive_members(archive):
    table = u32(archive, u32(archive, 24))
    return {u32(archive, u32(archive, table + index * 4)) for index in range(u32(archive, 12))}


def television_screen(prefix, map_reports, address):
    """(map pack, texture index) of the screen model's one map batch, or
    None. The stage renderer binds a texture that exactly one batch of its
    pack uses (c3_stage_renderer_set_tv)."""
    found = []
    for report in map_reports:
        name = report["file"]
        if not (name.startswith(prefix + "_map") and name.endswith(".c3t")):
            continue
        number = name[len(prefix) + 4:-4]
        if not number.isdigit():
            continue
        textures = [mesh["texture_index"] for mesh in report["source_meshes"]]
        for mesh in report["source_meshes"]:
            if mesh["hsd_root_address"] == address:
                found.append((int(number), mesh["texture_index"], textures.count(mesh["texture_index"])))
    if len(found) != 1 or found[0][2] != 1:
        return None
    return found[0][:2]


def prepare_television(archive, profile, reports, output):
    """The floor's field-camera texture entries (tools/field_tv.py).

    fn_80117E58 selects entry 0 when the floor starts and natives AB/D3
    select others; fn_80117D14 draws the entry's model with its camera into
    a 256x256 texture that replaces the screen model's placeholder. Each
    (model, camera) pair the floor can show becomes <prefix>_tv<N>.c3t with
    the entries' clips, looping (GSmodelStartAnimation), sampled at 60 Hz
    with the entry camera; <prefix>.c3v binds them to the screen batch.
    Returns the manifest record, or None when the floor has no table. A
    screen or model the packs cannot represent leaves the placeholder
    (record "skipped") instead of failing the floor."""
    import field_tv
    from gsvm_disasm import disassemble
    from prepare_field_geometry import (load_scene, Model, build_mesh, expand_image_swaps, instance_meshes,
                                        prepare_pack_set, fixed_lights, sample_sequence)
    if "rel" not in profile or "map" not in profile:
        return None
    rows = field_tv.texture_table(member(archive, profile["rel"]), archive_members(archive), profile["map"])
    if not rows:
        return None
    wanted = (field_tv.script_entry_ids(disassemble(member(archive, profile["script"])))
              if profile.get("script") else {0})
    packs, entries, omitted = field_tv.plan(rows, wanted)
    record = {"screen_resource": "%08x" % rows[0]["screen"],
              "table": [{"id": row["id"], "model_resource": "%08x" % row["model"], "clip": row["clip"],
                         "camera_resource": "%08x" % row["camera"], "width": row["width"],
                         "height": row["height"]} for row in rows],
              "selectable_ids": sorted(wanted & {row["id"] for row in rows}), "omitted_ids": omitted}
    prefix, version = profile["prefix"], profile.get("pack_format", 2)
    _data, raw, _scene = load_scene(archive, profile["map"])
    screen_model = rows[0]["screen"] & 255
    if screen_model >= len(raw.models):
        raise ValueError("Texture-change screen names a model the floor scene lacks")
    screen = television_screen(prefix, reports, raw.models[screen_model].root_joint.address)
    if screen is None or not packs:
        record["skipped"] = ("The screen model is not one map batch with its own texture." if packs else
                             "No entry is selectable.")
        return record
    written = []
    try:
        for index, pack in enumerate(packs):
            data, raw_model, scene = load_scene(archive, pack["model_member"])
            _camera_data, _camera_raw, camera_scene = load_scene(archive, pack["camera"])
            if pack["model_index"] >= len(scene.models) or len(camera_scene.cameras) != 1:
                raise ValueError("Television model or camera topology is unsupported")
            fixed = title.camera_values(camera_scene.cameras[0], 0)
            model = Model(scene.models[pack["model_index"]], raw_model.models[pack["model_index"]],
                          allow_image_swaps=True, allow_instances=True, allow_paths=True,
                          frozen_material_colors=True)
            if any(clip >= max(len(model.ends), 1) for clip in pack["clips"]):
                raise ValueError("Television entry names a clip its model lacks")
            # Texture selectors the pack runtime rejects leave their mesh out,
            # as on the interiors (the native reader refuses the whole pack).
            omitted = []
            meshes = runtime_meshes(expand_image_swaps([build_mesh(model, i) for i in range(len(model.meshes))]),
                                    pack["model_index"], True, omitted)
            meshes += instance_meshes(model, meshes)
            schedules = [(clip, sample_sequence(model.ends[clip]) if model.ends and model.ends[clip] else [0.], True)
                         for clip in pack["clips"]]
            samples = sum(len(sequence) for _clip, sequence, _loop in schedules)
            group = prepare_pack_set(meshes, schedules, 60, [fixed] * samples, fixed_lights(raw_model, data),
                                     profile["sha256"], output, field_tv.tv_pack_name(prefix, index)[:-4],
                                     expected_count=1, format_version=version, gpu_textures=True)
            group[0].update(sampling="television", source_resource="%08x" % pack["model"],
                            camera_resource="%08x" % pack["camera"], source_clips=pack["clips"],
                            omitted_meshes=omitted)
            written += group
    except ValueError as error:
        for report in written:
            (output / report["file"]).unlink()
        record["skipped"] = str(error)
        return record
    binding = field_tv.encode(screen[0], screen[1], len(packs), entries)
    name = field_tv.sidecar_name(prefix)
    (output / name).write_bytes(binding)
    record.update(file=name, bytes=len(binding), sha256=hashlib.sha256(binding).hexdigest(),
                  screen_pack=screen[0], screen_texture=screen[1],
                  entries=[{"id": ident, "pack": pack, "clip": clip} for ident, pack, clip in entries],
                  packs=written)
    return record


def interior_manifest(manifest, profile, extras):
    """Story-floor manifest: what the interior packs hold and what they omit."""
    limitations = [
        "Map, collision, camera defaults, Wes and object packs only; actors, scripts and events come from the area and story converters.",
        "Object packs keep every source clip at its own index without looping; the door and event natives that play them are runtime work.",
        "HSD instance joints are expanded into additional sampled draws of their shared subtrees.",
        "Geometry is sampled at 60 Hz. GX filtering, lighting and blending use the native PICA material subset.",
        ("Field-camera texture entries are rendered by the television pass (<prefix>.c3v, <prefix>_tv<N>.c3t; see television)."
         if extras.get("television", {}).get("file") else
         "Television screen quads keep their original texture; the field-camera television pass is not prepared here.")]
    if extras["frozen_material_tracks"]:
        limitations.append("Animated material RGB tracks cannot be sampled by the pack format: those colours keep their first-clip frame-zero value (frozen_material_tracks).")
    if extras["path_joints"]:
        limitations.append("HSD path tracks are evaluated with the original cardinal-spline arc-length routine in double precision.")
    if extras["omitted_meshes"]:
        limitations.append("Meshes whose HSD texture colour/alpha mode the pack runtime rejects are not in the packs (omitted_meshes).")
    manifest = dict(manifest)
    manifest.update(developer_only=False, scene="interior", pack_format=profile.get("pack_format", 2),
                    objects=[{"model": report["source_models"][0], "file": report["file"],
                              "source_clip_ends": report["source_clip_ends"],
                              # Elevator parts kept as bones (prepare_story.py --floor-manifest).
                              **({"retained_parts": {str(part["source_part"]): part["sampled_bone"]
                                                     for part in report["retained_parts"]}}
                                 if report.get("retained_parts") else {})}
                             for report in extras["objects"]],
                    frozen_material_tracks=extras["frozen_material_tracks"],
                    path_joints=extras["path_joints"], omitted_meshes=extras["omitted_meshes"],
                    ccd_objects=extras["ccd_objects"], limitations=limitations)
    if "television" in extras:
        manifest["television"] = extras["television"]
    return manifest


_WES_VERTICES = None


def wes_vertex_count(field_common):
    """Expanded vertices of the field Wes model (field_common 00F70400), which
    the C3F records as the actor bound; the same for every floor."""
    global _WES_VERTICES
    if _WES_VERTICES is None:
        from prepare_field_geometry import load_scene, Model, build_mesh
        _data, raw, scene = load_scene(field_common, 0x00F70400)
        model = Model(scene.models[0], raw.models[0])
        _WES_VERTICES = sum(len(build_mesh(model, i)["vertices"]) for i in range(len(model.meshes)))
    return _WES_VERTICES


def prepare_field_only(disc, output, parser, floor):
    """The C3F of one floor without map, object or Wes packs: original
    collision, entrances and REL field data plus the map scene's fixed camera,
    for the headless whole-game story test. map_pack_count is 1 by contract
    of the C3F reader; no map pack is written."""
    from floor_profiles import FLOORS
    from prepare_field_geometry import import_parser, load_scene
    profile = FLOORS[floor]
    disc = verify_disc(disc)
    output = check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    import_parser(title.verify_parser(parser))
    archive = admit_floor(disc, profile)
    commons = {name: admit_archive(disc / "files" / name) for name in ("field_common.fsys", "common.fsys")}
    _data, _raw, scene = load_scene(archive, profile["map"])
    if not scene.cameras:
        raise ValueError("Original floor scene has no camera")
    camera = scene.cameras[0]
    fixed = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
             camera.near, camera.far, camera.aspect, 0.)
    rel_id = common_rel(commons["common.fsys"])
    field, metadata = field_metadata(
        member(archive, profile["rel"]), member(commons["common.fsys"], rel_id),
        member(archive, profile["collision"]), fixed[:11], wes_vertex_count(commons["field_common.fsys"]),
        module=profile["module"], spawn_table=profile["entrance_table"],
        spawn_count=profile["entrance_count"], floor=profile["floor"],
        map_resource=profile["map"], map_pack_count=1, arrival_pack_count=0)
    output.mkdir(parents=True, exist_ok=True)
    name = profile["prefix"] + ".c3f"
    (output / name).write_bytes(field)
    return {"floor_profile": floor, "floor": profile["floor"], "file": name, "bytes": len(field),
            "sha256": hashlib.sha256(field).hexdigest(), "scene_cameras": len(scene.cameras)}


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
    if profile.get("auto"):
        # The models prepare_story.py names in the floor's door table.
        from prepare_story import door_objects, extract_doors
        rel = member(commons["common.fsys"], common_rel(commons["common.fsys"]))
        rows = extract_doors(rel, {profile["floor"]})
        objects = door_objects(profile, rows)
        # floorEventCtrlElevator moves the party to GSmodelGetPart(model,
        # partIndex) of an elevator row (door type 2): that part is kept as
        # the object pack's last sampled bone (prepare_story.py records it).
        parts = {objects[row["index"]]: row["part_index"] for row in rows
                 if row["door_type"] == 2 and row["index"] in objects}
        profile = dict(profile, door_objects=objects, elevator_parts=parts)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".floor-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        packs, camera, actor_vertices, map_packs, instances, extras = prepare_floor_geometry(
            archive, commons["field_common.fsys"], profile, stage)
        television = prepare_television(archive, profile, packs, stage)
        if television is not None:
            extras["television"] = television
            packs = packs + television.get("packs", [])
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
        # Surveyed floors get the interior geometry of auto_geometry too.
        if profile.get("scene") == "interior" or profile.get("auto"):
            # The C3F only implies an object count from its walk/wall rows;
            # doorway surfaces may sit on later CCD objects.
            extras["ccd_objects"] = u32(member(archive, profile["collision"]), 4)
            manifest = interior_manifest(manifest, profile, extras)
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
