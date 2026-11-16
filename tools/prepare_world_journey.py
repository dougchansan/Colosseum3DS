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
  journey-manifest.json

The bike follows the same path as the Outskirt arrival
(prepare_field_geometry.prepare_arrival): an attachment matrix per sample and
the camera curve sampled at 60 Hz with two samples per authored frame.
"""
import argparse
import hashlib
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


def prepare(disc, output, parser, bikes=("hizuki", "pokemon"), anims=None):
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
    cam = scene.cameras[0]
    fixed = (*cam.position, *cam.target_position, cam.roll, cam.field_of_view, cam.near, cam.far, cam.aspect, 0.)
    output.parent.mkdir(parents=True, exist_ok=True)
    reports = []
    with tempfile.TemporaryDirectory(prefix=".journey-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        meshes = [geo.build_mesh(scenery, i) for i in range(len(scenery.meshes))]
        meshes += [geo.build_mesh(ground, i) for i in range(len(ground.meshes))]
        loop = scenery.ends[0]
        frames = [i * loop / SCENERY_SAMPLES for i in range(SCENERY_SAMPLES + 1)]
        reports += geo.prepare_pack_set(meshes, [(0, frames, True)], SCENERY_SAMPLES / (loop / 30.0),
                                        [fixed] * len(frames), lights, ARCHIVE[2], stage, "journey_map",
                                        gpu_textures=True)
        route = geo.sample_sequence(ROUTE_END)
        bike_frames = [frame % BIKE_LOOP for frame in route]
        cameras = {}
        for name in bikes:
            _, bike_raw, bike_scene = geo.load_scene(archive, BIKES[name])
            bike = geo.Model(bike_scene.models[0], bike_raw.models[0])
            if bike.ends[0] != BIKE_LOOP:
                raise ValueError("Bike animation 0 length changed")
            bike_meshes = [geo.build_mesh(bike, i) for i in range(len(bike.meshes))]
            for anim in anims:
                camera_id = pairs[anim]
                if camera_id not in cameras:
                    _, _, camera_scene = geo.load_scene(archive, camera_id)
                    cameras[camera_id] = [title.camera_values(camera_scene.cameras[0], f) for f in route]
                attachment = np.asarray([path_model.world(anim, f)[0][0] for f in route])
                prefix = "journey_%s_a%02d_" % (name, anim)
                part = geo.prepare_pack_set(bike_meshes, [(0, bike_frames, False)], 60, cameras[camera_id],
                                            lights, ARCHIVE[2], stage, prefix, attachment=attachment,
                                            format_version=3, gpu_textures=True)
                for report in part:
                    report.update({"bike_resource": "%08x" % BIKES[name], "route_animation": anim,
                                   "camera_resource": "%08x" % camera_id,
                                   "particle_callbacks_unrendered": bike.particle_events[0]})
                reports += part
        manifest = {
            "source_commit": title.SOURCE_COMMIT, "parser_commit": title.PARSER_COMMIT,
            "archive": {"name": ARCHIVE[0], "bytes": ARCHIVE[1], "sha256": ARCHIVE[2]},
            "route_cameras": {str(a): "%08x" % c for a, c in sorted(pairs.items())},
            "packs": reports,
            "limitations": [
                "The bike's projected shadow on move_demo model 2 (GSmodelSetShadow*) is not drawn.",
                "bike_effect_poke (0C831400) dust particles are not rendered; their callbacks are listed per pack.",
                "The scenery's 2999-frame loop is sampled sparsely (%d samples)." % SCENERY_SAMPLES,
                "The menu-capture transition quads (fn_80025A80) are not drawn.",
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
    args = parser.parse_args()
    anims = [int(a) for a in args.anims.split(",")] if args.anims else None
    try:
        manifest = prepare(args.disc, args.output, args.parser, tuple(args.bikes.split(",")), anims)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, "Journey preparation failed: %s\n" % error)
    print("wrote %d packs to %s" % (len(manifest["packs"]), args.output))


if __name__ == "__main__":
    main()
