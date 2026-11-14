#!/usr/bin/env python3
"""Extract the original waza camera tables for src/wzx_camera.c.

Reads sys/main.dol of the admitted GC6E01 disc (verify_disc) and writes the
private C3WK pack (default private/battle3d/wzx_camera.c3k) plus a .json
report. Contents, all from the DOL:
  lbl_80371F60  short FOV pattern table, 8 rows x 19 words (GetPattern 0x801D2B4C)
  lbl_803721C0  long FOV pattern table, 13 rows x 19 words
  lbl_8047E388..lbl_8047E39C  fn_801DABAC scale per owner selector (-2,-1,1,2,3,other)
  lbl_8047DF58  battleCameraStartRandom gate
  lbl_80478CD8 / lbl_80478CAC / lbl_80478CA8  .sdata initial previous motion,
                last idle candidate and idle timer
Layout (little-endian, 1672 bytes): "C3WK", u32 version 1, u32 bytes,
u32 8, u32 13, u32 19, u32 0, u32 0; short rows at 32; long rows at 640;
float scale[6] at 1628; float gate at 1652; s32 previous at 1656; u32 last
at 1660; s32 timer at 1664; u32 FNV-1a of bytes [0,1668) at 1668.
The data stays private (never commit it).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_game_state import Dol, SOURCE_COMMIT
from prepare_model import check_output

ROOT = Path(__file__).resolve().parents[1]
SHORT, LONG = 0x80371F60, 0x803721C0
SCALES = (0x8047E388, 0x8047E38C, 0x8047E390, 0x8047E394, 0x8047E398, 0x8047E39C)
GATE, PREVIOUS, LAST, TIMER = 0x8047DF58, 0x80478CD8, 0x80478CAC, 0x80478CA8
FILE_BYTES = 1672


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def be_float(dol, address):
    return struct.unpack('>f', dol.read(address, 4))[0]


def rows(dol, address, count):
    raw = dol.read(address, 76 * count)
    words = struct.unpack(f'>{19 * count}I', raw)
    table = [list(words[19 * i:19 * i + 19]) for i in range(count)]
    for row in table:
        weight = struct.unpack('>f', struct.pack('>I', row[0]))[0]
        if not math.isfinite(weight) or not 0 <= weight <= 1:
            raise ValueError(f'Implausible camera pattern weight at {address:#x}')
        for segment in range(2):
            s = row[1 + 9 * segment:10 + 9 * segment]
            thresholds = struct.unpack('>4f', struct.pack('>4I', *s[2:6]))
            if not all(math.isfinite(t) for t in thresholds):
                raise ValueError(f'Implausible camera pattern threshold at {address:#x}')
            if s[0] > 16 or s[1] > 16 or s[6] > 7 or s[7] > 7 or s[8] > 4:
                raise ValueError(f'Implausible camera pattern segment at {address:#x}')
    return raw, table


def prepare(disc, output):
    disc = verify_disc(disc)
    output = check_output(output)
    dol = Dol((disc / 'sys/main.dol').read_bytes())
    short_raw, short = rows(dol, SHORT, 8)
    long_raw, long = rows(dol, LONG, 13)
    scale = [be_float(dol, a) for a in SCALES]
    if not all(math.isfinite(s) and 0 < s <= 100 for s in scale):
        raise ValueError('Implausible fn_801DABAC scale')
    gate = be_float(dol, GATE)
    if not math.isfinite(gate):
        raise ValueError('Implausible idle camera gate')
    previous = struct.unpack('>i', dol.read(PREVIOUS, 4))[0]
    last = struct.unpack('>I', dol.read(LAST, 4))[0]
    timer = struct.unpack('>i', dol.read(TIMER, 4))[0]

    out = bytearray(b'C3WK' + struct.pack('<7I', 1, FILE_BYTES, 8, 13, 19, 0, 0))
    for row in short + long:
        out += struct.pack('<19I', *row)
    out += struct.pack('<6f', *scale) + struct.pack('<fiIi', gate, previous, last, timer)
    out += struct.pack('<I', fnv1a(out))
    if len(out) != FILE_BYTES:
        raise AssertionError('C3WK layout')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(out)
    report = dict(
        source_commit=SOURCE_COMMIT,
        output=str(output), bytes=len(out), sha256=hashlib.sha256(out).hexdigest(),
        short_table=dict(address=hex(SHORT), bytes=len(short_raw), sha256=hashlib.sha256(short_raw).hexdigest()),
        long_table=dict(address=hex(LONG), bytes=len(long_raw), sha256=hashlib.sha256(long_raw).hexdigest()),
        scale_addresses=[hex(a) for a in SCALES], idle_gate_address=hex(GATE),
        initial_words={hex(PREVIOUS): previous, hex(LAST): last, hex(TIMER): timer},
        note='Private original data for src/wzx_camera.c; table values are not reproduced in this report.')
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--disc', type=Path, required=True, help='extracted GC6E01 disc root (contains sys/main.dol)')
    p.add_argument('--output', type=Path, default=ROOT / 'private/battle3d/wzx_camera.c3k')
    a = p.parse_args()
    print(json.dumps(prepare(a.disc, a.output), indent=2))
