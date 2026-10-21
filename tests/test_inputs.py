import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "verify_inputs", Path(__file__).resolve().parents[1] / "tools/verify_inputs.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InputVerificationTests(unittest.TestCase):
    def test_missing_corrupt_and_matching_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "sys").mkdir()
            manifest = root / "hashes.txt"
            manifest.write_text(hashlib.sha1(b"synthetic").hexdigest() +
                                "  build/GC6E01/main.dol\n")
            self.assertFalse(MODULE.verify(root, manifest)[0]["ok"])
            (root / "sys/main.dol").write_bytes(b"corrupt")
            self.assertFalse(MODULE.verify(root, manifest)[0]["ok"])
            (root / "sys/main.dol").write_bytes(b"synthetic")
            self.assertTrue(MODULE.verify(root, manifest)[0]["ok"])

    def test_rejects_duplicate_and_traversal_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "hashes.txt"
            line = "0" * 40 + "  build/GC6E01/main.dol\n"
            manifest.write_text(line + line)
            with self.assertRaises(ValueError):
                MODULE.load_manifest(manifest)
            manifest.write_text("0" * 40 + "  ../../main.dol\n")
            with self.assertRaises(ValueError):
                MODULE.load_manifest(manifest)

    def test_rejects_symlink_to_outside_originals(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "orig/sys").mkdir(parents=True)
            (root / "outside").write_bytes(b"synthetic")
            (root / "orig/sys/main.dol").symlink_to(root / "outside")
            manifest = root / "hashes.txt"
            manifest.write_text(hashlib.sha1(b"synthetic").hexdigest() +
                                "  build/GC6E01/main.dol\n")
            self.assertFalse(MODULE.verify(root / "orig", manifest)[0]["ok"])


if __name__ == "__main__":
    unittest.main()
