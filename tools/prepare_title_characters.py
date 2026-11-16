#!/usr/bin/env python3
"""Extract original title window characters into a private C3TC SD pack."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile

from prepare_boot_media import MAIN_DOL_SHA1
from prepare_bottom_ui import decode_texture, dol_offset
from prepare_game_menu import read_member
from prepare_model import check_output
from prepare_title import admit_archive, ARCHIVE_SHA256, SOURCE_COMMIT

MAX_BYTES = 4 * 1024 * 1024
TEXTURE_IDS = tuple(range(0x100C1200, 0x101D1200, 0x10000))
# fn_8001F304 in pinned gs_title_8001EF78.c encodes these page/column cases;
# unlike coordinates these are instructions, not a DOL data table.
PAGE_COLUMNS = ((0,3),(0,4),(0,0),(0,1),(1,3),(1,4),(1,5),(1,0),(1,1),(1,2),
                (2,3),(2,4),(2,0),(2,1),(2,2),(3,0),(3,1),(3,3),(3,4))


def dol_record(dol, address, size):
    start = dol_offset(dol, address)
    end = dol_offset(dol, address + size - 1)
    if end != start + size - 1 or end >= len(dol):
        raise ValueError("DOL record crosses section boundary")
    return dol[start:end + 1]


def extract_layout(dol):
    items = []
    for index, (page, column) in enumerate(PAGE_COLUMNS):
        item_id = 0xEFB + index
        record = dol_record(dol, 0x802EF0A8 + item_id * 0x1C, 0x1C)
        x, y, w, h, sprite = struct.unpack_from(">5h", record, 2)
        callback = struct.unpack_from(">I", record, 0x14)[0]
        following = struct.unpack_from(">H", record, 0x18)[0]
        if callback != 0x8001F304 or following != (item_id + 1 if index < 18 else 0x2C3):
            raise ValueError("Original title item chain/callback differs")
        source = dol_record(dol, 0x802E7CE8 + sprite * 0x18, 0x18)
        u, v, sw, sh, texture_id = struct.unpack_from(">4HI", source, 8)
        if u or v or (sw, sh) != (w, h) or texture_id not in TEXTURE_IDS:
            raise ValueError("Original title sprite is not a whole reviewed texture")
        items.append((TEXTURE_IDS.index(texture_id), page, column, x, y, w, h, 0))
    return items


def encode_pack(textures, items):
    if len(textures) != 17 or len(items) != 19:
        raise ValueError("Unexpected title character counts")
    table, payload = bytearray(), bytearray()
    start = 32 + 17 * 16 + 19 * 32
    for w, h, rgba in textures:
        if not isinstance(w, int) or not isinstance(h, int) or not 1 <= w <= 512 or not 1 <= h <= 512 or len(rgba) != w*h*4:
            raise ValueError("Invalid title character texture")
        table.extend(struct.pack("<4I", w, h, start + len(payload), len(rgba)))
        payload.extend(rgba)
    records = bytearray()
    for index, item in enumerate(items):
        if len(item) != 8 or any(not isinstance(v, int) for v in item):
            raise ValueError("Invalid title character item")
        tex, page, column, x, y, w, h, reserved = item
        if not 0 <= tex < 17 or (page, column) != PAGE_COLUMNS[index] or reserved or not 0 <= x < 640 or not 0 <= y < 480 or not 1 <= w <= 512 or not 1 <= h <= 480 or x+w > 640 or y+h > 480:
            raise ValueError("Invalid title character layout")
        if (w, h) != textures[tex][:2]:
            raise ValueError("Title character dimensions disagree")
        records.extend(struct.pack("<8i", *item))
    total = start + len(payload)
    if total > MAX_BYTES:
        raise ValueError("Title characters exceed 4 MiB")
    return struct.pack("<4s7I", b"C3TC", 1, total, 17, 19, 4, 0, 0) + table + records + payload


def prepare(disc, output):
    disc = Path(disc)
    for relative in ("", "sys", "sys/main.dol", "files"):
        if (disc / relative).is_symlink():
            raise ValueError("Symbolic links are not accepted in disc inputs")
    source_dol = disc / "sys/main.dol"
    if source_dol.stat().st_size != 3779808:
        raise ValueError("Unexpected main.dol size")
    dol = source_dol.read_bytes()
    if hashlib.sha1(dol).hexdigest() != MAIN_DOL_SHA1:
        raise ValueError("Expected pinned GC6E01 revision 0 main.dol")
    output = check_output(output)
    archive = admit_archive(disc / "files/title.fsys")
    textures = []
    for texture_id in TEXTURE_IDS:
        source = read_member(archive, texture_id, 1024 * 1024)
        if len(source) < 128 or struct.unpack_from(">I", source, 8)[0] != 0x90:
            raise ValueError("Expected original RGB5A3 title character")
        textures.append(decode_texture(source))
    items = extract_layout(dol)
    pack = encode_pack(textures, items)
    manifest = {"format":"C3TC", "version":1, "bytes":len(pack),
        "sha256":hashlib.sha256(pack).hexdigest(), "source_commit":SOURCE_COMMIT,
        "main_dol_sha1":MAIN_DOL_SHA1, "archive_sha256":ARCHIVE_SHA256,
        "textures":[{"file_id":hex(i),"width":t[0],"height":t[1],"sha256":hashlib.sha256(t[2]).hexdigest()} for i,t in zip(TEXTURE_IDS,textures)],
        "items":[{"item_id":hex(0xEFB+i),"texture_index":v[0],"page":v[1],"column":v[2],"rect":v[3:7]} for i,v in enumerate(items)],
        "layout":"Original 640x480 coordinates; page/column cases from pinned fn_8001F304; whole top-down RGBA8 textures without resampling."}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".title-characters-", dir=output.parent) as temp:
        staged = Path(temp) / "characters.c3tc"
        staged.write_bytes(pack)
        sidecar = staged.with_suffix(".c3tc.json")
        with sidecar.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(manifest, indent=2) + "\n")
        target_manifest = output.with_suffix(output.suffix + ".json")
        if output.exists() or target_manifest.exists():
            raise ValueError("Output appeared during conversion")
        staged.rename(output)
        sidecar.rename(target_manifest)
    print("Prepared %s (%d bytes)" % (output, len(pack)))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        prepare(args.disc, args.output)
    except (ValueError, OSError, struct.error) as error:
        parser.exit(1, "Title character preparation failed: %s\n" % error)


if __name__ == "__main__":
    main()
