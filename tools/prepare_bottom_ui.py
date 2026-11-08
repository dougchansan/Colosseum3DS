#!/usr/bin/env python3
"""Prepare original GC6E01 UI artwork and ASCII font for the 320x240 screen.

Pure Python; no new packages. Inputs and outputs remain private. This is a
reflow of original art, not execution of the original GameCube window engine.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import MAIN_DOL_SHA1
from prepare_game_menu import read_member
from prepare_model import check_output

PROFILES = {
    "common.fsys": (743136, "f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a"),
    "fight_common.fsys": (194144, "3e01ba0812dabe1b99739a1c670765f9834a7d9e14d3ef5b1cdf3002e2de7bda"),
    "topmenu.fsys": (926208, "4b27263e182b3b2f0f9ce7ba0ea042cc365d718b966d641272265f3faa6e2da9"),
}
# C3_UI_* order. Crops are from actual GS texture atlases, measured in texels.
SPRITES = (
    ("topmenu.fsys", 0x06DA1200, (0, 0, 268, 244)),
    ("topmenu.fsys", 0x06DA1200, (0, 252, 268, 244)),
    ("topmenu.fsys", 0x06DA1200, (0, 504, 210, 60)),
    ("topmenu.fsys", 0x0C5D1200, (3, 59, 138, 43)),
    ("topmenu.fsys", 0x0C5D1200, (151, 83, 69, 49)),
    ("topmenu.fsys", 0x03041200, (0, 91, 267, 31)),
    ("fight_common.fsys", 0x00EC1200, (61, 7, 163, 49)),
    ("common.fsys", 0x00EE1200, (128, 74, 91, 47)),
    ("common.fsys", 0x01E11200, (0, 0, 144, 118)),
    ("common.fsys", 0x00EE1200, (213, 204, 33, 32)),
)


def decode_texture(data):
    if len(data) < 128:
        raise ValueError("Truncated GS texture")
    width, height = struct.unpack_from(">HH", data)
    fmt, offset = struct.unpack_from(">I", data, 8)[0], struct.unpack_from(">I", data, 40)[0]
    if not 1 <= width <= 1024 or not 1 <= height <= 1024 or fmt not in (0x90, 0x45, 0xB0) or offset != 128:
        raise ValueError("Unsupported GS UI texture")
    if fmt == 0xB0:  # GS 0xB0 -> GX_CMPR: four 4x4 DXT1 blocks per 8x8 tile.
        required = ((width + 7) // 8) * ((height + 7) // 8) * 32
        if offset + required != len(data):
            raise ValueError("CMPR texture payload length mismatch")
        out = bytearray(width * height * 4)
        def rgb565(v):
            return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
        for ty in range(0, height, 8):
            for tx in range(0, width, 8):
                for block in range(4):
                    c0, c1, indices = struct.unpack_from(">HHI", data, offset)
                    offset += 8
                    a, b = rgb565(c0), rgb565(c1)
                    colors = [a, b]
                    if c0 > c1:
                        colors += [tuple((2*a[k]+b[k])//3 for k in range(3))+(255,), tuple((a[k]+2*b[k])//3 for k in range(3))+(255,)]
                    else:
                        colors += [tuple((a[k]+b[k])//2 for k in range(3))+(255,), (0, 0, 0, 0)]
                    for yy in range(4):
                        for xx in range(4):
                            x, y = tx + (block % 2)*4 + xx, ty + (block // 2)*4 + yy
                            if x < width and y < height:
                                pos = (y*width+x)*4
                                out[pos:pos+4] = bytes(colors[(indices >> (30-2*(yy*4+xx))) & 3])
        return width, height, bytes(out)
    tile_bytes = 32 if fmt == 0x90 else 64
    required = ((width + 3) // 4) * ((height + 3) // 4) * tile_bytes
    if offset + required != len(data):
        raise ValueError("GS texture payload length mismatch")
    out = bytearray(width * height * 4)
    for ty in range(0, height, 4):
        for tx in range(0, width, 4):
            tile = data[offset:offset + tile_bytes]
            offset += tile_bytes
            for yy in range(4):
                for xx in range(4):
                    i = yy * 4 + xx
                    if fmt == 0x90:  # GS 0x90 -> GX_RGB5A3 (gs_texture.c)
                        v = struct.unpack_from(">H", tile, i * 2)[0]
                        if v & 0x8000:
                            rgba = (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
                        else:
                            rgba = (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)
                    else:  # GS 0x45 -> GX_RGBA8, AR and GB tile planes
                        rgba = (tile[i * 2 + 1], tile[32 + i * 2], tile[33 + i * 2], tile[i * 2])
                    if tx + xx < width and ty + yy < height:
                        pos = ((ty + yy) * width + tx + xx) * 4
                        out[pos:pos + 4] = bytes(rgba)
    return width, height, bytes(out)


def crop(image, rect):
    width, height, data = image
    x, y, w, h = rect
    if x < 0 or y < 0 or w < 1 or h < 1 or x + w > width or y + h > height:
        raise ValueError("UI crop exceeds atlas")
    return w, h, b"".join(data[((y + row) * width + x) * 4:((y + row) * width + x + w) * 4] for row in range(h))


def dol_offset(data, address):
    if len(data) < 256:
        raise ValueError("Truncated DOL")
    for i in range(18):
        offset, start, size = (struct.unpack_from(">I", data, base + i * 4)[0] for base in (0, 0x48, 0x90))
        if start <= address < start + size:
            result = offset + address - start
            if offset + size > len(data):
                raise ValueError("Truncated DOL section")
            return result
    raise ValueError("Font address is not mapped")


def extract_font(dol):
    # main.c registers 0x8027A500 first. gs_msg.c FontFileHeader / FontBank /
    # GlyphEntry and fn_800FD69C establish dimensions, bearing and linear I4.
    base = dol_offset(dol, 0x8027A500)
    font_id, width, height, _ = struct.unpack_from(">HBBI", dol, base)
    bank = base + 8
    count = struct.unpack_from(">H", dol, bank)[0]
    pixels = struct.unpack_from(">I", dol, bank + 4)[0]
    if (font_id, width, height, count) != (0, 22, 22, 408):
        raise ValueError("Original main font header differs")
    glyphs = {}
    for i in range(count):
        code, w, h, packed = struct.unpack_from(">HBBI", dol, bank + 16 + i * 8)
        if not 32 <= code <= 126:
            continue
        bearing = struct.unpack("b", bytes((packed >> 24,)))[0]
        offset = bank + pixels + (packed & 0xFFFFFF)
        stride = (w + 1) // 2
        if not 1 <= w <= 22 or h != 22 or bearing < 0 or bearing + h > 48 or offset + stride * h > len(dol):
            raise ValueError("Original ASCII glyph exceeds bounds")
        # Area-average 2x2 coverage. Preserve original advance and top bearing
        # at half GameCube resolution, with one native pixel letter spacing.
        out = bytearray(256)
        for y in range(16):
            for x in range(16):
                total = 0
                for dy in range(2):
                    sy = y * 2 + dy - bearing
                    for dx in range(2):
                        sx = x * 2 + dx
                        if 0 <= sx < w and 0 <= sy < h:
                            value = dol[offset + sy * stride + sx // 2]
                            total += ((value & 15) if sx & 1 else value >> 4) * 17
                out[y * 16 + x] = (total + 2) // 4
        glyphs[code] = ((w + 1) // 2 + 1, bytes(out))
    # The source has no curly braces. Current text packs use reviewed ASCII;
    # unknown printable punctuation is explicitly represented by source '?'.
    for code in (123, 125):
        if code not in glyphs and 63 in glyphs:
            glyphs[code] = glyphs[63]
    if len(glyphs) != 95:
        raise ValueError("Original printable ASCII font is incomplete")
    return [glyphs[code] for code in range(32, 127)]


def encode_pack(sprites, glyphs):
    if len(sprites) != 10 or len(glyphs) != 95:
        raise ValueError("Unexpected UI resource count")
    start = 32 + 16 * len(sprites) + 8 * len(glyphs)
    table, font_table, payload = bytearray(), bytearray(), bytearray()
    for w, h, pixels in sprites:
        if not 8 <= w <= 512 or not 8 <= h <= 512 or len(pixels) != w * h * 4:
            raise ValueError("Invalid UI sprite")
        table.extend(struct.pack("<IIII", w, h, start + len(payload), len(pixels)))
        payload.extend(pixels)
    for advance, pixels in glyphs:
        if not 1 <= advance <= 16 or len(pixels) != 256:
            raise ValueError("Invalid UI glyph")
        font_table.extend(struct.pack("<II", advance, start + len(payload)))
        payload.extend(pixels)
    size = start + len(payload)
    if size > 1024 * 1024:
        raise ValueError("UI pack exceeds memory budget")
    return struct.pack("<4s7I", b"C3UI", 1, size, 10, 95, 0, 0, 0) + table + font_table + payload


def prepare(main_dol, files):
    main_dol, files = Path(main_dol), Path(files)
    if main_dol.is_symlink() or main_dol.stat().st_size != 3779808:
        raise ValueError("Unexpected main.dol path or size")
    dol = main_dol.read_bytes()
    if hashlib.sha1(dol).hexdigest() != MAIN_DOL_SHA1:
        raise ValueError("Expected pinned GC6E01 revision 0 main.dol")
    archives, provenance, images = {}, {}, {}
    for name, (size, digest) in PROFILES.items():
        path = files / name
        if path.is_symlink() or path.stat().st_size != size:
            raise ValueError("Unexpected archive path or size: " + name)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Archive differs from reviewed GC6E01 source: " + name)
        archives[name] = data
        provenance[name] = digest
    sprites = []
    for name, member, rect in SPRITES:
        key = name, member
        if key not in images:
            images[key] = decode_texture(read_member(archives[name], member, 1024 * 1024))
        sprites.append(crop(images[key], rect))
    data = encode_pack(sprites, extract_font(dol))
    return data, {"format": "C3UI", "version": 1, "main_dol_sha1": MAIN_DOL_SHA1,
        "archives": provenance, "sprites": [{"archive": n, "member": hex(m), "crop": r} for n, m, r in SPRITES],
        "font": {"address": "0x8027A500", "id": 0, "characters": "ASCII 32..126; absent source braces display '?'", "resampling": "2x2 area average; original top bearing and half-width advance plus 1px spacing"},
        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
        "scope": "Original art/font with native 320x240 layout. GS window animations and non-ASCII controls are not ported."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main-dol", type=Path, required=True)
    parser.add_argument("--files", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        output = check_output(args.output)
        data, manifest = prepare(args.main_dol, args.files)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as stream:
            stream.write(data)
        with output.with_suffix(output.suffix + ".json").open("x") as stream:
            json.dump(manifest, stream, indent=2)
            stream.write("\n")
        print("Prepared original bottom-screen UI: %d bytes -> %s" % (len(data), output))
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, "UI preparation failed: %s\n" % error)


if __name__ == "__main__":
    main()
