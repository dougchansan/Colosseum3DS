#!/usr/bin/env python3
"""Compare complete private runtime-trainer constructor observations with C.

Three exact retained destination views cover all 10468 bytes. The separate
2840-byte Hero and r5/r6/r7 arguments come exclusively from the original entry.
The pinned status3F/kaisuu0 profile has no random draw. This does not qualify
sequence creation, active fighters, intervening work or complete battle entry.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

import check_battle_pokemon_oracle as pokemon
import check_battle_trainer_oracle as trainer

ENTRY, CALLER, RETURN = 0x801FA6D8, 0x8020D3B0, 0x8020D3B4
RUNTIME_BYTES, HERO_BYTES = 10468, 2840
VIEWS = {
    "runtime_head": ("r3", 0, 4096),
    "runtime_middle": ("r3", 4096, 4096),
    "runtime_tail": ("r3", 8192, 2276),
    "hero": ("r4", 0, HERO_BYTES),
}


def validate_dol(dol):
    if (len(dol) != pokemon.DOL_BYTES or hashlib.sha1(dol).hexdigest() != pokemon.DOL_SHA1 or
            hashlib.sha256(dol).hexdigest() != pokemon.DOL_SHA256):
        raise ValueError("Original runtime-trainer DOL identity mismatch")
    word = int.from_bytes(trainer.dol_read(dol, CALLER, 4), "big")
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    if word & 0xFC000003 != 0x48000001 or CALLER + displacement != ENTRY:
        raise ValueError("Original runtime-trainer caller branch instruction mismatch")
    validate_deterministic_profile(dol)


def validate_deterministic_profile(dol):
    status_count = int.from_bytes(trainer.dol_read(dol, 0x80478B48, 4), "big")
    kaisuu_count = int.from_bytes(trainer.dol_read(dol, 0x80478B20, 4), "big")
    row = trainer.dol_read(dol, 0x8035BBA8 + 0x3F * 20, 20)
    limits = struct.unpack(">ii", trainer.dol_read(dol, 0x80315690, 8))
    if (status_count != 86 or kaisuu_count != 10 or tuple(row[:4]) != (6, 0, 1, 0) or
            pokemon.u32(row, 4) != 0 or pokemon.u16(row, 8) != 0x2A or pokemon.u16(row, 10) != 0 or
            pokemon.u32(row, 12) != 0 or pokemon.u32(row, 16) != 0 or limits != (-1, -1)):
        raise ValueError("Original runtime-trainer deterministic status3F/kaisuu0 profile mismatch")


def mem1(address, size):
    if not 0x80000000 <= address <= 0x81800000 - size:
        raise ValueError("Original runtime-trainer extent is outside MEM1")
    return address


def reconstruct(point, destination, source):
    rows = point["objects"]
    if (not isinstance(rows, list) or len(rows) != len(VIEWS) or
            len({row["name"] for row in rows}) != len(VIEWS) or {row["name"] for row in rows} != set(VIEWS)):
        raise ValueError("Original runtime-trainer has missing, duplicate or unsupported fixed views")
    chunks, hero = [], None
    for row in rows:
        register, offset, size = VIEWS[row["name"]]
        reference = source if register == "r4" else destination
        if (row["register"] != register or type(row["offset"]) is not int or row["offset"] != offset or
                type(row["size"]) is not int or row["size"] != size or
                pokemon.integer(row["address"]) != mem1(reference + offset, size)):
            raise ValueError("Original retained runtime-trainer fixed view offset/address/extent mismatch")
        raw = pokemon.hex_bytes(row["data_hex"], size)
        if register == "r4":
            hero = raw
        else:
            chunks.append((offset, raw))
    reconstructed, covered = bytearray(), 0
    for offset, raw in sorted(chunks):
        if offset != covered:
            raise ValueError("Original runtime-trainer destination views have a gap or overlap")
        reconstructed.extend(raw)
        covered += len(raw)
    if covered != RUNTIME_BYTES or hero is None:
        raise ValueError("Original runtime-trainer fixed views do not cover the complete objects")
    return bytes(reconstructed), hero


def validate_record(record, index):
    number = pokemon.integer
    if (record.get("capture_id") != f"{index:02d}-runtimeTrainerCreate" or
            record.get("candidate") != "runtimeTrainerCreate" or
            record.get("boundary_status") != "candidate_until_caller_context_validated" or
            number(record["entry_address"]) != ENTRY or number(record["caller_address"]) != CALLER):
        raise ValueError("Original runtime-trainer boundary/caller identity mismatch")
    entry, returned = record["entry"], record["return"]
    regs, out_regs = entry["registers"], returned["registers"]
    if (number(regs["pc"]) != ENTRY or number(regs["lr"]) != RETURN or
            number(out_regs["pc"]) != RETURN or number(regs["r1"]) != number(out_regs["r1"])):
        raise ValueError("Original runtime-trainer entry/return register context mismatch")
    destination, source = mem1(number(regs["r3"]), RUNTIME_BYTES), mem1(number(regs["r4"]), HERO_BYTES)
    if destination < source + HERO_BYTES and source < destination + RUNTIME_BYTES:
        raise ValueError("Original runtime-trainer destination/source overlap is outside native admission")
    before, source_before = reconstruct(entry, destination, source)
    after, source_after = reconstruct(returned, destination, source)
    if source_before != source_after:
        raise ValueError("Original runtime-trainer borrowed Hero changed during constructor")
    for point in (entry, returned):
        rng = point["rng"]
        if number(rng["pointer_address"]) != 0x80478C94 or number(rng["seed_address"]) != 0x80478C90:
            raise ValueError("Original independent runtime-trainer RNG observation missing")
        attest = point["attest"]
        if number(attest["pointer_address"]) != 0x8047ADB8 or number(attest["offset"]) != 0:
            raise ValueError("Original independent runtime-trainer attest observation missing")
        mem1(number(attest["pointee_address"]), 4)
        pokemon.hex_bytes(attest["data_hex"], 4)
    if entry["attest"] != returned["attest"]:
        raise ValueError("Original runtime-trainer attest context changed across call")
    seed = number(entry["rng"]["seed"])
    if seed != number(returned["rng"]["seed"]):
        raise ValueError("Original runtime-trainer RNG changed on pinned deterministic profile")
    if record.get("rng_observation") != {"seed_unchanged": True, "seed_address_unchanged": True}:
        raise ValueError("Original explicit runtime-trainer no-RNG observation missing or inconsistent")
    return {"capture_id": record["capture_id"], "destination_reference": destination, "source_reference": source,
            "before": before, "after": after, "source_hero": source_before,
            "trainer_id": number(regs["r5"]) & 65535, "controller_id": number(regs["r6"]),
            "sequence_identity": number(regs["r7"]), "seed": seed}


def load_records(directory, dol):
    validate_dol(dol)
    directory = Path(directory)
    report = json.loads((directory / "observation.json").read_text())
    if (report.get("schema") != 1 or report.get("kind") != "private_original_game_candidate_observation" or
            report.get("full_battle_qualified") is not False or report.get("dol_sha256") != pokemon.DOL_SHA256 or
            report.get("gdb_source_sha256") != pokemon.GDB_SHA256 or
            report.get("register_layout") != "Dolphin g=32x32be; p40/42/43/44/45"):
        raise ValueError("Original runtime-trainer observation provenance mismatch")
    raw = report.get("records")
    if (not isinstance(raw, list) or not 1 <= len(raw) <= 64 or type(report.get("stops")) is not int or
            report["stops"] != 2 * len(raw)):
        raise ValueError("Original runtime-trainer manifest stop/record coverage mismatch")
    entries = dict(trainer.OBSERVATION_ENTRIES, partySort=0x801F99C8, runtimeTrainerCreate=ENTRY)
    selected = []
    for index, row in enumerate(raw):
        candidate = row.get("candidate")
        if (candidate not in entries or row.get("capture_id") != f"{index:02d}-{candidate}" or
                pokemon.integer(row["entry_address"]) != entries[candidate] or
                row.get("boundary_status") != "candidate_until_caller_context_validated" or
                not isinstance(row.get("entry"), dict) or not isinstance(row.get("return"), dict)):
            raise ValueError("Original runtime-trainer aggregate record identity mismatch")
        if json.loads((directory / (row["capture_id"] + ".json")).read_text()) != row:
            raise ValueError("Original runtime-trainer manifest differs from individual record")
        if candidate == "runtimeTrainerCreate":
            selected.append(validate_record(row, index))
    if len(selected) != 2:
        raise ValueError("Original runtime-trainer corpus requires two constructor calls")
    return selected


def native_function(library):
    native = ctypes.CDLL(str(Path(library).resolve()))
    invoke = native.c3_battle_runtime_trainer_create
    invoke.argtypes = [pokemon.BytePointer, pokemon.BytePointer, ctypes.c_uint16, ctypes.c_uint32, ctypes.c_uint32]
    invoke.restype = ctypes.c_int
    return invoke


def replay(records, invoke):
    summaries = []
    for record in records:
        backing = (pokemon.U8 * RUNTIME_BYTES).from_buffer_copy(record["before"])
        source = (pokemon.U8 * HERO_BYTES).from_buffer_copy(record["source_hero"])
        if invoke(backing, source, record["trainer_id"], record["controller_id"], record["sequence_identity"]) != 1:
            raise ValueError("Native runtime-trainer rejected original call " + record["capture_id"])
        if bytes(source) != record["source_hero"]:
            raise ValueError("Native runtime-trainer mutated borrowed source Hero")
        if bytes(backing) != record["after"]:
            offsets = [i for i, (a, b) in enumerate(zip(bytes(backing), record["after"])) if a != b]
            raise ValueError(f"Original/native {record['capture_id']} runtime trainer differs at {len(offsets)} bytes; offsets {offsets[:32]}")
        summaries.append({"capture_id": record["capture_id"], "trainer_id": record["trainer_id"],
                          "controller_id": record["controller_id"], "sequence_identity": f"0x{record['sequence_identity']:08x}",
                          "matched_runtime_bytes": RUNTIME_BYTES, "unchanged_source_hero_bytes": HERO_BYTES,
                          "original_rng_unchanged": True, "draws": 0, "seed": f"0x{record['seed']:08x}"})
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
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f"Runtime-trainer constructor differential failed: {error}\n")
    print(json.dumps({"schema": 1, "scope": "two observed runtime-trainer field constructors only",
                      "sequence_creation_qualified": False, "active_fighters_qualified": False,
                      "full_battle_entry_qualified": False, "full_battle_qualified": False,
                      "inter_call_rng_qualified": False, "source_status3f_kaisuu0_no_rng_profile": True,
                      "dol_sha1": pokemon.DOL_SHA1,
                      "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                      "observation_sha256": hashlib.sha256((args.capture_dir / "observation.json").read_bytes()).hexdigest(),
                      "matched": len(summaries), "cases": summaries}, indent=2))


if __name__ == "__main__":
    main()
