#!/usr/bin/env python3
"""Extract reviewed GC6E01 menu text for the native bottom-screen interface.

Only the specific top-menu and player-name messages are emitted. Original
window sprites, font rendering and the complete message-control engine are
not part of this pack. Inputs and generated text remain private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_boot_media import verify_disc
from prepare_model import check_output

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
MAX_PACK_BYTES = 16384
PROFILES = {
    "topmenu.fsys": {
        "bytes": 926208,
        "sha256": "4b27263e182b3b2f0f9ce7ba0ea042cc365d718b966d641272265f3faa6e2da9",
        "member": 0x0C930A00,
        "ids": (0x3C14, 0x3C15, 0x3C17, 0x3C19, 0x3C1B, 0x3C1D,
                0x3C22, 0x3C35, 0x3C3A, 0x3D82, 0x43FB, 0x43FC,
                0x43FD, 0x444B, 0x44C9),
    },
    "name_entry_menu.fsys": {
        "bytes": 191520,
        "sha256": "17beb141f390e7f0d49fe658107e849e1991b709580e912372002e4ce9cdb36b",
        "member": 0x02FD0A00,
        "ids": (0x2EED, 0x2EF0, 0x2EF1, 0x2EF3, 0x2EF4, 0x2EF6,
                0x2EFD, 0x2EFE, 0x2EFF, 0x2F00),
    },
}


def be32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("Truncated big-endian word")
    return struct.unpack_from(">I", data, offset)[0]


def unpack_lzss(data, expected, maximum=65536):
    """The original zero-window 4KiB LZSS, with exact input/output bounds."""
    if not 0 < expected <= maximum or len(data) < 16 or data[:4] != b"LZSS":
        raise ValueError("Invalid bounded message compression header")
    if be32(data, 4) != expected or be32(data, 8) != len(data):
        raise ValueError("Message compression sizes disagree")
    window = bytearray(4096)
    cursor, position, flags = 0xFEE, 16, 0
    output = bytearray()
    while len(output) < expected:
        flags >>= 1
        if not flags & 0x100:
            if position >= len(data):
                raise ValueError("Truncated LZSS flags")
            flags = data[position] | 0xFF00
            position += 1
        if flags & 1:
            if position >= len(data):
                raise ValueError("Truncated LZSS literal")
            values = bytes((data[position],))
            position += 1
            window[cursor] = values[0]
            cursor = (cursor + 1) & 0xFFF
            output.extend(values)
        else:
            if position + 2 > len(data):
                raise ValueError("Truncated LZSS reference")
            low, high = data[position:position + 2]
            position += 2
            start, length = low | (high & 0xF0) << 4, (high & 15) + 3
            if length > expected - len(output):
                raise ValueError("LZSS reference exceeds declared message size")
            for index in range(length):
                value = window[(start + index) & 0xFFF]
                output.append(value)
                window[cursor] = value
                cursor = (cursor + 1) & 0xFFF
    if position != len(data):
        raise ValueError("Trailing bytes in compressed message stream")
    return bytes(output)


def read_member(data, member_id, maximum=65536):
    if len(data) < 0x44 or data[:4] != b"FSYS":
        raise ValueError("Expected FSYS archive")
    count, tables = be32(data, 0x0C), be32(data, 0x18)
    entries = be32(data, tables)
    if not 0 < count <= 64 or entries + count * 4 > len(data):
        raise ValueError("Invalid FSYS member table")
    found = None
    for index in range(count):
        entry = be32(data, entries + 4 * index)
        if entry + 0x28 > len(data):
            raise ValueError("Truncated FSYS member")
        if be32(data, entry) != member_id:
            continue
        if found is not None:
            raise ValueError("Duplicate expected FSYS member")
        offset, unpacked, flags = (be32(data, entry + value) for value in (4, 8, 12))
        size = be32(data, entry + 0x14)
        if not 0 < unpacked <= maximum or size > maximum or offset + size > len(data):
            raise ValueError("Message member exceeds bounds")
        payload = data[offset:offset + size]
        found = unpack_lzss(payload, unpacked, maximum) if flags & 0x80000000 else payload
        if len(found) != unpacked:
            raise ValueError("Message member size mismatch")
    if found is None:
        raise ValueError("Expected message member missing")
    return found


def message_bytes(group, wanted):
    """GSmsg MessageGroup: 0x10-byte header and sorted {key,offset} pairs."""
    if len(group) < 16:
        raise ValueError("Truncated message group")
    group_id, count = struct.unpack_from(">H", group)[0], struct.unpack_from(">H", group, 4)[0]
    if group_id or not 0 < count <= 512 or 16 + count * 8 > len(group):
        raise ValueError("Unexpected menu message group")
    entries = [struct.unpack_from(">II", group, 16 + index * 8) for index in range(count)]
    if any(entries[i][0] >= entries[i + 1][0] for i in range(count - 1)):
        raise ValueError("Message IDs are not strictly ordered")
    offsets = sorted({offset for _, offset in entries})
    if offsets[0] < 16 + count * 8 or offsets[-1] >= len(group):
        raise ValueError("Invalid message string offset")
    ends = dict(zip(offsets, offsets[1:] + [len(group)]))
    result = {key: group[offset:ends[offset]] for key, offset in entries if key in wanted}
    if set(result) != set(wanted):
        raise ValueError("Required menu message is absent")
    return result


def console_text(raw):
    """Decode the reviewed text/control subset, fail on any unknown control.

    GSchar glyphs are BE16, but FFFF is followed by a one-byte control, so
    whole-message UTF-16 decoding is incorrect after an odd-length command.
    Font/style and wait controls are removed. Newlines and the name placeholder
    survive; e-acute is transliterated for libctru's ASCII console font.
    """
    result, position = [], 0
    while position + 2 <= len(raw):
        glyph = struct.unpack_from(">H", raw, position)[0]
        position += 2
        if glyph == 0:
            text = "".join(result)
            if len(text.encode("ascii")) + 1 > 512:
                raise ValueError("Native menu message exceeds 511 characters")
            return text
        if glyph == 0xFFFF:
            if position >= len(raw):
                raise ValueError("Truncated message control")
            control = raw[position]
            position += 1
            if control == 0:
                result.append("\n")
            elif control == 2:
                pass  # Original wait-for-button; the native menu owns input.
            elif control == 7:
                if position >= len(raw):
                    raise ValueError("Truncated font control")
                position += 1
            elif control == 0x4D:
                result.append("{name}")
            else:
                raise ValueError("Unsupported menu message control 0x%02X" % control)
        elif 32 <= glyph <= 126:
            result.append(chr(glyph))
        elif glyph == 0xE9:
            result.append("e")
        else:
            raise ValueError("Unsupported native console glyph U+%04X" % glyph)
    raise ValueError("Menu message is not terminated")


def encode_pack(messages):
    if not 0 < len(messages) <= 64:
        raise ValueError("Native menu text count exceeds bounds")
    table, payload = bytearray(), bytearray()
    start = 16 + len(messages) * 12
    for key, value in sorted(messages.items()):
        if not 0 < key <= 0xFFFFFFFF:
            raise ValueError("Invalid menu message ID")
        if any((ord(char) < 32 and char != "\n") or ord(char) > 126 for char in value):
            raise ValueError("Console controls are not allowed")
        data = value.encode("ascii") + b"\0"
        if len(data) > 512:
            raise ValueError("Menu message exceeds bounds")
        table.extend(struct.pack("<III", key, start + len(payload), len(data)))
        payload.extend(data)
    size = start + len(payload)
    if size > MAX_PACK_BYTES:
        raise ValueError("Native menu text pack exceeds bounds")
    return struct.pack("<4sIII", b"C3U1", 1, size, len(messages)) + table + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        output = check_output(args.output)
        disc = verify_disc(args.disc)
        messages, records = {}, []
        for filename, profile in PROFILES.items():
            path = disc / "files" / filename
            if path.is_symlink() or path.stat().st_size != profile["bytes"]:
                raise ValueError("Unexpected source archive size or symbolic link: " + filename)
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != profile["sha256"]:
                raise ValueError("Source archive differs from the reviewed GC6E01 revision: " + filename)
            group = read_member(data, profile["member"])
            selected = message_bytes(group, profile["ids"])
            for key, raw in selected.items():
                if key in messages:
                    raise ValueError("Duplicate selected menu message ID")
                messages[key] = console_text(raw)
            records.append({"archive": filename, "archive_sha256": profile["sha256"],
                "member_id": profile["member"], "member_sha256": hashlib.sha256(group).hexdigest(),
                "message_sha256": {str(key): hashlib.sha256(value).hexdigest() for key, value in selected.items()}})
        packed = encode_pack(messages)
        output.parent.mkdir(parents=True, exist_ok=True)
        manifest = {"format": "C3U1", "source_commit": SOURCE_COMMIT, "sources": records,
            "bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest(), "message_count": len(messages),
            "conversion": "Source glyphs/control subset to ASCII console text; font and wait controls removed; e-acute -> e; name placeholder retained.",
            "ui_scope": "Native bottom-screen layout and ASCII keyboard; original window sprites/font are not ported."}
        with output.open("xb") as stream:
            stream.write(packed)
        with output.with_suffix(output.suffix + ".json").open("x") as stream:
            json.dump(manifest, stream, indent=2)
            stream.write("\n")
        print("Prepared %d source menu messages (%d bytes): %s" % (len(messages), len(packed), output))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, "Menu preparation failed: %s\n" % error)


if __name__ == "__main__":
    main()
