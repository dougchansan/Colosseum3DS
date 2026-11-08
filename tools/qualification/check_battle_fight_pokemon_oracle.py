#!/usr/bin/env python3
"""Compare private original FightPokemon constructor observations with native C.

Classifies only source-pinned observed callers. All 340 destination bytes, all
312 unchanged source bytes and the original no-RNG observation are mandatory.
Original bytes stay private; no party-sort or complete-entry result is inferred.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path

import check_battle_pokemon_oracle as pokemon
import check_battle_trainer_oracle as trainer

ENTRY = 0x80206AEC
DEST_BYTES, SOURCE_BYTES = 340, 312
CALLERS = {
    0x8020D47C: "final-entry-primary",
    0x8020D570: "final-entry-fill",
    0x802333EC: "temporary-sort-selection",
    0x80233620: "temporary-sort-selection",
    0x8023374C: "temporary-sort-selection",
    0x80233F74: "temporary-selection-candidate",
    0x80213C40: "catch-path",
}


def validate_dol(dol):
    if (len(dol) != pokemon.DOL_BYTES or hashlib.sha1(dol).hexdigest() != pokemon.DOL_SHA1 or
            hashlib.sha256(dol).hexdigest() != pokemon.DOL_SHA256):
        raise ValueError("Original FightPokemon DOL identity mismatch")


def verify_caller(dol, caller):
    if caller not in CALLERS:
        raise ValueError("Unqualified original FightPokemon caller")
    word = int.from_bytes(trainer.dol_read(dol, caller, 4), "big")
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    if word & 0xFC000003 != 0x48000001 or caller + displacement != ENTRY:
        raise ValueError("Original FightPokemon caller branch instruction mismatch")
    return CALLERS[caller]


def validate_record(record, index, dol):
    number = pokemon.integer
    if (record.get("capture_id") != f"{index:02d}-fightPokemonCreate" or
            record.get("candidate") != "fightPokemonCreate" or number(record["entry_address"]) != ENTRY or
            record.get("boundary_status") != "candidate_until_caller_context_validated"):
        raise ValueError("Original FightPokemon constructor identity mismatch")
    caller = number(record["caller_address"])
    classification = verify_caller(dol, caller)
    entry, returned = record["entry"], record["return"]
    regs, out_regs = entry["registers"], returned["registers"]
    if (number(regs["pc"]) != ENTRY or number(regs["lr"]) != caller + 4 or
            number(out_regs["pc"]) != caller + 4 or number(regs["r1"]) != number(out_regs["r1"])):
        raise ValueError("Original FightPokemon entry/return register context mismatch")
    destination, source = number(regs["r3"]), number(regs["r4"])
    if (not 0x80000000 <= destination <= 0x81800000 - DEST_BYTES or
            not 0x80000000 <= source <= 0x81800000 - SOURCE_BYTES or
            destination < source + SOURCE_BYTES and source < destination + DEST_BYTES):
        raise ValueError("Original FightPokemon backing/source extents are invalid or overlap")
    before, after = {}, {}
    for point, output in ((entry, before), (returned, after)):
        objects = point["objects"]
        if len(objects) != 2 or {row["name"] for row in objects} != {"fight_pokemon", "pokemon"}:
            raise ValueError("Original full FightPokemon/source objects missing")
        for row in objects:
            is_dest = row["name"] == "fight_pokemon"
            size, address, register = (DEST_BYTES, destination, "r3") if is_dest else (SOURCE_BYTES, source, "r4")
            if row["size"] != size or number(row["address"]) != address or row["register"] != register:
                raise ValueError("Original retained FightPokemon/source extent mismatch")
            output[row["name"]] = pokemon.hex_bytes(row["data_hex"], size)
        rng = point["rng"]
        if number(rng["pointer_address"]) != 0x80478C94 or number(rng["seed_address"]) != 0x80478C90:
            raise ValueError("Original independent FightPokemon RNG observation missing")
    if before["pokemon"] != after["pokemon"]:
        raise ValueError("Original FightPokemon source changed during constructor")
    seed = number(entry["rng"]["seed"])
    if seed != number(returned["rng"]["seed"]):
        raise ValueError("Original FightPokemon RNG changed during constructor")
    if record.get("rng_observation") != {"seed_unchanged": True, "seed_address_unchanged": True}:
        raise ValueError("Original explicit no-RNG observation missing or inconsistent")
    entry_id = number(regs["r5"]) & 65535
    return {"capture_id": record["capture_id"], "caller": caller, "classification": classification,
            "before": before["fight_pokemon"], "after": after["fight_pokemon"],
            "source": before["pokemon"], "source_reference": source,
            "entry_id": entry_id if entry_id < 32768 else entry_id - 65536, "seed": seed}


def load_records(directory, dol):
    validate_dol(dol)
    raw = trainer.load_observation(directory)
    selected = [(index, row) for index, row in enumerate(raw) if row["candidate"] == "fightPokemonCreate"]
    if not selected:
        raise ValueError("Original observation contains no FightPokemon constructor calls")
    return [validate_record(row, index, dol) for index, row in selected]


def native_function(library):
    native = ctypes.CDLL(str(Path(library).resolve()))
    invoke = native.c3_battle_fight_pokemon_create
    invoke.argtypes = [pokemon.BytePointer, pokemon.BytePointer, ctypes.c_uint32, ctypes.c_int16]
    invoke.restype = ctypes.c_int
    return invoke


def replay(records, invoke):
    summaries = []
    for record in records:
        backing = (pokemon.U8 * DEST_BYTES).from_buffer_copy(record["before"])
        source = (pokemon.U8 * SOURCE_BYTES).from_buffer_copy(record["source"])
        if invoke(backing, source, record["source_reference"], record["entry_id"]) != 1:
            raise ValueError("Native FightPokemon constructor rejected " + record["capture_id"])
        if bytes(source) != record["source"]:
            raise ValueError("Native FightPokemon constructor mutated borrowed source")
        if bytes(backing) != record["after"]:
            differing = [i for i, (a, b) in enumerate(zip(bytes(backing), record["after"])) if a != b]
            raise ValueError(f"Original/native {record['capture_id']} differs at {len(differing)} destination bytes; offsets {differing[:32]}")
        summaries.append({"capture_id": record["capture_id"], "caller": f"0x{record['caller']:08x}",
                          "classification": record["classification"], "matched_destination_bytes": DEST_BYTES,
                          "unchanged_source_bytes": SOURCE_BYTES, "original_rng_unchanged": True,
                          "draws": 0, "seed": f"0x{record['seed']:08x}"})
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-root", required=True, type=Path)
    parser.add_argument("--capture-dir", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    args = parser.parse_args()
    try:
        records = load_records(args.capture_dir, (args.private_root / "sys/main.dol").read_bytes())
        summaries = replay(records, native_function(args.library))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"FightPokemon constructor differential failed: {error}\n")
    print(json.dumps({"schema": 1, "scope": "observed source-caller FightPokemon constructors only",
                      "full_party_sort_qualified": False, "full_battle_entry_qualified": False,
                      "full_battle_qualified": False, "inter_call_rng_qualified": False,
                      "dol_sha1": pokemon.DOL_SHA1,
                      "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                      "observation_sha256": hashlib.sha256((args.capture_dir / "observation.json").read_bytes()).hexdigest(),
                      "matched": len(summaries), "cases": summaries}, indent=2))


if __name__ == "__main__":
    main()
