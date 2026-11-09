#!/usr/bin/env python3
"""Prepare the private battle DOL-table pack (C3W1) from the canonical GC6E01 main.dol.

The pack holds original bytes only: the move-script bytecode region, the
move-effect -> script table (lbl_80379BFF), the initial bytes of the two
initialized-data windows that script operands address, and the condition
table (lbl_8035BBA8) with its count (lbl_80478B48), and the accuracy-stage
table (lbl_80375DD0) with its count (lbl_80478D70). It holds no battle state
or results. Keep it private: it is original game data and must never be added
to the source checkout.

Layout (big-endian): 64-byte header
  +0  "C3W1"   +4 version   +8 total bytes   +12 section count
  +16 main.dol SHA-1 (20)   +36 SHA-1 of everything after the header (20)
then a directory of (tag[4], GameCube base u32, length u32) entries and the
section bytes in directory order.
"""
import argparse
import hashlib
import struct
from pathlib import Path

DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
DOL_BYTES = 3779808
VERSION = 3
HEADER_BYTES = 64
EFFECT_COUNT = 214
SECTIONS = (
    (b"SCR ", 0x80375E60, 0x80379BF4 - 0x80375E60),  # move-script bytecode
    (b"EFF ", 0x80379BFF, EFFECT_COUNT * 4),          # effect -> script pointers
    (b"WSD ", 0x80478D78, 0x10),                      # .sdata script work bytes
    (b"WDT ", 0x8038FF5A, 0x80390000 - 0x8038FF5A),   # .data script variables
    (b"CNC ", 0x80478B48, 4),                         # .sdata condition count
    (b"CND ", 0x8035BBA8, 0x6B8),                     # condition rows (0x14 each)
    (b"HTC ", 0x80478D70, 4),                         # .sdata accuracy-stage count
    (b"HIT ", 0x80375DD0, 13 * 2),                    # accuracy stages (multiplier, divisor)
)
CONDITION_ROW = 0x14


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def dol_read(dol, address, size, label):
    for i in range(18):
        start, extent = u32(dol, 0x48 + i * 4), u32(dol, 0x90 + i * 4)
        if start <= address and address + size <= start + extent:
            offset = u32(dol, i * 4) + address - start
            return dol[offset:offset + size]
    raise ValueError("Original DOL address is not mapped: " + label)


def make_pack(dol):
    if len(dol) != DOL_BYTES or hashlib.sha1(dol).hexdigest() != DOL_SHA1:
        raise ValueError("main.dol differs from canonical GC6E01")
    blobs = [dol_read(dol, base, length, tag.decode()) for tag, base, length in SECTIONS]
    scr_base, scr_len = SECTIONS[0][1], SECTIONS[0][2]
    for i in range(EFFECT_COUNT):
        target = u32(blobs[1], 4 * i)
        if not scr_base <= target < scr_base + scr_len:
            raise ValueError(f"Effect {i} script pointer {target:#x} is outside the script region")
    count = u32(blobs[4], 0)
    if count * CONDITION_ROW > len(blobs[5]):
        raise ValueError(f"Condition count {count} exceeds the condition table")
    if u32(blobs[6], 0) != 13:
        raise ValueError("Unexpected accuracy-stage count")
    directory = b"".join(struct.pack(">4sII", tag, base, length) for tag, base, length in SECTIONS)
    payload = directory + b"".join(blobs)
    header = bytearray(HEADER_BYTES)
    header[0:4] = b"C3W1"
    struct.pack_into(">III", header, 4, VERSION, HEADER_BYTES + len(payload), len(SECTIONS))
    header[16:36] = bytes.fromhex(DOL_SHA1)
    header[36:56] = hashlib.sha1(payload).digest()
    return bytes(header) + payload


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dol", type=Path)
    ap.add_argument("output", type=Path)
    a = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = a.output.resolve()
    if root in out.parents or out == root:
        raise SystemExit("Refusing to write original data inside the source checkout")
    data = make_pack(a.dol.read_bytes())
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"{out} {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
