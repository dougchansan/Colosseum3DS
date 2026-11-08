"""Synthetic checker regressions; no original runtime, Hero or instruction bytes."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/qualification"))
spec = importlib.util.spec_from_file_location("battle_runtime_trainer_oracle", ROOT / "tools/qualification/check_battle_runtime_trainer_oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
p = oracle.pokemon


def raw_record(index=0, trainer_id=10, seed=0x12345678):
    destination, source = 0x80600000, 0x80501008
    before = bytes((i % 239 for i in range(10468)))
    after = bytes(((i + 97) % 239 for i in range(10468)))
    hero = bytes(((i + 13) % 251 for i in range(2840)))
    def point(returned):
        objects = []
        for name, (register, offset, size) in oracle.VIEWS.items():
            reference = source if register == "r4" else destination
            data = hero if register == "r4" else (after if returned else before)[offset:offset + size]
            objects.append({"name": name, "register": register, "offset": offset,
                            "address": hex(reference + offset), "size": size, "data_hex": data.hex()})
        return {"registers": {"pc": hex(oracle.RETURN if returned else oracle.ENTRY), "lr": hex(oracle.RETURN),
                               "r1": "0x80500008", "r3": hex(destination), "r4": hex(source),
                               "r5": hex(0xF00D0000 | trainer_id), "r6": "0x12345600", "r7": "0x81234567"},
                "objects": objects,
                "rng": {"pointer_address": "0x80478c94", "seed_address": "0x80478c90", "seed": hex(seed)},
                "attest": {"pointer_address": "0x8047adb8", "pointee_address": "0x80408460", "offset": 0, "data_hex": "11223344"}}
    return {"capture_id": f"{index:02d}-runtimeTrainerCreate", "candidate": "runtimeTrainerCreate",
            "boundary_status": "candidate_until_caller_context_validated", "entry_address": hex(oracle.ENTRY),
            "caller_address": hex(oracle.CALLER), "entry": point(False), "return": point(True),
            "rng_observation": {"seed_unchanged": True, "seed_address_unchanged": True}}


class RuntimeTrainerOracleTests(unittest.TestCase):
    def invoke(self, records, mutation=None):
        calls = []
        def native(backing, hero, trainer_id, controller_id, sequence_identity):
            record = records[len(calls)]
            calls.append(trainer_id)
            self.assertEqual(bytes(backing), record["before"])
            self.assertEqual(bytes(hero), record["source_hero"])
            # Scalars come from original entry registers, even if the expected
            # byte stream happens to encode unrelated values at those fields.
            self.assertEqual((trainer_id, controller_id, sequence_identity),
                             (record["trainer_id"], 0x12345600, 0x81234567))
            backing[:] = record["after"]
            if mutation == "destination-last-byte":
                backing[10467] ^= 1
            elif mutation == "source-last-byte":
                hero[2839] ^= 1
            return 0 if mutation == "reject" else 1
        return native, calls

    def test_all_destination_views_and_entry_only_hero_arguments_are_forwarded(self):
        raw = raw_record()
        # Capture order is irrelevant: reconstruct views by fixed offsets.
        raw["entry"]["objects"].reverse()
        raw["return"]["objects"] = raw["return"]["objects"][2:] + raw["return"]["objects"][:2]
        record = oracle.validate_record(raw, 0)
        self.assertEqual(len(record["before"]), 10468)
        self.assertEqual(len(record["source_hero"]), 2840)
        self.assertEqual(record["trainer_id"], 10)
        native, _ = self.invoke([record])
        summary = oracle.replay([record], native)[0]
        self.assertEqual((summary["matched_runtime_bytes"], summary["unchanged_source_hero_bytes"], summary["draws"]),
                         (10468, 2840, 0))

    def test_final_destination_byte_hero_tail_and_native_rejection_fail(self):
        record = oracle.validate_record(raw_record(), 0)
        for mutation in ("destination-last-byte", "source-last-byte", "reject"):
            with self.subTest(mutation=mutation):
                native, _ = self.invoke([record], mutation)
                with self.assertRaisesRegex(ValueError, "runtime trainer differs.*10467|mutated borrowed source Hero|rejected"):
                    oracle.replay([record], native)

    def test_missing_duplicate_gap_overlap_and_retained_reference_view_rejections(self):
        for mutation in ("missing", "duplicate", "gap", "overlap", "tag", "address", "extent", "last-byte-missing", "register"):
            with self.subTest(mutation=mutation):
                raw = raw_record()
                views = raw["return"]["objects"]
                if mutation == "missing":
                    views.pop(1)
                elif mutation == "duplicate":
                    views[1] = copy.deepcopy(views[0])
                elif mutation == "gap":
                    views[1]["offset"] += 4
                elif mutation == "overlap":
                    views[1]["offset"] -= 4
                elif mutation == "tag":
                    views[2]["offset"] = 4096
                elif mutation == "address":
                    views[1]["address"] = "0x80601004"
                elif mutation == "extent":
                    views[2]["size"] = 2275
                elif mutation == "last-byte-missing":
                    views[2]["data_hex"] = views[2]["data_hex"][:-2]
                else:
                    views[3]["register"] = "r3"
                with self.assertRaises(ValueError):
                    oracle.validate_record(raw, 0)

    def test_original_source_rng_attest_and_entry_register_context_must_match(self):
        for mutation in ("hero", "rng", "annotation", "attest", "overlap", "return", "sequence-input"):
            with self.subTest(mutation=mutation):
                raw = raw_record()
                if mutation == "hero":
                    hero = bytearray.fromhex(raw["return"]["objects"][3]["data_hex"])
                    hero[2839] ^= 1
                    raw["return"]["objects"][3]["data_hex"] = hero.hex()
                elif mutation == "rng":
                    raw["return"]["rng"]["seed"] = "0x12345679"
                elif mutation == "annotation":
                    del raw["rng_observation"]
                elif mutation == "attest":
                    del raw["entry"]["attest"]
                elif mutation == "overlap":
                    raw["entry"]["registers"]["r4"] = "0x80600100"
                elif mutation == "return":
                    raw["return"]["registers"]["pc"] = "0x8020d3b0"
                else:
                    del raw["entry"]["registers"]["r7"]
                with self.assertRaises((ValueError, KeyError)):
                    oracle.validate_record(raw, 0)

    def test_each_actor_call_is_independent_of_order_and_intervening_rng(self):
        records = [oracle.validate_record(raw_record(0, 1, 0xABCDEF01), 0),
                   oracle.validate_record(raw_record(1, 10, 0x12345678), 1)]
        native, calls = self.invoke(records)
        self.assertEqual(len(oracle.replay(records, native)), 2)
        self.assertEqual(calls, [1, 10])

    def test_aggregate_manifest_and_all_original_individual_files_are_consistent(self):
        rows = [raw_record(), raw_record(1, 1, 0xABCDEF01)]
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 4,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": rows}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for row in rows:
                (directory / (row["capture_id"] + ".json")).write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            # Synthetic record fixture tests manifest logic; real DOL identity
            # and no-draw profile are verified separately by the production CLI.
            with patch.object(oracle, "validate_dol"):
                self.assertEqual(len(oracle.load_records(directory, b"synthetic")), 2)
            (directory / "01-runtimeTrainerCreate.json").write_text(json.dumps(dict(rows[1], changed=True)))
            with patch.object(oracle, "validate_dol"), self.assertRaisesRegex(ValueError, "manifest differs"):
                oracle.load_records(directory, b"synthetic")

    def test_deterministic_source_profile_rejects_unexpected_random_path(self):
        profile = {0x80478B48: struct.pack(">I", 86), 0x80478B20: struct.pack(">I", 10),
                   0x8035BBA8 + 0x3F * 20: struct.pack(">BBBBIHHII", 6, 0, 1, 0, 0, 0x2A, 0, 0, 0),
                   0x80315690: struct.pack(">ii", -1, -1)}
        def read(_dol, address, size):
            self.assertEqual(len(profile[address]), size)
            return profile[address]
        with patch.object(oracle.trainer, "dol_read", side_effect=read):
            oracle.validate_deterministic_profile(b"synthetic")
            profile[0x80315690] = struct.pack(">ii", -1, 0)
            with self.assertRaisesRegex(ValueError, "deterministic status3F/kaisuu0"):
                oracle.validate_deterministic_profile(b"synthetic")
        with self.assertRaisesRegex(ValueError, "DOL identity"):
            oracle.validate_dol(b"untrusted")


if __name__ == "__main__":
    unittest.main()
