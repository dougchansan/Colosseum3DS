"""Public synthetic checker regressions; no original backing or table bytes."""
import copy
import ctypes
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("battle_pokemon_oracle", ROOT / "tools/qualification/check_battle_pokemon_oracle.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def synthetic_tables():
    roster = bytearray(80)
    roster[1:3] = b"\xff\xff"  # both source personality requests unrestricted
    species = bytearray(284)
    species[2] = 127
    return {"rosters": {238: bytes(roster), 239: bytes(roster)}, "species": bytes(species),
            "moves": bytes(358 * 56), "growth": bytes(404), "natures": bytes(1000),
            "nature_rates": bytes(16), "messages": {123: b"\0A\0\0"}}


def synthetic_record(roster=238, seed=10):
    final = seed
    for _ in range(7):
        final, _ = oracle.next16(final)
    # No bytes here are original observations. Tests deliberately choose an
    # output differing only in the final retained byte to exercise full extent.
    before = bytes((i % 251 for i in range(312)))
    after = before[:-1] + bytes((before[-1] ^ 255,))
    return {"roster": roster, "trainer": 0x12345678, "seed_before": seed,
            "seed_after": final, "attest": b"\x11\x22\x33\x44", "before": before, "after": after}


def capture_record(record):
    attest = {"pointer_address": "0x8047adb8", "pointee_address": "0x80408460", "offset": 0,
              "data_hex": record["attest"].hex()}
    def point(seed, body, returned=False):
        return {"registers": {"pc": hex(oracle.RETURN if returned else oracle.ENTRY),
                               "lr": hex(oracle.RETURN), "r1": "0x80500000", "r4": "0x80500100",
                               "r3": "0x1" if returned else hex(record["roster"]),
                               "r5": hex(record["trainer"])},
                "rng": {"pointer_address": "0x80478c94", "seed_address": "0x80478c90", "seed": hex(seed)},
                "objects": [{"name": "pokemon", "register": "r4", "address": "0x80500100", "size": 312,
                             "data_hex": body.hex()}], "attest": dict(attest)}
    return {"capture_id": f"{record['roster'] - 238:02d}-postHeroPreRoster", "candidate": "postHeroPreRoster",
            "boundary_status": "candidate_until_caller_context_validated", "entry_address": hex(oracle.ENTRY),
            "caller_address": hex(oracle.CALLER), "entry": point(record["seed_before"], record["before"]),
            "return": point(record["seed_after"], record["after"], True)}


class ConstructorOracleChecks(unittest.TestCase):
    def test_full_extent_and_independent_inputs_are_passed_to_native(self):
        record, tables = synthetic_record(), synthetic_tables()
        def invoke(backing, request_ptr, data_ptr, rng_ptr):
            self.assertEqual(bytes(backing), record["before"])
            request = ctypes.cast(request_ptr, ctypes.POINTER(oracle.Request)).contents
            rng = ctypes.cast(rng_ptr, ctypes.POINTER(oracle.Rng)).contents
            data = ctypes.cast(data_ptr, ctypes.POINTER(oracle.Data)).contents
            self.assertEqual((request.roster_id, request.opponent_trainer_id, bytes(request.attest)),
                             (238, record["trainer"], record["attest"]))
            self.assertEqual((rng.seed, rng.draws), (10, 0))
            self.assertEqual(ctypes.string_at(data.roster, 80), tables["rosters"][238])
            message, size = oracle.BytePointer(), ctypes.c_size_t()
            self.assertEqual(data.message(None, 123, ctypes.byref(message), ctypes.byref(size)), 1)
            self.assertEqual(ctypes.string_at(message, size.value), tables["messages"][123])
            self.assertEqual(data.message(None, 999, ctypes.byref(message), ctypes.byref(size)), 0)
            backing[:] = record["after"]
            rng.seed, rng.draws = record["seed_after"], 7
            return 1
        result = oracle.replay([record], tables, invoke)
        self.assertEqual(result[0]["matched_bytes"], 312)
        self.assertEqual(result[0]["draws"], 7)

    def test_last_byte_seed_drawcount_and_native_failure_are_detected(self):
        record, tables = synthetic_record(), synthetic_tables()
        for mutation in ("last_byte", "seed", "draws", "reject"):
            with self.subTest(mutation=mutation):
                def invoke(backing, _request, _data, rng_ptr):
                    backing[:] = record["after"]
                    rng = ctypes.cast(rng_ptr, ctypes.POINTER(oracle.Rng)).contents
                    rng.seed, rng.draws = record["seed_after"], 7
                    if mutation == "last_byte":
                        backing[311] ^= 1
                    elif mutation == "seed":
                        rng.seed ^= 1
                    elif mutation == "draws":
                        rng.draws += 2
                    return 0 if mutation == "reject" else 1
                with self.assertRaisesRegex(ValueError, "differs|RNG/count|rejected"):
                    oracle.replay([record], tables, invoke)

    def test_observed_seed_must_agree_with_source_filter_before_native(self):
        record = synthetic_record()
        record["seed_after"], _ = oracle.next16(record["seed_after"])
        with self.assertRaisesRegex(ValueError, "source draw count"):
            oracle.replay([record], synthetic_tables(), lambda *_: self.fail("Native must not run"))
        with self.assertRaisesRegex(ValueError, "stream interval"):
            oracle.seed_distance(10, 10)

    def test_missing_independent_attest_wrong_boundary_and_truncated_backing_reject(self):
        valid = capture_record(synthetic_record())
        self.assertEqual(oracle.validate_record(valid, 238)["attest"], b"\x11\x22\x33\x44")
        for mutation in ("attest", "caller", "return", "roster", "backing", "rng", "capture_id"):
            with self.subTest(mutation=mutation):
                row = copy.deepcopy(valid)
                if mutation == "attest":
                    row["entry"].pop("attest")
                elif mutation == "caller":
                    row["caller_address"] = "0x801f9e34"
                elif mutation == "return":
                    row["return"]["registers"]["pc"] = "0x801f9e40"
                elif mutation == "roster":
                    row["entry"]["registers"]["r3"] = "0xef"
                elif mutation == "backing":
                    row["return"]["objects"][0]["data_hex"] = "00" * 311
                elif mutation == "rng":
                    row["entry"]["rng"]["seed_address"] = "0x80478c94"
                else:
                    row["capture_id"] = "01-postHeroPreRoster"
                with self.assertRaises((ValueError, KeyError)):
                    oracle.validate_record(row, 238)

    def test_observation_manifest_identity_and_record_consistency(self):
        first = synthetic_record()
        second = synthetic_record(239, first["seed_after"])
        rows = [capture_record(first), capture_record(second)]
        report = {"schema": 1, "kind": "private_original_game_candidate_observation", "full_battle_qualified": False,
                  "dol_sha256": oracle.DOL_SHA256, "gdb_source_sha256": oracle.GDB_SHA256, "stops": 4,
                  "register_layout": "Dolphin g=32x32be; p40/42/43/44/45", "records": rows}
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            for i, row in enumerate(rows):
                (directory / f"{i:02d}-postHeroPreRoster.json").write_text(json.dumps(row))
            (directory / "observation.json").write_text(json.dumps(report))
            self.assertEqual(len(oracle.load_records(directory)), 2)
            for key, value in (("dol_sha256", "0" * 64), ("full_battle_qualified", True), ("records", rows[:1])):
                broken = dict(report, **{key: value})
                (directory / "observation.json").write_text(json.dumps(broken))
                with self.assertRaisesRegex(ValueError, "provenance|manifest"):
                    oracle.load_records(directory)

    def test_unpinned_original_files_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "DOL identity"):
            oracle.canonical_tables(b"not original", b"not original")
        for value in (True, 1.25, -1, 0x100000000):
            with self.subTest(value=value), self.assertRaises(ValueError):
                oracle.integer(value)


if __name__ == "__main__":
    unittest.main()
