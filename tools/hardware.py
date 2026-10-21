#!/usr/bin/env python3
"""Passive FTP evidence collection and guarded Colosseum3DS deployment.

FTP supplies no encryption. Lifecycle, input and video observations are manual.
"""
import argparse
import datetime
import ftplib
import hashlib
import json
import io
import os
import re
from pathlib import Path
import socket
import sys
import uuid

APP_DIR = "/3ds/Colosseum3DS"
FILES = ("Colosseum3DS.3dsx", "Colosseum3DS.smdh")
MAX_APPLICATION_BYTES = 32 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_remote(ftp, path):
    chunks = []
    size = 0
    def collect(block):
        nonlocal size
        size += len(block)
        if size > MAX_APPLICATION_BYTES:
            raise ValueError("Application transfer exceeds 32 MiB: " + path)
        chunks.append(block)
    ftp.retrbinary("RETR " + path, collect)
    return b"".join(chunks)


def tcp_probe(host, port, timeout):
    """Connection only: an open port proves no video or input capability."""
    result = {"port": port, "reachable": False, "protocol_verified": False}
    try:
        with socket.create_connection((host, port), timeout=min(timeout, 5)) as connection:
            result.update(reachable=True, local_source_address=connection.getsockname()[0])
    except OSError as exc:
        result["error"] = str(exc)
    return result


def validate_build_metadata(metadata):
    if not isinstance(metadata, dict) or not re.fullmatch("[0-9a-fA-F]{40}", str(metadata.get("git_commit", ""))):
        raise ValueError("Deployment requires a full git_commit in build metadata")
    for key in ("build_configuration", "toolchain"):
        value = metadata.get(key)
        if not isinstance(value, str) or not value.strip() or value.strip().lower() in ("unknown", "unavailable", "unverified"):
            raise ValueError("Deployment requires known " + key + " build metadata")


def inspect(ftp, output, evidence, discover_nested=True):
    """Read known application files, including a known accidental nesting."""
    directories = [APP_DIR]
    if discover_nested:
        directories.append(APP_DIR + "/3ds/Colosseum3DS")
    payloads = None
    for directory in directories:
        try:
            payloads = {name: read_remote(ftp, directory + "/" + name) for name in FILES}
            break
        except ftplib.error_perm as exc:
            if not str(exc).startswith("550") or directory == directories[-1]:
                raise
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    evidence["installed_directory"] = directory
    evidence["nested_installation"] = directory != APP_DIR
    evidence["files"] = []
    for name, data in payloads.items():
        (output / name).write_bytes(data)
        evidence["files"].append({"name": name, "bytes": len(data),
                                  "sha256": digest(data)})


def deploy(ftp, source, backup, evidence, app_exited=False, expected_sha256=None, checkpoint=None):
    """Stage and read back all payloads before moving installed files aside."""
    if not app_exited:
        raise ValueError("Exit Colosseum3DS and start the FTP server; --app-exited is required")
    if not expected_sha256 or set(expected_sha256) != set(FILES):
        raise ValueError("Deployment requires expected_installed_sha256 for both direct installed files")
    validate_build_metadata(evidence.get("build_metadata"))
    payloads = {}
    for name in FILES:
        with (Path(source) / name).open("rb") as stream:
            payloads[name] = stream.read(MAX_APPLICATION_BYTES + 1)
        if len(payloads[name]) > MAX_APPLICATION_BYTES:
            raise ValueError("Local application exceeds 32 MiB: " + name)
    if any(not data for data in payloads.values()):
        raise ValueError("Application files must not be empty")
    evidence["app_exited_attested"] = True
    evidence["deployment"] = []
    # A complete local backup is mandatory before the first console write.
    inspect(ftp, backup, evidence, discover_nested=False)
    for item in evidence["files"]:
        if item["sha256"] != expected_sha256[item["name"]]:
            raise ValueError("Installed hash differs from expected: " + item["name"])
    originals = {name: (Path(backup) / name).read_bytes() for name in FILES}
    token = uuid.uuid4().hex
    evidence.update(deployment_verified=False, rollback_verified=False, recovery_required=False,
                    remote_backups=[APP_DIR + "/" + name + ".backup-" + token for name in FILES],
                    journal=[], cleanup_errors=[], rollback_errors=[])
    evidence["recovery_note"] = "Local originals and recorded remote backups must be retained until recovery is verified."
    entries = {}
    for name, data in payloads.items():
        entries[name] = {"name": name, "bytes": len(data), "sha256": digest(data),
                         "staged_verified": False, "installed_verified": False}
    evidence["deployment"] = list(entries.values())
    def record(phase, **details):
        evidence["journal"].append({"phase": phase, **details})
        if checkpoint:
            checkpoint()
    def optional_read(path):
        try:
            return read_remote(ftp, path)
        except ftplib.error_perm as exc:
            if str(exc).startswith("550"):
                return None
            raise
    staged = []
    attempted = []
    evidence["recovery_required"] = True
    record("prepared")
    try:
        for name, data in payloads.items():
            target = APP_DIR + "/" + name
            temporary = target + ".upload-" + token
            staged.append(temporary)
            record("upload_attempt", path=temporary)
            ftp.storbinary("STOR " + temporary, io.BytesIO(data))
            if read_remote(ftp, temporary) != data:
                raise ValueError("Uploaded readback differs: " + name)
            entries[name]["staged_verified"] = True
            record("upload_verified", path=temporary)
        for name in FILES:
            target = APP_DIR + "/" + name
            previous = target + ".backup-" + token
            attempted.append(name)
            record("backup_rename_attempt", source=target, destination=previous)
            ftp.rename(target, previous)
            record("backup_rename_complete", source=target, destination=previous)
            record("install_rename_attempt", source=target + ".upload-" + token, destination=target)
            ftp.rename(target + ".upload-" + token, target)
            staged.remove(target + ".upload-" + token)
            record("install_rename_complete", destination=target)
        for name, data in payloads.items():
            if read_remote(ftp, APP_DIR + "/" + name) != data:
                raise ValueError("Installed readback differs: " + name)
            entries[name]["installed_verified"] = True
        evidence["deployment_verified"] = True
        evidence["recovery_required"] = False
        record("deployment_verified")
    except Exception:
        evidence["deployment_verified"] = False
        evidence["recovery_required"] = True
        for entry in entries.values():
            entry["installed_verified"] = False
            entry["staged_verified"] = False
        # A failed rename response does not prove the server left files untouched.
        # Reconcile every attempted path, preserving originals and unknown bytes.
        for name in reversed(attempted):
            target = APP_DIR + "/" + name
            previous = target + ".backup-" + token
            restoration = target + ".restore-" + token
            try:
                current = optional_read(target)
                saved = optional_read(previous)
                record("rollback_observed", path=target,
                       current_sha256=None if current is None else digest(current),
                       backup_sha256=None if saved is None else digest(saved))
                if current == originals[name]:
                    continue
                if saved != originals[name]:
                    raise ValueError("Remote backup is missing or differs; retain local backup for manual recovery")
                if current is not None and current != payloads[name]:
                    raise ValueError("Active bytes are unknown; refusing to replace them")
                record("restore_upload_attempt", path=restoration)
                ftp.storbinary("STOR " + restoration, io.BytesIO(originals[name]))
                if read_remote(ftp, restoration) != originals[name]:
                    raise ValueError("Restoration upload readback differs")
                if current is not None:
                    # Preserve the verified new payload instead of deleting it.
                    quarantine = target + ".failed-" + token
                    record("quarantine_rename_attempt", source=target, destination=quarantine)
                    ftp.rename(target, quarantine)
                    record("quarantine_rename_complete", destination=quarantine)
                record("restore_rename_attempt", source=restoration, destination=target)
                ftp.rename(restoration, target)
                record("restore_rename_complete", destination=target)
            except Exception as exc:
                evidence["rollback_errors"].append({"path": target, "error": str(exc)})
        restored = True
        for name in FILES:
            try:
                if read_remote(ftp, APP_DIR + "/" + name) != originals[name]:
                    raise ValueError("Restored original readback differs")
            except Exception as exc:
                restored = False
                evidence["rollback_errors"].append({"path": APP_DIR + "/" + name, "error": str(exc)})
        evidence["rollback_verified"] = restored
        evidence["recovery_required"] = not restored
        record("rollback_verification", verified=restored)
        raise
    finally:
        for path in staged:
            try:
                # Applied-then-error rename may have consumed this staging file.
                if optional_read(path) is not None:
                    ftp.delete(path)
            except Exception as exc:
                evidence["cleanup_errors"].append({"path": path, "error": str(exc)})
        record("finished")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("diagnose", "inspect", "deploy"))
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--evidence", required=True, type=Path,
                        help="New local evidence directory (never overwritten)")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--app-exited", action="store_true",
                        help="Attest application has exited and FTP server is active")
    args = parser.parse_args(argv)
    if args.command == "deploy" and (not args.app_exited or not args.source):
        parser.error("deploy requires --source and --app-exited")
    args.evidence.mkdir(parents=True, exist_ok=False)
    evidence = {"command": args.command, "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "application_directory": APP_DIR, "status": "failed",
                "limits": "FTP bytes only; launch, exit, input, screens and gameplay require manual observation"}
    def save_manifest():
        temporary = args.evidence / "manifest.json.tmp"
        with temporary.open("w") as stream:
            stream.write(json.dumps(evidence, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(args.evidence / "manifest.json")
    ftp = None
    try:
        config = json.loads(args.config.read_text())
        if config.get("app_directory", APP_DIR) != APP_DIR:
            raise ValueError("Only " + APP_DIR + " is supported")
        evidence["build_metadata"] = {key: config.get(key) for key in
                                      ("git_commit", "build_configuration", "toolchain")}
        if args.command == "deploy":
            validate_build_metadata(evidence["build_metadata"])
        host = config["host"]
        port = int(config.get("port", 5000))
        timeout = float(config.get("timeout_seconds", 15))
        if not 0 < timeout <= 120:
            raise ValueError("timeout_seconds must be in (0, 120]")
        evidence["endpoint"] = {"host": host, "port": port}
        evidence["resolved_addresses"] = sorted({item[4][0] for item in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)})
        if args.command == "diagnose":
            evidence["tcp_probes"] = {"ftp": tcp_probe(host, port, timeout),
                                      "ntr": tcp_probe(host, 8000, timeout)}
        ftp = ftplib.FTP()
        ftp.connect(host, port, timeout=timeout)
        evidence["ftp_local_source_address"] = ftp.sock.getsockname()[0]
        ftp.login(config.get("username", "anonymous"), os.environ.get(config.get("password_env", "COLOSSEUM_FTP_PASSWORD"), ""))
        ftp.set_pasv(True)
        evidence["passive_ftp"] = True
        evidence["server_system"] = ftp.sendcmd("SYST")
        ftp.cwd(APP_DIR)
        evidence["application_directory_accessible"] = True
        if args.command == "inspect":
            inspect(ftp, args.evidence / "installed", evidence)
        elif args.command == "deploy":
            deploy(ftp, args.source, args.evidence / "backup", evidence, args.app_exited,
                   config.get("expected_installed_sha256"), checkpoint=save_manifest)
        evidence["status"] = "passed"
        if evidence.get("cleanup_errors"):
            evidence["status"] = "failed"
            evidence["error"] = "Temporary-file cleanup requires manual review"
    except (OSError, ValueError, KeyError, ftplib.Error) as exc:
        evidence["error"] = str(exc)
    finally:
        if ftp is not None:
            try:
                ftp.close()
            except OSError:
                pass
        save_manifest()
    print(json.dumps(evidence, indent=2))
    return 0 if evidence["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
