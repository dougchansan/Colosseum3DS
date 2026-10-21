#!/usr/bin/env python3
"""Read-only verification of local GC6E01 Rev 0 private build originals."""
import argparse
import hashlib
import json
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def load_manifest(path):
    result = {}
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 2 or len(parts[0]) != 40:
            raise ValueError(f"Malformed manifest line {number}")
        digest, name = parts
        if any(ch not in "0123456789abcdef" for ch in digest):
            raise ValueError(f"Malformed digest on line {number}")
        original = PurePosixPath(name)
        if original.is_absolute() or ".." in original.parts:
            raise ValueError("Unsafe manifest path")
        leaf = original.name
        if leaf == "main.dol":
            relative = "sys/main.dol"
        elif leaf.endswith(".rel"):
            relative = "files/" + leaf
        else:
            raise ValueError(f"Unsupported original: {leaf}")
        if relative in result:
            raise ValueError(f"Duplicate original: {relative}")
        result[relative] = digest
    if not result:
        raise ValueError("Empty original manifest")
    return result


def file_hash(path, algorithm="sha1"):
    digest = hashlib.new(algorithm)
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify(orig, manifest):
    orig = Path(orig).resolve(strict=True)
    results = []
    for relative, expected in load_manifest(manifest).items():
        path = orig / relative
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(orig)
            if path.is_symlink() or any(p.is_symlink() for p in path.parents if p != orig):
                raise ValueError("Symbolic links are not accepted")
            actual = file_hash(path)
            results.append({"path": relative, "sha1": actual, "expected_sha1": expected,
                            "ok": actual == expected})
        except (OSError, ValueError) as exc:
            results.append({"path": relative, "ok": False, "error": str(exc)})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig", required=True, type=Path,
                        help="pkmn-colosseum-build/orig/GC6E01 (read only)")
    parser.add_argument("--manifest", type=Path,
                        default=ROOT / "vendor/colosseum/config/GC6E01/build.sha1")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        results = verify(args.orig, args.manifest)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"Input verification failed: {exc}\n")
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for item in results:
            print(f"{'PASS' if item['ok'] else 'FAIL'} {item['path']}")
        print(f"{sum(item['ok'] for item in results)}/{len(results)} originals verified")
    return 0 if all(item["ok"] for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
