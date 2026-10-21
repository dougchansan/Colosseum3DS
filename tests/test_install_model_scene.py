"""Synthetic isolated-scene FTP transactions; never contact a console."""
import ftplib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("install_model_scene", TOOLS / "install_model_scene.py")
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)
hardware = installer.hardware


class FakeFTP:
    def __init__(self):
        root = hardware.APP_DIR
        self.directories = {root, root + "/3ds", root + "/3ds/Colosseum3DS"}
        self.files = {root + "/3ds/Colosseum3DS/" + name: b"original-" + name.encode()
                      for name in hardware.FILES}
        self.writes, self.corrupt = [], False

    def nlst(self, directory):
        if directory not in self.directories:
            raise ftplib.error_temp("450 unavailable")
        prefix = directory + "/"
        return [path for path in self.directories | self.files.keys()
                if path.startswith(prefix) and "/" not in path[len(prefix):]]

    def retrbinary(self, command, callback):
        path = command[5:]
        if path not in self.files:
            raise ftplib.error_perm("550 absent")
        callback(self.files[path])

    def mkd(self, path):
        self.writes.append(("mkdir", path))
        if path in self.directories or path in self.files:
            raise ftplib.error_perm("550 exists")
        self.directories.add(path)

    def storbinary(self, command, stream):
        path = command[5:]
        self.writes.append(("store", path))
        if path in self.files:
            raise AssertionError("Attempt to replace a file")
        self.files[path] = stream.read() + (b"corrupt" if self.corrupt else b"")

    def rename(self, source, destination):
        self.writes.append(("rename", source, destination))
        if destination in self.files:
            raise AssertionError("Attempt to overwrite a destination")
        self.files[destination] = self.files.pop(source)


class SceneInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        for name, magic in zip(hardware.FILES, (b"3DSX", b"SMDH")):
            (self.source / name).write_bytes(magic + b"fixture")
        self.model = self.root / "fixture.c3m"
        self.model.write_bytes(b"C3M1fixture")
        self.ftp = FakeFTP()
        self.original = dict(self.ftp.files)
        self.expected = {name: hardware.digest(self.original[hardware.APP_DIR + "/3ds/Colosseum3DS/" + name])
                         for name in hardware.FILES}
        self.evidence = {"build_metadata": {"git_commit": "a" * 40,
                         "build_configuration": "Makefile.3ds", "toolchain": "devkitARM fixture"}}
        self.checkpoints = []

    def install(self):
        installer.install(self.ftp, self.source, self.model, self.root, self.evidence,
                          True, self.expected, lambda: self.checkpoints.append(len(self.ftp.writes)))

    def assert_original_intact(self):
        for path, content in self.original.items():
            self.assertEqual(self.ftp.files[path], content)
            self.assertFalse(any(path in action[1:] for action in self.ftp.writes))

    def test_absent_data_directory_success_and_manifest_before_writes(self):
        self.install()
        self.assertTrue(self.evidence["nested_installation"])
        self.assertTrue(self.evidence["deployment_verified"])
        self.assertFalse(self.evidence["recovery_required"])
        self.assertEqual(self.checkpoints[0], 0)
        self.assertEqual(self.ftp.files[hardware.APP_DIR + "/data/eevee.c3m"], self.model.read_bytes())
        self.assertEqual(sum(action[0] == "rename" for action in self.ftp.writes), 3)
        self.assert_original_intact()

    def test_existing_equal_model_reused_without_upload(self):
        self.ftp.directories.add(hardware.APP_DIR + "/data")
        path = hardware.APP_DIR + "/data/eevee.c3m"
        self.ftp.files[path] = self.model.read_bytes()
        self.install()
        self.assertFalse(any("eevee.c3m" in action[1] for action in self.ftp.writes))
        self.assertEqual(sum(action[0] == "rename" for action in self.ftp.writes), 2)
        self.assert_original_intact()

    def test_existing_different_model_refuses_before_writes(self):
        self.ftp.directories.add(hardware.APP_DIR + "/data")
        self.ftp.files[hardware.APP_DIR + "/data/eevee.c3m"] = b"other"
        with self.assertRaisesRegex(ValueError, "differs"):
            self.install()
        self.assertEqual(self.ftp.writes, [])
        self.assert_original_intact()

    def test_nested_baseline_mismatch_refuses_before_writes(self):
        self.expected[hardware.FILES[0]] = "0" * 64
        with self.assertRaisesRegex(ValueError, "baseline hash"):
            self.install()
        self.assertEqual(self.ftp.writes, [])
        self.assert_original_intact()

    def test_corrupt_staging_never_renames(self):
        self.ftp.corrupt = True
        with self.assertRaisesRegex(ValueError, "Staging readback"):
            self.install()
        self.assertTrue(self.evidence["recovery_required"])
        self.assertFalse(any(action[0] == "rename" for action in self.ftp.writes))
        self.assert_original_intact()

    def test_existing_scene_refuses_before_writes(self):
        self.ftp.directories.add(hardware.APP_DIR + "/scene-" + "a" * 12)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.install()
        self.assertEqual(self.ftp.writes, [])

    def test_listing_failure_never_means_absent(self):
        self.ftp.nlst = lambda directory: (_ for _ in ()).throw(ftplib.error_temp("450 unavailable"))
        with self.assertRaises(ftplib.error_temp):
            self.install()
        self.assertEqual(self.ftp.writes, [])

    def test_final_readback_failure_requires_recovery_and_preserves_originals(self):
        rename = self.ftp.rename
        def corrupt_final(source, destination):
            rename(source, destination)
            if destination.endswith(".smdh"):
                self.ftp.files[destination] += b"corrupt"
        self.ftp.rename = corrupt_final
        with self.assertRaisesRegex(ValueError, "Final readback"):
            self.install()
        self.assertTrue(self.evidence["recovery_required"])
        self.assertFalse(self.evidence["deployment_verified"])
        self.assert_original_intact()

    def test_invalid_local_magic_refuses_before_remote_writes(self):
        (self.source / hardware.FILES[0]).write_bytes(b"invalid")
        with self.assertRaisesRegex(ValueError, "Invalid"):
            self.install()
        self.assertEqual(self.ftp.writes, [])


if __name__ == "__main__":
    unittest.main()
