#!/usr/bin/env python3
"""Independently verify the ARM probe's synthetic GCI, without the C codec.

Uses byte-wise XOR and Python hashlib, verifies every slot, and requires the
known synthetic body. This is not a validator for arbitrary playable saves.
No output save or private game asset is created.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

GCI_SIZE = 0x60040
SLOT_SIZE = 0x1E000
HASHED_SIZE = 0x1DFD8
BODY_SIZE = 0x1DFD0


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(path):
    data = path.read_bytes()
    require(len(data) == GCI_SIZE, "Expected an exact 393280-byte GCI")
    require(data[:6] == b"GC6E01", "Not the GC6E/01 fixture envelope")
    require(data[8:26] == b"pokemon_colosseum\0", "Unexpected card filename")
    require(struct.unpack_from(">H", data, 0x38)[0] == 48,
            "Unexpected card block count")
    body = bytearray((i * 37 + (i >> 8) * 11 + 13) & 255
                     for i in range(BODY_SIZE))
    body[4:8] = b"\0" * 4
    body[0x18:0x1C] = b"\0" * 4
    counts = []
    for index in range(3):
        start = 0x6040 + index * SLOT_SIZE
        slot = bytearray(data[start:start + SLOT_SIZE])
        require(slot[:2] == b"\1\1", f"Slot {index}: bad version bytes")
        require(sum(struct.unpack(">8I", slot[:32])) & 0xFFFFFFFF == 0,
                f"Slot {index}: bad encrypted-header checksum")
        count = struct.unpack_from(">I", slot, 4)[0]
        require(0 < count <= 0x7FFFFFFF, f"Slot {index}: invalid save count")
        expected_digest = bytes(slot[0x1DFEC:])
        slot[12:16] = b"\0" * 4
        key = bytes(value ^ 255 for value in expected_digest)
        for offset in range(0x18, HASHED_SIZE, 20):
            encrypted = bytes(slot[offset:offset + 20])
            slot[offset:offset + 20] = bytes(value ^ pad for value, pad
                                            in zip(encrypted, key))
            key = hashlib.sha1(encrypted).digest()
        require(hashlib.sha1(slot[:HASHED_SIZE]).digest() == expected_digest,
                f"Slot {index}: payload SHA-1 mismatch")
        require(slot[8:HASHED_SIZE] == body,
                f"Slot {index}: not the probe's known synthetic body")
        counts.append(count)
    return {
        "path": str(path),
        "synthetic_format_fixture": True,
        "playable_game_progress": False,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "slot_counts": counts,
        "latest_count": max(counts),
        "all_slot_hashes_and_bodies_verified": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gci", type=Path, nargs="+",
                        help="Probe .gci and optionally its .bak")
    parser.add_argument("--expect-latest", type=int,
                        help="Require this latest count in the first file")
    args = parser.parse_args()
    try:
        reports = [verify(path) for path in args.gci]
        if args.expect_latest is not None:
            require(reports[0]["latest_count"] == args.expect_latest,
                    "First file's latest count does not match --expect-latest")
        print(json.dumps(reports, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Save probe verification failed: {error}\n")


if __name__ == "__main__":
    main()
