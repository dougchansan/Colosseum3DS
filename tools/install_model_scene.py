#!/usr/bin/env python3
"""Install an isolated model scene over passive FTP, preserving the original app."""
import argparse
import ftplib
import io
import json
import os
from pathlib import Path
import re
import sys
import uuid

import hardware


def names(ftp, directory):
    """A successful directory listing establishes absence; errors never do."""
    # SD filenames are case insensitive; retain the server spelling for reads
    # and evidence instead of turning normalized comparison keys into paths.
    return {name.casefold(): name for item in ftp.nlst(directory)
            if (name := item.rstrip("/").rsplit("/", 1)[-1]) not in (".", "..")}


def install(ftp, source, modelpack, evidence_dir, evidence, app_exited, expected, checkpoint):
    if not app_exited:
        raise ValueError("--app-exited is required")
    metadata = evidence["build_metadata"]
    hardware.validate_build_metadata(metadata)
    if not isinstance(expected, dict) or set(expected) != set(hardware.FILES) or any(
            not re.fullmatch("[0-9a-fA-F]{64}", str(value)) for value in expected.values()):
        raise ValueError("Both expected_installed_sha256 baseline hashes are required")
    payloads = {}
    for name, magic in zip(hardware.FILES, (b"3DSX", b"SMDH")):
        with (Path(source) / name).open("rb") as stream:
            data = stream.read(hardware.MAX_APPLICATION_BYTES + 1)
        if len(data) > hardware.MAX_APPLICATION_BYTES or not data.startswith(magic):
            raise ValueError("Invalid or oversized application: " + name)
        payloads[name] = data
    with Path(modelpack).open("rb") as stream:
        model = stream.read(16 * 1024 * 1024 + 1)
    if len(model) > 16 * 1024 * 1024 or not model.startswith(b"C3M1"):
        raise ValueError("Model must be C3M1 and at most 16 MiB")
    root = hardware.APP_DIR
    scene = root + "/scene-" + metadata["git_commit"][:12].lower()
    data_dir, asset = root + "/data", root + "/data/eevee.c3m"
    hardware.inspect(ftp, Path(evidence_dir) / "original", evidence, discover_nested=True)
    for item in evidence["files"]:
        if item["sha256"] != expected[item["name"]].lower():
            raise ValueError("Installed baseline hash differs: " + item["name"])
    listing = names(ftp, root)
    if scene.rsplit("/", 1)[-1].casefold() in listing:
        raise ValueError("Dedicated scene directory already exists")
    data_exists = "data" in listing
    if data_exists:
        data_dir = root + "/" + listing["data"]
    data_listing = names(ftp, data_dir) if data_exists else {}
    asset_exists = "eevee.c3m" in data_listing
    asset = data_dir + "/" + data_listing.get("eevee.c3m", "eevee.c3m")
    if asset_exists and hardware.read_remote(ftp, asset) != model:
        raise ValueError("Existing model pack differs; refusing overwrite")
    token = uuid.uuid4().hex
    entries = []
    for name, content in [*payloads.items(), ("eevee.c3m", model)]:
        target = asset if name == "eevee.c3m" else scene + "/" + name
        entries.append({"name": name, "path": target, "bytes": len(content),
                        "sha256": hardware.digest(content), "staging": target + ".tmp-" + token,
                        "reused": name == "eevee.c3m" and asset_exists})
    local = Path(evidence_dir) / "payloads"
    local.mkdir()
    for name, content in [*payloads.items(), ("eevee.c3m", model)]:
        (local / name).write_bytes(content)
    evidence.update(scene_directory=scene, source_commit=metadata["git_commit"],
                    deployment=entries, local_backups={"original": str(Path(evidence_dir) / "original"),
                    "payloads": str(local)}, recovery_required=True, deployment_verified=False,
                    app_exited_attested=True, journal=[])

    def record(phase, **details):
        evidence["journal"].append({"phase": phase, **details})
        checkpoint()

    record("prepared")
    record("mkdir_attempt", path=scene)
    ftp.mkd(scene)
    if not data_exists:
        record("mkdir_attempt", path=data_dir)
        ftp.mkd(data_dir)
    for entry in entries:
        if entry["reused"]:
            continue
        content = model if entry["name"] == "eevee.c3m" else payloads[entry["name"]]
        directory = data_dir if entry["name"] == "eevee.c3m" else scene
        if entry["staging"].rsplit("/", 1)[-1].casefold() in names(ftp, directory):
            raise ValueError("Unique staging name unexpectedly exists")
        record("stage_attempt", path=entry["staging"])
        ftp.storbinary("STOR " + entry["staging"], io.BytesIO(content))
        if hardware.read_remote(ftp, entry["staging"]) != content:
            raise ValueError("Staging readback differs: " + entry["name"])
        record("stage_verified", path=entry["staging"])
    # Every required upload has been downloaded and compared before any rename.
    for entry in entries:
        if entry["reused"]:
            continue
        directory = data_dir if entry["name"] == "eevee.c3m" else scene
        if entry["name"].casefold() in names(ftp, directory):
            raise ValueError("Destination appeared; refusing overwrite: " + entry["name"])
        record("rename_attempt", source=entry["staging"], destination=entry["path"])
        ftp.rename(entry["staging"], entry["path"])
        record("rename_complete", path=entry["path"])
    for entry in entries:
        content = hardware.read_remote(ftp, entry["path"])
        if hardware.digest(content) != entry["sha256"]:
            raise ValueError("Final readback differs: " + entry["name"])
        entry["installed_verified"] = True
    evidence.update(recovery_required=False, deployment_verified=True)
    record("verified")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--sourcebuilddir", type=Path, required=True)
    parser.add_argument("--modelpack", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--app-exited", action="store_true")
    args = parser.parse_args(argv)
    if not args.app_exited:
        parser.error("--app-exited is required")
    args.evidence.mkdir(parents=True, exist_ok=False)
    evidence, ftp = {"status": "failed", "recovery_required": False}, None

    def save():
        temporary = args.evidence / "manifest.json.tmp"
        with temporary.open("w") as stream:
            stream.write(json.dumps(evidence, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(args.evidence / "manifest.json")
    try:
        config = json.loads(args.config.read_text())
        if config.get("app_directory", hardware.APP_DIR) != hardware.APP_DIR:
            raise ValueError("Only the fixed application directory is supported")
        evidence["build_metadata"] = {key: config.get(key) for key in
                                      ("git_commit", "build_configuration", "toolchain")}
        hardware.validate_build_metadata(evidence["build_metadata"])
        timeout = float(config.get("timeout_seconds", 15))
        if not 0 < timeout <= 120:
            raise ValueError("timeout_seconds must be in (0, 120]")
        ftp = ftplib.FTP()
        ftp.connect(config["host"], int(config.get("port", 5000)), timeout=timeout)
        ftp.login(config.get("username", "anonymous"), os.environ.get(config.get("password_env", "COLOSSEUM_FTP_PASSWORD"), ""))
        ftp.set_pasv(True)
        evidence["passive_ftp"] = True
        install(ftp, args.sourcebuilddir, args.modelpack, args.evidence, evidence,
                args.app_exited, config.get("expected_installed_sha256"), save)
        evidence["status"] = "passed"
    except (OSError, EOFError, ValueError, KeyError, ftplib.Error) as exc:
        evidence["error"] = str(exc) or type(exc).__name__
    finally:
        if ftp is not None:
            try:
                ftp.close()
            except OSError:
                pass
        save()
    print(json.dumps(evidence, indent=2))
    return 0 if evidence["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
