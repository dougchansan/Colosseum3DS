"""Synthetic FTP tests: no network or console required."""
import ftplib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("hardware", Path(__file__).resolve().parents[1] / "tools/hardware.py")
hardware = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(hardware)


class FakeFTP:
    def __init__(self):
        self.files = {hardware.APP_DIR + "/" + name: b"old-" + name.encode() for name in hardware.FILES}
        self.writes = []
        self.corrupt = False
        self.fail_install = False
        self.fail_backup = False

    def retrbinary(self, command, callback):
        path = command[5:]
        if self.fail_backup and path.endswith(hardware.FILES[1]):
            raise ftplib.error_perm("550 unavailable")
        if path not in self.files:
            raise ftplib.error_perm("550 missing")
        callback(self.files[path])

    def storbinary(self, command, stream):
        path = command[5:]
        self.writes.append(("store", path))
        self.files[path] = stream.read() + (b"corrupt" if self.corrupt else b"")

    def rename(self, old, new):
        self.writes.append(("rename", old, new))
        if self.fail_install and ".upload-" in old and new.endswith(hardware.FILES[1]):
            raise ftplib.error_perm("450 rename failure")
        self.files[new] = self.files.pop(old)

    def delete(self, path):
        self.writes.append(("delete", path))
        if path not in self.files:
            raise ftplib.error_perm("550 missing")
        del self.files[path]


class HardwareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        for name in hardware.FILES:
            (self.source / name).write_bytes(b"new-" + name.encode())
        self.ftp = FakeFTP()
        self.original = dict(self.ftp.files)
        self.evidence = {"build_metadata": {"git_commit": "a" * 40,
                                            "build_configuration": "Makefile.3ds",
                                            "toolchain": "devkitARM GCC test fixture"}}

    def deploy(self, **kwargs):
        hardware.deploy(self.ftp, self.source, self.root / "backup", self.evidence,
                        expected_sha256={name: hardware.digest(self.original[hardware.APP_DIR + "/" + name])
                                         for name in hardware.FILES}, **kwargs)

    def test_inspect_hashes_without_writes(self):
        hardware.inspect(self.ftp, self.root / "installed", self.evidence)
        self.assertEqual(self.ftp.writes, [])
        for item in self.evidence["files"]:
            self.assertEqual(item["sha256"], hardware.digest((self.root / "installed" / item["name"]).read_bytes()))

    def test_oversized_remote_backup_aborts_before_console_writes(self):
        for name in hardware.FILES:
            (self.source / name).write_bytes(b"new")
        with patch.object(hardware, "MAX_APPLICATION_BYTES", 8):
            with self.assertRaisesRegex(ValueError, "transfer exceeds"):
                hardware.deploy(self.ftp, self.source, self.root / "backup", self.evidence,
                                app_exited=True, expected_sha256={name: "a" * 64 for name in hardware.FILES})
        self.assertEqual(self.ftp.writes, [])

    def test_oversized_transfer_stops_at_first_excess_block(self):
        callbacks = []
        def transfer(command, callback):
            for block in (b"1234", b"5678", b"9", b"unread"):
                callbacks.append(block)
                callback(block)
        with patch.object(hardware, "MAX_APPLICATION_BYTES", 8), patch.object(self.ftp, "retrbinary", side_effect=transfer):
            with self.assertRaisesRegex(ValueError, "transfer exceeds"):
                hardware.read_remote(self.ftp, hardware.APP_DIR + "/" + hardware.FILES[0])
        self.assertEqual(callbacks, [b"1234", b"5678", b"9"])

    def test_unknown_build_metadata_blocks_writes(self):
        self.evidence["build_metadata"]["toolchain"] = "unknown"
        with self.assertRaisesRegex(ValueError, "toolchain"):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.writes, [])

    def test_tcp_probe_records_source_and_no_protocol_claim(self):
        with patch.object(hardware.socket, "create_connection") as connect:
            connect.return_value.__enter__.return_value.getsockname.return_value = ("192.0.2.20", 1234)
            result = hardware.tcp_probe("192.0.2.10", 8000, 15)
        self.assertTrue(result["reachable"])
        self.assertEqual(result["local_source_address"], "192.0.2.20")
        self.assertFalse(result["protocol_verified"])
        connect.assert_called_once_with(("192.0.2.10", 8000), timeout=5)

    def test_unknown_installed_hash_never_writes(self):
        self.ftp.files[hardware.APP_DIR + "/" + hardware.FILES[0]] = b"unknown"
        with self.assertRaises(ValueError):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.writes, [])

    def test_nested_inspection_is_read_only_and_reported(self):
        self.ftp.files = {path.replace(hardware.APP_DIR, hardware.APP_DIR + "/3ds/Colosseum3DS", 1): data
                          for path, data in self.original.items()}
        hardware.inspect(self.ftp, self.root / "installed", self.evidence)
        self.assertTrue(self.evidence["nested_installation"])
        self.assertEqual(self.ftp.writes, [])

    def test_nested_installation_is_not_a_deployment_target(self):
        self.ftp.files = {path.replace(hardware.APP_DIR, hardware.APP_DIR + "/3ds/Colosseum3DS", 1): data
                          for path, data in self.original.items()}
        with self.assertRaises(ftplib.error_perm):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.writes, [])

    def test_rollback_failure_preserves_recovery_evidence(self):
        self.ftp.fail_install = True
        rename = self.ftp.rename
        def fail_restore(old, new):
            if ".restore-" in old:
                raise OSError("connection lost during rollback")
            return rename(old, new)
        self.ftp.rename = fail_restore
        with self.assertRaises(ftplib.error_perm):
            self.deploy(app_exited=True)
        self.assertTrue(self.evidence["rollback_errors"])
        self.assertTrue(self.evidence["recovery_required"])
        self.assertFalse(self.evidence["rollback_verified"])
        self.assertEqual(len(self.evidence["remote_backups"]), 2)
        self.assertTrue((self.root / "backup" / hardware.FILES[0]).exists())

    def test_requires_explicit_exited_attestation(self):
        with self.assertRaises(ValueError):
            self.deploy()
        self.assertEqual(self.ftp.writes, [])
        self.assertFalse((self.root / "backup").exists())

    def test_backup_failure_never_writes_console(self):
        self.ftp.fail_backup = True
        with self.assertRaises(ftplib.error_perm):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.writes, [])
        self.assertEqual(self.ftp.files, self.original)

    def test_mismatch_prevents_install_and_cleans_staging(self):
        self.ftp.corrupt = True
        with self.assertRaises(ValueError):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.files, self.original)
        self.assertFalse(any(item[0] == "rename" for item in self.ftp.writes))

    def test_second_install_failure_restores_both_originals(self):
        self.ftp.fail_install = True
        with self.assertRaises(ftplib.error_perm):
            self.deploy(app_exited=True)
        for path, original in self.original.items():
            self.assertEqual(self.ftp.files[path], original)
        self.assertTrue(self.evidence["rollback_verified"])
        self.assertFalse(self.evidence["recovery_required"])
        self.assertEqual(self.evidence["rollback_errors"], [])

    def test_backup_rename_applied_then_error_is_reconciled(self):
        rename = self.ftp.rename
        fired = False
        def ambiguous(old, new):
            nonlocal fired
            rename(old, new)
            if ".backup-" in new and not fired:
                fired = True
                raise OSError("response lost after applied backup rename")
        self.ftp.rename = ambiguous
        with self.assertRaises(OSError):
            self.deploy(app_exited=True)
        for path, original in self.original.items():
            self.assertEqual(self.ftp.files[path], original)
        self.assertTrue(self.evidence["rollback_verified"])
        self.assertFalse(self.evidence["recovery_required"])
        self.assertTrue(any(".backup-" in path for path in self.ftp.files))

    def test_install_rename_applied_then_error_is_reconciled(self):
        rename = self.ftp.rename
        def ambiguous(old, new):
            rename(old, new)
            if ".upload-" in old:
                raise OSError("response lost after applied install rename")
        self.ftp.rename = ambiguous
        with self.assertRaises(OSError):
            self.deploy(app_exited=True)
        for path, original in self.original.items():
            self.assertEqual(self.ftp.files[path], original)
        self.assertTrue(self.evidence["rollback_verified"])
        self.assertFalse(self.evidence["recovery_required"])
        self.assertTrue(all(not item["installed_verified"] for item in self.evidence["deployment"]))

    def test_connection_loss_after_applied_rename_requires_recovery(self):
        rename = self.ftp.rename
        def disconnect(old, new):
            rename(old, new)
            self.ftp.retrbinary = lambda *args: (_ for _ in ()).throw(OSError("connection lost"))
            raise OSError("connection lost")
        self.ftp.rename = disconnect
        with self.assertRaises(OSError):
            self.deploy(app_exited=True)
        self.assertTrue(self.evidence["recovery_required"])
        self.assertFalse(self.evidence["rollback_verified"])
        self.assertFalse(self.evidence["deployment_verified"])
        self.assertTrue(self.evidence["rollback_errors"])
        self.assertTrue(any(".backup-" in path for path in self.ftp.files))

    def test_unknown_active_bytes_are_preserved(self):
        rename = self.ftp.rename
        target = hardware.APP_DIR + "/" + hardware.FILES[0]
        def unknown(old, new):
            if ".upload-" in old:
                self.ftp.files[new] = b"unknown concurrent bytes"
                raise OSError("uncertain install")
            rename(old, new)
        self.ftp.rename = unknown
        with self.assertRaises(OSError):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.files[target], b"unknown concurrent bytes")
        self.assertTrue(self.evidence["recovery_required"])
        self.assertFalse(self.evidence["rollback_verified"])
        self.assertFalse(any(item[0] == "delete" and item[1] == target for item in self.ftp.writes))

    def test_rollback_readback_mismatch_is_not_reported_verified(self):
        self.ftp.fail_install = True
        rename = self.ftp.rename
        def corrupt_restore(old, new):
            rename(old, new)
            if ".restore-" in old:
                self.ftp.files[new] = b"restoration corrupted"
        self.ftp.rename = corrupt_restore
        with self.assertRaises(ftplib.error_perm):
            self.deploy(app_exited=True)
        self.assertFalse(self.evidence["rollback_verified"])
        self.assertTrue(self.evidence["recovery_required"])
        self.assertTrue(any("readback differs" in item["error"] for item in self.evidence["rollback_errors"]))

    def test_journal_checkpoint_precedes_console_mutation(self):
        snapshots = []
        def checkpoint():
            snapshots.append((json.loads(json.dumps(self.evidence)), len(self.ftp.writes)))
        hardware.deploy(self.ftp, self.source, self.root / "backup", self.evidence,
                        app_exited=True,
                        expected_sha256={name: hardware.digest(self.original[hardware.APP_DIR + "/" + name])
                                         for name in hardware.FILES}, checkpoint=checkpoint)
        self.assertEqual(snapshots[0][1], 0)
        self.assertEqual(snapshots[0][0]["journal"][-1]["phase"], "prepared")
        self.assertTrue(snapshots[0][0]["recovery_required"])
        phases = [snapshot[0]["journal"][-1]["phase"] for snapshot in snapshots]
        self.assertIn("backup_rename_attempt", phases)
        self.assertIn("backup_rename_complete", phases)
        self.assertIn("install_rename_attempt", phases)
        self.assertIn("install_rename_complete", phases)

    def test_success_keeps_local_and_remote_backups(self):
        self.deploy(app_exited=True)
        self.assertTrue(self.evidence["deployment_verified"])
        for name in hardware.FILES:
            path = hardware.APP_DIR + "/" + name
            self.assertEqual(self.ftp.files[path], (self.source / name).read_bytes())
            self.assertEqual((self.root / "backup" / name).read_bytes(), self.original[path])
        self.assertEqual(len(self.evidence["remote_backups"]), 2)
        self.assertFalse(any(".upload-" in path for path in self.ftp.files))
        self.assertTrue(all(path.startswith(hardware.APP_DIR + "/") for path in self.ftp.files))

    def test_existing_backup_is_never_overwritten(self):
        (self.root / "backup").mkdir()
        with self.assertRaises(FileExistsError):
            self.deploy(app_exited=True)
        self.assertEqual(self.ftp.writes, [])

    def test_diagnose_probes_independently_and_ftp_failure_stays_failed(self):
        config = self.root / "config.json"
        config.write_text(json.dumps({"host": "192.0.2.10"}))
        evidence = self.root / "evidence"
        with patch.object(hardware.socket, "getaddrinfo", return_value=[(2, 1, 6, "", ("192.0.2.10", 5000))]), \
             patch.object(hardware.socket, "create_connection", side_effect=[OSError("FTP TCP unavailable"), OSError("NTR unavailable")]) as probe, \
             patch.object(hardware.ftplib, "FTP") as ftp:
            ftp.return_value.connect.side_effect = OSError("FTP unavailable")
            result = hardware.main(["diagnose", "--config", str(config), "--evidence", str(evidence)])
        manifest = json.loads((evidence / "manifest.json").read_text())
        self.assertEqual(result, 1)
        self.assertEqual(manifest["status"], "failed")
        self.assertEqual(probe.call_count, 2)
        self.assertFalse(manifest["tcp_probes"]["ftp"]["reachable"])
        self.assertFalse(manifest["tcp_probes"]["ntr"]["reachable"])
        self.assertEqual(manifest["error"], "FTP unavailable")

    def test_connection_failure_writes_failure_manifest(self):
        config = self.root / "config.json"
        config.write_text(json.dumps({"host": "example.invalid"}))
        evidence = self.root / "evidence"
        with patch.object(hardware.socket, "getaddrinfo", side_effect=OSError("unreachable")):
            self.assertEqual(hardware.main(["diagnose", "--config", str(config), "--evidence", str(evidence)]), 1)
        self.assertEqual(json.loads((evidence / "manifest.json").read_text())["status"], "failed")

    def test_directory_override_is_rejected_before_connection(self):
        config = self.root / "config.json"
        config.write_text(json.dumps({"host": "example.invalid", "app_directory": "/"}))
        with patch.object(hardware.ftplib, "FTP") as ftp:
            result = hardware.main(["diagnose", "--config", str(config), "--evidence", str(self.root / "evidence")])
        self.assertEqual(result, 1)
        ftp.assert_not_called()


if __name__ == "__main__":
    unittest.main()
