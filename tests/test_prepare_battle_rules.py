import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_battle_rules", ROOT / "tools/prepare_battle_rules.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BattleRulesSourceTests(unittest.TestCase):
    def copy_donor(self, folder):
        donor = Path(folder) / "donor"
        for path in module.PINS:
            target = donor / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "vendor/colosseum" / path, target)
        return donor

    def test_extracted_bodies_change_only_symbols_and_floor_handle_type(self):
        generated = module.generate(ROOT / "vendor/colosseum")
        self.assertIn(module.COMMIT, generated)
        for path, signature in module.FUNCTIONS:
            with self.subTest(signature=signature):
                original = module.function((ROOT / "vendor/colosseum" / path).read_text(), signature)
                lowered_signature = "static " + signature.replace("void*", "u32")
                for name in module.SYMBOLS:
                    lowered_signature = lowered_signature.replace(name, "c3r_src_" + name)
                extracted = module.function(generated, lowered_signature)
                restored = extracted[len("static "):].replace("c3r_src_", "")
                restored = restored.replace("u32 floor", "void* floor")
                restored = restored.replace("fightFloorGetStatus(u32,", "fightFloorGetStatus(void*,")
                self.assertEqual(restored, original)
        self.assertIn("return c3r_src_fn_801ADCD8();", generated)
        self.assertIn("c3r_src_fn_800E0C54() % tieCount", generated)
        self.assertNotIn("bounded32", generated)
        self.assertNotIn("#pragma", generated)

    def test_line_endings_are_portable_without_altering_source(self):
        with tempfile.TemporaryDirectory() as folder:
            donor = self.copy_donor(folder)
            expected = module.generate(donor)
            for path in module.PINS:
                target = donor / path
                target.write_bytes(target.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            self.assertEqual(module.generate(donor), expected)

    def test_rejects_each_modified_or_truncated_source(self):
        with tempfile.TemporaryDirectory() as folder:
            donor = self.copy_donor(folder)
            for path in module.PINS:
                target = donor / path
                original = target.read_bytes()
                for changed in (original + b"\n", original[:len(original) // 2]):
                    with self.subTest(path=path, size=len(changed)):
                        target.write_bytes(changed)
                        with self.assertRaisesRegex(ValueError, "source differs from pin"):
                            module.generate(donor)
                target.write_bytes(original)

    def test_rejects_missing_source(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(OSError):
                module.generate(Path(folder))

    def test_rejects_manifest_and_lock_drift(self):
        manifest = json.loads((ROOT / "config/battle-rules-source-files.json").read_text())
        lock = json.loads((ROOT / "source-lock.json").read_text())
        for changed in (dict(manifest, commit="0" * 40), dict(manifest, files=[])):
            with patch.object(module.json, "loads", side_effect=[changed, lock]):
                with self.assertRaisesRegex(ValueError, "manifest/lock pin changed"):
                    module.generate(ROOT / "vendor/colosseum")
        changed_lock = dict(lock, decomp=dict(lock["decomp"], commit="0" * 40))
        with patch.object(module.json, "loads", side_effect=[manifest, changed_lock]):
            with self.assertRaisesRegex(ValueError, "manifest/lock pin changed"):
                module.generate(ROOT / "vendor/colosseum")

    def test_cli_failure_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as folder:
            donor = self.copy_donor(folder)
            target = donor / next(iter(module.PINS))
            target.write_bytes(target.read_bytes() + b"\n")
            output = Path(folder) / "rules.inc"
            output.write_text("preserve me")
            result = subprocess.run([sys.executable, str(ROOT / "tools/prepare_battle_rules.py"),
                                     "--source", str(donor), "--output", str(output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("source differs from pin", result.stderr)
            self.assertEqual(output.read_text(), "preserve me")

    def test_missing_and_unterminated_definitions_rejected(self):
        for text in ("s32 f(void);", "s32 f(void) { return 1;"):
            with self.assertRaises(ValueError):
                module.function(text, "s32 f(void)")


if __name__ == "__main__":
    unittest.main()
