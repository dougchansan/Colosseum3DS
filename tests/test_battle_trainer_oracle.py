"""Synthetic parent-checker tests; no original hero, save or table bytes."""
import copy
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/qualification"))
spec = importlib.util.spec_from_file_location("battle_trainer_oracle", ROOT / "tools/qualification/check_battle_trainer_oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
p = oracle.pokemon


def tables():
    trainers = bytearray(820 * 52)
    trainers[52 + 3], trainers[52 + 7] = 1, 1  # trainer1, saved-player kind
    trainers[10 * 52 + 3], trainers[10 * 52 + 5], trainers[10 * 52 + 7] = 2, 238, 2
    roster = bytearray(80)
    roster[1:3], roster[10:12] = b"\xff\xff", b"\x01\x20"
    species = bytearray(284)
    species[2] = 127
    return {"trainers": bytes(trainers), "rosters": {238: bytes(roster), 239: bytes(roster), 240: bytes(80)},
            "species": bytes(species), "moves": bytes(358 * 56), "growth": bytes(404),
            "natures": bytes(1000), "nature_rates": bytes(16),
            "messages": {123: b"\0A\0\0", 0xFA2: b"\0B\0\0"}, "items": {7: (900, 2)}, "item_count": 548}


def records():
    before = bytes((i % 251 for i in range(oracle.HERO_SIZE)))
    saved = bytes(((i + 17) % 251 for i in range(oracle.HERO_SIZE)))
    scratch = bytes((i % 241 for i in range(312)))
    base = {"input_device": 1, "seed_before": 10, "attest": b"\x11\x22\x33\x44", "before": before,
            "scratch": scratch, "saved_player": saved, "selector": 99, "special_name": None}
    copied = dict(base, trainer_id=1, after=saved, seed_after=10)
    seed = 10
    for _ in range(16):
        seed, _ = p.next16(seed)
    generated = dict(base, trainer_id=10, after=b"\x76" * oracle.HERO_SIZE, seed_after=seed)
    return [copied, generated]


def capture(record, index):
    stack, address, savebase = 0x80500108, 0x80501000, 0x80408460  # actual ABI also permits 8 mod16
    attest = {"pointer_address": "0x8047adb8", "pointee_address": hex(savebase), "offset": 0,
              "data_hex": record["attest"].hex()}
    def point(returned):
        return {"registers": {"pc": hex(oracle.RETURN if returned else oracle.ENTRY), "lr": hex(oracle.RETURN),
                               "r1": hex(stack), "r3": hex(record["trainer_id"]),
                               "r4": hex(record["input_device"] & 0xFFFFFFFF), "r5": hex(address)},
                "objects": [{"name": "hero", "register": "r5", "address": hex(address), "size": oracle.HERO_SIZE,
                             "data_hex": record["after" if returned else "before"].hex()}],
                "rng": {"pointer_address": "0x80478c94", "seed_address": "0x80478c90",
                        "seed": hex(record["seed_after" if returned else "seed_before"])}, "attest": dict(attest)}
    row = {"capture_id": f"{index:02d}-opponentTrainerCreate", "candidate": "opponentTrainerCreate",
           "boundary_status": "candidate_until_caller_context_validated", "entry_address": hex(oracle.ENTRY),
           "caller_address": hex(oracle.CALLER), "entry": point(False), "return": point(True)}
    row["entry"]["parent_entry_inputs"] = {
        "pokemon_scratch": {"entry_stack": hex(stack), "offset": -0x148, "address": hex(stack - 0x148),
                            "size": 312, "data_hex": record["scratch"].hex()},
        "saved_hero": {"savedata_base": hex(savebase), "offset": 0x70, "address": hex(savebase + 0x70),
                       "size": oracle.HERO_SIZE, "data_hex": record["saved_player"].hex()},
        "trainer_selector": {"address": "0x80478960", "value": hex(record["selector"])},
        "entry_trainer_id": hex(record["trainer_id"]), "replacement_name": {"applies": False}}
    return row


class ParentConstructorOracleTests(unittest.TestCase):
    def invoke(self, rows, mutation=None):
        calls = []
        def native(hero, scratch, request_ptr, data_ptr, rng_ptr):
            request = ctypes.cast(request_ptr, ctypes.POINTER(oracle.Request)).contents
            data = ctypes.cast(data_ptr, ctypes.POINTER(oracle.Data)).contents
            rng = ctypes.cast(rng_ptr, ctypes.POINTER(p.Rng)).contents
            record = rows[len(calls)]
            calls.append(request.trainer_id)
            self.assertEqual(bytes(hero), record["before"])
            self.assertEqual(bytes(scratch), record["scratch"])
            self.assertEqual(ctypes.string_at(data.saved_player, oracle.HERO_SIZE), record["saved_player"])
            self.assertEqual(bytes(request.attest), record["attest"])
            self.assertEqual((request.trainer_id, request.input_device, rng.seed, rng.draws),
                             (record["trainer_id"], record["input_device"], record["seed_before"], 0))
            self.assertEqual(data.special_trainer_id, record["selector"])
            if record["special_name"] is not None:
                self.assertEqual(ctypes.string_at(data.special_name, data.special_name_size), record["special_name"])
            output = p.Data()
            self.assertEqual(data.roster(None, 240, ctypes.byref(output)), 1)
            self.assertEqual(ctypes.string_at(output.roster, 80), bytes(80))
            self.assertEqual(data.roster(None, 238, ctypes.byref(output)), 1)
            name, kind = ctypes.c_uint32(), ctypes.c_uint8()
            self.assertEqual(data.item(None, 7, ctypes.byref(name), ctypes.byref(kind)), 1)
            self.assertEqual((name.value, kind.value), (900, 2))
            message, size = p.BytePointer(), ctypes.c_size_t()
            self.assertEqual(data.message(None, 123, ctypes.byref(message), ctypes.byref(size)), 1)
            self.assertEqual(ctypes.string_at(message, size.value), b"\0A\0\0")
            hero[:] = record["after"]
            scratch[:] = b"\x99" * 312  # no observed final scratch: never claim a comparison
            rng.seed = record["seed_after"]
            rng.draws = 0 if request.trainer_id == 1 else 16
            if mutation == "last-byte":
                hero[-1] ^= 1
            elif mutation == "rng":
                rng.draws += 1
            elif mutation == "missing-item":
                data.item(None, 8, ctypes.byref(name), ctypes.byref(kind))
            return 0 if mutation == "reject" else 1
        return native, calls

    def test_complete_hero_and_independent_entry_inputs_forwarded(self):
        rows, data = records(), tables()
        rows[1]["special_name"] = b"\0Z\0\0"
        rows[1]["selector"] = 10
        native, calls = self.invoke(rows)
        report = oracle.replay(rows, data, native)
        self.assertEqual(calls, [1, 10])
        self.assertEqual([row["matched_hero_bytes"] for row in report], [2840, 2840])
        self.assertEqual([row["draws"] for row in report], [0, 16])
        self.assertTrue(all(row["scratch_final_observed"] is False for row in report))

    def test_last_hero_byte_rng_missing_callback_and_rejection_fail(self):
        for mutation in ("last-byte", "rng", "missing-item", "reject"):
            with self.subTest(mutation=mutation):
                rows = records()
                native, _ = self.invoke(rows, mutation)
                with self.assertRaisesRegex(ValueError, "differs|RNG/count|rejected"):
                    oracle.replay(rows, tables(), native)

    def test_seed_source_filter_and_branch_coverage_checked(self):
        rows = records()
        rows[0]["seed_after"] ^= 1
        with self.assertRaisesRegex(ValueError, "source call pattern"):
            oracle.replay(rows, tables(), lambda *_: self.fail("native must not run"))
        rows = records()
        native, _ = self.invoke(rows[:1])
        with self.assertRaisesRegex(ValueError, "corpus must cover"):
            oracle.replay(rows[:1], tables(), native)
        row = dict(rows[1], trainer_id=11)
        with self.assertRaisesRegex(ValueError, "outside qualified"):
            # Give trainer11 a valid generated count/kind but an unqualified roster.
            data = tables()
            trainers = bytearray(data["trainers"])
            trainers[11 * 52 + 7] = 1
            data["trainers"] = bytes(trainers)
            oracle.source_result(row, data)

    def test_capture_independent_inputs_bounds_and_replacement_condition(self):
        row = capture(records()[0], 0)
        validated = oracle.validate_record(row, 0)
        self.assertEqual(validated["scratch"], records()[0]["scratch"])
        self.assertEqual(validated["saved_player"], records()[0]["saved_player"])
        for mutation in ("scratch", "save", "attest", "selector", "condition", "tail", "stack"):
            with self.subTest(mutation=mutation):
                bad = copy.deepcopy(row)
                inputs = bad["entry"]["parent_entry_inputs"]
                if mutation == "scratch":
                    inputs["pokemon_scratch"]["offset"] += 4
                elif mutation == "save":
                    inputs["saved_hero"]["address"] = "0x80408460"
                elif mutation == "attest":
                    bad["entry"].pop("attest")
                elif mutation == "selector":
                    inputs["trainer_selector"]["address"] = "0x80478964"
                elif mutation == "condition":
                    inputs["replacement_name"]["applies"] = True
                elif mutation == "tail":
                    bad["return"]["objects"][0]["data_hex"] = "00" * 2839
                else:
                    bad["entry"]["registers"]["r1"] = "0x80500104"
                    bad["return"]["registers"]["r1"] = "0x80500104"
                with self.assertRaises((ValueError, KeyError)):
                    oracle.validate_record(bad, 0)

    def test_actual_selector_match_requires_independent_name_pointer(self):
        row = capture(records()[1], 1)
        inputs = row["entry"]["parent_entry_inputs"]
        inputs["trainer_selector"]["value"] = "0xa"
        inputs["replacement_name"] = {"applies": True, "pointer_address": "0x8047a670", "address": "0x80502000",
                                      "size": 22, "data_hex": (b"\0Z\0\0" + b"\x77" * 18).hex()}
        self.assertEqual(oracle.validate_record(row, 1)["special_name"], b"\0Z\0\0")
        del inputs["replacement_name"]["data_hex"]
        with self.assertRaises(KeyError):
            oracle.validate_record(row, 1)

    def test_manifest_provenance_and_consistency(self):
        rows = [capture(record, i) for i, record in enumerate(records())]
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 4,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": rows}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for i, row in enumerate(rows):
                (directory / f"{i:02d}-opponentTrainerCreate.json").write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            self.assertEqual(len(oracle.load_records(directory)), 2)
            report["records"] = rows[:1]
            (directory / "observation.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, "manifest"):
                oracle.load_records(directory)

    def test_opponent_before_player_with_intervening_rng_is_supported(self):
        # The real caller observes generated opponent first, then player copy.
        # Its surrounding sequence work consumes RNG between the two calls;
        # that gap is outside this checker's per-constructor qualification.
        generated = dict(records()[1], seed_before=0x743CCD37, input_device=0)
        seed = generated["seed_before"]
        for _ in range(16):
            seed, _ = p.next16(seed)
        generated["seed_after"] = seed
        player = dict(records()[0], seed_before=0x8FF186AB, seed_after=0x8FF186AB)
        pair = [generated, player]
        self.assertNotEqual(generated["seed_after"], player["seed_before"])
        raw = [capture(record, i) for i, record in enumerate(pair)]
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 4,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": raw}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for i, row in enumerate(raw):
                (directory / f"{i:02d}-opponentTrainerCreate.json").write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            loaded = oracle.load_records(directory)
        native, calls = self.invoke(loaded)
        summaries = oracle.replay(loaded, tables(), native)
        self.assertEqual(calls, [10, 1])
        self.assertEqual([row["draws"] for row in summaries], [16, 0])
        self.assertEqual([row["branch"] for row in summaries], ["ordinary-Willie-generated", "saved-player-copy"])
        loaded[1]["seed_after"] ^= 1
        native, calls = self.invoke(loaded)
        with self.assertRaisesRegex(ValueError, "source call pattern"):
            oracle.replay(loaded, tables(), native)
        self.assertEqual(calls, [10])

    def test_plain_name_bounds_and_unpinned_tables_fail(self):
        for name in (b"\0A", b"x", b"\xff\xff\0\0"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                oracle.plain_string(name)
        with self.assertRaisesRegex(ValueError, "DOL identity"):
            oracle.canonical_tables(b"untrusted", b"untrusted")

    def test_mixed_original_manifest_keeps_original_parent_indices_and_checks_all_files(self):
        pair = [records()[1], records()[0]]
        raw = []
        for index in range(6):
            if index in (0, 3):
                row = capture(pair[index // 3], index)
                scratch = row["entry"]["parent_entry_inputs"]["pokemon_scratch"]
                row["return"]["parent_return_scratch"] = {
                    "address": scratch["address"], "size": 312, "data_hex": (b"\x99" * 312).hex()}
            else:
                # Parent selection checks aggregate identity/integrity; the
                # separate FightPokemon checker owns these objects' contents.
                row = {"capture_id": f"{index:02d}-fightPokemonCreate", "candidate": "fightPokemonCreate",
                       "boundary_status": "candidate_until_caller_context_validated",
                       "entry_address": "0x80206aec", "entry": {}, "return": {}}
            raw.append(row)
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 12,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": raw}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for row in raw:
                (directory / (row["capture_id"] + ".json")).write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            loaded = oracle.load_records(directory)
            self.assertEqual([row["trainer_id"] for row in loaded], [10, 1])
            self.assertTrue(all(row["scratch_after"] == b"\x99" * 312 for row in loaded))
            native, _ = self.invoke(loaded)
            summaries = oracle.replay(loaded, tables(), native)
            self.assertTrue(all(row["scratch_final_observed"] is True for row in summaries))
            # Even an unselected original record must match its manifest entry.
            (directory / "02-fightPokemonCreate.json").write_text(json.dumps(dict(raw[2], extra=True)))
            with self.assertRaisesRegex(ValueError, "manifest differs"):
                oracle.load_records(directory)
            (directory / "02-fightPokemonCreate.json").write_text(json.dumps(raw[2]))
            report["stops"] = 4
            (directory / "observation.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, "stop/record"):
                oracle.load_records(directory)

    def test_final_scratch_last_byte_mismatch_and_retained_address_are_checked(self):
        rows = records()
        for row in rows:
            row["scratch_after"] = b"\x99" * 312
        native, _ = self.invoke(rows)
        def bad_tail(hero, scratch, *args):
            result = native(hero, scratch, *args)
            scratch[311] ^= 1
            return result
        with self.assertRaisesRegex(ValueError, "final scratch bytes; offsets \\[311\\]"):
            oracle.replay(rows, tables(), bad_tail)
        raw = capture(rows[0], 0)
        raw["return"]["parent_return_scratch"] = {"address": "0x80500000", "size": 312,
                                                    "data_hex": (b"\x99" * 312).hex()}
        with self.assertRaisesRegex(ValueError, "retained parent final scratch"):
            oracle.validate_record(raw, 0)
        del raw["return"]["parent_return_scratch"]
        self.assertIsNone(oracle.validate_record(raw, 0)["scratch_after"])


if __name__ == "__main__":
    unittest.main()
