#!/usr/bin/env python3
"""Prepare the world-map menu for the native bottom screen (private pack).

Reads the user's own disc: world_map.fsys (the menu's map image and its
sprite sheet) and sys/main.dol (the menu item records, whose x/y place each
destination on the 640x480 map). Writes boot/worldmap.c3w for
platform/3ds/world_map_scene.c. The output is derived from original game
data: keep it private and never commit it.

C3W1 layout (little endian):
  "C3W1", u32 image count, u32 item count
  per image: u32 key, u32 width, u32 height, width*height RGBA8
    key 0 map (320x240), 1 cursor (outline diamond), 2 marker (solid diamond)
  per item: u16 item id, s16 x, s16 y, u16 w, u16 h  (bottom-screen pixels)
"""
import argparse
from pathlib import Path
import struct

from PIL import Image

import prepare_pda as pda

MAP_IMAGE, SPRITES = 0x0C681200, 0x0C691200
ITEM_TABLE, ITEM_BYTES = 0x802EF0A8, 0x1C
DESTINATION_ITEMS = (0x907, 0x908, 0x909, 0x90A, 0x90B, 0x90C, 0x90D, 0x918, 0x919)


def red_box(image, region):
    """Bounding box of the strongly red pixels inside region (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = region
    found = None
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = image.getpixel((x, y))
            if a > 128 and r > 180 and g < 90 and b < 90:
                found = (x, y, x, y) if found is None else (
                    min(found[0], x), min(found[1], y), max(found[2], x), max(found[3], y))
    if found is None:
        raise ValueError("Sprite not found in region %r" % (region,))
    return (found[0], found[1], found[2] + 1, found[3] + 1)


def prepare(disc):
    disc = Path(disc)
    archive = (disc / "files" / "world_map.fsys").read_bytes()
    dol = pda.Dol((disc / "sys" / "main.dol").read_bytes())
    w, h, pixels = pda.decode_texture(pda.read_member(archive, MAP_IMAGE, 1024 * 1024))
    if (w, h) != (640, 480):
        raise ValueError("Unexpected world-map image size %dx%d" % (w, h))
    board = Image.frombytes("RGBA", (w, h), bytes(pixels)).resize((320, 240), Image.LANCZOS)
    w, h, pixels = pda.decode_texture(pda.read_member(archive, SPRITES, 1024 * 1024))
    sheet = Image.frombytes("RGBA", (w, h), bytes(pixels))
    cursor = sheet.crop(red_box(sheet, (24, 30, 66, 70)))
    marker = sheet.crop(red_box(sheet, (24, 68, 46, 90)))
    images = [(0, board), (1, cursor), (2, marker)]
    items = []
    for item in DESTINATION_ITEMS:
        record = dol.read(ITEM_TABLE + item * ITEM_BYTES, ITEM_BYTES)
        x, y, iw, ih = struct.unpack(">hhHH", record[2:10])
        items.append((item, x // 2, y // 2, max(1, iw // 2), max(1, ih // 2)))
    out = bytearray(b"C3W1" + struct.pack("<II", len(images), len(items)))
    for key, image in images:
        out += struct.pack("<III", key, image.width, image.height) + image.tobytes()
    for row in items:
        out += struct.pack("<HhhHH", *row)
    return bytes(out), items


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("disc", type=Path, help="disc dump with files/ and sys/")
    parser.add_argument("output", type=Path, help="pack path, e.g. <sd>/3ds/Colosseum3DS/boot/worldmap.c3w")
    args = parser.parse_args()
    packed, items = prepare(args.disc)
    args.output.write_bytes(packed)
    print("wrote %s: %d bytes, %d destinations" % (args.output, len(packed), len(items)))


if __name__ == "__main__":
    main()
