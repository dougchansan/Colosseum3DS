"""Native regressions against numeric observations from original Dolphin runs.

No DOL bytes or emulator dependency are required. Replaying original PowerPC
qualification itself is a separate private-original workflow.
"""
import importlib.util
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PREPARE = module("rng_source_prepare", ROOT / "tools/prepare_battle_rng.py")
ORACLE = module("rng_oracle_prepare", ROOT / "tools/qualification/prepare_rng_oracle.py")
QUALIFY = module("rng_qualify", ROOT / "tools/qualification/qualify_rng.py")
VECTORS = json.loads((ROOT / "docs/rng-original-vectors.json").read_text())


class QualificationInputTests(unittest.TestCase):
    def test_specialization_retains_real_interpreter_and_checks_pin(self):
        data = ORACLE.BASE.read_bytes()
        source = ORACLE.specialize(data)
        self.assertEqual(source.count("system.GetInterpreter().SingleStepInner();"), 1)
        self.assertIn("instruction_pcs.emplace_back", source)
        self.assertIn("!rng_pc", source)
        with self.assertRaisesRegex(ValueError, "reviewed pin"):
            ORACLE.specialize(data + b"\n// changed\n")

    def test_vectors_bind_original_identity_and_boundary_coverage(self):
        self.assertEqual(VECTORS["dol_sha1"], QUALIFY.DOL_SHA1)
        self.assertEqual(VECTORS["cases"], 72)
        self.assertFalse(VECTORS["original_zero_modulus_invoked"])
        for function, count in (("next16", 8), ("bounded32", 64)):
            rows = [row for row in VECTORS["vectors"] if row["function"] == function]
            self.assertEqual(len(rows), count)
            self.assertEqual({row["seed"] for row in rows}, set(QUALIFY.SEEDS))
            if function == "bounded32":
                self.assertEqual({row["modulus"] for row in rows}, set(QUALIFY.MODULI))
            for row in rows:
                self.assertEqual(row["draws"], 1 if function == "next16" else 2)
                self.assertEqual(row["instructions"], 13 if function == "next16" else 46)
                self.assertEqual(len(row["draw_trace"]), row["draws"])

    def test_unmapped_dol_extent_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unmapped"):
            QUALIFY.dol_read(bytes(256), 0x80000000, 4)

    def test_malformed_original_response_fails_closed(self):
        row = next(row for row in VECTORS["vectors"] if row["function"] == "next16")
        fixture_id = "negative-response-test"
        response = {"id": fixture_id, "status": "returned", "instructions": 13,
                    "draw_trace": copy.deepcopy(row["draw_trace"]),
                    "instruction_pcs": [hex(pc) for pc in range(QUALIFY.NEXT, QUALIFY.NEXT + 0x34, 4)],
                    "final": {"pc": hex(QUALIFY.RETURN),
                              "gpr": {"1": hex(QUALIFY.STACK), "3": hex(row["value"]), "13": hex(QUALIFY.SDA),
                                      "30": "0x30303030", "31": "0x31313131"},
                              "memory": [{"data_hex": f"{row['final_seed']:08x}"},
                                         {"data_hex": f"{QUALIFY.SEED:08x}"}]}}
        native = {"ok": 1, "value": row["value"], "seed": row["final_seed"], "draws": 1}
        QUALIFY.compare(response, native, "next16", fixture_id)
        for mutation in ("wrong_id", "missing_step", "wrong_count", "missing_draw", "wrong_return"):
            changed = copy.deepcopy(response)
            if mutation == "wrong_id": changed["id"] = "other-fixture"
            elif mutation == "missing_step": changed["instruction_pcs"].pop()
            elif mutation == "wrong_count": changed["instructions"] = 12
            elif mutation == "missing_draw": changed["draw_trace"] = []
            else: changed["draw_trace"][0]["return_pc"] = "0x80000000"
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                QUALIFY.compare(changed, native, "next16", fixture_id)


class NativeOriginalVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc") or shutil.which("gcc")
        if not compiler:
            raise unittest.SkipTest("C compiler unavailable; numeric vectors retained, native replay skipped")
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        output = Path(cls.directory.name)
        (output / "battle_rng_source.h").write_text(PREPARE.generate(ROOT / "vendor/colosseum"))
        cls.binary = output / "rng-native"
        subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        "-I" + str(ROOT / "include"), "-I" + str(output),
                        str(ROOT / "tools/qualification/rng_native_vectors.c"), str(ROOT / "src/battle_rng.c"),
                        "-o", str(cls.binary)], check=True, capture_output=True, text=True)

    def test_native_results_match_all_original_instruction_vectors(self):
        for row in VECTORS["vectors"]:
            with self.subTest(function=row["function"], seed=row["seed"], modulus=row.get("modulus")):
                command = [str(self.binary), row["function"], hex(row["seed"])]
                if "modulus" in row:
                    command.append(hex(row["modulus"]))
                actual = json.loads(subprocess.check_output(command, text=True))
                self.assertEqual(actual, {"ok": 1, "value": row["value"],
                                          "seed": row["final_seed"], "draws": row["draws"]})

    def test_native_rejects_zero_modulus_without_draws(self):
        for seed in (0, 1, 0xFFFFFFFF):
            actual = json.loads(subprocess.check_output([str(self.binary), "bounded32", hex(seed), "0"], text=True))
            self.assertEqual(actual, {"ok": 0, "value": 0xDEADBEEF, "seed": seed, "draws": 0})


if __name__ == "__main__":
    unittest.main()
