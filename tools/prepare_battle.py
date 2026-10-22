#!/usr/bin/env python3
"""Extract the exact private encounter-10 records; no outcome or combat is generated."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_game_state import COMMON_BYTES, COMMON_SHA1, SOURCE_COMMIT, u16, u32
from prepare_model import check_output

HEADER_SIZE = 128
PACK_SIZE = 644
TABLES = {
    "encounter": (0x109FBC, 0x14A6B8, 56, 566),
    "floor": (0x8F974, 0x14A68C, 24, 50),
    "kind": (0x90F4C, 0x14A694, 32, 19),
    "type": (0x9164C, 0x14A6A8, 8, 7),
    "trainer": (0x91684, 0x14A6AC, 52, 820),
    "trainer_kind": (0x8F724, 0x14A684, 12, 41),
    "side": (0x8F910, 0x14A688, 20, 5),
    "pokemon": (0x9E5DC, 0x14A6B4, 80, 5510),
}


def make_pack(disc):
    disc = verify_disc(disc)
    path = disc / "files/common_rel.rel"
    if path.is_symlink() or path.stat().st_size != COMMON_BYTES:
        raise ValueError("Unexpected common_rel.rel path or size")
    rel = path.read_bytes()
    if hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common_rel.rel differs from the admitted GC6E01 original")
    sections = u32(rel, 0x10)
    if u32(rel, 0x0C) != 11:
        raise ValueError("Unexpected common REL section count")
    base = u32(rel, sections + 32)
    if base != 0x184C or u32(rel, sections + 36) != 0x14A714:
        raise ValueError("Unexpected common REL data section")
    spans = []

    def record(table, index):
        offset, count_at, stride, expected = TABLES[table]
        count = u32(rel, base + count_at)
        if count != expected or not 0 <= index < count:
            raise ValueError(f"Invalid original {table} index/count")
        start = base + offset + index * stride
        if start + stride > len(rel):
            raise ValueError("Truncated original battle table")
        data = rel[start:start + stride]
        spans.append({"table": table, "index": index, "file_offset": start,
                      "bytes": stride, "sha256": hashlib.sha256(data).hexdigest()})
        return data

    encounter = record("encounter", 10)
    if encounter[:3] != bytes((2, 1, 1)) or u16(encounter, 4) != 5:
        raise ValueError("Encounter 10 differs from reviewed battle kind/type/floor")
    slots = [(u16(encounter, 24 + i * 8), u32(encounter, 28 + i * 8)) for i in range(4)]
    if slots != [(1, 1), (10, 0), (0, 0), (0, 0)]:
        raise ValueError("Unexpected encounter trainer/input slots")
    floor = record("floor", u16(encounter, 4))
    kind = record("kind", encounter[0])
    fight_type = record("type", encounter[1])
    trainers = [record("trainer", slots[i][0]) for i in range(2)]
    trainer_kind = record("trainer_kind", u16(trainers[1], 2))
    sides = [record("side", u16(floor, 20 + i * 2)) for i in range(2)]
    # fightTrainerCreateFightTrainerDataIdToHero reads field 5, then creates
    # consecutive records until species==0. It is not the trainer item array.
    first = u16(trainers[1], 4)
    roster = [record("pokemon", first + i) for i in range(3)]
    if first != 238 or [u16(p, 10) for p in roster] != [288, 288, 0] or \
       [p[4] for p in roster[:2]] != [24, 24] or fight_type[:3] != bytes((1, 6, 2)):
        raise ValueError("Unexpected first-battle roster or double-battle limits")
    body = b"".join([encounter, floor, kind, fight_type, *trainers, trainer_kind, *sides, *roster])
    header = bytearray(HEADER_SIZE)
    struct.pack_into(">4sIII", header, 0, b"C3B1", 1, PACK_SIZE, 10)
    header[16:36] = bytes.fromhex(SOURCE_COMMIT)
    header[36:56] = bytes.fromhex(COMMON_SHA1)
    header[56:76] = hashlib.sha1(body).digest()
    pack = bytes(header) + body
    if len(pack) != PACK_SIZE:
        raise ValueError("Internal battle record layout mismatch")
    report = {"format": "C3B1", "source_commit": SOURCE_COMMIT,
              "common_rel_sha1": COMMON_SHA1, "encounter": 10,
              "battle_floor": u16(floor, 2), "source_spans": spans,
              "roster": [{"record": first+i, "species": u16(p, 10), "level": p[4],
                          "moves": [struct.unpack_from(">i", p, 52+j*8)[0] for j in range(4)]}
                         for i, p in enumerate(roster[:2])],
              "bytes": len(pack), "sha256": hashlib.sha256(pack).hexdigest(),
              "scope": "Original encounter data only; combat backend unavailable; no result or save writes"}
    return pack, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = check_output(args.output)
    pack, report = make_pack(args.disc)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(pack)
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
