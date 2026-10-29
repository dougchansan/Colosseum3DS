"""Source predicate and record-view gates; scene/constructor owners are test spies."""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "entry_records_reference", ROOT / "tools/prepare_battle_entry_records_reference.py")
REFERENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REFERENCE)
CC = shlex.split(os.environ.get("CC", "cc"))


class EntryRecordSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="c3-entry-records-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.donor = self.root / "donor"
        for name in REFERENCE.FILES:
            path = self.donor / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / "vendor/colosseum" / name).read_bytes())
        self.output = self.root / "reference.h"

    def test_original_predicates_retained(self):
        REFERENCE.prepare(self.output, self.donor)
        text = self.output.read_text(encoding="utf-8")
        self.assertIn("val >= (header = (PokemonRangeObjHeader*)", text)
        self.assertIn("0x19c", text)
        self.assertIn("ref_pokemon_scalar(ptr, 0, 0xb8, 0) != 1", text)
        self.assertIn("ref_fightPokemonCheckValidInline(pokemon)", text)
        self.assertIn("if (moveData == data)", text)
        self.assertIn("*param_5 = -1", text)
        self.assertNotIn("    extern ", text)
        self.assertNotIn("ref_result(context)", text)
        self.assertNotIn("c3_battle_entry_records", text)

    def test_crlf_is_identical(self):
        REFERENCE.prepare(self.output, self.donor)
        expected = self.output.read_bytes()
        for name in REFERENCE.FILES:
            p = self.donor / name
            p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        REFERENCE.prepare(self.output, self.donor)
        self.assertEqual(expected, self.output.read_bytes())

    def test_every_protected_source_rejects_drift_before_output(self):
        for name in REFERENCE.FILES:
            with self.subTest(name=name):
                path = self.donor / name
                original = path.read_bytes()
                self.output.write_text("preserve existing output", encoding="utf-8")
                path.write_bytes(original + b"\n/* drift */\n")
                with self.assertRaisesRegex(ValueError, "source drift"):
                    REFERENCE.prepare(self.output, self.donor)
                self.assertEqual("preserve existing output", self.output.read_text(encoding="utf-8"))
                path.write_bytes(original)

    def test_missing_source_does_not_publish(self):
        (self.donor / next(iter(REFERENCE.FILES))).unlink()
        with self.assertRaises(FileNotFoundError):
            REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_repin_requires_qualification(self):
        data = json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))
        data["decomp"]["commit"] = "0" * 40
        (self.root / "source-lock.json").write_text(json.dumps(data), encoding="utf-8")
        with patch.object(REFERENCE, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "Unsupported source pin"):
                REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_output_does_not_modify_dependency(self):
        with self.assertRaisesRegex(ValueError, "Never generate"):
            REFERENCE.prepare(self.donor / "bad.h", self.donor)
        self.assertFalse((self.donor / "bad.h").exists())

    def test_ambiguous_or_incomplete_definition_rejected(self):
        signature = "int sample(void)"
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            REFERENCE.body("int sample(void) { return 0; }\nint sample(void) { return 1; }", signature)
        with self.assertRaisesRegex(ValueError, "Unbalanced"):
            REFERENCE.body("int sample(void) {", signature)


@unittest.skipUnless(CC and shutil.which(CC[0]), "Host C compiler required; supplied by make test")
class EntryRecordCompiledTests(unittest.TestCase):
    def test_predicates_bounds_immutability_and_suspended_entry(self):
        with tempfile.TemporaryDirectory(prefix="c3-record-compiled-") as temp:
            output = Path(temp)
            REFERENCE.prepare(output / "battle_entry_records_reference.h")
            flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
            links = shlex.split(os.environ.get("LDFLAGS", ""))
            target = output / ("test_entry_records.exe" if os.name == "nt" else "test_entry_records")
            command = CC + flags + ["-Iinclude", "-I" + str(output),
                "tests/test_battle_entry_records.c", "src/battle_entry_records.c",
                "src/battle_trainer_entry_native.c", "src/battle_trainer_entry.c",
                "src/battle_sequence.c", "src/battle_action.c"] + links + ["-o", str(target)]
            result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=90)
            self.assertEqual(0, result.returncode, result.stdout)
            result = subprocess.run([str(target)], cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=90)
            self.assertEqual(0, result.returncode, result.stdout)
            self.assertIn("source predicates and synthetic owner integration", result.stdout)
