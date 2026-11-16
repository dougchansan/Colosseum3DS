#!/usr/bin/env python3
"""Prepare the world-map journey (menu 0xC3) packs from the user's own disc.

gs_title.c fn_80025F84 plays the journey after a destination is chosen:
  - move_demo (world_map.fsys 0C6A0200): model 0 (0xC6A1000) is a one-joint
    route path with fifteen 149-frame animations, model 1 (0xC6A1001) the
    desert scenery, model 2 (0xC6A1002) the ground the bike's shadow falls on;
  - the bike, bike_hizuki (0C6B0400, Wes with Rui) when story flag 0x3E5 is
    set, else bike_pokemon (0C6C0400), attached to the route joint
    (fn_801CB61C(bike, 0xC6A1000, 0)) and looping its animation 0;
  - route animation frame_a on 0xC6A1000 and camera animation frame_b
    (cam_move_demo_XX, type 0x18), one row of the REL table lbl_3_data_270.

Outputs (private; derived from original game data, never commit them):
  journey_map<N>.c3t           scenery + ground, one clock
  journey_<bike>_a<AA>_<N>.c3t bike on route animation AA, with that row's
                               camera sampled into the pack
  journey_dust.c3p             bike_effect_poke (0C831400), C3PT v2
  journey_<bike>_a<AA>.c3j     the bike's particle callbacks on route AA
  journey-manifest.json

C3J2 (little endian): "C3J2", u32 sample count, u32 emitter count, u32
samples per bike loop, then per emitter u32 generator and u32 raw callback
value, then per sample: the first emitter joint's world origin (3 floats,
source units = pack units x 10) and rotation (9 floats, row-major,
orthonormal), then every emitter's position in that joint's frame (3 floats
each, source units). The bike's animation 0 fires every callback at its
loop's frame 0 (type40 key value = generator << 6 | 12); the engine starts
that generator of the linked bank at the joint (fn_80119BD0) and, with
attach mode 4, the emitter follows the joint.

The bike follows the same path as the Outskirt arrival
(prepare_field_geometry.prepare_arrival): an attachment matrix per sample and
the camera curve sampled at 60 Hz with two samples per authored frame.
"""
import argparse
import hashlib
import struct
import json
from pathlib import Path
import re
import tempfile

import numpy as np

import prepare_title as title
from prepare_model import check_output

ARCHIVE = ("world_map.fsys", 1583840, "50ddbbeb40b0c18b77eb567efc1bbbebc74160767674f81cf7cc7d7ac92a759e")
MOVE_DEMO = 0x0C6A0200
BIKES = {"hizuki": 0x0C6B0400, "pokemon": 0x0C6C0400}
REL_SOURCE = title.ROOT / "vendor/colosseum/src/rel/worldmap_menu_rel/worldmap_menu_rel.c"
ROUTE_END = 149       # every move_demo route and camera animation
BIKE_LOOP = 15        # bike animation 0 of both models
SCENERY_SAMPLES = 600 # sparse samples of the scenery's slow 2999-frame loop
SHADOW_LIGHT = (-1267.0223, 1867.7216, 1426.1390)
DUST_BANK = 0x0C831400
SOURCE_PER_PACK = 10.0  # particle positions are source units (pack units / 0.1)


def dust_pack(archive, roots):
    import prepare_particle as particle
    from prepare_outskirt import member
    data = member(archive, DUST_BANK)
    report = particle.general_bank(data, sorted(set(roots)))
    return particle.encode_general(data, report, hashlib.sha256(data).hexdigest()), report


def emitter_events(bike):
    events = bike.particle_events[0]
    if not events or any(e["frame"] != 0 for e in events) or any(e["argument"] & 63 != 12 for e in events):
        raise ValueError("Bike particle callbacks changed")
    return events


def emitters(bike, events, attachment, bike_frames):
    """C3J2 bytes: the reference joint's frame and every callback joint in it."""
    out = bytearray(b"C3J2" + struct.pack("<3I", len(bike_frames), len(events), BIKE_LOOP * 2))
    for e in events:
        out += struct.pack("<2I", e["argument"] >> 6, e["argument"])
    for i, frame in enumerate(bike_frames):
        world = bike.world(0, frame)[0]
        ref = attachment[i] @ world[events[0]["bone"]]
        rotation = ref[:3, :3] / np.linalg.norm(ref[:3, :3], axis=0)
        origin = ref[:3, 3]
        out += struct.pack("<3f", *(float(v) * SOURCE_PER_PACK for v in origin))
        out += struct.pack("<9f", *(float(v) for v in rotation.reshape(9)))
        for e in events:
            p = (attachment[i] @ world[e["bone"]])[:3, 3]
            local = rotation.T @ (p - origin)
            out += struct.pack("<3f", *(float(v) * SOURCE_PER_PACK for v in local))
    return bytes(out)


def rel_rows():
    """lbl_3_data_270: 20 rows {0x01000000, route animation, camera id, rate}."""
    text = REL_SOURCE.read_text()
    match = re.search(r"u32 lbl_3_data_270\[80\] = \{(.*?)\};", text, re.S)
    if not match:
        raise ValueError("REL camera table lbl_3_data_270 not found in the pinned source")
    words = [int(w, 16) for w in re.findall(r"0x[0-9A-Fa-f]+", match.group(1))]
    if len(words) != 80:
        raise ValueError("REL camera table length changed")
    rows = [tuple(words[i:i + 4]) for i in range(0, 80, 4)]
    if any(row[0] != 0x01000000 for row in rows):
        raise ValueError("REL camera table row tag changed")
    return rows


def route_cameras(rows):
    pairs = {}
    for _, anim, camera, _ in rows:
        if pairs.setdefault(anim, camera) != camera:
            raise ValueError("Route animation %d pairs with two cameras" % anim)
    if sorted(pairs) != list(range(15)):
        raise ValueError("REL camera table does not cover route animations 0-14")
    return pairs


def admit(disc):
    path = Path(disc) / "files" / ARCHIVE[0]
    if path.is_symlink() or path.stat().st_size != ARCHIVE[1]:
        raise ValueError("Unexpected world_map.fsys path or size")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != ARCHIVE[2]:
        raise ValueError("world_map.fsys differs from the admitted GC6E01 revision 0 file")
    return data


def prepare(disc, output, parser, bikes=("hizuki", "pokemon"), anims=None, emitters_only=False):
    import prepare_field_geometry as geo
    output = check_output(output)
    geo.import_parser(title.verify_parser(parser))
    archive = admit(disc)
    pairs = route_cameras(rel_rows())
    anims = sorted(pairs) if anims is None else anims
    data, raw, scene = geo.load_scene(archive, MOVE_DEMO)
    if len(scene.models) != 3:
        raise ValueError("move_demo topology changed")
    path_model = geo.Model(scene.models[0], raw.models[0])
    scenery = geo.Model(scene.models[1], raw.models[1], allow_instances=True)
    ground = geo.Model(scene.models[2], raw.models[2])
    if len(path_model.model.bones) != 1 or path_model.ends != [ROUTE_END] * 15:
        raise ValueError("move_demo route animations changed")
    lights = geo.fixed_lights(raw, data)
    # The shadow light 0xC6A1603 (world_map_scene.c JOURNEY_LIGHT_*): light
    # set 3, directional (flags & 3 == 1), black.
    shadow = raw.lights[3].light
    position = tuple(shadow.position.position)
    if (shadow.flags & 3) != 1 or any(abs(a - b) > 0.01 for a, b in zip(position, SHADOW_LIGHT)):
        raise ValueError("move_demo shadow light 0xC6A1603 changed")
    cam = scene.cameras[0]
    fixed = (*cam.position, *cam.target_position, cam.roll, cam.field_of_view, cam.near, cam.far, cam.aspect, 0.)
    output.parent.mkdir(parents=True, exist_ok=True)
    reports = []
    with tempfile.TemporaryDirectory(prefix=".journey-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        if not emitters_only:
            meshes = [geo.build_mesh(scenery, i) for i in range(len(scenery.meshes))]
            meshes += [geo.build_mesh(ground, i) for i in range(len(ground.meshes))]
            loop = scenery.ends[0]
            frames = [i * loop / SCENERY_SAMPLES for i in range(SCENERY_SAMPLES + 1)]
            reports += geo.prepare_pack_set(meshes, [(0, frames, True)], SCENERY_SAMPLES / (loop / 30.0),
                                            [fixed] * len(frames), lights, ARCHIVE[2], stage, "journey_map",
                                            gpu_textures=True)
        roots, dust = set(), {}
        route = geo.sample_sequence(ROUTE_END)
        bike_frames = [frame % BIKE_LOOP for frame in route]
        cameras = {}
        for name in bikes:
            _, bike_raw, bike_scene = geo.load_scene(archive, BIKES[name])
            bike = geo.Model(bike_scene.models[0], bike_raw.models[0])
            if bike.ends[0] != BIKE_LOOP:
                raise ValueError("Bike animation 0 length changed")
            events = emitter_events(bike)
            roots |= {e["argument"] >> 6 for e in events}
            bike_meshes = [] if emitters_only else [geo.build_mesh(bike, i) for i in range(len(bike.meshes))]
            for anim in anims:
                camera_id = pairs[anim]
                if camera_id not in cameras:
                    _, _, camera_scene = geo.load_scene(archive, camera_id)
                    cameras[camera_id] = [title.camera_values(camera_scene.cameras[0], f) for f in route]
                attachment = np.asarray([path_model.world(anim, f)[0][0] for f in route])
                sidecar = "journey_%s_a%02d.c3j" % (name, anim)
                (stage / sidecar).write_bytes(emitters(bike, events, attachment, bike_frames))
                dust[sidecar] = len(events)
                if emitters_only:
                    continue
                prefix = "journey_%s_a%02d_" % (name, anim)
                part = geo.prepare_pack_set(bike_meshes, [(0, bike_frames, False)], 60, cameras[camera_id],
                                            lights, ARCHIVE[2], stage, prefix, attachment=attachment,
                                            format_version=3, gpu_textures=True)
                for report in part:
                    report.update({"bike_resource": "%08x" % BIKES[name], "route_animation": anim,
                                   "camera_resource": "%08x" % camera_id,
                                   "particle_callbacks": bike.particle_events[0]})
                reports += part
        packed, dust_report = dust_pack(archive, roots)
        (stage / "journey_dust.c3p").write_bytes(packed)
        manifest = {
            "source_commit": title.SOURCE_COMMIT, "parser_commit": title.PARSER_COMMIT,
            "archive": {"name": ARCHIVE[0], "bytes": ARCHIVE[1], "sha256": ARCHIVE[2]},
            "route_cameras": {str(a): "%08x" % c for a, c in sorted(pairs.items())},
            "packs": reports,
            "dust": {"file": "journey_dust.c3p", "bytes": len(packed),
                     "sha1": hashlib.sha1(packed).hexdigest(), "roots": dust_report["root_generators"],
                     "closure": dust_report["closure"], "emitters": dust},
            "limitations": [
                "The bike's shadow is drawn by the runtime as a planar projection along light 0xC6A1603.",
                "The scenery's 2999-frame loop is sampled sparsely (%d samples)." % SCENERY_SAMPLES,
            ]}
        (stage / "journey-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--disc", required=True, type=Path, help="extracted disc root (files/world_map.fsys)")
    parser.add_argument("--output", required=True, type=Path, help="new private output directory")
    parser.add_argument("--parser", type=Path, default=title.ROOT / "vendor/hsd-importer")
    parser.add_argument("--bikes", default="hizuki,pokemon")
    parser.add_argument("--anims", default=None, help="comma-separated route animations (default all 15)")
    parser.add_argument("--emitters-only", action="store_true",
                        help="write only journey_dust.c3p and the .c3j emitter files")
    args = parser.parse_args()
    anims = [int(a) for a in args.anims.split(",")] if args.anims else None
    try:
        manifest = prepare(args.disc, args.output, args.parser, tuple(args.bikes.split(",")), anims,
                           args.emitters_only)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, "Journey preparation failed: %s\n" % error)
    print("wrote %d packs to %s" % (len(manifest["packs"]), args.output))


if __name__ == "__main__":
    main()
