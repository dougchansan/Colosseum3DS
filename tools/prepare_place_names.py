#!/usr/bin/env python3
"""Prepare the original place name of every floor (private boot pack).

The original names an area by the message its floor record holds: the
lifecycle table in common REL125 (data +0x8BCF4, 0x4C bytes a row, count at
data +0x14A670; floorDataBiosGetPtr) keeps the floor ID at +0x0C and the
place-name message at +0x24 (fn_80115840, read through fn_8011396C by the
P*DA location text, msgctrl 0x31). The messages are in the two common REL
message groups (data +0x58044 and +0x647B4).

The pack is place_names.c3pn for sdmc:/3ds/Colosseum3DS/boot/ (little-endian,
src/floor_profile.c):

  0  "C3PN"   4 version 1   8 total bytes   12 row count
  16 rows: u32 floor, u32 offset of its NUL-terminated UTF-8 name
  then the names.

Rows are sorted by floor; floors whose record has no name (menus, the world
map) are left out. Names keep e-acute and the gender signs as UTF-8: the
bottom-screen header draws them with the original font's glyphs
(src/bottom_ui.c, tools/prepare_bottom_ui.py version 3), the same rule as
c3_pda_text_utf8. Outputs stay private; nothing original is committed.

  python3 tools/prepare_place_names.py --disc /disc --output private/place_names.c3pn
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_battle_init import COMMON_SHA1
from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import member, rel_data
from prepare_pda import messages

COMMON_MEMBER = 0x01C71C00  # common_rel.rel in common.fsys (prepare_music.py)
FLOOR_TABLE, FLOOR_COUNT, FLOOR_ROW = 0x8BCF4, 0x14A670, 0x4C
GROUPS = ((0x58044, 0x647B4), (0x647B4, None))
NAME_MAX = 47  # bytes before the NUL (C3_FLOOR_NAME_MAX - 1)
# text_context (src/pda.c) with utf8 set.
SPECIAL = {0xE9: "é", 0xC9: "É", 0x2605: "*", 0x3000: " ", 0x2031: "???", 0x2026: "...",
           0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0xFF0D: "-", 0x25CB: "O",
           0x2640: "♀", 0x2642: "♂"}
u32 = lambda d, o: struct.unpack_from(">I", d, o)[0]


def decode(raw):
    out = []
    for p in range(0, len(raw) - 1, 2):
        code = struct.unpack_from(">H", raw, p)[0]
        if code == 0:
            return "".join(out)
        if 32 <= code <= 126:
            out.append(chr(code))
        elif code in SPECIAL:
            out.append(SPECIAL[code])
        else:
            raise ValueError("Place name holds an unsupported character %04x" % code)
    raise ValueError("Unterminated place name")


def common_rel(disc):
    path = disc / "files/common_rel.rel"
    if path.is_file() and not path.is_symlink():
        rel = path.read_bytes()
    else:
        archive = disc / "files/common.fsys"
        if archive.is_symlink():
            raise ValueError("Symbolic links are not accepted: common.fsys")
        rel = member(archive.read_bytes(), COMMON_MEMBER)
    if hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from the admitted GC6E01 original")
    return rel


def names(rel):
    data = rel_data(rel, 125)
    table = {}
    for start, end in GROUPS:
        for key, raw in messages(data[start:end]).items():
            table.setdefault(key, raw)
    count = u32(data, FLOOR_COUNT)
    if not 1 <= count <= 256 or FLOOR_TABLE + count * FLOOR_ROW > len(data):
        raise ValueError("Floor table count differs")
    rows = {}
    for i in range(count):
        row = data[FLOOR_TABLE + i * FLOOR_ROW:FLOOR_TABLE + (i + 1) * FLOOR_ROW]
        floor, message = u32(row, 0x0C), u32(row, 0x24)
        if not message:
            continue
        if message not in table:
            raise ValueError("Floor %03x names missing message %x" % (floor, message))
        name = decode(table[message])
        if not name or len(name.encode("utf-8")) > NAME_MAX:
            raise ValueError("Floor %03x place name length differs" % floor)
        if rows.setdefault(floor, name) != name:
            raise ValueError("Floor %03x has two place names" % floor)
    if not rows:
        raise ValueError("No floor names a place")
    return dict(sorted(rows.items()))


def encode(rows):
    if not 0 < len(rows) <= 512:
        raise ValueError("Place-name row count exceeds bound")
    base = 16 + len(rows) * 8
    index, text = bytearray(), bytearray()
    for floor in sorted(rows):
        encoded = rows[floor].encode("utf-8")
        if not encoded or len(encoded) > NAME_MAX or b"\0" in encoded:
            raise ValueError("Invalid place name")
        index.extend(struct.pack("<II", floor, base + len(text)))
        text.extend(encoded + b"\0")
    size = base + len(text)
    return struct.pack("<4s3I", b"C3PN", 1, size, len(rows)) + index + text


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--disc", type=Path, required=True, help="extracted GC6E01 disc root (sys/, files/)")
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    try:
        output = check_output(a.output)
        rows = names(common_rel(verify_disc(a.disc)))
        data = encode(rows)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as f:
            f.write(data)
        manifest = {"format": "C3PN", "version": 1, "common_rel_sha1": COMMON_SHA1, "rows": len(rows),
                    "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                    "floors": {"%03x" % k: v for k, v in rows.items()},
                    "scope": "Original place names from the floor table; private original data, not distributable source."}
        with output.with_suffix(output.suffix + ".json").open("x", encoding="utf-8", newline="\n") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("Prepared original place names: %d floors, %d bytes" % (len(rows), len(data)))
    except (ValueError, OSError, struct.error) as e:
        p.exit(1, "Place-name preparation failed: %s\n" % e)


if __name__ == "__main__":
    main()
