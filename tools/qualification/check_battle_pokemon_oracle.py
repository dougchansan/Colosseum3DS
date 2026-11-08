#!/usr/bin/env python3
"""Compare the native temporary constructor with private original entry/returns.

The admitted corpus is the two ordinary Willie roster calls (238/239) at the
reviewed caller. Original inputs, byte strings and outputs stay private. This
checker never manufactures an observation or uses return bytes as input data.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
REL_SHA1 = "e816761813900aa0fd4f87d012b3b2a19d2f003e"
DOL_BYTES, REL_BYTES = 3779808, 1369520
DOL_SHA256 = "7e6c00a3bd632126d5466cbab856c63ac3c52a5da20b60dba7e756538152c9f4"
GDB_SHA256 = "dbc772fd356f490cab2a6d8ed6ec77cbcd35f647d01e0b0b06542cbd750abae1"
ENTRY, CALLER, RETURN = 0x801F9F78, 0x801F9E38, 0x801F9E3C
RETRY_LIMIT = 65536
ROSTERS = (238, 239)
U8 = ctypes.c_uint8
BytePointer = ctypes.POINTER(U8)
MessageCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32,
                                  ctypes.POINTER(BytePointer), ctypes.POINTER(ctypes.c_size_t))


class Rng(ctypes.Structure):
    _fields_ = [("seed", ctypes.c_uint32), ("draws", ctypes.c_uint64)]


class Request(ctypes.Structure):
    _fields_ = [("roster_id", ctypes.c_uint16), ("opponent_trainer_id", ctypes.c_uint32),
                ("attest", U8 * 4)]


class Data(ctypes.Structure):
    _fields_ = [("roster", BytePointer), ("species", BytePointer), ("species_count", ctypes.c_uint32),
                ("moves", BytePointer), ("move_count", ctypes.c_uint32), ("growth", BytePointer),
                ("natures", BytePointer), ("nature_rates", BytePointer), ("rate_count", ctypes.c_uint32),
                ("message", MessageCallback), ("user", ctypes.c_void_p)]


def integer(value):
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError("Expected an integer original numeric observation")
    result = int(value, 0) if isinstance(value, str) else int(value)
    if not 0 <= result <= 0xFFFFFFFF:
        raise ValueError("Original numeric observation is outside u32")
    return result


def u16(data, offset):
    return struct.unpack_from(">H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def take(data, offset, size):
    if offset < 0 or size <= 0 or offset + size > len(data):
        raise ValueError("Original table extent is out of bounds")
    return data[offset:offset + size]


def canonical_tables(dol, rel):
    """File offsets independently resolved from original DOL/common_rel owners."""
    if len(dol) != DOL_BYTES or hashlib.sha1(dol).hexdigest() != DOL_SHA1:
        raise ValueError("Original DOL identity mismatch")
    if len(rel) != REL_BYTES or hashlib.sha1(rel).hexdigest() != REL_SHA1:
        raise ValueError("Original common REL identity mismatch")
    if hashlib.sha256(dol).hexdigest() != DOL_SHA256:
        raise ValueError("Original DOL SHA256 mismatch")
    # common_rel.c offsets are relative to its data section at file 0x184C.
    for offset, count in ((0x14BF00, 5510), (0x14BF20, 358), (0x14BF24, 25),
                          (0x14BF2C, 413), (0x14BF30, 8)):
        if u32(rel, offset) != count:
            raise ValueError("Original REL table count mismatch")
    if u32(dol, 0x397A20) != 8:
        raise ValueError("Original growth-table count mismatch")
    tables = {
        "rosters": {i: take(rel, 0x184C + 0x9E5DC + 80 * i, 80) for i in ROSTERS},
        "species": take(rel, 0x184C + 0x121A04 + 284 * 288, 284),
        "moves": take(rel, 0x11E010, 358 * 56),
        "growth": take(dol, 0x35B940, 404),
        "natures": take(rel, 0x122E60, 25 * 40),
        "nature_rates": take(rel, 0x13FC7C, 8 * 2),
        "messages": {0x4EF: take(rel, 0x5EA06, 20)},
    }
    for row in tables["rosters"].values():
        if u16(row, 10) != 288 or row[3] or row[4] != 24 or u32(row, 20) != 0x4EF:
            raise ValueError("Original roster differs from admitted ordinary Willie calls")
    if tables["species"][0] != 0 or u32(tables["species"], 24) != 0x4EF:
        raise ValueError("Original species growth/name identity mismatch")
    name = tables["messages"][0x4EF]
    if name[-2:] != b"\0\0" or any(u16(name, i) in (0, 0xFFFF) for i in range(0, 18, 2)):
        raise ValueError("Original message is not the admitted plain GSchar string")
    # Validate the original linked branch at the reviewed call site from DOL.
    for i in range(7):
        base, size = u32(dol, 0x48 + 4 * i), u32(dol, 0x90 + 4 * i)
        if base <= CALLER < base + size:
            word = u32(dol, u32(dol, 4 * i) + CALLER - base)
            displacement = word & 0x03FFFFFC
            if displacement & 0x02000000:
                displacement -= 0x04000000
            if word & 0xFC000003 != 0x48000001 or CALLER + displacement != ENTRY:
                raise ValueError("Original reviewed caller does not branch to constructor")
            break
    else:
        raise ValueError("Original caller is not mapped in DOL text")
    return tables


def hex_bytes(value, size):
    if not isinstance(value, str) or len(value) != size * 2:
        raise ValueError("Incomplete original byte observation")
    try:
        result = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError("Invalid original byte observation") from error
    if len(result) != size:
        raise ValueError("Incomplete original byte observation")
    return result


def validate_record(record, roster):
    if (record.get("capture_id") != f"{roster - ROSTERS[0]:02d}-postHeroPreRoster" or
            record.get("candidate") != "postHeroPreRoster" or
            record.get("boundary_status") != "candidate_until_caller_context_validated" or
            integer(record["entry_address"]) != ENTRY or integer(record["caller_address"]) != CALLER):
        raise ValueError("Original constructor/caller identity mismatch")
    entry, returned = record["entry"], record["return"]
    regs, out_regs = entry["registers"], returned["registers"]
    if (integer(regs["pc"]) != ENTRY or integer(regs["lr"]) != RETURN or
            integer(out_regs["pc"]) != RETURN or integer(out_regs["r3"]) != 1 or
            integer(regs["r3"]) != roster or integer(regs["r1"]) != integer(out_regs["r1"])):
        raise ValueError("Original entry/return register context mismatch")
    address = integer(regs["r4"])
    if not 0x80000000 <= address <= 0x81800000 - 312:
        raise ValueError("Original backing is not in MEM1")
    bodies = []
    for point in (entry, returned):
        objects = point["objects"]
        if (len(objects) != 1 or objects[0].get("name") != "pokemon" or
                objects[0].get("register") != "r4" or objects[0].get("size") != 312 or
                integer(objects[0]["address"]) != address):
            raise ValueError("Original full 312-byte backing observation missing")
        bodies.append(hex_bytes(objects[0]["data_hex"], 312))
        rng = point["rng"]
        if integer(rng["pointer_address"]) != 0x80478C94 or integer(rng["seed_address"]) != 0x80478C90:
            raise ValueError("Original independent RNG observation missing")
        attest = point["attest"]
        if (integer(attest["pointer_address"]) != 0x8047ADB8 or integer(attest["offset"]) != 0 or
                not 0x80000000 <= integer(attest["pointee_address"]) <= 0x81800000 - 4):
            raise ValueError("Original independent attest observation missing")
        hex_bytes(attest["data_hex"], 4)
    if entry["attest"] != returned["attest"]:
        raise ValueError("Original attest changed during constructor")
    return {"roster": roster, "trainer": integer(regs["r5"]),
            "seed_before": integer(entry["rng"]["seed"]), "seed_after": integer(returned["rng"]["seed"]),
            "attest": hex_bytes(entry["attest"]["data_hex"], 4), "before": bodies[0], "after": bodies[1]}


def load_records(directory):
    directory = Path(directory)
    report = json.loads((directory / "observation.json").read_text())
    if (report.get("schema") != 1 or report.get("kind") != "private_original_game_candidate_observation" or
            report.get("full_battle_qualified") is not False or report.get("dol_sha256") != DOL_SHA256 or
            report.get("gdb_source_sha256") != GDB_SHA256 or report.get("stops") != 4 or
            report.get("register_layout") != "Dolphin g=32x32be; p40/42/43/44/45"):
        raise ValueError("Original observation provenance/context mismatch")
    raw = [json.loads((directory / f"{i:02d}-postHeroPreRoster.json").read_text()) for i in range(2)]
    if report.get("records") != raw:
        raise ValueError("Original observation manifest and individual records differ")
    records = [validate_record(row, roster) for row, roster in zip(raw, ROSTERS)]
    if records[0]["seed_after"] != records[1]["seed_before"] or records[0]["trainer"] != records[1]["trainer"]:
        raise ValueError("Original consecutive roster seed/trainer context mismatch")
    return records


def next16(seed):
    seed = (seed * 214013 + 2531011) & 0xFFFFFFFF
    return seed, seed >> 16


def source_draws(record, tables):
    """Independent source-call count: five base draws, then two per fit attempt."""
    seed = record["seed_before"]
    seed, high = next16(seed)
    seed, low = next16(seed)
    initial_pid = high << 16 | low
    for _ in range(3):
        seed, _ = next16(seed)
    row, ratio = tables["rosters"][record["roster"]], tables["species"][2]
    requested_sex, nature = (v if v < 128 else v - 256 for v in row[1:3])
    def sex(pid):
        return 1 if ratio > (pid & 255) else 0
    for attempt in range(1, RETRY_LIMIT + 1):
        seed, high = next16(seed)
        seed, low = next16(seed)
        pid = high << 16 | low
        if requested_sex >= 0 and ratio not in (0, 254, 255):
            wanted = sex(initial_pid) if requested_sex == 2 else requested_sex
            if sex(pid) != wanted:
                continue
        if nature >= 0 and pid % 25 != nature:
            continue
        trainer = record["trainer"]
        if ((trainer >> 16) ^ (trainer & 65535) ^ high ^ low) < 8:
            continue
        if seed != record["seed_after"]:
            raise ValueError("Original final seed disagrees with independent source draw count")
        return 5 + 2 * attempt
    raise ValueError("Original capture exceeds admitted source personality retry budget")


def seed_distance(before, after):
    seed = before
    for count in range(1, 5 + 2 * RETRY_LIMIT + 1):
        seed, _ = next16(seed)
        if seed == after:
            return count
    raise ValueError("Original final seed is not in the admitted HSD stream interval")


def native_function(library):
    native = ctypes.CDLL(str(Path(library).resolve()))
    function = native.c3_battle_pokemon_create_pre_catch
    function.argtypes = [BytePointer, ctypes.POINTER(Request), ctypes.POINTER(Data), ctypes.POINTER(Rng)]
    function.restype = ctypes.c_int
    return function


def replay(records, tables, invoke):
    summaries = []
    for record in records:
        draws = source_draws(record, tables)
        if seed_distance(record["seed_before"], record["seed_after"]) != draws:
            raise ValueError("Original seed-distance/source-call count mismatch")
        buffers = {key: (U8 * len(value)).from_buffer_copy(value) for key, value in tables.items()
                   if isinstance(value, bytes)}
        roster = (U8 * 80).from_buffer_copy(tables["rosters"][record["roster"]])
        messages = {key: (U8 * len(value)).from_buffer_copy(value) for key, value in tables["messages"].items()}
        @MessageCallback
        def message(_user, key, output, size):
            if key not in messages:
                return 0
            output[0] = ctypes.cast(messages[key], BytePointer)
            size[0] = len(messages[key])
            return 1
        data = Data(roster, buffers["species"], 413, buffers["moves"], 358, buffers["growth"],
                    buffers["natures"], buffers["nature_rates"], 8, message, None)
        request = Request(record["roster"], record["trainer"], (U8 * 4).from_buffer_copy(record["attest"]))
        backing = (U8 * 312).from_buffer_copy(record["before"])
        rng = Rng(record["seed_before"], 0)
        if invoke(backing, ctypes.byref(request), ctypes.byref(data), ctypes.byref(rng)) != 1:
            raise ValueError(f"Native constructor rejected original roster {record['roster']}")
        output = bytes(backing)
        if output != record["after"]:
            differing = [i for i, (a, b) in enumerate(zip(output, record["after"])) if a != b]
            raise ValueError(f"Original/native roster {record['roster']} differs at {len(differing)} bytes; offsets {differing[:32]}")
        if (rng.seed, rng.draws) != (record["seed_after"], draws):
            raise ValueError(f"Original/native roster {record['roster']} final RNG/count mismatch")
        summaries.append({"roster_id": record["roster"], "matched_bytes": 312, "draws": draws,
                          "seed_before": f"0x{record['seed_before']:08x}", "seed_after": f"0x{record['seed_after']:08x}"})
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-root", type=Path, required=True, help="Original GC6E01 root containing sys/ and files/")
    parser.add_argument("--capture-dir", type=Path, required=True, help="Private probe directory with observation.json and paired records")
    parser.add_argument("--library", type=Path, required=True)
    args = parser.parse_args()
    try:
        tables = canonical_tables((args.private_root / "sys/main.dol").read_bytes(),
                                  (args.private_root / "files/common_rel.rel").read_bytes())
        records = load_records(args.capture_dir)
        summaries = replay(records, tables, native_function(args.library))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"Temporary constructor differential failed: {error}\n")
    print(json.dumps({"schema": 1, "scope": "two ordinary Willie temporary pre-catch constructors",
                      "full_party_qualified": False, "full_battle_qualified": False,
                      "dol_sha1": DOL_SHA1, "common_rel_sha1": REL_SHA1,
                      "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                      "observation_sha256": hashlib.sha256((args.capture_dir / "observation.json").read_bytes()).hexdigest(),
                      "matched": len(summaries), "cases": summaries}, indent=2))


if __name__ == "__main__":
    main()
