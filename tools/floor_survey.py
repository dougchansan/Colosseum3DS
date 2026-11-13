#!/usr/bin/env python3
"""Survey every original floor for the native story port (metadata only).

For each lifecycle row with an archive this reads the archive's member list
and the floor REL's _prolog relocations. A floor REL publishes its field
tables to DOL globals (vendor/colosseum/src/rel/*/*.c _prolog):

  lbl_80479748 entrance table, lbl_80479744 -> entrance count
  lbl_80479D30 people table,   lbl_80479D2C -> people count

so the entrance/people offsets and counts that floor_profiles.py rows hold
by review can be derived for every floor. Profiled floors are cross-checked:
any disagreement is reported. Writes a private JSON survey; no original
bytes beyond counts and offsets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_outskirt import member, u32
import prepare_story_index as index
from floor_profiles import FLOORS

ENTRANCES, ENTRANCE_COUNT = 0x80479748, 0x80479744
PEOPLE, PEOPLE_COUNT = 0x80479D30, 0x80479D2C


def members(archive):
    count = u32(archive, 12)
    table = u32(archive, u32(archive, 24))
    return [u32(archive, u32(archive, table + i * 4)) for i in range(count)]


def published(rel):
    """{DOL global: (section, offset)} for each address the REL prolog stores."""
    module = u32(rel, 0)
    records = sorted((key, record) for key, rows in index.relocations(rel).items() for record in rows)
    result = {}
    for i, ((section, offset), record) in enumerate(records):
        if record["import_module"] != 0 or record["type"] != 6:
            continue
        following = [r for k, r in records[i + 1:i + 3] if k[0] == section and r["import_module"] == module]
        if following and following[0]["type"] == 6:
            result[record["addend"]] = (following[0]["source_section"], following[0]["addend"])
    return result


# Each floor REL publishes (FieldTable, count, table) triplets to its own DOL
# slots (the table slot 4 above the count slot, 8 above the FieldTable).
# The slots of different kinds interleave per floor, so a table's kind comes
# from its records instead: entrances are 16 bytes (M1_out lbl_15_data_0,
# 40 words for 10), people 36 bytes (lbl_15_data_A0, 243 words for 27). The
# record size is the gap to the next published object over the count.
RECORD_KINDS = {16: "entrance", 36: "people"}


def floor_tables(rel):
    sections = index.sections(rel)
    tables = published(rel)
    found = {}
    for table in sorted(g for g in tables if g - 4 in tables and g - 8 in tables):
        sec, offset = tables[table]
        csec, coffset = tables[table - 4]
        count = u32(rel, sections[csec]["file_offset"] + coffset)
        if not 1 <= count <= 256:
            continue
        following = sorted(o for s, o in tables.values() if s == sec and o > offset)
        end = following[0] if following else sections[sec]["bytes"]
        size, rest = divmod(end - offset, count)
        kind = RECORD_KINDS.get(size) if not rest else None
        if not kind or kind + "_table" in found:
            continue
        found[kind + "_section"] = sec
        found[kind + "_table"] = offset
        found[kind + "_count"] = count
    return found


def survey(disc, index_path):
    story = json.loads(Path(index_path).read_text())
    rows = []
    for row in story["lifecycle"]:
        name = row.get("archive")
        entry = {"lifecycle_row": row["index"], "floor": row["floor"], "resource_group": row["resource_group"],
                 "archive": name, "main_script": row["main_script"], "init_script": row["init_script"],
                 "pre_script": row["pre_script"], "post_script": row["post_script"]}
        rows.append(entry)
        if not name:
            entry["status"] = "no archive"; continue
        path = Path(disc) / "files" / name
        if not path.is_file():
            entry["status"] = "archive missing"; continue
        archive = path.read_bytes()
        entry["bytes"] = len(archive)
        entry["sha256"] = hashlib.sha256(archive).hexdigest()
        ids = members(archive)
        kinds = {}
        for ident in ids:
            kinds.setdefault((ident >> 8) & 255, []).append(ident)
        entry["members"] = {"%02x" % k: ["%08x" % i for i in v] for k, v in sorted(kinds.items())}
        entry["script"] = kinds.get(0x0e, [None])[0]
        entry["messages"] = kinds.get(0x0a, [None])[0]
        entry["collision"] = kinds.get(0x06, [None])[0]
        entry["maps"] = kinds.get(0x02, [])
        rels = kinds.get(0x1c, [])
        entry["rel"] = rels[0] if rels else None
        if rels:
            rel = member(archive, rels[0])
            entry["module"] = u32(rel, 0)
            entry.update(floor_tables(rel))
        entry["status"] = "ok"
    return rows


def cross_check(rows):
    by_floor = {r["floor"]: r for r in rows}
    problems = []
    for name, profile in FLOORS.items():
        row = by_floor.get(profile.get("floor"))
        if not row:
            continue
        for key in ("module", "entrance_table", "entrance_count", "people_table", "people_count",
                    "script", "messages", "collision", "rel"):
            if key in profile and row.get(key) != profile[key]:
                problems.append("%s %s: profile %r survey %r" % (name, key, profile[key], row.get(key)))
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--index", required=True, type=Path, help="story-source-index.json (prepare_story_index.py)")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    rows = survey(args.disc, args.index)
    problems = cross_check(rows)
    args.output.write_text(json.dumps({"floors": rows, "profile_mismatches": problems}, indent=1) + "\n")
    with_tables = [r for r in rows if "entrance_table" in r]
    print(json.dumps({"floors": len(rows), "with_rel": sum(1 for r in rows if r.get("rel")),
                      "with_entrances": len(with_tables),
                      "with_people": sum(1 for r in rows if "people_table" in r),
                      "with_script": sum(1 for r in rows if r.get("script")),
                      "profile_mismatches": problems}, indent=2))


if __name__ == "__main__":
    main()
