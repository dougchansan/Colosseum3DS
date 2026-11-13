#!/usr/bin/env python3
"""Compress prepared .c3t packs in place for faster loads (C3Z2 zstd or C3Z1 zlib).

A compressed pack is a 4-byte magic, the raw pack size (u32 little-endian),
then one stream of the raw pack: b"C3Z2" with a zstd frame (default; needs
the `zstd` command) or b"C3Z1" with a zlib stream. src/title.c loads every
form, so a directory may mix them. A pack already in the requested form is
left alone; one in the other form is converted. Each file is replaced
atomically (written beside it, then renamed) after a round-trip check.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

import pack_textures

MAGICS = {b"C3Z1": "zlib", b"C3Z2": "zstd"}


def _zstd(arguments, data):
    return subprocess.run(["zstd", "-q", "-c", *arguments], input=data, stdout=subprocess.PIPE,
                          check=True).stdout


def compress_pack(raw, codec="zstd", level=None):
    if codec == "zstd":
        body, magic = _zstd(["-T0", "-%d" % (level or 19)], raw), b"C3Z2"
    else:
        body, magic = zlib.compress(raw, level or 9), b"C3Z1"
    return magic + struct.pack("<I", len(raw)) + body


def decompress_pack(data):
    codec = MAGICS.get(data[:4])
    if not codec:
        return data
    raw = _zstd(["-d"], data[8:]) if codec == "zstd" else zlib.decompress(data[8:])
    if len(raw) != struct.unpack_from("<I", data, 4)[0]:
        raise ValueError("Compressed pack raw size mismatch")
    return raw


def compress_directory(directory, codec="zstd", level=None, pattern="*.c3t", max_texture=None):
    wanted = b"C3Z2" if codec == "zstd" else b"C3Z1"
    before = after = changed = 0
    for path in sorted(Path(directory).glob(pattern)):
        data = path.read_bytes()
        if data[:4] == wanted and not max_texture:
            before += struct.unpack_from("<I", data, 4)[0]
            after += len(data)
            continue
        raw = decompress_pack(data)
        if max_texture and len(raw) > 112 and raw[112] == 0:
            # Screen-sized GPU textures (tools/pack_textures.py): opaque
            # images become RGB565 with mips, capped at max_texture texels.
            try:
                repacked = pack_textures.repack(raw, max_texture)
                raw = repacked[0] if isinstance(repacked, tuple) else repacked
            except ValueError:
                pass
        elif data[:4] == wanted:
            before += len(raw)
            after += len(data)
            continue
        packed = compress_pack(raw, codec, level)
        if decompress_pack(packed) != raw:
            raise ValueError("Compressed pack round trip failed: %s" % path)
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_bytes(packed)
        temporary.replace(path)
        before += len(raw)
        after += len(packed)
        changed += 1
    return {"compressed": changed, "raw_bytes": before, "stored_bytes": after}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--codec", choices=("zstd", "zlib"), default="zstd")
    parser.add_argument("--level", type=int)
    parser.add_argument("--max-texture", type=int, choices=(128, 256, 512, 1024),
                        help="Also re-encode textures for the GPU, capped at this size")
    args = parser.parse_args()
    report = compress_directory(args.directory, args.codec, args.level, max_texture=args.max_texture)
    ratio = report["raw_bytes"] / max(report["stored_bytes"], 1)
    print("compressed %d packs: %.1f MB -> %.1f MB (%.2fx)" % (
        report["compressed"], report["raw_bytes"] / 1e6, report["stored_bytes"] / 1e6, ratio))


if __name__ == "__main__":
    main()
