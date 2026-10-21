#!/usr/bin/env python3
"""Stage a verified local GC6E01 Rev 0 extracted disc for the SD archive probe."""
import argparse
import json
import shutil
import sys
from pathlib import Path

from verify_inputs import ROOT, file_hash, load_manifest


def prepare(disc, destination):
    disc = Path(disc).resolve(strict=True)
    destination = Path(destination).resolve()
    # The source is never rewritten, and an existing destination is never replaced.
    if destination.exists():
        raise ValueError("Choose a new destination; an existing path is never overwritten")
    if destination == disc or disc in destination.parents or destination in disc.parents:
        raise ValueError("Source and destination trees must not overlap")
    if ROOT == destination or ROOT in destination.parents:
        raise ValueError("Keep staged disc data outside the source repository")
    for relative in ("sys", "sys/boot.bin", "sys/main.dol"):
        if (disc / relative).is_symlink():
            raise ValueError("Symbolic links in system inputs are not accepted")
    with (disc / "sys/boot.bin").open("rb") as stream:
        boot = stream.read(8)
    if len(boot) < 8 or boot[:6] != b"GC6E01" or boot[7] != 0:
        raise ValueError("Expected an extracted GC6E01 revision 0 disc")
    manifest = load_manifest(ROOT / "vendor/colosseum/config/GC6E01/build.sha1")
    if file_hash(disc / "sys/main.dol") != manifest["sys/main.dol"]:
        raise ValueError("main.dol differs from the pinned GC6E01 original")
    files = disc / "files"
    if files.is_symlink() or not files.is_dir():
        raise ValueError("Expected an ordinary extracted files directory")
    if not (files / "common.fsys").is_file():
        raise ValueError("The full disc files/common.fsys is required")
    paths = sorted(files.rglob("*"))
    if (files / "colosseum3ds-data.json").exists():
        raise ValueError("The extracted disc uses the reserved staging manifest name")
    for path in paths:
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f"Unsupported disc entry: {path.relative_to(files)}")
    total = sum(path.stat().st_size for path in paths if path.is_file())
    parent = destination.parent
    parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(parent).free < total + 16 * 1024 * 1024:
        raise ValueError(f"Insufficient destination space for {total} bytes")
    # copytree creates the destination exclusively. A failed copy is retained for
    # inspection; the user may remove it explicitly before trying again.
    shutil.copytree(files, destination, symlinks=False)
    with (destination / "colosseum3ds-data.json").open("x") as output:
        json.dump({"schema": 1, "region": "GC6E01", "revision": 0,
                   "source_main_dol_sha1": manifest["sys/main.dol"],
                   "files": sum(path.is_file() for path in paths), "bytes": total,
                   "purpose": "Native archive probe; gameplay is not implemented"},
                  output, indent=2)
        output.write("\n")
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path,
                        help="Extracted disc root containing sys/ and files/")
    parser.add_argument("--output", required=True, type=Path,
                        help="New SD data directory outside this source checkout")
    args = parser.parse_args()
    try:
        total = prepare(args.disc, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Data preparation failed: {exc}\n")
    print(f"Staged {total} bytes at {args.output}. No game boot is available yet.")


if __name__ == "__main__":
    main()
