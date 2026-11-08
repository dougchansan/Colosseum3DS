"""Synthetic isolated-scene FTP transactions; never contact a console."""
import ftplib
import importlib.util
from pathlib import Path
from unittest.mock import patch
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

    @staticmethod
    def resolve(paths, path):
        return next((existing for existing in paths if existing.casefold() == path.casefold()), None)

    def nlst(self, directory):
        directory = self.resolve(self.directories, directory)
        if directory is None:
            raise ftplib.error_temp("450 unavailable")
        prefix = directory + "/"
        return [path for path in self.directories | self.files.keys()
                if path.casefold().startswith(prefix.casefold()) and "/" not in path[len(prefix):]]

    def retrbinary(self, command, callback):
        path = self.resolve(self.files, command[5:])
        if path is None:
            raise ftplib.error_perm("550 absent")
        callback(self.files[path])

    def mkd(self, path):
        self.writes.append(("mkdir", path))
        if self.resolve(self.directories | self.files.keys(), path) is not None:
            raise ftplib.error_perm("550 exists")
        self.directories.add(path)

    def storbinary(self, command, stream):
        path = command[5:]
        self.writes.append(("store", path))
        existing = self.resolve(self.files, path)
        if existing is not None:
            del self.files[existing]
        self.files[path] = stream.read() + (b"corrupt" if self.corrupt else b"")

    def rename(self, source, destination):
        self.writes.append(("rename", source, destination))
        source = self.resolve(self.files, source)
        content = self.files.pop(source)
        existing = self.resolve(self.files, destination)
        if existing is not None:
            del self.files[existing]
        self.files[destination] = content


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

    def test_case_alias_different_model_refuses_before_any_write(self):
        for directory, filename in (("data", "EEVEE.C3M"), ("DaTa", "EeVeE.c3M")):
            with self.subTest(directory=directory, filename=filename):
                self.ftp = FakeFTP()
                path = hardware.APP_DIR + "/" + directory + "/" + filename
                self.ftp.directories.add(hardware.APP_DIR + "/" + directory)
                self.ftp.files[path] = b"previous model must survive"
                before = dict(self.ftp.files)
                with tempfile.TemporaryDirectory() as evidence_dir:
                    with self.assertRaisesRegex(ValueError, "differs"):
                        installer.install(self.ftp, self.source, self.model, evidence_dir, self.evidence,
                                          True, self.expected, lambda: None)
                self.assertEqual(self.ftp.files, before)
                self.assertEqual(self.ftp.writes, [])

    def test_case_alias_equal_model_reused_with_actual_path(self):
        self.ftp.directories.add(hardware.APP_DIR + "/DaTa")
        path = hardware.APP_DIR + "/DaTa/EEVEE.C3M"
        self.ftp.files[path] = self.model.read_bytes()
        self.install()
        asset = self.evidence["deployment"][-1]
        self.assertEqual(asset["path"], path)
        self.assertTrue(asset["reused"])
        self.assertTrue(asset["installed_verified"])
        self.assertFalse(any("eevee" in str(action).casefold() for action in self.ftp.writes))
        self.assertEqual(sum(action[0] == "rename" for action in self.ftp.writes), 2)

    def test_case_alias_scene_refuses_before_any_write(self):
        self.ftp.directories.add(hardware.APP_DIR + "/ScEnE-" + "A" * 12)
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.install()
        self.assertEqual(self.ftp.writes, [])

    def test_case_alias_staging_name_is_not_overwritten(self):
        token = "b" * 32
        mkd = self.ftp.mkd
        def create_staging_alias(path):
            mkd(path)
            if "/scene-" in path:
                self.ftp.files[path + "/" + hardware.FILES[0].upper() + ".TMP-" + token.upper()] = b"keep"
        self.ftp.mkd = create_staging_alias
        with patch.object(installer.uuid, "uuid4") as identifier:
            identifier.return_value.hex = token
            with self.assertRaisesRegex(ValueError, "staging name"):
                self.install()
        self.assertFalse(any(action[0] in ("store", "rename") for action in self.ftp.writes))

    def test_case_alias_destination_appearing_after_stage_is_preserved(self):
        for filename in ("EEVEE.C3M", hardware.FILES[0].upper()):
            with self.subTest(filename=filename):
                # Each transaction needs its own local backup directories.
                with tempfile.TemporaryDirectory() as evidence_dir:
                    self.ftp = FakeFTP()
                    store = self.ftp.storbinary
                    raced_path = []
                    def stage_and_race(command, stream):
                        store(command, stream)
                        if "eevee.c3m.tmp-" in command:
                            directory = (hardware.APP_DIR + "/data" if filename == "EEVEE.C3M"
                                         else hardware.APP_DIR + "/scene-" + "a" * 12)
                            raced_path.append(directory + "/" + filename)
                            self.ftp.files[raced_path[-1]] = b"arrived during upload"
                    self.ftp.storbinary = stage_and_race
                    with self.assertRaisesRegex(ValueError, "Destination appeared"):
                        installer.install(self.ftp, self.source, self.model, evidence_dir, self.evidence,
                                          True, self.expected, lambda: None)
                    self.assertEqual(self.ftp.files[raced_path[-1]], b"arrived during upload")
                    self.assertFalse(any(action[0] == "rename" and
                                         action[2].casefold() == raced_path[-1].casefold()
                                         for action in self.ftp.writes))

    def test_fake_rename_replaces_case_alias_like_sd_filesystem(self):
        self.ftp.files["/source"] = b"new"
        self.ftp.files["/DESTINATION"] = b"old"
        self.ftp.rename("/SOURCE", "/destination")
        self.assertEqual(self.ftp.files["/destination"], b"new")
        self.assertNotIn("/DESTINATION", self.ftp.files)

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

    def test_case_alias_data_listing_failure_refuses_before_writes(self):
        self.ftp.directories.add(hardware.APP_DIR + "/DaTa")
        nlst = self.ftp.nlst
        def fail_data(directory):
            if directory.casefold().endswith("/data"):
                raise ftplib.error_temp("450 unavailable")
            return nlst(directory)
        self.ftp.nlst = fail_data
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
