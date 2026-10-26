"""Synthetic sorting-checker tests; all observed backing and rows are invented."""
import copy
import ctypes
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/qualification"))
spec = importlib.util.spec_from_file_location("battle_party_sort_oracle", ROOT / "tools/qualification/check_battle_party_sort_oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
p = oracle.pokemon


def synthetic_tables():
    return [bytes((table + 1,)) * (count * stride) for table, (count, stride) in enumerate(zip(oracle.COUNTS, oracle.STRIDES))]


def raw_record(index=0, skipped=False):
    reference, stack = 0x80600000, 0x80501008
    head = bytearray(i % 251 for i in range(3524))
    struct.pack_into(">H", head, 0, 1 if skipped else 10)
    head_after = bytearray(head)
    if not skipped:
        head_after[4:2844] = b"\x83" * 2840
    scratch = bytes((i % 239 for i in range(1872)))
    scratch_after = scratch if skipped else b"\x62" * 1872
    selected = struct.pack(">6I", 0xDEADBEEF, 7, 0, 0xFFFFFFFF, 42, 99)
    selected_after = selected if skipped else struct.pack(">6I", reference + 0x34, reference + 0x34 + 312, 0, 0, 0, 0)
    final_seed = 10
    for _ in range(0 if skipped else 2):
        final_seed, _ = p.next16(final_seed)
    encounter = {"id_address": "0x8046dda2", "id": 10, "count_pointer_address": "0x80478f50",
                 "count_pointer": "0x807b0000", "count": 566, "data_pointer_address": "0x80478f54",
                 "base": "0x807a0000", "row_address": hex(0x807A0000 + 10 * 56), "row_size": 56,
                 "row_hex": (b"\0\7" + b"\x53" * 54).hex(), "trainer_byte": 7}
    def halfword(address, value):
        return {"address": hex(address), "size": 2, "data_hex": struct.pack(">H", value).hex(), "value_u16": value}
    def point(returned):
        return {"registers": {"pc": hex(oracle.RETURN if returned else oracle.ENTRY), "lr": hex(oracle.RETURN),
                               "r1": hex(stack), "r3": hex(reference), "r4": "0x2", "r5": "0x2"},
                "objects": [{"name": "trainer_head", "register": "r3", "address": hex(reference), "size": 3524,
                             "data_hex": bytes(head_after if returned else head).hex()}],
                "rng": {"pointer_address": "0x80478c94", "seed_address": "0x80478c90", "seed": hex(final_seed if returned else 10)},
                "attest": {"pointer_address": "0x8047adb8", "pointee_address": "0x80408460", "offset": 0, "data_hex": "11223344"},
                "party_sort": {"entry_stack": hex(stack),
                    "sorted_pokemon": {"address": hex(stack - 0x770), "size": 1872,
                                       "data_hex": (scratch_after if returned else scratch).hex()},
                    "selected": {"address": hex(stack - 0x788), "size": 24,
                                 "data_hex": (selected_after if returned else selected).hex()},
                    "entry_counter": halfword(0x80478250, (65532 if skipped or not returned else 4)),
                    "trainer_data_id": halfword(reference, 1 if skipped else 10),
                    "global_result": halfword(0x8047B5D8, 0), "encounter": copy.deepcopy(encounter)}}
    return {"capture_id": f"{index:02d}-partySort", "candidate": "partySort",
            "boundary_status": "candidate_until_caller_context_validated", "entry_address": hex(oracle.ENTRY),
            "caller_address": hex(oracle.CALLER), "entry": point(False), "return": point(True)}


class PartySortOracleTests(unittest.TestCase):
    def fake_native(self, records, mutation=None):
        calls = []
        def invoke(state_ptr, request_ptr, data_ptr):
            record = records[len(calls)]
            before, after = record["before"], record["after"]
            calls.append(record["capture_id"])
            state = ctypes.cast(state_ptr, ctypes.POINTER(oracle.State)).contents
            request = ctypes.cast(request_ptr, ctypes.POINTER(oracle.Request)).contents
            data = ctypes.cast(data_ptr, ctypes.POINTER(oracle.Data)).contents
            self.assertEqual(bytes(state.hero), before["head"][4:2844])
            self.assertEqual(b"".join(bytes(row) for row in state.fight_pokemon), before["head"][2844:])
            self.assertEqual(b"".join(bytes(row) for row in state.scratch), before["sorted_pokemon"])
            self.assertEqual(struct.pack(">6I", *state.selected), before["selected"])
            reference = record["reference"]
            self.assertEqual((request.trainer_reference, request.hero_reference), (reference, reference + 4))
            self.assertEqual(list(request.fight_references), [reference + 0xB1C, reference + 0xB1C + 340])
            self.assertEqual(list(request.party_references), [reference + 0x34 + i * 312 for i in range(6)])
            self.assertEqual((data.battle_result, data.host_side), (before["result"], 7))
            self.assertEqual((state.entry_counter, state.rng.seed, state.rng.draws), (before["counter"], before["seed"], 0))
            for table, row_id in ((0, 10), (1, 81), (2, 238), (3, 288), (4, 0)):
                row, size = p.BytePointer(), ctypes.c_size_t()
                self.assertEqual(data.lookup(None, table, row_id, ctypes.byref(row), ctypes.byref(size)), 1)
                self.assertEqual(size.value, oracle.STRIDES[table])
                self.assertEqual(ctypes.string_at(row, size.value), bytes((table + 1,)) * size.value)
                if mutation == "table" and table == 3:
                    row[0] ^= 1
            state.hero[:] = after["head"][4:2844]
            for i in range(2):
                state.fight_pokemon[i][:] = after["head"][2844 + i * 340:2844 + (i + 1) * 340]
            for i in range(6):
                state.scratch[i][:] = after["sorted_pokemon"][i * 312:(i + 1) * 312]
            state.selected[:] = struct.unpack(">6I", after["selected"])
            state.entry_counter, state.rng = after["counter"], p.Rng(after["seed"], record["draws"])
            if mutation == "head-tail":
                state.fight_pokemon[1][339] ^= 1
            elif mutation == "scratch-tail":
                state.scratch[5][311] ^= 1
            elif mutation == "selected-tail":
                state.selected[5] ^= 1
            elif mutation == "counter":
                state.entry_counter ^= 1
            elif mutation == "rng":
                state.rng.draws += 1
            elif mutation == "request":
                request.selection_count += 1
            elif mutation == "unknown-side":
                output = ctypes.c_uint32()
                self.assertEqual(data.target_side(None, 1, 7, ctypes.byref(output)), 0)
            elif mutation == "missing-row":
                row, size = p.BytePointer(), ctypes.c_size_t()
                self.assertEqual(data.lookup(None, 0, 820, ctypes.byref(row), ctypes.byref(size)), 0)
            return 0 if mutation == "reject" else 1
        return invoke, calls

    def test_full_source_extents_logical_handles_mutable_context_and_skip(self):
        records = [oracle.validate_record(raw_record(), 0), oracle.validate_record(raw_record(1, True), 1)]
        native, calls = self.fake_native(records)
        summaries = oracle.replay(records, synthetic_tables(), native)
        self.assertEqual(len(calls), 2)
        self.assertEqual([row["draws"] for row in summaries], [2, 0])
        self.assertEqual([row["entry_counter_after"] for row in summaries], [4, 65532])
        for summary in summaries:
            self.assertEqual((summary["matched_trainer_head_bytes"], summary["matched_sorted_scratch_bytes"],
                              summary["matched_selected_handle_bytes"]), (3524, 1872, 24))

    def test_every_final_extent_counter_rng_and_immutable_input_mismatch_rejects(self):
        for mutation in ("head-tail", "scratch-tail", "selected-tail", "counter", "rng", "table", "request",
                         "unknown-side", "missing-row", "reject"):
            with self.subTest(mutation=mutation):
                records = [oracle.validate_record(raw_record(), 0)]
                native, _ = self.fake_native(records, mutation)
                with self.assertRaisesRegex(ValueError, "differs|counter or RNG|mutated borrowed|rejected"):
                    oracle.replay(records, synthetic_tables(), native)

    def test_independent_stack_counter_trainer_context_bounds_and_readonly_checks(self):
        for mutation in ("scratch", "selected", "counter", "trainer", "encounter-byte", "encounter-row",
                         "global-result", "attest", "head", "return"):
            with self.subTest(mutation=mutation):
                raw = raw_record()
                before, after = raw["entry"]["party_sort"], raw["return"]["party_sort"]
                if mutation == "scratch":
                    before["sorted_pokemon"]["address"] = "0x80500000"
                elif mutation == "selected":
                    before["selected"]["data_hex"] = "00" * 23
                elif mutation == "counter":
                    before["entry_counter"]["value_u16"] = 0
                elif mutation == "trainer":
                    before["trainer_data_id"]["data_hex"] = "0001"
                    before["trainer_data_id"]["value_u16"] = 1
                elif mutation == "encounter-byte":
                    before["encounter"]["trainer_byte"] = 8
                elif mutation == "encounter-row":
                    before["encounter"]["row_address"] = "0x807a0000"
                elif mutation == "global-result":
                    after["global_result"]["data_hex"], after["global_result"]["value_u16"] = "0001", 1
                elif mutation == "attest":
                    del raw["entry"]["attest"]
                elif mutation == "head":
                    raw["return"]["objects"][0]["data_hex"] = "00" * 3523
                else:
                    raw["return"]["registers"]["pc"] = "0x8020d3ec"
                with self.assertRaises((ValueError, KeyError)):
                    oracle.validate_record(raw, 0)

    def test_source_draw_bound_rejects_missing_draws_and_no_intercall_link_is_assumed(self):
        raw = raw_record()
        seed = 10
        for _ in range(3):
            seed, _ = p.next16(seed)
        raw["return"]["rng"]["seed"] = hex(seed)
        with self.assertRaisesRegex(ValueError, "source selection draw bound"):
            oracle.validate_record(raw, 0)
        first = oracle.validate_record(raw_record(), 0)
        second_raw = raw_record(1, True)
        second_raw["entry"]["rng"]["seed"] = second_raw["return"]["rng"]["seed"] = "0x98765432"
        second = oracle.validate_record(second_raw, 1)
        native, _ = self.fake_native([first, second])
        self.assertEqual(len(oracle.replay([first, second], synthetic_tables(), native)), 2)

    def test_aggregate_manifest_exact_files_ids_and_provenance(self):
        rows = [raw_record(), raw_record(1, True)]
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 4,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": rows}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for row in rows:
                (directory / (row["capture_id"] + ".json")).write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            self.assertEqual(len(oracle.load_records(directory)), 2)
            (directory / "01-partySort.json").write_text(json.dumps(dict(rows[1], changed=True)))
            with self.assertRaisesRegex(ValueError, "manifest differs"):
                oracle.load_records(directory)
            (directory / "01-partySort.json").write_text(json.dumps(rows[1]))
            report["dol_sha256"] = "0" * 64
            (directory / "observation.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, "provenance"):
                oracle.load_records(directory)

    def test_unpinned_canonical_tables_reject(self):
        with self.assertRaisesRegex(ValueError, "DOL identity"):
            oracle.canonical_tables(b"not original", b"not original")


if __name__ == "__main__":
    unittest.main()
