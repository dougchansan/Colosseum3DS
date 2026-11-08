#!/usr/bin/env python3
"""Prepare the private trainer-entry table pack (C3E1) from original GC6E01 files.

The pack holds original table rows the trainer-entry session reads: encounter,
floor, trainer, side, type, kind, species, roster, AI, move, nature, message,
item, growth, party-sort type, sequence-loader and action tables. It holds no
generated Pokemon, RNG results, saves or combat outcomes. Keep it private: it
is original game data and must never be added to the source checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
REL_SHA1 = "e816761813900aa0fd4f87d012b3b2a19d2f003e"
REL_BYTES = 1369520
DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
DOL_BYTES = 3779808
REL_BASE = 0x184C  # common_rel data section file offset
ENTRY_FLOW = 0x8020D1FC  # fightActionFlowKaisiNyuujouTrainer
VERSION = 1
HEADER_BYTES = 128

# tag, REL data offset, count location, stride, extra boundary rows
REL_TABLES = (
    (b"ENC ", 0x109FBC, 0x14A6B8, 56, 0),
    (b"FLR ", 0x08F974, 0x14A68C, 24, 0),
    (b"TRN ", 0x091684, 0x14A6AC, 52, 0),
    (b"SID ", 0x08F910, 0x14A688, 20, 0),
    (b"TYP ", 0x09164C, 0x14A6A8, 8, 1),   # getter admits index == count
    (b"KND ", 0x090F4C, 0x14A694, 32, 1),  # getter admits index == count
    (b"SPC ", 0x121A04, 0x14A6E0, 284, 0),
    (b"RST ", 0x09E5DC, 0x14A6B4, 80, 0),
    (b"AI  ", 0x09BD14, 0x14A6B0, 40, 0),
)
EXPECTED_COUNTS = {b"ENC ": 566, b"FLR ": 50, b"TRN ": 820, b"SID ": 5, b"TYP ": 7,
                   b"KND ": 19, b"SPC ": 413, b"RST ": 5510, b"AI  ": 261}


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def take(data, offset, size, label):
    if offset < 0 or size <= 0 or offset + size > len(data):
        raise ValueError("Original table extent is invalid: " + label)
    return data[offset:offset + size]


def dol_read(dol, address, size, label):
    for i in range(18):
        start, extent = u32(dol, 0x48 + i * 4), u32(dol, 0x90 + i * 4)
        if start <= address and address + size <= start + extent:
            return take(dol, u32(dol, i * 4) + address - start, size, label)
    raise ValueError("Original DOL address is not mapped: " + label)


def make_pack(rel, dol):
    if len(rel) != REL_BYTES or hashlib.sha1(rel).hexdigest() != REL_SHA1:
        raise ValueError("common_rel.rel differs from canonical GC6E01")
    if len(dol) != DOL_BYTES or hashlib.sha1(dol).hexdigest() != DOL_SHA1:
        raise ValueError("main.dol differs from canonical GC6E01")
    sections = []  # (tag, bytes, count)
    for tag, offset, count_at, stride, extra in REL_TABLES:
        count = u32(rel, REL_BASE + count_at)
        if count != EXPECTED_COUNTS[tag]:
            raise ValueError("Unexpected original count for " + tag.decode())
        sections.append((tag, take(rel, REL_BASE + offset, (count + extra) * stride, tag.decode()), count))
    moves = take(rel, REL_BASE + 0x11C7C4, 358 * 56, "moves")
    natures = take(rel, REL_BASE + 0x121614, 25 * 40, "natures")
    rates = take(rel, REL_BASE + 0x13E430, 8 * 2, "nature_rates")
    for offset, count in ((0x14BF20, 358), (0x14BF24, 25), (0x14BF30, 8)):
        if u32(rel, offset) != count:
            raise ValueError("Unexpected original move/nature counts")
    sections += [(b"MOV ", moves, 358), (b"NAT ", natures, 25), (b"RAT ", rates, 8)]
    message = take(rel, REL_BASE + 0x58044, 0xC770, "message group 0")
    message_count = u16(message, 4)
    if message_count != 2018 or 16 + message_count * 8 > len(message):
        raise ValueError("Unexpected original message group header")
    sections.append((b"MSG ", message, message_count))

    item_count = u32(dol_read(dol, 0x80478BD8, 4, "item_count"), 0)
    slot_count = u32(dol_read(dol, 0x80478BB0, 4, "item_slots"), 0)
    growth_count = u32(dol_read(dol, 0x80478B60, 4, "growth_count"), 0)
    type_count = u32(dol_read(dol, 0x80478B38, 4, "type_count"), 0)
    special_trainer = u32(dol_read(dol, 0x80478960, 4, "special_trainer"), 0)
    loader_count = u32(dol_read(dol, 0x80478CC8, 4, "sequence_count"), 0)
    kind_count = u32(dol_read(dol, 0x80478D48, 4, "action_kind_count"), 0)
    if (item_count, slot_count, growth_count, type_count, special_trainer, loader_count) != \
            (548, 397, 8, 18, 9, 76) or kind_count != 20:
        raise ValueError("Unexpected original DOL scalar")
    sections.append((b"ITX ", dol_read(dol, 0x803681E8, item_count * 2, "item_index"), item_count))
    sections.append((b"ITM ", dol_read(dol, 0x80363CE8, slot_count * 40, "item_rows"), slot_count))
    sections.append((b"GRW ", dol_read(dol, 0x8035E940, growth_count * 0x194, "growth"), growth_count))
    sections.append((b"T44 ", dol_read(dol, 0x8035B500, type_count * 44, "party_type"), type_count))
    sections.append((b"SEQ ", dol_read(dol, 0x80370840, loader_count * 12, "sequence_loader"), loader_count))

    kinds = dol_read(dol, 0x80375BB8, kind_count * 12, "action_kinds")
    if u32(kinds, 3 * 12 + 4) != ENTRY_FLOW:
        raise ValueError("Original action kind 3 does not flow to trainer entry")
    sections.append((b"AKD ", kinds, kind_count))
    actions = bytearray()
    action_rows = 0
    while True:
        row = dol_read(dol, 0x80375CC8 + action_rows * 8, 8, "action_data")
        actions += row
        action_rows += 1
        if u16(row, 0) == 0:
            break
        if action_rows > 64:
            raise ValueError("Original action data has no bounded terminator")
    order = [(u16(actions, i * 8), u32(actions, i * 8 + 4)) for i in range(action_rows - 1)]
    if (3, 4) not in order or (3, 5) not in order:
        raise ValueError("Original action data lacks trainer-entry rows")
    sections.append((b"ACT ", bytes(actions), action_rows))
    scalars = struct.pack(">IIII", item_count, special_trainer, 0x80375CC8, 8)
    sections.append((b"VAL ", scalars, 4))

    directory = bytearray()
    body = bytearray()
    offset = HEADER_BYTES + 16 * len(sections)
    spans = []
    for tag, data, count in sections:
        pad = (-offset) % 4
        body += b"\0" * pad
        offset += pad
        directory += struct.pack(">4sIII", tag, offset, len(data), count)
        spans.append({"tag": tag.decode().strip(), "bytes": len(data), "count": count,
                      "sha256": hashlib.sha256(data).hexdigest()})
        body += data
        offset += len(data)
    header = bytearray(HEADER_BYTES)
    struct.pack_into(">4sIII", header, 0, b"C3E1", VERSION, offset, len(sections))
    header[16:36] = bytes.fromhex(SOURCE_COMMIT)
    header[36:56] = bytes.fromhex(REL_SHA1)
    header[56:76] = bytes.fromhex(DOL_SHA1)
    header[76:96] = hashlib.sha1(bytes(directory + body)).digest()
    pack = bytes(header + directory + body)
    report = {"format": "C3E1", "version": VERSION, "bytes": len(pack),
              "sha256": hashlib.sha256(pack).hexdigest(), "source_commit": SOURCE_COMMIT,
              "common_rel_sha1": REL_SHA1, "dol_sha1": DOL_SHA1, "sections": spans,
              "scope": "Original table rows only; no generated Pokemon, RNG, save or result"}
    return pack, report


def write_pack(rel_path, dol_path, output):
    output = Path(output).expanduser().absolute()
    root = Path(__file__).resolve().parents[1]
    resolved = output.resolve()
    if resolved == root or root in resolved.parents:
        raise ValueError("Refusing to write private original tables inside the checkout")
    metadata = output.with_suffix(output.suffix + ".json")
    if output.exists() or metadata.exists():
        raise ValueError("Refusing to overwrite an existing pack or manifest")
    pack, report = make_pack(Path(rel_path).read_bytes(), Path(dol_path).read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(pack)
    with metadata.open("x", encoding="utf8", newline="\n") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--common-rel", required=True, type=Path)
    parser.add_argument("--dol", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = write_pack(args.common_rel, args.dol, args.output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Trainer-entry pack preparation failed: {error}\n")
    print(json.dumps({k: report[k] for k in ("format", "bytes", "sha256")}, indent=2))


if __name__ == "__main__":
    main()
