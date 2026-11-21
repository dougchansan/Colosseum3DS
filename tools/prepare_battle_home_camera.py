#!/usr/bin/env python3
"""Extract the original battle base camera for src/wzx_camera.c.

fn_801EF8F4 (battle_main.c) calls fn_801C2F00 at every command phase:
cameraPlayOffsetAnime(data, 0x007B1800, current frame, loop) with a zero
offset position/rotation and a scale chosen by the field's largest Pokemon
(lbl_8047DF5C/60/64 for fields 1/2/3, lbl_8047DF68 otherwise). The camera
DAT is member 0x007B1800 of fight_common.fsys: one perspective camera
circling the arena. GScameraSetAnimRate 0.5 -> one sample per 60 Hz tick at
0.5 authored frames; the animation loops, so the last frame (equal to the
first) is not stored.

Writes the private C3WH pack (default private/battle3d/battle_home.c3h) and
a .json report. Layout (little-endian): "C3WH", u32 version 1, u32 bytes,
u32 sample count, float scale[4] (field 1, 2, 3, other), then per sample
8 floats (eye xyz, target xyz in importer units = original * 0.1, roll,
vertical FOV degrees), then u32 FNV-1a of every preceding byte.
The data stays private (never commit it).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import prepare_title as title
from prepare_boot_media import verify_disc
from prepare_game_state import Dol, SOURCE_COMMIT
from prepare_model import check_output
from prepare_outskirt import member

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = 'fight_common.fsys'
MEMBER = 0x007B1800
RATE = 0.5  # GScameraSetAnimRate 0.5 (camera.c cameraPlayOffsetAnime)
SCALES = (0x8047DF5C, 0x8047DF60, 0x8047DF64, 0x8047DF68)
MAX_SAMPLES = 8192


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def parse_dat(dat):
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    options = dict(filepath="home.dat", game="COLO_XD", max_frame=1000000000, section_names=[], ik_hack=False)
    sections = parse_sections(dat, route_sections(dat), options)
    scenes = [s for s in sections if s.section_name == "scene_data"]
    if len(scenes) != 1:
        raise ValueError("Expected one scene_data root")
    return describe_scene(scenes, options)


def samples(camera):
    if not camera.animations:
        raise ValueError('Base camera has no animation')
    end = camera.animations[0].end_frame
    count = int(math.floor(end / RATE + 1e-6))
    if not 1 <= count <= MAX_SAMPLES:
        raise ValueError('Implausible base camera length')
    out = []
    for i in range(count):
        v = title.camera_values(camera, i * RATE)
        if not all(math.isfinite(x) for x in v[:8]) or not 0 < v[7] < 180:
            raise ValueError('Nonfinite base camera sample')
        out.append(v[:8])
    return end, out


def prepare(disc, parser, output):
    disc = verify_disc(disc)
    output = check_output(output)
    title.import_parser(parser)
    archive = (disc / 'files' / ARCHIVE).read_bytes()
    dat = member(archive, MEMBER)
    scene = parse_dat(dat)
    if len(scene.cameras) != 1:
        raise ValueError('Expected one base camera')
    end, rows = samples(scene.cameras[0])
    dol = Dol((disc / 'sys/main.dol').read_bytes())
    scale = [struct.unpack('>f', dol.read(a, 4))[0] for a in SCALES]
    if not all(math.isfinite(s) and 0 < s <= 10 for s in scale):
        raise ValueError('Implausible base camera scale')
    out = bytearray(b'C3WH' + struct.pack('<3I', 1, 32 + 32 * len(rows) + 4, len(rows)) + struct.pack('<4f', *scale))
    for row in rows:
        out += struct.pack('<8f', *row)
    out += struct.pack('<I', fnv1a(out))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(out)
    report = dict(
        source_commit=SOURCE_COMMIT, archive=ARCHIVE, member='%08x' % MEMBER,
        member_sha256=hashlib.sha256(dat).hexdigest(), end_frame=end, samples=len(rows), rate=RATE,
        scale_addresses=[hex(a) for a in SCALES],
        output=str(output), bytes=len(out), sha256=hashlib.sha256(out).hexdigest(),
        note='Private original data for src/wzx_camera.c; sample values are not reproduced in this report.')
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--disc', type=Path, required=True, help='extracted GC6E01 disc root')
    p.add_argument('--parser', type=Path, default=ROOT / 'vendor/hsd-importer', help='HSD parser checkout (LF export)')
    p.add_argument('--output', type=Path, default=ROOT / 'private/battle3d/battle_home.c3h')
    a = p.parse_args()
    print(json.dumps(prepare(a.disc, a.parser, a.output), indent=2))
