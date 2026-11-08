#!/usr/bin/env python3
"""Replay private original parent calls against the native HeroBios constructor.

Admits a saved-player copy and the ordinary Willie generation branch only.
No original hero, savedata, scratch, message or table bytes are emitted.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

import check_battle_pokemon_oracle as pokemon

ENTRY, CALLER, RETURN = 0x801F9CBC, 0x8020D38C, 0x8020D390
HERO_SIZE, SCRATCH_SIZE = 0xB18, 312
OBSERVATION_ENTRIES = {
    "opponentTrainerCreate": ENTRY, "postHeroPreRoster": 0x801F9F78,
    "fightPokemonCreate": 0x80206AEC, "floorStart": 0x801F17B0,
    "floorInit": 0x801F4860, "turnCount": 0x801F68C8,
    "oneTurnPost": 0x8020BBA4, "resultSetter": 0x801F3984, "partyCopyback": 0x801F86C0,
}
RosterCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16, ctypes.POINTER(pokemon.Data))
ItemCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint16,
                               ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8))


class Request(ctypes.Structure):
    _fields_ = [("trainer_id", ctypes.c_uint16), ("input_device", ctypes.c_int32), ("attest", pokemon.U8 * 4)]


class Data(ctypes.Structure):
    _fields_ = [("trainer", pokemon.BytePointer), ("saved_player", pokemon.BytePointer),
                ("roster", RosterCallback), ("message", pokemon.MessageCallback), ("item", ItemCallback),
                ("item_count", ctypes.c_uint32), ("special_trainer_id", ctypes.c_uint32),
                ("special_name", pokemon.BytePointer), ("special_name_size", ctypes.c_size_t), ("user", ctypes.c_void_p)]


def dol_read(dol, address, size):
    for i in range(18):
        start, extent = pokemon.u32(dol, 0x48 + i * 4), pokemon.u32(dol, 0x90 + i * 4)
        if start <= address and address + size <= start + extent:
            return pokemon.take(dol, pokemon.u32(dol, i * 4) + address - start, size)
    raise ValueError("Original parent table address is not mapped in DOL")


def plain_string(data):
    if len(data) < 2 or len(data) % 2:
        raise ValueError("Incomplete original GSchar input")
    for i in range(0, len(data), 2):
        code = pokemon.u16(data, i)
        if code == 0:
            return data[:i + 2]
        if code == 0xFFFF:
            raise ValueError("Unqualified original GSchar control sequence")
    raise ValueError("Original GSchar input has no bounded terminator")


def message_row(rel, key):
    # common REL's original message group at data-relative 0x58044.
    base = 0x184C + 0x58044
    count = pokemon.u16(rel, base + 4)
    if count > 4096:
        raise ValueError("Original parent message table count invalid")
    entries = [struct.unpack(">II", pokemon.take(rel, base + 16 + i * 8, 8)) for i in range(count)]
    matches = [offset for message, offset in entries if message == key]
    if len(matches) != 1:
        raise ValueError("Original parent message is missing or duplicated")
    start = matches[0]
    end = min((offset for _, offset in entries if offset > start), default=len(rel) - base)
    if start < 16 + count * 8 or end <= start:
        raise ValueError("Original parent message bounds invalid")
    return plain_string(pokemon.take(rel, base + start, end - start))


def canonical_tables(dol, rel):
    tables = pokemon.canonical_tables(dol, rel)  # canonical hash/count validation
    if pokemon.u32(rel, 0x14BEF8) != 820:
        raise ValueError("Original trainer table count mismatch")
    tables["trainers"] = pokemon.take(rel, 0x184C + 0x91684, 820 * 52)
    terminal = pokemon.take(rel, 0x184C + 0x9E5DC + 240 * 80, 80)
    if pokemon.u16(terminal, 10):
        raise ValueError("Original Willie roster has no expected terminating row")
    tables["rosters"][240] = terminal
    item_count = int.from_bytes(dol_read(dol, 0x80478BD8, 4), "big")
    slot_count = int.from_bytes(dol_read(dol, 0x80478BB0, 4), "big")
    if item_count != 548 or slot_count != 397:
        raise ValueError("Original item indirection counts mismatch")
    lookup = dol_read(dol, 0x803681E8, item_count * 2)
    rows = dol_read(dol, 0x80363CE8, slot_count * 40)
    tables["items"] = {}
    for item in range(item_count):
        slot = pokemon.u16(lookup, item * 2)
        tables["items"][item] = ((pokemon.u32(rows, slot * 40 + 0x10), rows[slot * 40])
                                  if slot < slot_count else (0, 0))
    tables["item_count"] = item_count
    tables["messages"][0xFA2] = message_row(rel, 0xFA2)
    willie = tables["trainers"][10 * 52:11 * 52]
    if pokemon.u16(willie, 4) != 238 or pokemon.u16(willie, 2) == 1 or pokemon.u32(willie, 8) != 0x1014:
        raise ValueError("Original Willie trainer identity mismatch")
    tables["messages"][0x1014] = message_row(rel, 0x1014)
    word = int.from_bytes(dol_read(dol, CALLER, 4), "big")
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    if word & 0xFC000003 != 0x48000001 or CALLER + displacement != ENTRY:
        raise ValueError("Original parent caller instruction mismatch")
    return tables


def validate_record(record, index):
    number = pokemon.integer
    if (record.get("capture_id") != f"{index:02d}-opponentTrainerCreate" or
            record.get("candidate") != "opponentTrainerCreate" or
            record.get("boundary_status") != "candidate_until_caller_context_validated" or
            number(record["entry_address"]) != ENTRY or number(record["caller_address"]) != CALLER):
        raise ValueError("Original parent boundary/caller identity mismatch")
    entry, returned = record["entry"], record["return"]
    regs, out_regs = entry["registers"], returned["registers"]
    if (number(regs["pc"]) != ENTRY or number(regs["lr"]) != RETURN or
            number(out_regs["pc"]) != RETURN or number(regs["r1"]) != number(out_regs["r1"])):
        raise ValueError("Original parent entry/return register context mismatch")
    trainer, device = number(regs["r3"]), number(regs["r4"])
    if trainer >= 820:
        raise ValueError("Original parent trainer ID is outside canonical table")
    address = number(regs["r5"])
    bodies = []
    for point in (entry, returned):
        objects = point["objects"]
        if (len(objects) != 1 or objects[0].get("name") != "hero" or objects[0].get("register") != "r5" or
                objects[0].get("size") != HERO_SIZE or number(objects[0]["address"]) != address or
                not 0x80000000 <= address <= 0x81800000 - HERO_SIZE):
            raise ValueError("Original full HeroBios observation missing")
        bodies.append(pokemon.hex_bytes(objects[0]["data_hex"], HERO_SIZE))
        rng = point["rng"]
        if number(rng["pointer_address"]) != 0x80478C94 or number(rng["seed_address"]) != 0x80478C90:
            raise ValueError("Original parent independent RNG observation missing")
        attest = point["attest"]
        if (number(attest["pointer_address"]) != 0x8047ADB8 or number(attest["offset"]) != 0 or
                not 0x80000000 <= number(attest["pointee_address"]) <= 0x81800000 - 4):
            raise ValueError("Original parent independent attest observation missing")
        pokemon.hex_bytes(attest["data_hex"], 4)
    if entry["attest"] != returned["attest"]:
        raise ValueError("Original parent attest changed across call")
    inputs = entry["parent_entry_inputs"]
    if number(inputs["entry_trainer_id"]) != trainer:
        raise ValueError("Original parent input trainer identity mismatch")
    stack = number(regs["r1"])
    scratch = inputs["pokemon_scratch"]
    if (number(scratch["entry_stack"]) != stack or scratch["offset"] != -0x148 or scratch["size"] != 312 or
            number(scratch["address"]) != stack - 0x148 or stack % 8 or
            not 0x80000000 <= stack - 0x148 <= 0x81800000 - SCRATCH_SIZE):
        raise ValueError("Original independent initial scratch observation missing")
    saved = inputs["saved_hero"]
    base = number(entry["attest"]["pointee_address"])
    if (number(saved["savedata_base"]) != base or saved["offset"] != 0x70 or saved["size"] != HERO_SIZE or
            number(saved["address"]) != base + 0x70 or base + 0x70 + HERO_SIZE > 0x81800000):
        raise ValueError("Original independent saved HeroBios observation missing")
    selector = inputs["trainer_selector"]
    if number(selector["address"]) != 0x80478960:
        raise ValueError("Original independent trainer selector missing")
    selector_value = number(selector["value"])
    replacement = inputs["replacement_name"]
    if replacement["applies"] is not (trainer == selector_value):
        raise ValueError("Original replacement-name condition mismatch")
    special_name = None
    if replacement["applies"]:
        if (number(replacement["pointer_address"]) != 0x8047A670 or replacement["size"] != 22 or
                not 0x80000000 <= number(replacement["address"]) <= 0x81800000 - 22):
            raise ValueError("Original independent replacement name missing")
        special_name = plain_string(pokemon.hex_bytes(replacement["data_hex"], 22))
    final_scratch = returned.get("parent_return_scratch")
    if final_scratch is not None:
        if (final_scratch["size"] != SCRATCH_SIZE or
                number(final_scratch["address"]) != number(scratch["address"])):
            raise ValueError("Original retained parent final scratch extent mismatch")
        final_scratch = pokemon.hex_bytes(final_scratch["data_hex"], SCRATCH_SIZE)
    return {"trainer_id": trainer, "input_device": device if device < 0x80000000 else device - 0x100000000,
            "seed_before": number(entry["rng"]["seed"]), "seed_after": number(returned["rng"]["seed"]),
            "attest": pokemon.hex_bytes(entry["attest"]["data_hex"], 4), "before": bodies[0], "after": bodies[1],
            "scratch": pokemon.hex_bytes(scratch["data_hex"], 312),
            "scratch_after": final_scratch,
            "saved_player": pokemon.hex_bytes(saved["data_hex"], HERO_SIZE),
            "selector": selector_value, "special_name": special_name}


def load_observation(directory):
    directory = Path(directory)
    report = json.loads((directory / "observation.json").read_text())
    if (report.get("schema") != 1 or report.get("kind") != "private_original_game_candidate_observation" or
            report.get("full_battle_qualified") is not False or report.get("dol_sha256") != pokemon.DOL_SHA256 or
            report.get("gdb_source_sha256") != pokemon.GDB_SHA256 or
            report.get("register_layout") != "Dolphin g=32x32be; p40/42/43/44/45"):
        raise ValueError("Original parent observation provenance mismatch")
    raw = report.get("records")
    if (not isinstance(raw, list) or not 1 <= len(raw) <= 64 or
            type(report.get("stops")) is not int or report["stops"] != len(raw) * 2):
        raise ValueError("Original observation manifest stop/record coverage mismatch")
    for index, record in enumerate(raw):
        candidate = record.get("candidate")
        if (candidate not in OBSERVATION_ENTRIES or
                record.get("capture_id") != f"{index:02d}-{candidate}" or
                pokemon.integer(record["entry_address"]) != OBSERVATION_ENTRIES[candidate] or
                record.get("boundary_status") != "candidate_until_caller_context_validated" or
                not isinstance(record.get("entry"), dict) or not isinstance(record.get("return"), dict)):
            raise ValueError("Original observation manifest capture identity mismatch")
        individual = json.loads((directory / f"{index:02d}-{candidate}.json").read_text())
        if individual != record:
            raise ValueError("Original observation manifest differs from individual record")
    return raw


def load_records(directory):
    raw = load_observation(directory)
    parents = [(i, row) for i, row in enumerate(raw) if row["candidate"] == "opponentTrainerCreate"]
    if len(parents) != 2:
        raise ValueError("Original parent observation must contain exactly two parent calls")
    # Capture order and intervening RNG belong to the surrounding caller, not
    # this constructor. Each observed call supplies its own independent stream.
    return [validate_record(row, i) for i, row in parents]


def source_result(record, tables):
    row = pokemon.take(tables["trainers"], record["trainer_id"] * 52, 52)
    count, kind = pokemon.u16(row, 6), pokemon.u16(row, 2)
    seed = record["seed_before"]
    if not record["trainer_id"] or (not count and not record["input_device"]):
        return "no-op", seed, 0
    if kind == 1:
        return "saved-player-copy", seed, 0
    if record["trainer_id"] != 10 or pokemon.u16(row, 4) != 238:
        raise ValueError("Original generated trainer is outside qualified ordinary Willie roster")
    seed, low = pokemon.next16(seed)
    seed, high = pokemon.next16(seed)
    trainer, draws = high << 16 | low, 2
    for roster_id in (238, 239):
        roster = tables["rosters"][roster_id]
        seed, initial_high = pokemon.next16(seed)
        seed, initial_low = pokemon.next16(seed)
        initial_pid = initial_high << 16 | initial_low
        for _ in range(3):
            seed, _ = pokemon.next16(seed)
        draws += 5
        ratio = tables["species"][2]
        sex, nature = (v if v < 128 else v - 256 for v in roster[1:3])
        for _ in range(pokemon.RETRY_LIMIT):
            seed, high = pokemon.next16(seed)
            seed, low = pokemon.next16(seed)
            draws += 2
            pid = high << 16 | low
            if sex >= 0 and ratio not in (0, 254, 255):
                wanted = int(ratio > (initial_pid & 255)) if sex == 2 else sex
                if wanted != int(ratio > (pid & 255)):
                    continue
            if nature >= 0 and nature != pid % 25:
                continue
            if ((trainer >> 16) ^ (trainer & 65535) ^ high ^ low) < 8:
                continue
            break
        else:
            raise ValueError("Original parent source replay exceeds personality budget")
    return "ordinary-Willie-generated", seed, draws


def parent_seed_distance(before, after):
    seed = before
    for count in range(1, 2 + 2 * (5 + 2 * pokemon.RETRY_LIMIT) + 1):
        seed, _ = pokemon.next16(seed)
        if seed == after:
            return count
    raise ValueError("Original parent final seed exceeds admitted HSD stream interval")


def native_function(library):
    native = ctypes.CDLL(str(Path(library).resolve()))
    invoke = native.c3_battle_trainer_create
    invoke.argtypes = [pokemon.BytePointer, pokemon.BytePointer, ctypes.POINTER(Request), ctypes.POINTER(Data), ctypes.POINTER(pokemon.Rng)]
    invoke.restype = ctypes.c_int
    return invoke


def replay(records, tables, invoke):
    summaries = []
    branches = []
    for record in records:
        branch, final_seed, draws = source_result(record, tables)
        if record["seed_after"] != final_seed:
            raise ValueError("Original parent final seed disagrees with independent source call pattern")
        if draws and parent_seed_distance(record["seed_before"], final_seed) != draws:
            raise ValueError("Original parent source draw count/seed distance mismatch")
        buffers = {key: (pokemon.U8 * len(value)).from_buffer_copy(value) for key, value in tables.items() if isinstance(value, bytes)}
        trainer_row = pokemon.take(tables["trainers"], record["trainer_id"] * 52, 52)
        trainer = (pokemon.U8 * 52).from_buffer_copy(trainer_row)
        saved = (pokemon.U8 * HERO_SIZE).from_buffer_copy(record["saved_player"])
        special = ((pokemon.U8 * len(record["special_name"])).from_buffer_copy(record["special_name"])
                   if record["special_name"] is not None else None)
        messages = {key: (pokemon.U8 * len(value)).from_buffer_copy(value) for key, value in tables["messages"].items()}
        roster_buffers = {key: (pokemon.U8 * 80).from_buffer_copy(value) for key, value in tables["rosters"].items()}
        callback_errors = []
        @pokemon.MessageCallback
        def message(_user, key, output, size):
            if key not in messages:
                callback_errors.append("message")
                return 0
            output[0] = ctypes.cast(messages[key], pokemon.BytePointer)
            size[0] = len(messages[key])
            return 1
        @RosterCallback
        def roster(_user, key, output):
            if key not in roster_buffers:
                callback_errors.append("roster")
                return 0
            output[0] = pokemon.Data(roster_buffers[key], buffers["species"], 413, buffers["moves"], 358,
                                     buffers["growth"], buffers["natures"], buffers["nature_rates"], 8, message, None)
            return 1
        @ItemCallback
        def item(_user, key, name, kind):
            if key not in tables["items"]:
                callback_errors.append("item")
                return 0
            name[0], kind[0] = tables["items"][key]
            return 1
        data = Data(trainer, saved, roster, message, item, tables["item_count"], record["selector"],
                    special, len(special) if special is not None else 0, None)
        request = Request(record["trainer_id"], record["input_device"], (pokemon.U8 * 4).from_buffer_copy(record["attest"]))
        hero = (pokemon.U8 * HERO_SIZE).from_buffer_copy(record["before"])
        scratch = (pokemon.U8 * SCRATCH_SIZE).from_buffer_copy(record["scratch"])
        rng = pokemon.Rng(record["seed_before"], 0)
        if invoke(hero, scratch, ctypes.byref(request), ctypes.byref(data), ctypes.byref(rng)) != 1 or callback_errors:
            raise ValueError(f"Native parent rejected original trainer {record['trainer_id']}")
        if bytes(hero) != record["after"]:
            differing = [i for i, (a, b) in enumerate(zip(bytes(hero), record["after"])) if a != b]
            raise ValueError(f"Original/native trainer {record['trainer_id']} differs at {len(differing)} hero bytes; offsets {differing[:32]}")
        if record.get("scratch_after") is not None and bytes(scratch) != record["scratch_after"]:
            differing = [i for i, (a, b) in enumerate(zip(bytes(scratch), record["scratch_after"])) if a != b]
            raise ValueError(f"Original/native trainer {record['trainer_id']} differs at {len(differing)} final scratch bytes; offsets {differing[:32]}")
        if (rng.seed, rng.draws) != (final_seed, draws):
            raise ValueError("Original/native parent final RNG/count mismatch")
        branches.append(branch)
        summaries.append({"trainer_id": record["trainer_id"], "branch": branch, "matched_hero_bytes": HERO_SIZE,
                          "draws": draws, "seed_before": f"0x{record['seed_before']:08x}",
                          "seed_after": f"0x{final_seed:08x}",
                          "scratch_final_observed": record.get("scratch_after") is not None})
    if sorted(branches) != ["ordinary-Willie-generated", "saved-player-copy"]:
        raise ValueError("Parent corpus must cover one saved-player copy and one ordinary Willie generation")
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-root", required=True, type=Path)
    parser.add_argument("--capture-dir", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    args = parser.parse_args()
    try:
        tables = canonical_tables((args.private_root / "sys/main.dol").read_bytes(), (args.private_root / "files/common_rel.rel").read_bytes())
        summaries = replay(load_records(args.capture_dir), tables, native_function(args.library))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"Parent trainer differential failed: {error}\n")
    print(json.dumps({"schema": 1, "scope": "original saved-player and ordinary Willie parent HeroBios construction",
                      "full_fight_pokemon_qualified": False, "full_battle_qualified": False,
                      "inter_call_rng_qualified": False,
                      "dol_sha1": pokemon.DOL_SHA1, "common_rel_sha1": pokemon.REL_SHA1,
                      "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                      "observation_sha256": hashlib.sha256((args.capture_dir / "observation.json").read_bytes()).hexdigest(),
                      "matched": len(summaries), "cases": summaries}, indent=2))


if __name__ == "__main__":
    main()
