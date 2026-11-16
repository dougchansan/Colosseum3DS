#!/usr/bin/env python3
"""Prepare the original shop stock table (script native 19, menuShopOpen).

menuShopOpen stores its shop number and links to menu floor 0x38F, whose
archive pocket_menu.fsys carries REL module 2. That module's _prolog
publishes its data tables to DOL globals; the shop menu (menuShop.c) reads
lbl_80478E54 as 4-byte shop entries {u8, u8 kind, u16 list offset} and
lbl_80478E44 as the 0-terminated u16 item lists those offsets index, with
their counts in lbl_80478E50 and lbl_80478E40. This tool admits the archive,
checks that the prolog publishes exactly those tables, and writes a private
C3H1 pack of the entries and lists. Prices are not copied: they live in the
item records already prepared for the P*DA (bytes 6-7, big-endian).

Original data; the output must stay private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import member, u32
import prepare_story_index as index

ARCHIVE = ("pocket_menu.fsys", 484256, "33007fb68097f2222738b3f93e559ada98569f7236bdf97caaa12784e334f8b0")
MODULE = 0x03711C00
# DOL global -> (role, count global), from REL module 2's _prolog relocations.
ENTRIES, ENTRY_COUNT = 0x80478E54, 0x80478E50
LISTS, LIST_COUNT = 0x80478E44, 0x80478E40


def published(rel):
    """{DOL global: data-section offset} for every address the prolog stores."""
    records = sorted((key, record) for key, rows in index.relocations(rel).items() for record in rows)
    result = {}
    for i, ((section, offset), record) in enumerate(records):
        if record["import_module"] != 0 or record["type"] != 6:
            continue
        # lis rG, global@ha ; lis rD, data@ha ; addi rD, rD, data@l ; stw rD, global@l(rG)
        following = [r for k, r in records[i + 1:i + 3] if k[0] == section and r["import_module"] == 2]
        if following and following[0]["type"] == 6:
            result[record["addend"]] = (following[0]["source_section"], following[0]["addend"])
    return result


def extract(rel):
    sections = index.sections(rel)
    tables = published(rel)
    for address in (ENTRIES, ENTRY_COUNT, LISTS, LIST_COUNT):
        if address not in tables:
            raise ValueError("Shop module does not publish %08x" % address)
    if len({tables[a][0] for a in (ENTRIES, ENTRY_COUNT, LISTS, LIST_COUNT)}) != 1:
        raise ValueError("Shop tables span sections")
    section = sections[tables[ENTRIES][0]]
    data = rel[section["file_offset"]:section["file_offset"] + section["bytes"]]

    def word(address):
        return u32(data, tables[address][1])

    entry_count, list_count = word(ENTRY_COUNT), word(LIST_COUNT)
    if not 1 <= entry_count <= 64 or not 1 <= list_count <= 1024:
        raise ValueError("Shop table counts exceed their reviewed bounds")
    entries = [data[tables[ENTRIES][1] + i * 4:tables[ENTRIES][1] + i * 4 + 4] for i in range(entry_count)]
    lists = [struct.unpack_from(">H", data, tables[LISTS][1] + i * 2)[0] for i in range(list_count)]
    for entry in entries:
        offset = struct.unpack_from(">H", entry, 2)[0]
        if offset >= list_count or 0 not in lists[offset:]:
            raise ValueError("Shop list offset is out of bounds or unterminated")
    return entries, lists


def encode(entries, lists):
    body = b"".join(bytes([e[0], e[1]]) + struct.pack("<H", struct.unpack_from(">H", e, 2)[0]) for e in entries)
    body += b"".join(struct.pack("<H", item) for item in lists)
    return struct.pack("<4s4I", b"C3H1", 1, 20 + len(body), len(entries), len(lists)) + body


def prepare(disc, output):
    disc = verify_disc(disc)
    output = check_output(output)
    path = disc / "files" / ARCHIVE[0]
    archive = path.read_bytes()
    if path.is_symlink() or len(archive) != ARCHIVE[1] or hashlib.sha256(archive).hexdigest() != ARCHIVE[2]:
        raise ValueError("Original archive differs from admitted GC6E01 revision 0: " + ARCHIVE[0])
    entries, lists = extract(member(archive, MODULE))
    pack = encode(entries, lists)
    output.mkdir(parents=True, exist_ok=True)
    (output / "shops.c3h").write_bytes(pack)
    report = {"file": "shops.c3h", "bytes": len(pack), "sha256": hashlib.sha256(pack).hexdigest(),
              "shops": len(entries), "list_words": len(lists),
              "source": {"archive": ARCHIVE[0], "sha256": ARCHIVE[2], "member": "%08x" % MODULE}}
    (output / "shops-manifest.json").write_bytes((json.dumps(report, indent=2) + "\n").encode())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.disc, args.output), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, "Shop preparation failed: %s\n" % error)


if __name__ == "__main__":
    main()
