#!/usr/bin/env python3
"""Merge extra rendered sounds into an existing music pack (private).

prepare_music.py --sounds renders chosen sound-table rows into a new pack.
This merges such a pack (e.g. the jingles 0x3C9/0x3CA that native 82 and
item gifts wait for) into an installed pack: the C3MI index gets the union
of both row sets (rows of --add win) and the added streams are copied.

    python3 tools/merge_music_index.py --base <sd>/music --add <jingle pack> --output <dir>

The output directory then holds index.bin and the added .c3m streams; copy
them over the SD's music/ folder (the base streams stay as they are).
"""
import argparse
from pathlib import Path
import shutil
import struct


def read_index(path):
    data = Path(path).read_bytes()
    if data[:4] != b"C3MI" or struct.unpack_from("<I", data, 4)[0] != 1:
        raise SystemExit("not a C3MI v1 index: %s" % path)
    count = struct.unpack_from("<I", data, 8)[0]
    rows = [struct.unpack_from("<HHI", data, 12 + i * 8) for i in range(count)]
    at = 12 + count * 8
    files = struct.unpack_from("<I", data, at)[0]
    names = [data[at + 4 + i * 16: at + 20 + i * 16].rstrip(b"\0").decode("ascii") for i in range(files)]
    return {sound: (names[f] if f != 0xFFFF else None, extra) for sound, f, extra in rows}


def write_index(path, rows):
    names = sorted({name for name, _ in rows.values() if name})
    out = bytearray(b"C3MI" + struct.pack("<II", 1, len(rows)))
    for sound in sorted(rows):
        name, extra = rows[sound]
        out += struct.pack("<HHI", sound, names.index(name) if name else 0xFFFF, extra)
    out += struct.pack("<I", len(names))
    for name in names:
        raw = name.encode("ascii")
        if len(raw) > 16:
            raise SystemExit("stream name too long: " + name)
        out += raw.ljust(16, b"\0")
    Path(path).write_bytes(bytes(out))
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--add", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = read_index(args.base / "index.bin")
    args.output.mkdir(parents=True, exist_ok=True)
    added = []
    for pack in args.add:
        extra = read_index(pack / "index.bin")
        for sound, (name, _x) in extra.items():
            if name:
                shutil.copyfile(pack / name, args.output / name)
                added.append("0x%03X %s" % (sound, name))
        rows.update(extra)
    names = write_index(args.output / "index.bin", rows)
    print("index: %d sounds, %d streams; added %s" % (len(rows), len(names), ", ".join(added)))


if __name__ == "__main__":
    main()
