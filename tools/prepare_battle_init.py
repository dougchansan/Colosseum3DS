#!/usr/bin/env python3
"""Prepare private encounter-10 initialization tables from canonical common REL.

No Pokemon, RNG results, saves, combat outcomes, or executable bytes are emitted.
The checksum is integrity-only; provenance is the exact original input hash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
COMMON_SHA1 = "e816761813900aa0fd4f87d012b3b2a19d2f003e"
COMMON_BYTES = 1369520
PACK_BYTES = 768
BASE = 0x184C
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


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def make_pack(rel):
    if len(rel) != COMMON_BYTES or hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common_rel.rel differs from canonical GC6E01")
    sections = u32(rel, 0x10)
    if u32(rel, 12) != 11 or u32(rel, sections + 32) != BASE or \
            u32(rel, sections + 36) != 0x14A714:
        raise ValueError("Unexpected original common REL section layout")
    spans = []

    def take(offset, size, label):
        if offset < 0 or size < 0 or offset + size > len(rel):
            raise ValueError("Original table extent is invalid: " + label)
        data = rel[offset:offset + size]
        spans.append({"label": label, "file_offset": offset, "bytes": size,
                      "sha256": hashlib.sha256(data).hexdigest()})
        return data

    def record(table, index):
        offset, count_at, stride, expected = TABLES[table]
        if u32(rel, BASE + count_at) != expected or not 0 <= index < expected:
            raise ValueError("Invalid original battle table count/index")
        return take(BASE + offset + stride * index, stride, f"{table}[{index}]")

    encounter = record("encounter", 10)
    if encounter[:3] != bytes((2, 1, 1)) or u16(encounter, 4) != 5:
        raise ValueError("Unsupported encounter profile")
    slots = [(u16(encounter, 24 + i * 8), u32(encounter, 28 + i * 8)) for i in range(4)]
    if slots != [(1, 1), (10, 0), (0, 0), (0, 0)]:
        raise ValueError("Unsupported trainer/input mapping")
    floor = record("floor", 5)
    kind, fight_type = record("kind", 2), record("type", 1)
    trainers = [record("trainer", 1), record("trainer", 10)]
    trainer_kind = record("trainer_kind", u16(trainers[1], 2))
    sides = [record("side", u16(floor, 20 + i * 2)) for i in range(2)]
    first = u16(trainers[1], 4)
    roster = [record("pokemon", first + i) for i in range(3)]
    if first != 238 or [u16(r, 10) for r in roster] != [288, 288, 0] or \
            [r[4] for r in roster[:2]] != [24, 24] or fight_type[:3] != bytes((1, 6, 2)):
        raise ValueError("Unsupported opponent roster")
    battle_payload = b"".join([encounter, floor, kind, fight_type, *trainers,
                               trainer_kind, *sides, *roster])
    if len(battle_payload) != 516:
        raise ValueError("Battle profile layout differs from C3B1")

    species = take(BASE + 0x121A04 + 288 * 0x11C, 0x11C, "species[288]")
    payload = bytearray(640)
    struct.pack_into(">H", payload, 0, 288)
    payload[2:5] = bytes((species[2], species[0x32], species[0x33]))
    payload[8:20] = species[0x84:0x90]
    payload[20:22] = species[8:10]
    for n in range(25):
        row = take(BASE + 0x121614 + n * 0x28, 0x28, f"nature[{n}]")
        for stat in range(5):
            index = row[5 + stat]
            if index >= 8:
                raise ValueError("Invalid nature-rate index")
            rate = take(BASE + 0x13E430 + index * 2, 2, f"nature_rate[{index}]")
            if rate not in (b"\x5a\x64", b"\x64\x64", b"\x6e\x64"):
                raise ValueError("Unsupported nature ratio")
            at = 32 + n * 10 + stat * 2
            payload[at:at + 2] = rate
    moves = take(BASE + 0x11C7C4, 358 * 0x38, "move_table[0:358]")
    for i in range(358):
        payload[282 + i] = moves[i * 0x38 + 1]
    if payload[282] or any(x > 40 for x in payload[282:]):
        raise ValueError("Unsupported move-PP table")

    header = bytearray(128)
    struct.pack_into(">4sIII", header, 0, b"C3I1", 1, PACK_BYTES, 10)
    header[16:36] = bytes.fromhex(SOURCE_COMMIT)
    header[36:56] = bytes.fromhex(COMMON_SHA1)
    header[56:76] = hashlib.sha1(payload).digest()
    header[76:96] = hashlib.sha1(battle_payload).digest()
    pack = bytes(header + payload)
    report = {"format": "C3I1", "source_commit": SOURCE_COMMIT,
              "common_rel_sha1": COMMON_SHA1, "encounter": 10,
              "battle_payload_sha1": header[76:96].hex(), "bytes": len(pack),
              "sha256": hashlib.sha256(pack).hexdigest(), "source_spans": spans,
              "scope": "Private source tables only; not generated Pokemon or a combat result"}
    return pack, report


def write_pack(source, output):
    source, output = Path(source), Path(output)
    if source.is_symlink() or not source.is_file() or source.stat().st_size != COMMON_BYTES:
        raise ValueError("Expected a regular canonical common_rel.rel file")
    output = output.expanduser().absolute()
    root = Path(__file__).resolve().parents[1]
    resolved = output.resolve()
    # Private binary tables must not be added under the source checkout.
    if resolved == root or root in resolved.parents:
        raise ValueError("Initialization packs must be written outside the repository")
    metadata = output.with_suffix(output.suffix + ".json")
    if output.exists() or output.is_symlink() or metadata.exists() or metadata.is_symlink():
        raise ValueError("Refusing to overwrite an existing pack or manifest")
    pack, report = make_pack(source.read_bytes())
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
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = write_pack(args.common_rel, args.output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"Battle initialization preparation failed: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
