#!/usr/bin/env python3
"""Check private original party-sort wrappers against native source extraction.

Inputs are actual entry backing, stack scratch, logical guest identities and
mutable global projections. No return bytes supply native inputs. Observations
qualify these wrapper calls only, never intervening RNG or complete battle entry.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct

import check_battle_pokemon_oracle as pokemon
import check_battle_trainer_oracle as trainer

ENTRY, CALLER, RETURN = 0x801F99C8, 0x8020D3EC, 0x8020D3F0
HEAD_BYTES, HERO_BYTES, FIGHT_BYTES, SCRATCH_BYTES = 3524, 2840, 340, 1872
COUNTS = (820, 261, 5510, 413, 18)
STRIDES = (52, 40, 80, 284, 44)
LookupCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
                                 ctypes.POINTER(pokemon.BytePointer), ctypes.POINTER(ctypes.c_size_t))
SideCallback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint16,
                               ctypes.POINTER(ctypes.c_uint32))


class Data(ctypes.Structure):
    _fields_ = [("counts", ctypes.c_uint32 * 5), ("lookup", LookupCallback), ("target_side", SideCallback),
                ("user", ctypes.c_void_p), ("battle_result", ctypes.c_uint16), ("host_side", ctypes.c_uint16)]


class Request(ctypes.Structure):
    _fields_ = [("trainer_reference", ctypes.c_uint32), ("hero_reference", ctypes.c_uint32),
                ("fight_references", ctypes.c_uint32 * 2), ("party_references", ctypes.c_uint32 * 6),
                ("trainer_id", ctypes.c_uint16), ("selection_count", ctypes.c_uint16),
                ("selection_mode", ctypes.c_uint16), ("filter", ctypes.c_uint8)]


class State(ctypes.Structure):
    _fields_ = [("hero", pokemon.U8 * HERO_BYTES), ("fight_pokemon", (pokemon.U8 * FIGHT_BYTES) * 2),
                ("scratch", (pokemon.U8 * 312) * 6), ("selected", ctypes.c_uint32 * 6),
                ("candidates", ctypes.c_uint32 * 6), ("candidate_count", ctypes.c_uint16),
                ("entry_counter", ctypes.c_uint16), ("rng", pokemon.Rng)]


def canonical_tables(dol, rel):
    if (len(dol) != pokemon.DOL_BYTES or hashlib.sha1(dol).hexdigest() != pokemon.DOL_SHA1 or
            hashlib.sha256(dol).hexdigest() != pokemon.DOL_SHA256):
        raise ValueError("Original party-sort DOL identity mismatch")
    if len(rel) != pokemon.REL_BYTES or hashlib.sha1(rel).hexdigest() != pokemon.REL_SHA1:
        raise ValueError("Original party-sort common REL identity mismatch")
    for offset, expected in zip((0x14BEF8, 0x14BEFC, 0x14BF00, 0x14BF2C), COUNTS):
        if pokemon.u32(rel, offset) != expected:
            raise ValueError("Original party-sort table count mismatch")
    if int.from_bytes(trainer.dol_read(dol, 0x80478B38, 4), "big") != COUNTS[4]:
        raise ValueError("Original party-sort type count mismatch")
    tables = [pokemon.take(rel, 0x184C + offset, count * stride) for offset, count, stride in
              zip((0x91684, 0x9BD14, 0x9E5DC, 0x121A04), COUNTS, STRIDES)]
    tables.append(trainer.dol_read(dol, 0x8035B500, COUNTS[4] * STRIDES[4]))
    word = int.from_bytes(trainer.dol_read(dol, CALLER, 4), "big")
    displacement = word & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    if word & 0xFC000003 != 0x48000001 or CALLER + displacement != ENTRY:
        raise ValueError("Original party-sort caller branch instruction mismatch")
    return tables


def mem1(address, size):
    if not 0x80000000 <= address <= 0x81800000 - size:
        raise ValueError("Original party-sort extent is outside MEM1")
    return address


def halfword(row, address):
    if pokemon.integer(row["address"]) != address or row["size"] != 2:
        raise ValueError("Original independent halfword projection extent mismatch")
    value = int.from_bytes(pokemon.hex_bytes(row["data_hex"], 2), "big")
    if type(row["value_u16"]) is not int or row["value_u16"] != value:
        raise ValueError("Original independent halfword value mismatch")
    return value


def encounter_context(row):
    number = pokemon.integer
    if (number(row["id_address"]) != 0x8046DDA2 or
            number(row["count_pointer_address"]) != 0x80478F50 or
            number(row["data_pointer_address"]) != 0x80478F54 or row["row_size"] != 56):
        raise ValueError("Original mutable encounter projection identity mismatch")
    count, index = number(row["count"]), number(row["id"])
    if not 0 < count <= 65536 or not index < count:
        raise ValueError("Original mutable encounter count/ID is invalid")
    mem1(number(row["count_pointer"]), 4)
    base = mem1(number(row["base"]), 56)
    address = mem1(base + index * 56, 56)
    if number(row["row_address"]) != address:
        raise ValueError("Original mutable encounter row address mismatch")
    raw = pokemon.hex_bytes(row["row_hex"], 56)
    if type(row["trainer_byte"]) is not int or row["trainer_byte"] != raw[1]:
        raise ValueError("Original mutable encounter trainer byte mismatch")
    return raw[1]


def validate_record(record, index):
    number = pokemon.integer
    if (record.get("capture_id") != f"{index:02d}-partySort" or record.get("candidate") != "partySort" or
            record.get("boundary_status") != "candidate_until_caller_context_validated" or
            number(record["entry_address"]) != ENTRY or number(record["caller_address"]) != CALLER):
        raise ValueError("Original party-sort boundary/caller identity mismatch")
    entry, returned = record["entry"], record["return"]
    regs, out_regs = entry["registers"], returned["registers"]
    if (number(regs["pc"]) != ENTRY or number(regs["lr"]) != RETURN or number(out_regs["pc"]) != RETURN or
            number(regs["r1"]) != number(out_regs["r1"])):
        raise ValueError("Original party-sort entry/return register context mismatch")
    reference = mem1(number(regs["r3"]), HEAD_BYTES)
    stack = mem1(number(regs["r1"]), 4)
    if stack % 8:
        raise ValueError("Original party-sort stack is not ABI aligned")
    decoded = []
    for point in (entry, returned):
        objects = point["objects"]
        if (len(objects) != 1 or objects[0].get("name") != "trainer_head" or
                objects[0].get("register") != "r3" or objects[0].get("size") != HEAD_BYTES or
                number(objects[0]["address"]) != reference):
            raise ValueError("Original full retained trainer-head observation missing")
        head = pokemon.hex_bytes(objects[0]["data_hex"], HEAD_BYTES)
        sort = point["party_sort"]
        if number(sort["entry_stack"]) != stack:
            raise ValueError("Original retained party-sort stack identity mismatch")
        outputs = {}
        for key, offset, size in (("sorted_pokemon", -0x770, SCRATCH_BYTES), ("selected", -0x788, 24)):
            row = sort[key]
            if row["size"] != size or number(row["address"]) != mem1(stack + offset, size):
                raise ValueError("Original independent party-sort stack extent mismatch")
            outputs[key] = pokemon.hex_bytes(row["data_hex"], size)
        trainer_id = halfword(sort["trainer_data_id"], reference)
        if trainer_id != pokemon.u16(head, 0) or trainer_id >= COUNTS[0]:
            raise ValueError("Original runtime trainer ID projection mismatch")
        counter = halfword(sort["entry_counter"], 0x80478250)
        result = halfword(sort["global_result"], 0x8047B5D8)
        host_side = encounter_context(sort["encounter"])
        rng = point["rng"]
        if number(rng["pointer_address"]) != 0x80478C94 or number(rng["seed_address"]) != 0x80478C90:
            raise ValueError("Original independent party-sort RNG observation missing")
        attest = point["attest"]
        if number(attest["pointer_address"]) != 0x8047ADB8 or number(attest["offset"]) != 0:
            raise ValueError("Original independent party-sort attest observation missing")
        mem1(number(attest["pointee_address"]), 4)
        pokemon.hex_bytes(attest["data_hex"], 4)
        decoded.append(dict(outputs, head=head, trainer_id=trainer_id, counter=counter, result=result,
                            host_side=host_side, seed=number(rng["seed"])))
    before, after = decoded
    if (entry["attest"] != returned["attest"] or
            entry["party_sort"]["encounter"] != returned["party_sort"]["encounter"] or
            entry["party_sort"]["global_result"] != returned["party_sort"]["global_result"] or
            before["trainer_id"] != after["trainer_id"]):
        raise ValueError("Original read-only party-sort context changed during wrapper")
    selection_count = number(regs["r4"]) & 65535
    seed, draws = before["seed"], None
    # The driver draws once per selected slot; the wrapper caps slots at six.
    for count in range(min(selection_count, 6) + 1):
        if seed == after["seed"]:
            draws = count
            break
        seed, _ = pokemon.next16(seed)
    if draws is None:
        raise ValueError("Original party-sort final RNG is outside source selection draw bound")
    return {"capture_id": record["capture_id"], "reference": reference,
            "selection_count": selection_count, "selection_mode": number(regs["r5"]) & 65535,
            "before": before, "after": after, "draws": draws}


def load_records(directory):
    directory = Path(directory)
    report = json.loads((directory / "observation.json").read_text())
    if (report.get("schema") != 1 or report.get("kind") != "private_original_game_candidate_observation" or
            report.get("full_battle_qualified") is not False or report.get("dol_sha256") != pokemon.DOL_SHA256 or
            report.get("gdb_source_sha256") != pokemon.GDB_SHA256 or
            report.get("register_layout") != "Dolphin g=32x32be; p40/42/43/44/45"):
        raise ValueError("Original party-sort observation provenance mismatch")
    raw = report.get("records")
    if (not isinstance(raw, list) or not 1 <= len(raw) <= 64 or type(report.get("stops")) is not int or
            report["stops"] != 2 * len(raw)):
        raise ValueError("Original party-sort manifest stop/record coverage mismatch")
    entries = dict(trainer.OBSERVATION_ENTRIES, partySort=ENTRY)
    selected = []
    for index, row in enumerate(raw):
        candidate = row.get("candidate")
        if (candidate not in entries or row.get("capture_id") != f"{index:02d}-{candidate}" or
                pokemon.integer(row["entry_address"]) != entries[candidate] or
                row.get("boundary_status") != "candidate_until_caller_context_validated" or
                not isinstance(row.get("entry"), dict) or not isinstance(row.get("return"), dict)):
            raise ValueError("Original party-sort aggregate record identity mismatch")
        if json.loads((directory / (row["capture_id"] + ".json")).read_text()) != row:
            raise ValueError("Original party-sort manifest differs from individual record")
        if candidate == "partySort":
            selected.append(validate_record(row, index))
    if len(selected) != 2:
        raise ValueError("Original party-sort corpus requires two wrapper observations")
    return selected


def native_function(library):
    native = ctypes.CDLL(str(Path(library).resolve()))
    invoke = native.c3_battle_party_sort
    invoke.argtypes = [ctypes.POINTER(State), ctypes.POINTER(Request), ctypes.POINTER(Data)]
    invoke.restype = ctypes.c_int
    return invoke


def replay(records, tables, invoke):
    table_buffers = [(pokemon.U8 * len(raw)).from_buffer_copy(raw) for raw in tables]
    summaries = []
    for record in records:
        before, after = record["before"], record["after"]
        state = State()
        state.hero[:] = before["head"][4:4 + HERO_BYTES]
        for i in range(2):
            state.fight_pokemon[i][:] = before["head"][0xB1C + FIGHT_BYTES * i:0xB1C + FIGHT_BYTES * (i + 1)]
        for i in range(6):
            state.scratch[i][:] = before["sorted_pokemon"][i * 312:(i + 1) * 312]
        state.selected[:] = struct.unpack(">6I", before["selected"])
        # candidates/candidate_count are adapter-only internal observations;
        # no original input value is claimed for these unobserved outputs.
        state.entry_counter, state.rng = before["counter"], pokemon.Rng(before["seed"], 0)
        reference = record["reference"]
        request = Request(reference, reference + 4,
                          (ctypes.c_uint32 * 2)(reference + 0xB1C, reference + 0xB1C + FIGHT_BYTES),
                          (ctypes.c_uint32 * 6)(*(reference + 4 + 0x30 + i * 312 for i in range(6))),
                          before["trainer_id"], record["selection_count"], record["selection_mode"], 0)
        errors = []
        @LookupCallback
        def lookup(_user, table, row_id, output, size):
            if table >= len(COUNTS) or row_id >= COUNTS[table]:
                errors.append("table")
                return 0
            offset, width = row_id * STRIDES[table], STRIDES[table]
            output[0] = ctypes.cast(ctypes.byref(table_buffers[table], offset), pokemon.BytePointer)
            size[0] = width
            return 1
        @SideCallback
        def side(_user, _target, _host_side, _output):
            # No target-side graph was captured. Unknown context must reject.
            errors.append("unobserved target-side graph")
            return 0
        data = Data((ctypes.c_uint32 * 5)(*COUNTS), lookup, side, None, before["result"], before["host_side"])
        request_before, data_before = bytes(request), bytes(data)
        if invoke(ctypes.byref(state), ctypes.byref(request), ctypes.byref(data)) != 1 or errors:
            raise ValueError("Native party-sort rejected original call " + record["capture_id"])
        if bytes(request) != request_before or bytes(data) != data_before:
            raise ValueError("Native party-sort mutated borrowed request/data context")
        head = before["head"][:4] + bytes(state.hero) + b"".join(bytes(row) for row in state.fight_pokemon)
        scratch = b"".join(bytes(row) for row in state.scratch)
        selected = struct.pack(">6I", *state.selected)
        for name, actual, expected in (("trainer head", head, after["head"]),
                                       ("sorted Pokemon scratch", scratch, after["sorted_pokemon"]),
                                       ("selected handles", selected, after["selected"])):
            if actual != expected:
                offsets = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
                raise ValueError(f"Original/native {record['capture_id']} {name} differs at {len(offsets)} bytes; offsets {offsets[:32]}")
        if (state.entry_counter, state.rng.seed, state.rng.draws) != (after["counter"], after["seed"], record["draws"]):
            raise ValueError("Original/native party-sort counter or RNG/count mismatch")
        if any(bytes(buffer) != original for buffer, original in zip(table_buffers, tables)):
            raise ValueError("Native party-sort mutated borrowed canonical tables")
        summaries.append({"capture_id": record["capture_id"], "trainer_id": before["trainer_id"],
                          "matched_trainer_head_bytes": HEAD_BYTES, "matched_sorted_scratch_bytes": SCRATCH_BYTES,
                          "matched_selected_handle_bytes": 24, "entry_counter_before": before["counter"],
                          "entry_counter_after": after["counter"], "draws": record["draws"],
                          "seed_before": f"0x{before['seed']:08x}", "seed_after": f"0x{after['seed']:08x}",
                          "mutable_context_unchanged": True, "canonical_tables_unchanged": True})
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
        parser.exit(1, f"Party-sort wrapper differential failed: {error}\n")
    print(json.dumps({"schema": 1, "scope": "two observed party-copy/sort wrapper calls only",
                      "full_battle_entry_qualified": False, "full_battle_qualified": False,
                      "inter_call_rng_qualified": False, "target_side_graph_qualified": False,
                      "unobserved_candidate_scratch_qualified": False,
                      "dol_sha1": pokemon.DOL_SHA1, "common_rel_sha1": pokemon.REL_SHA1,
                      "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
                      "observation_sha256": hashlib.sha256((args.capture_dir / "observation.json").read_bytes()).hexdigest(),
                      "matched": len(summaries), "cases": summaries}, indent=2))


if __name__ == "__main__":
    main()
