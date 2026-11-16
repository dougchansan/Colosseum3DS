#!/usr/bin/env python3
"""Collect every original message group on the disc for the host message sweep.

The sweep pack (C3MS) holds each message group (FSYS member type 0x0A) of
every *.fsys archive, plus the common_rel.rel groups the field packs carry.
`tests/test_message_sweep.c` renders every message of every group through
`c3_story_message_with` and reports any control the decoder cannot interpret.
The pack is private (original bytes); only the report counts are published.

C3MS layout (little-endian header, original big-endian group bytes):
  "C3MS", u32 version 1, u32 total bytes, u32 group count
  per group: u32 name length, name bytes (UTF-8), u32 group bytes, group bytes

--tally prints a control histogram straight from Python (same table as the
decoder) without building the pack.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from prepare_outskirt import member, u32
from prepare_story import admit_common_rel

MESSAGE_TYPE = 0x0A
# Parameter bytes each control consumes after its control byte
# (msgctrl.c callbacks; the rest consume none).
PARAMETERS = {0x07: 1, 0x08: 4, 0x09: 1, 0x38: 1, 0x52: 1, 0x53: 1, 0x54: 1, 0x5B: 1, 0x5C: 1}
CONTROL_COUNT = 0x5F  # msgctrlcode[190] / 2


def be16(data, at):
    return struct.unpack_from(">H", data, at)[0]


def group_valid(data):
    if len(data) < 16:
        return False
    count = be16(data, 4)
    if not count or 16 + count * 8 > len(data):
        return False
    for i in range(count):
        if u32(data, 20 + i * 8) >= len(data):
            return False
    return True


def fsys_members(archive):
    """(identity) of every FSYS member, in directory order."""
    if len(archive) < 64 or archive[:4] != b"FSYS":
        return []
    count = u32(archive, 12)
    table = u32(archive, u32(archive, 24))
    return [u32(archive, u32(archive, table + i * 4)) for i in range(count)]


def common_groups(rel):
    base = u32(rel, u32(rel, 16) + 4 * 8) & ~3
    return [("common_rel.rel:names", rel[base + 0x58044:base + 0x647B4]),
            ("common_rel.rel:messages", rel[base + 0x647B4:base + 0x647B4 + 0x124DD])]


def collect(disc):
    files = disc / "files"
    groups = []
    for path in sorted(files.glob("*.fsys")):
        archive = path.read_bytes()
        for identity in fsys_members(archive):
            if (identity >> 8) & 0xFF != MESSAGE_TYPE:
                continue
            data = member(archive, identity)
            if group_valid(data):
                groups.append(("%s:%08x" % (path.name, identity), data))
    groups += common_groups(admit_common_rel(files))
    return groups


def messages(data):
    count = be16(data, 4)
    offsets = sorted({u32(data, 20 + i * 8) for i in range(count)} | {len(data)})
    for i in range(count):
        ident, start = u32(data, 16 + i * 8), u32(data, 20 + i * 8)
        end = next(o for o in offsets if o > start)
        yield (be16(data, 0) << 20) | ident, data[start:end]


def controls(text):
    at = 0
    while at + 2 <= len(text):
        glyph = be16(text, at)
        at += 2
        if glyph == 0:
            return
        if glyph == 0xFFFF and at < len(text):
            code = text[at]
            yield code
            at += 1 + PARAMETERS.get(code, 0)


def tally(groups):
    histogram, first = Counter(), {}
    for name, data in groups:
        for ident, text in messages(data):
            for code in controls(text):
                histogram[code] += 1
                first.setdefault(code, "%s %08x" % (name, ident))
    return {"%02x" % code: {"count": n, "first": first[code]} for code, n in sorted(histogram.items())}


def encode(groups):
    body = bytearray()
    for name, data in groups:
        raw = name.encode("utf-8")
        body += struct.pack("<I", len(raw)) + raw + struct.pack("<I", len(data)) + data
    return b"C3MS" + struct.pack("<3I", 1, 16 + len(body), len(groups)) + bytes(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--tally", action="store_true")
    args = parser.parse_args()
    groups = collect(args.disc)
    report = {"groups": len(groups), "messages": sum(be16(d, 4) for _n, d in groups)}
    if args.tally:
        report["controls"] = tally(groups)
    if args.output:
        pack = encode(groups)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(pack)
        report.update(output=str(args.output), bytes=len(pack), sha256=hashlib.sha256(pack).hexdigest())
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
