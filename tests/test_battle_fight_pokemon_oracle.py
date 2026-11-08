"""Synthetic FightPokemon-checker tests without original byte observations."""
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
spec = importlib.util.spec_from_file_location("battle_fight_pokemon_oracle", ROOT / "tools/qualification/check_battle_fight_pokemon_oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
p = oracle.pokemon


def synthetic_dol():
    data = bytearray(0x40100)
    struct.pack_into(">I", data, 0, 0x100)
    struct.pack_into(">I", data, 0x48, 0x80200000)
    struct.pack_into(">I", data, 0x90, 0x40000)
    for caller in oracle.CALLERS:
        # Synthetic linked branches generated from addresses, not copied DOL.
        word = 0x48000001 | ((oracle.ENTRY - caller) & 0x03FFFFFC)
        struct.pack_into(">I", data, 0x100 + caller - 0x80200000, word)
    return bytes(data)


def record(caller=0x802333EC, index=1):
    initial = bytes((i % 251 for i in range(340)))
    expected = bytes(((i + 97) % 251 for i in range(340)))
    source = bytes(((i + 13) % 251 for i in range(312)))
    def point(returned):
        return {"registers": {"pc": hex(caller + 4 if returned else oracle.ENTRY), "lr": hex(caller + 4),
                               "r1": "0x80500108", "r3": "0x80600000", "r4": "0x80700000", "r5": "0xfffffffd"},
                "objects": [{"name": "fight_pokemon", "register": "r3", "address": "0x80600000", "size": 340,
                             "data_hex": (expected if returned else initial).hex()},
                            {"name": "pokemon", "register": "r4", "address": "0x80700000", "size": 312,
                             "data_hex": source.hex()}],
                "rng": {"pointer_address": "0x80478c94", "seed_address": "0x80478c90", "seed": "0x12345678"}}
    return {"capture_id": f"{index:02d}-fightPokemonCreate", "candidate": "fightPokemonCreate",
            "boundary_status": "candidate_until_caller_context_validated", "entry_address": hex(oracle.ENTRY),
            "caller_address": hex(caller), "entry": point(False), "return": point(True),
            "rng_observation": {"seed_unchanged": True, "seed_address_unchanged": True}}


class FightPokemonOracleTests(unittest.TestCase):
    def test_full_destination_and_independent_immutable_source_forwarding(self):
        normalized = oracle.validate_record(record(), 1, synthetic_dol())
        def invoke(backing, source, source_reference, entry_id):
            self.assertEqual(bytes(backing), normalized["before"])
            self.assertEqual(bytes(source), normalized["source"])
            self.assertEqual((source_reference, entry_id), (0x80700000, -3))
            backing[:] = normalized["after"]
            return 1
        report = oracle.replay([normalized], invoke)
        self.assertEqual(report[0]["matched_destination_bytes"], 340)
        self.assertEqual(report[0]["unchanged_source_bytes"], 312)
        self.assertEqual(report[0]["draws"], 0)

    def test_last_destination_byte_source_tail_and_native_rejection_fail(self):
        normalized = oracle.validate_record(record(), 1, synthetic_dol())
        for mutation in ("destination-tail", "source-tail", "reject"):
            with self.subTest(mutation=mutation):
                def invoke(backing, source, _reference, _entry):
                    backing[:] = normalized["after"]
                    if mutation == "destination-tail":
                        backing[339] ^= 1
                    elif mutation == "source-tail":
                        source[311] ^= 1
                    return 0 if mutation == "reject" else 1
                with self.assertRaisesRegex(ValueError, "destination bytes; offsets \\[339\\]|mutated borrowed source|rejected"):
                    oracle.replay([normalized], invoke)

    def test_original_source_or_rng_changes_and_missing_annotation_reject(self):
        for mutation in ("source-tail", "rng", "annotation", "extent"):
            with self.subTest(mutation=mutation):
                raw = record()
                if mutation == "source-tail":
                    data = bytearray.fromhex(raw["return"]["objects"][1]["data_hex"])
                    data[311] ^= 1
                    raw["return"]["objects"][1]["data_hex"] = data.hex()
                elif mutation == "rng":
                    raw["return"]["rng"]["seed"] = "0x12345679"
                elif mutation == "annotation":
                    del raw["rng_observation"]
                else:
                    raw["return"]["objects"][0]["data_hex"] = "00" * 339
                with self.assertRaisesRegex(ValueError, "source changed|RNG changed|no-RNG|Incomplete"):
                    oracle.validate_record(raw, 1, synthetic_dol())

    def test_only_observed_pinned_caller_controls_classification(self):
        dol = synthetic_dol()
        self.assertEqual(oracle.validate_record(record(0x8020D47C, 4), 4, dol)["classification"], "final-entry-primary")
        self.assertEqual(oracle.validate_record(record(0x802333EC, 5), 5, dol)["classification"], "temporary-sort-selection")
        raw = record()
        raw["caller_address"] = "0x802333e8"
        with self.assertRaisesRegex(ValueError, "Unqualified"):
            oracle.validate_record(raw, 1, dol)
        changed = bytearray(dol)
        struct.pack_into(">I", changed, 0x100 + 0x802333EC - 0x80200000, 0x48000001)
        with self.assertRaisesRegex(ValueError, "branch instruction"):
            oracle.validate_record(record(), 1, bytes(changed))

    def test_retained_pointer_overlap_and_wrong_return_context_reject(self):
        for mutation in ("pointer", "overlap", "return", "register"):
            with self.subTest(mutation=mutation):
                raw = record()
                if mutation == "pointer":
                    raw["return"]["objects"][1]["address"] = "0x80700004"
                elif mutation == "overlap":
                    raw["entry"]["registers"]["r4"] = "0x80600010"
                elif mutation == "return":
                    raw["return"]["registers"]["pc"] = "0x802333ec"
                else:
                    raw["entry"]["objects"][1]["register"] = "r3"
                with self.assertRaises(ValueError):
                    oracle.validate_record(raw, 1, synthetic_dol())

    def test_mixed_manifest_uses_original_indices_without_parent_transform(self):
        rows = []
        for index in range(6):
            if index in (0, 3):
                rows.append({"capture_id": f"{index:02d}-opponentTrainerCreate", "candidate": "opponentTrainerCreate",
                             "entry_address": hex(oracle.trainer.ENTRY), "entry": {}, "return": {},
                             "boundary_status": "candidate_until_caller_context_validated"})
            else:
                rows.append(record(0x802333EC if index < 3 else 0x8020D47C, index))
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": p.DOL_SHA256, "gdb_source_sha256": p.GDB_SHA256, "stops": 12,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": rows}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for row in rows:
                (directory / (row["capture_id"] + ".json")).write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            # Only identity hashing is bypassed for this synthetic DOL fixture.
            with patch.object(oracle, "validate_dol"):
                loaded = oracle.load_records(directory, synthetic_dol())
            self.assertEqual([row["capture_id"] for row in loaded], [f"{i:02d}-fightPokemonCreate" for i in (1, 2, 4, 5)])
            self.assertEqual([row["classification"] for row in loaded],
                             ["temporary-sort-selection"] * 2 + ["final-entry-primary"] * 2)

    def test_unpinned_dol_rejected(self):
        with self.assertRaisesRegex(ValueError, "DOL identity"):
            oracle.validate_dol(synthetic_dol())


if __name__ == "__main__":
    unittest.main()
