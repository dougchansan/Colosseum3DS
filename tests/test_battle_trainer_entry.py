"""Required host-discovery coverage for the cooperative trainer-entry phase.

The source reference retains the pinned original control. Record/resource
owners and constructor spies are synthetic: these tests cannot authorize live
native98 or claim original-binary/full-entry/physical-console qualification.
"""
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
    "trainer_entry_reference", ROOT / "tools/prepare_battle_trainer_entry_reference.py")
REFERENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REFERENCE)
CC = shlex.split(os.environ.get("CC", "cc"))


class TrainerEntrySourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="c3-entry-source-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.donor = self.root / "donor"
        self.source = self.donor / REFERENCE.FILE
        self.source.parent.mkdir(parents=True, exist_ok=True)
        self.source.write_bytes((ROOT / "vendor/colosseum" / REFERENCE.FILE).read_bytes())
        self.output = self.root / "entry.h"

    def test_original_body_and_only_documented_host_adaptations(self):
        REFERENCE.prepare(self.output, self.donor)
        text = self.output.read_text(encoding="utf-8")
        self.assertIn("u8 hero[0xB1C]", text)
        self.assertIn("oracle_seed_stack(hero, sizeof hero)", text)
        self.assertIn("(void*)(uintptr_t)fightSideGetStatus", text)
        self.assertEqual(2, text.count("fightPokemonCreate(fightPokemon, pokemon"))
        self.assertLess(text.index("fightTrainerCreateFightTrainerDataIdToHero(dataId"),
                        text.index("sequence = fightTrainerCreateSequence(dataId)"))
        self.assertLess(text.index("sequence = fightTrainerCreateSequence(dataId)"),
                        text.index("fightTrainerCreate(trainer, hero"))
        self.assertNotIn("    extern ", text)

    def test_crlf_has_identical_reference(self):
        REFERENCE.prepare(self.output, self.donor)
        expected = self.output.read_bytes()
        self.source.write_bytes(self.source.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        REFERENCE.prepare(self.output, self.donor)
        self.assertEqual(expected, self.output.read_bytes())

    def test_source_drift_preserves_existing_output(self):
        self.output.write_text("keep", encoding="utf-8")
        self.source.write_bytes(self.source.read_bytes() + b"\n/* altered */\n")
        with self.assertRaisesRegex(ValueError, "source drift"):
            REFERENCE.prepare(self.output, self.donor)
        self.assertEqual("keep", self.output.read_text(encoding="utf-8"))

    def test_missing_source_is_error(self):
        self.source.unlink()
        with self.assertRaises(FileNotFoundError):
            REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_repin_needs_new_qualification(self):
        data = json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))
        data["decomp"]["commit"] = "0" * 40
        (self.root / "source-lock.json").write_text(json.dumps(data), encoding="utf-8")
        with patch.object(REFERENCE, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "Unsupported source pin"):
                REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_output_cannot_overwrite_dependency(self):
        with self.assertRaisesRegex(ValueError, "Never generate"):
            REFERENCE.prepare(self.donor / "bad.h", self.donor)
        self.assertFalse((self.donor / "bad.h").exists())


@unittest.skipUnless(CC and shutil.which(CC[0]), "Host C compiler required; provided by make test in CI")
class TrainerEntryCompiledTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="c3-trainer-entry-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.output = Path(cls.temp.name)
        REFERENCE.prepare(cls.output / "battle_trainer_entry_reference.h")

    def compile_run(self, name, sources, message):
        target = self.output / (name + (".exe" if os.name == "nt" else ""))
        flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
        links = shlex.split(os.environ.get("LDFLAGS", ""))
        command = CC + flags + ["-Iinclude", "-I" + str(self.output), "tests/" + name + ".c"]
        result = subprocess.run(command + sources + links + ["-o", str(target)], cwd=ROOT,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        self.assertEqual(0, result.returncode, result.stdout)
        result = subprocess.run([str(target)], cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertIn(message, result.stdout)

    def test_original_entry_control_waits_and_queue(self):
        self.compile_run("test_battle_trainer_entry", ["src/battle_trainer_entry.c",
            "src/battle_sequence.c", "src/battle_resume.c", "src/battle_action.c"],
            "checks against pinned original control; owner services are test doubles")

    def test_native_record_constructor_glue(self):
        self.compile_run("test_battle_trainer_entry_native", ["src/battle_trainer_entry_native.c"],
                         "checks; constructor dependency spies, not original execution")
