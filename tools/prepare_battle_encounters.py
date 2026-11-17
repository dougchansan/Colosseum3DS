#!/usr/bin/env python3
"""Prepare the private story-battle pack (C3B2) and list every requested encounter.

The pack holds original big-endian rows only: every encounter row with its
floor, kind, type, opponent trainer, trainer kind and up to six trainer
Pokemon records, plus the move, species, type-chart and growth tables the
native general battle (src/battle_general.c) reads, and (version 2) the
Shadow table that snagging reads, and (version 3) the side-0 CPU trainer
of encounters the hero does not fight in (525). Nothing is generated.

With --stories, every C3S1 story pack in the given directories is scanned for
native 0x98/0x66 call sites; the JSON report then lists each requested
encounter with whether the general battle admits it, or why not.

    python3 tools/prepare_battle_encounters.py --disc <extraction> \
        --common-rel <canonical common_rel.rel> --output private/battles.c3x \
        --stories private/world/boot /boot
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

HEADER_SIZE = 128
ENTRY_SIZE = 672
MAGIC = b"C3B2"
REL_DATA = 0x184C
# name: (row offset, count offset, stride, expected count) in the common REL data.
TABLES = {
    "encounter": (0x109FBC, 0x14A6B8, 56, 566),
    "floor": (0x8F974, 0x14A68C, 24, 50),
    "kind": (0x90F4C, 0x14A694, 32, 19),
    "type": (0x9164C, 0x14A6A8, 8, 7),
    "trainer": (0x91684, 0x14A6AC, 52, 820),
    "trainer_kind": (0x8F724, 0x14A684, 12, 41),
    "pokemon": (0x9E5DC, 0x14A6B4, 80, 5510),
    "move": (0x11C7C4, 0x14A6D4, 56, 358),
    "species": (0x121A04, 0x14A6E0, 284, 413),
}
# Shadow table (fn_801EEE44 catch rate +0, field08 gauge +8, snag flag +0x12),
# ids 0..0x60, appended as pack version 2.
SHADOW = (0x1439D8, 0x38, 0x61)
TYPE_CHART = (0x8035B500, 44, 18)   # rows x 0x2C, gs_colsys.c fn_8010C508
GROWTH = (0x8035E940, 0x194, 8)     # u32 exp[101] per group
NAME_SIZE = 16
# Common REL message groups (tools/prepare_pda.py): species +0x18, move +0x20,
# trainer +8 and trainer kind +4 hold UTF-16BE message IDs.
MESSAGE_GROUPS = ((0x58044, 0x647B4), (0x647B4, None))
# Version 3: a player-side section for encounters whose side 0 is a CPU
# trainer instead of the hero (fightEncount slot 0 != (1, 1), e.g. 525,
# Eagun vs Cipher Peon Skrub): u32 count, then per encounter u16 id, u8
# party count, u8 0, the trainer row (52), its trainer kind row (12), six
# 80-byte Pokemon records and two 16-byte names (trainer, title).
SIDE0_ROW = 4 + 52 + 12 + 6 * 80 + 2 * NAME_SIZE
REASONS = {0: "admitted", 1: "player side is not the hero alone",
           2: "more than one opponent trainer", 3: "opponent has no Pokemon",
           4: "unsupported battle type", 5: "encounter index out of range"}


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


class Rel:
    """Bounded row reader over a pinned common REL image."""

    def __init__(self, rel):
        self.rel = rel

    def count(self, table):
        offset, count_at, stride, expected = TABLES[table]
        count = u32(self.rel, REL_DATA + count_at)
        if count != expected:
            raise ValueError(f"Unexpected original {table} count {count}")
        return count

    def row(self, table, index):
        offset, _, stride, _ = TABLES[table]
        if not 0 <= index < self.count(table):
            raise ValueError(f"Invalid original {table} index {index}")
        start = REL_DATA + offset + index * stride
        if start + stride > len(self.rel):
            raise ValueError("Truncated original table")
        return self.rel[start:start + stride]

    def table(self, name):
        offset, _, stride, _ = TABLES[name]
        start = REL_DATA + offset
        return self.rel[start:start + stride * self.count(name)]


def roster(rel, trainer):
    """fightTrainerCreateFightTrainerDataIdToHero: consecutive records from
    trainer field +4 until species 0, at most six."""
    first = u16(trainer, 4)
    party = []
    for index in range(first, min(first + 6, rel.count("pokemon"))):
        record = rel.row("pokemon", index)
        if not u16(record, 10):
            break
        party.append(record)
    return party


def side0_roster(rel, slot):
    """A CPU trainer on side 0 (fightTrainerCreate for slot 0 when it is not
    the hero): its trainer row, kind row and roster, or None."""
    if slot == (1, 1) or not slot[0]:
        return None
    try:
        trainer = rel.row("trainer", slot[0])
        kind = rel.row("trainer_kind", u16(trainer, 2))
    except ValueError:
        return None
    party = roster(rel, trainer)
    return (trainer, kind, party) if party else None


def side0_section(rel, rel_bytes):
    table = message_names(rel_bytes)
    rows = []
    for index in range(rel.count("encounter")):
        encounter = rel.row("encounter", index)
        side0 = side0_roster(rel, (u16(encounter, 24), u32(encounter, 28)))
        _, reason, _ = encounter_entry(rel, index)
        if not side0 or reason:
            continue
        trainer, kind, party = side0
        row = bytearray(SIDE0_ROW)
        struct.pack_into(">HB", row, 0, index, len(party))
        row[4:56] = trainer
        row[56:68] = kind
        for i, record in enumerate(party):
            row[68 + i * 80:148 + i * 80] = record
        row[548:564] = ascii_name(table, u32(trainer, 8))
        row[564:580] = ascii_name(table, u32(kind, 4))
        rows.append(bytes(row))
    return struct.pack(">I", len(rows)) + b"".join(rows), len(rows)


def encounter_entry(rel, index):
    entry = bytearray(ENTRY_SIZE)
    struct.pack_into(">H", entry, 0, index)
    encounter = rel.row("encounter", index)
    slots = [(u16(encounter, 24 + i * 8), u32(encounter, 28 + i * 8)) for i in range(4)]
    reason = 0
    try:
        floor = rel.row("floor", u16(encounter, 4))
        kind = rel.row("kind", encounter[0])
        fight_type = rel.row("type", encounter[1])
    except ValueError:
        return bytes(entry[:4]) + bytes(ENTRY_SIZE - 4), 5, []
    opponents = [s for s in slots[1:] if s[0]]
    side0 = side0_roster(rel, slots[0])
    if slots[0] != (1, 1) and not side0:
        reason = 1
    elif len(opponents) != 1:
        reason = 2
    elif fight_type[0] != 1 or fight_type[2] not in (1, 2) or not 1 <= fight_type[1] <= 6:
        reason = 4
    trainer = rel.row("trainer", opponents[0][0]) if opponents else bytes(52)
    trainer_kind = rel.row("trainer_kind", u16(trainer, 2)) if opponents else bytes(12)
    party = roster(rel, trainer) if opponents else []
    if not reason and not party:
        reason = 3
    entry[2] = len(party)
    struct.pack_into(">H", entry, 4, reason)
    entry[8:64] = encounter
    entry[64:88] = floor
    entry[88:120] = kind
    entry[120:128] = fight_type
    entry[128:180] = trainer
    entry[180:192] = trainer_kind
    for i, record in enumerate(party):
        entry[192 + i * 80:272 + i * 80] = record
    return bytes(entry), reason, party


def dol_read(dol, address, size):
    """Read from a GameCube DOL by virtual address (text/data sections)."""
    for i in range(18):
        offset, start, length = u32(dol, i * 4), u32(dol, 0x48 + i * 4), u32(dol, 0x90 + i * 4)
        if length and start <= address and address + size <= start + length:
            return dol[offset + address - start:offset + address - start + size]
    raise ValueError(f"DOL address {address:#x} outside every section")


def message_names(rel_bytes):
    from prepare_pda import messages
    table = {}
    for start, end in MESSAGE_GROUPS:
        table.update(messages(rel_bytes[REL_DATA + start:REL_DATA + end if end else None]))
    return table


def ascii_name(table, message):
    """Fixed 16-byte display name; accented letters fold to ASCII."""
    text = table.get(message, b"").decode("utf-16-be", "replace").split(chr(0))[0]
    folded = "".join({chr(0xE9): "e", chr(0xC9): "E", chr(0x2642): "M", chr(0x2640): "F"}.get(c, c)
                     for c in text)
    data = "".join(c if 32 <= ord(c) < 127 else "?" for c in folded).encode("ascii")[:NAME_SIZE - 1]
    return data + bytes(NAME_SIZE - len(data))


def names_section(rel, rel_bytes, entries):
    table = message_names(rel_bytes)
    out = bytearray()
    for i in range(rel.count("species")):
        out += ascii_name(table, u32(rel.row("species", i), 0x18))
    for i in range(rel.count("move")):
        out += ascii_name(table, u32(rel.row("move", i), 0x20))
    for entry in entries:
        out += ascii_name(table, u32(entry, 128 + 8)) + ascii_name(table, u32(entry, 180 + 4))
    return bytes(out)


def make_pack(rel_bytes, dol_bytes, source_commit=None, common_sha1=None, dol_sha1=None):
    rel = Rel(rel_bytes)
    count = rel.count("encounter")
    moves, species = rel.table("move"), rel.table("species")
    types = dol_read(dol_bytes, TYPE_CHART[0], TYPE_CHART[1] * TYPE_CHART[2])
    growth = dol_read(dol_bytes, GROWTH[0], GROWTH[1] * GROWTH[2])
    entries, summary = [], []
    for index in range(count):
        entry, reason, party = encounter_entry(rel, index)
        entries.append(entry)
        summary.append({"encounter": index, "reason": reason, "party": len(party),
                        "species": [u16(p, 10) for p in party],
                        "shadow": [p[3] for p in party],
                        "levels": [p[4] for p in party]})
    moves_at = HEADER_SIZE
    species_at = moves_at + len(moves)
    types_at = species_at + len(species)
    growth_at = types_at + len(types)
    entries_at = growth_at + len(growth)
    shadow = rel_bytes[REL_DATA + SHADOW[0]:REL_DATA + SHADOW[0] + SHADOW[1] * SHADOW[2]]
    side0, _ = side0_section(rel, rel_bytes)
    body = (moves + species + types + growth + b"".join(entries) + names_section(rel, rel_bytes, entries)
            + shadow + side0)
    header = bytearray(HEADER_SIZE)
    struct.pack_into(">4s7I", header, 0, MAGIC, 3, HEADER_SIZE + len(body), count,
                     rel.count("move"), rel.count("species"), TYPE_CHART[2], GROWTH[2])
    if source_commit:
        header[32:52] = bytes.fromhex(source_commit)
    if common_sha1:
        header[52:72] = bytes.fromhex(common_sha1)
    header[72:92] = hashlib.sha1(body).digest()
    if dol_sha1:
        header[92:112] = bytes.fromhex(dol_sha1)
    struct.pack_into(">4I", header, 112, species_at, types_at, growth_at, entries_at)
    return bytes(header) + body, summary


NATIVE_CALL = re.compile(r"; native (98|66)\((.*)\)")


def scan_story_pack(path):
    """Yield (floor, bank, function, native, first-argument text) per call site."""
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from gsvm_disasm import disassemble
    data = Path(path).read_bytes()
    magic, _, _, floor, banks, _, bank_dir, _ = struct.unpack_from("<4s7I", data, 0)
    if magic != b"C3S1":
        raise ValueError(f"{path} is not a C3S1 story pack")
    for i in range(banks):
        bank, offset, length, _ = struct.unpack_from("<4I", data, bank_dir + 16 * i)
        function = None
        for line in disassemble(data[offset:offset + length]):
            if line.startswith("; function"):
                function = int(line.split()[2])
            match = NATIVE_CALL.search(line)
            if match:
                first = match.group(2).split(",")[0].strip()
                yield floor, bank, function, int(match.group(1), 16), first


def requested(story_dirs):
    sites = []
    for directory in story_dirs:
        for path in sorted(Path(directory).glob("*.c3s")):
            for floor, bank, function, native, first in scan_story_pack(path):
                encounter = int(first, 16) if re.fullmatch(r"0x[0-9a-f]+", first) else None
                sites.append({"pack": path.name, "floor": floor, "bank": f"{bank:04x}",
                              "function": function, "native": f"{native:02x}",
                              "encounter": encounter, "argument": first})
    return sites


def report(summary, sites):
    by_id = {s["encounter"]: s for s in summary}
    literal = sorted({s["encounter"] for s in sites if s["encounter"] is not None})
    rows = []
    for encounter in literal:
        entry = by_id.get(encounter)
        reason = entry["reason"] if entry else 5
        rows.append({"encounter": encounter, "status": "playable" if reason == 0 else "blocked",
                     "reason": REASONS[reason],
                     "floors": sorted({s["floor"] for s in sites if s["encounter"] == encounter}),
                     **({k: entry[k] for k in ("species", "levels", "shadow")} if entry else {})})
    computed = [s for s in sites if s["encounter"] is None]
    return {"requested_literal": len(literal), "call_sites": len(sites),
            "computed_sites": computed, "encounters": rows,
            "playable": sum(r["status"] == "playable" for r in rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--common-rel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--stories", type=Path, nargs="*", default=[])
    args = parser.parse_args()
    from prepare_boot_media import verify_disc, MAIN_DOL_SHA1
    from prepare_game_state import COMMON_SHA1, COMMON_BYTES, SOURCE_COMMIT
    from prepare_model import check_output
    output = check_output(args.output)
    disc = verify_disc(args.disc)
    rel = args.common_rel.read_bytes()
    if args.common_rel.is_symlink() or len(rel) != COMMON_BYTES or \
            hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common_rel.rel differs from the admitted GC6E01 original")
    pack, summary = make_pack(rel, (disc / "sys/main.dol").read_bytes(),
                              SOURCE_COMMIT, COMMON_SHA1, MAIN_DOL_SHA1)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(pack)
    result = {"format": "C3B2", "bytes": len(pack), "sha256": hashlib.sha256(pack).hexdigest(),
              "encounters_packed": len(summary),
              "admitted": sum(s["reason"] == 0 for s in summary)}
    if args.stories:
        result["requested"] = report(summary, requested(args.stories))
    output.with_suffix(output.suffix + ".json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "requested"}, indent=1))
    if "requested" in result:
        r = result["requested"]
        print(f"requested {r['requested_literal']} literal encounters at {r['call_sites']} sites, "
              f"{r['playable']} playable, {len(r['computed_sites'])} computed sites")


if __name__ == "__main__":
    main()
