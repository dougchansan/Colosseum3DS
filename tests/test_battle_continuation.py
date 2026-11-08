"""Required make-test discovery coverage for cooperative battle execution.

The normal ARM makefile discovers src/*.c. These tests compile the focused host
closures without requiring game assets, then exercise the pinned-source oracle.
They inherit make's CFLAGS/LDFLAGS, including its sanitizer pass.
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
    "c3_sequence_reference", ROOT / "tools/prepare_battle_sequence_reference.py")
REFERENCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REFERENCE)
CC = shlex.split(os.environ.get("CC", "cc"))


class SequenceSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="c3-sequence-source-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.donor = self.root / "donor"
        for name in REFERENCE.HASHES:
            path = self.donor / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / "vendor/colosseum" / name).read_bytes())
        self.output = self.root / "reference.h"

    def test_pinned_source_and_documented_abi_reconciliation(self):
        REFERENCE.prepare(self.output, self.donor)
        source = self.output.read_text(encoding="utf-8")
        self.assertIn("void* fn_801DE418(u16 index)", source)
        self.assertIn("extern void* fn_801DE418(u16);", source)
        self.assertIn("(u16)(uintptr_t)fightTrainerGetStatus", source)
        self.assertIn("_threadSwitch();", source)
        self.assertIn("sequenceLoad(sequence, resource)", source)

    def test_crlf_has_same_generated_reference(self):
        REFERENCE.prepare(self.output, self.donor)
        expected = self.output.read_bytes()
        for name in REFERENCE.HASHES:
            p = self.donor / name
            p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        REFERENCE.prepare(self.output, self.donor)
        self.assertEqual(expected, self.output.read_bytes())

    def test_each_source_drift_fails_before_output_write(self):
        for name in REFERENCE.HASHES:
            with self.subTest(path=name):
                p = self.donor / name
                old = p.read_bytes()
                self.output.write_text("preserve", encoding="utf-8")
                p.write_bytes(old + b"\n/* drift */\n")
                with self.assertRaisesRegex(ValueError, "Source drift"):
                    REFERENCE.prepare(self.output, self.donor)
                self.assertEqual("preserve", self.output.read_text(encoding="utf-8"))
                p.write_bytes(old)

    def test_missing_source_rejected(self):
        (self.donor / next(iter(REFERENCE.HASHES))).unlink()
        with self.assertRaises(FileNotFoundError):
            REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_repin_requires_new_qualification(self):
        lock = json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))
        lock["decomp"]["commit"] = "0" * 40
        (self.root / "source-lock.json").write_text(json.dumps(lock), encoding="utf-8")
        with patch.object(REFERENCE, "ROOT", self.root):
            with self.assertRaisesRegex(ValueError, "Unsupported source pin"):
                REFERENCE.prepare(self.output, self.donor)
        self.assertFalse(self.output.exists())

    def test_output_cannot_modify_donor(self):
        output = self.donor / "generated.h"
        with self.assertRaisesRegex(ValueError, "Never generate"):
            REFERENCE.prepare(output, self.donor)
        self.assertFalse(output.exists())

    def test_bad_extraction_is_explicit(self):
        for source in ("", "void f() {} void f() {}", "void f() {"):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    REFERENCE.extract(source, "void f()")


@unittest.skipUnless(CC and shutil.which(CC[0]), "Host C compiler required; executed by make test in Linux CI")
class BattleContinuationCompiledTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="c3-battle-continuation-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.output = Path(cls.temp.name)
        REFERENCE.prepare(cls.output / "battle_sequence_reference.h")

    def compile_and_run(self, name, sources, expected):
        binary = self.output / (name + (".exe" if os.name == "nt" else ""))
        flags = shlex.split(os.environ.get(
            "CFLAGS", "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
        links = shlex.split(os.environ.get("LDFLAGS", ""))
        command = CC + flags + ["-Iinclude", "-I" + str(self.output),
            "tests/" + name + ".c"] + sources + links + ["-o", str(binary)]
        compiled = subprocess.run(command, cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        self.assertEqual(0, compiled.returncode, compiled.stdout)
        result = subprocess.run([str(binary)], cwd=ROOT, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertIn(expected, result.stdout)

    def test_queue_parity_and_resumable_lifecycle(self):
        self.compile_and_run("test_battle_resume",
            ["src/battle_action.c", "src/battle_resume.c"], "checks; no scene or story-result binding")

    def test_original_sequence_control_trace(self):
        self.compile_and_run("test_battle_sequence", ["src/battle_sequence.c"],
            "checks against pinned source control; services are test doubles")

    def test_queue_resource_animation_integration(self):
        self.compile_and_run("test_battle_continuation",
            ["src/battle_action.c", "src/battle_resume.c", "src/battle_sequence.c"],
            "checks; synthetic owner services")


if __name__ == "__main__":
    unittest.main()
