#!/usr/bin/env python3
"""Prepare private original GC6E01 P*DA metadata, text, UI and mail pictures."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import MAIN_DOL_SHA1
from prepare_game_state import Dol, COMMON_SHA1
from prepare_game_menu import read_member
from prepare_bottom_ui import decode_texture, crop
from prepare_model import check_output

PIN = bytes.fromhex("51bfe4314a218dfaf9592fc80723c50498d0ee55")
ARCHIVES = {
    "common.fsys": (743136, "f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a"),
    "pda_menu.fsys": (1042624, "0f6e0e466a7430bc5c7f3e1b6b602975668c1ab46bf566941b684fe049cf2f6c"),
    "pocket_menu.fsys": (484256, "33007fb68097f2222738b3f93e559ada98569f7236bdf97caaa12784e334f8b0"),
    "mail000.fsys": (38784, "91138e307f76f9831fcfeaf0c525a2460ca5756dd4c6480b08df33e5d2af8c18"),
    "mail001.fsys": (38784, "107989ae8b6a95044d4670b10c8c4c5df12933efaf6e48127a0a130d8a23693d"),
    "mail002.fsys": (38784, "93a8e8b5764557c6c5a4e4ebdbe14564a7b8142c76018781470b221576bf31ec"),
}
u16 = lambda d, o: struct.unpack_from(">H", d, o)[0]
u32 = lambda d, o: struct.unpack_from(">I", d, o)[0]


def messages(group):
    if len(group) < 16:
        raise ValueError("Truncated message group")
    count = u16(group, 4)
    if not 0 < count <= 4096 or 16 + count * 8 > len(group):
        raise ValueError("Message table exceeds bounds")
    rows = [struct.unpack_from(">II", group, 16 + i * 8) for i in range(count)]
    if any(rows[i][0] >= rows[i + 1][0] for i in range(count - 1)):
        raise ValueError("Message IDs are not ordered")
    offsets = sorted({off for _, off in rows})
    if offsets[0] < 16 + count * 8 or offsets[-1] >= len(group):
        raise ValueError("Message payload exceeds group")
    ends = dict(zip(offsets, offsets[1:] + [len(group)]))
    # Preserve GSchar/control streams. Native decoding is bounded and supplies
    # the live hero/partner names; no placeholder is flattened during export.
    return {key: group[off:min(ends[off], off + 8192)] for key, off in rows}


def encode(entries):
    if not 0 < len(entries) <= 8192:
        raise ValueError("PDA directory exceeds bound")
    table, payload = bytearray(), bytearray()
    base = 64 + len(entries) * 16
    for (kind, key), data in sorted(entries.items()):
        if not 1 <= kind <= 19 or not data:
            raise ValueError("Invalid PDA entry")
        table.extend(struct.pack("<4I", kind, key, base + len(payload), len(data)))
        payload.extend(data)
    contents = table + payload
    if 64 + len(contents) > 8 * 1024 * 1024:
        raise ValueError("PDA pack exceeds bound")
    return struct.pack("<4s3I", b"C3PD", 1, 64 + len(contents), len(entries)) + hashlib.sha1(contents).digest() + PIN + bytes(8) + contents


def prepare(main_dol, common_rel, files):
    main_dol, common_rel, files = Path(main_dol), Path(common_rel), Path(files)
    for p in (main_dol, common_rel):
        if p.is_symlink():
            raise ValueError("Original input must not be a symlink")
    d, rel = main_dol.read_bytes(), common_rel.read_bytes()
    if len(d) != 3779808 or hashlib.sha1(d).hexdigest() != MAIN_DOL_SHA1:
        raise ValueError("Expected pinned GC6E01 revision 0 DOL")
    if len(rel) != 1369520 or hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("Expected pinned common_rel.rel")
    archives = {}
    for name, (size, digest) in ARCHIVES.items():
        path = files / name
        if path.is_symlink() or path.stat().st_size != size:
            raise ValueError("Unexpected source path/size: " + name)
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError("Original archive hash mismatch: " + name)
        archives[name] = raw
    dol, entries = Dol(d), {}
    groups = [rel[0x184C + 0x58044:0x184C + 0x647B4], rel[0x184C + 0x647B4:]]
    groups += [read_member(archives[n], mid) for n, mid in (("pda_menu.fsys", 0x3750A00), ("pocket_menu.fsys", 0x2E90A00))]
    for group in groups:
        for key, raw in messages(group).items():
            entries[1, key] = raw
    for i in range(1, 413):
        off = 0x184C + 0x121A04 + i * 0x11C
        entries[2, i] = rel[off:off + 0x34] + rel[off + 0x84:off + 0x90] + rel[off + 0x10C:off + 0x10D]
    max_ability = max(max(data[50:52]) for (kind, _), data in entries.items() if kind == 2)
    if max_ability >= 128:
        raise ValueError("Original ability index exceeds bound")
    for i in range(max_ability + 1):
        entries[12, i] = dol.read(0x8035F5E0 + i * 12, 12)
    mail = read_member(archives["common.fsys"], 0x06281C00)
    section = u32(mail, 0x10)
    data = u32(mail, section + 3 * 8)
    if data + 0x6C0 > len(mail) or u32(mail, data + 0x6BC) != 32 or u32(mail, data + 0x6B8) != 78:
        raise ValueError("Mail REL table layout differs")
    entries[8, 0] = mail[data:data + 0x138]
    for i in range(32):
        off = data + 0x138 + i * 44
        entries[3, i] = mail[off:off + 44]
    for i in range(1, 97):
        off = 0x184C + 0x1439D8 + i * 56
        entries[4, i] = rel[off:off + 56]
        action = u16(rel, off + 30)
        nodes = bytearray()
        for n in range(128):
            start = 0x184C + 0x144F10 + (action + n) * 8
            node = rel[start:start + 8]
            if len(node) != 8:
                raise ValueError("Snag action table exceeds source")
            nodes.extend(node)
            if not u32(node, 4):
                break
        else:
            raise ValueError("Snag action table lacks terminator")
        entries[11, i] = nodes
    floors = u32(rel, 0x184C + 0x14A670)
    if not 1 <= floors <= 256:
        raise ValueError("Floor table count differs")
    for i in range(floors):
        off = 0x184C + 0x8BCF4 + i * 76
        row = rel[off:off + 76]
        key = u32(row, 12)
        if (13, key) in entries:
            raise ValueError("Duplicate floor key")
        entries[13, key] = row
    entries[5, 0] = dol.read(0x80375240, 96)
    for i in range(548):
        slot = u16(dol.read(0x803681E8 + i * 2, 2), 0)
        if slot >= 397:
            raise ValueError("Original item remap exceeds table")
        entries[7, i] = dol.read(0x80363CE8 + slot * 40, 40)
    entries[9, 0] = dol.read(0x8035E940, 6 * 404)
    for i in range(358):
        off = 0x184C + 0x11C7C4 + i * 56
        entries[10, i] = rel[off:off + 56]
    for i in range(64):
        entries[14, i] = dol.read(0x80368630 + i * 16, 16)
    for i in range(1, 413):
        off = 0x184C + 0x121A04 + i * 284
        entries[15, i] = rel[off:off + 284]
    entries[16, 0] = dol.read(0x80368018, 58 * 8)
    for i in range(25):
        off = 0x184C + 0x121614 + i * 40
        entries[17, i] = rel[off:off + 40]
    entries[18, 0] = rel[0x184C + 0x13E430:0x184C + 0x13E440]
    entries[18, 1] = rel[0x184C + 0x1219FC:0x184C + 0x121A04]
    entries[18, 2] = dol.read(0x8035F988, 12 * 3)
    entries[19, 0] = dol.read(0x8036E0E0, 10 * 8)
    # Main P*DA frame, metallic header, memo console and unread/read envelopes.
    images = ((0, "pda_menu.fsys", 0x0B511200, (0, 0, 314, 240)),
              (1, "pda_menu.fsys", 0x0B4F1200, (0, 0, 640, 96)),
              (2, "pda_menu.fsys", 0x06271200, (0, 0, 560, 180)),
              (3, "pda_menu.fsys", 0x03731200, (110, 86, 31, 22)),
              (4, "pda_menu.fsys", 0x03731200, (159, 85, 31, 25)))
    for key, name, mid, rect in images:
        w, h, pixels = crop(decode_texture(read_member(archives[name], mid, 1024 * 1024)), rect)
        entries[6, key] = struct.pack("<II", w, h) + pixels
    for i, mid in enumerate((0x05CE1200, 0x11EA1200, 0x11EB1200)):
        w, h, pixels = decode_texture(read_member(archives["mail%03d.fsys" % i], mid, 1024 * 1024))
        group = u32(archives["mail%03d.fsys" % i], 8)
        entries[6, 100 + group] = struct.pack("<II", w, h) + pixels
    packed = encode(entries)
    return packed, {"format": "C3PD", "source_commit": PIN.hex(), "main_dol_sha1": MAIN_DOL_SHA1,
        "common_rel_sha1": COMMON_SHA1, "archives": {n: p[1] for n, p in ARCHIVES.items()},
        "entries": len(entries), "bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest(),
        "scope": "Original P*DA UI/text/species/item/mail/shadow data; private original assets, not distributable source."}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--main-dol", type=Path, required=True)
    p.add_argument("--common-rel", type=Path, required=True)
    p.add_argument("--files", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    try:
        output = check_output(a.output)
        data, manifest = prepare(a.main_dol, a.common_rel, a.files)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("xb") as f:
            f.write(data)
        with output.with_suffix(output.suffix + ".json").open("x") as f:
            json.dump(manifest, f, indent=2)
            f.write("\n")
        print("Prepared original PDA pack: %d bytes, %d entries" % (len(data), manifest["entries"]))
    except (ValueError, OSError, struct.error) as e:
        p.exit(1, "PDA preparation failed: %s\n" % e)


if __name__ == "__main__":
    main()
