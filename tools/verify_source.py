#!/usr/bin/env python3
"""Check the imported translation units against their reviewed source snapshot."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify(materialized=False):
    lock = json.loads((ROOT / "source-lock.json").read_text())
    source = ROOT / lock["decomp"]["path"]
    manifest = json.loads((ROOT / "config/upstream-files.json").read_text())
    if manifest["commit"] != lock["decomp"]["commit"]:
        raise ValueError("Source file manifest and source lock refer to different commits")
    if not materialized:
        # Require a real submodule, not the parent's repository discovered by git.
        if not (source / ".git").exists():
            raise ValueError("Run git submodule update --init --recursive first")
        head = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if head != lock["decomp"]["commit"]:
            raise ValueError(f"Upstream HEAD differs from source-lock.json: {head}")
    for entry in manifest["files"]:
        path = source / entry["path"]
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise ValueError(f"Changed upstream source: {entry['path']}")
    print(f"Verified {len(manifest['files'])} pinned upstream source/header files")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--materialized", action="store_true",
                        help="Check exact connector-fetched file hashes without git metadata")
    args = parser.parse_args()
    try:
        verify(args.materialized)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Source verification failed: {exc}", file=sys.stderr)
        sys.exit(1)
