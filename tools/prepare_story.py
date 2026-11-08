#!/usr/bin/env python3
"""Extract original Outskirt Stand GSVM and message banks for the native port.

No rewritten event script, dialogue, or victory state is generated. These
private packs retain the original instruction and message bytes. The portable
executor yields game-native functions to the scene, which must implement each
one before the original script can continue.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import extent, member, rel_data, u32

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
PROFILES = {
    "S1_out.fsys": (1468000, "37900ccab1353ab835c7e4acc40cd5e1349237589a53b441c78ce91a891f8034"),
    "S1_shop_1F.fsys": (2947200, "7d74dcb75c7f8aebc7479fee695e87a02ab58112d110dada15e88353d7215329"),
    "common_rel.rel": (1369520, "0c89e679305f5e84eff3c774b542120c727ff350a0841cfc516235b3556df69e"),
}
AREAS = {
    1: ("S1_out.fsys", 0x01430E00, 0x01420A00, "outskirt.c3s"),
    2: ("S1_shop_1F.fsys", 0x01460E00, 0x01450A00, "outskirt_shop.c3s"),
}
# Published data tables in the pinned GC6E01 revision. These are offsets into
# admitted original files, not replacements for their event/name/lifecycle data.
ACTOR_TABLES = {
    1: ("S1_out.fsys", 0x02571C00, 131, 0x30, 7),
    2: ("S1_shop_1F.fsys", 0x02581C00, 5, 0x180, 5),
}
OPCODE_SIZES = {0: 1, 1: 1, 2: 1, 3: 4, 4: 9, 5: 1, 6: 5, 7: 5, 8: 6,
                0xD: 3, 0xE: 3, 0xF: 2, 0x10: 4, 0x11: 2, 0x12: 2,
                0x13: 2, **{i: 3 for i in range(0x14, 0x20)},
                0x20: 5, 0x21: 3, 0x22: 3, 0x23: 5, 0x24: 5, 0x25: 5}


def be16(data, at):
    if not 0 <= at <= len(data) - 2:
        raise ValueError("Truncated BE16")
    return struct.unpack_from(">H", data, at)[0]


def admit(path):
    size, digest = PROFILES[path.name]
    if path.is_symlink() or path.stat().st_size != size:
        raise ValueError("Unexpected source size/path: " + path.name)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("Source differs from admitted GC6E01 revision 0: " + path.name)
    return data


def inspect_bank(data):
    """Walk the original reachable instruction graph without executing natives.

    This is a structural inventory, never a claim that every native function is
    implemented. All function entry points are roots, even later-story ones.
    """
    if len(data) < 28 or len(data) > 262144 or data[10]:
        raise ValueError("Invalid original GSVM bank")
    bank, count = be16(data, 0), be16(data, 4)
    if not 0 < count <= 2048 or 24 + 4*count > len(data):
        raise ValueError("Invalid original GSVM function count")
    functions = [u32(data, 24+i*4) for i in range(count)]
    globals_at = u32(data, 16)
    if min(functions) < globals_at or (min(functions)-globals_at) % 4:
        raise ValueError("Invalid original GSVM globals")
    pending, visited, opcodes = list(functions), {}, set()
    while pending:
        pc = pending.pop()
        if pc in visited:
            continue
        if not min(functions) <= pc < len(data):
            raise ValueError("Original GSVM branch is out of bounds")
        op = data[pc]
        size = OPCODE_SIZES.get(op)
        if size is None or pc + size > len(data):
            raise ValueError(f"Unsupported/truncated opcode {op:02x} at {pc:04x}")
        visited[pc] = size
        opcodes.add(op)
        if op in (6, 7):
            pending.append(u32(data, pc+1))
        if op == 4:
            target = u32(data, pc+1)
            if target >= count:
                raise ValueError("Invalid original GSVM function call")
            pending.append(functions[target])
        if op not in (1, 5, 6):
            pending.append(pc+size)
    occupied = set()
    for pc, size in visited.items():
        if any(at in occupied for at in range(pc, pc+size)):
            raise ValueError("Original GSVM branches into an operand")
        occupied.update(range(pc, pc+size))
    # Recover the callee constant by walking stack effects backwards within
    # each basic block. This never invents an ID at joins/dynamic producers.
    preceding = {pc+size: pc for pc, size in visited.items()}
    targets = set(functions)
    targets.update(u32(data, pc+1) for pc in visited if data[pc] in (6, 7))

    def native_index(pc, depth):
        for _ in range(128):
            if pc in targets or pc not in preceding:
                return None
            pc = preceding[pc]
            op = data[pc]
            if op in (1, 4, 5, 6, 7):
                return None
            if op == 8:
                if depth == 0:
                    return u32(data, pc+2)
                popped, pushed = 0, 1
            elif op == 2:
                if depth == 0:
                    return 0xffffffff
                popped, pushed = 0, 1
            elif op == 3:
                popped, pushed = be16(data, pc+2), 0
            elif op == 0x10:
                popped, pushed = 1, be16(data, pc+2)
            elif op == 0x20:
                pushed = be16(data, pc+3)
                popped = 1+pushed if data[pc+1] & 0x80 else 2
            elif op in (0x23, 0x24, 0x25):
                if op == 0x25 and depth == 0:
                    return None  # the preceding call wrote its return slot
                popped, pushed = be16(data, pc+3), 0
            elif op in (0xF, 0x11, 0x12, 0x13):
                popped, pushed = 1, 1
            elif op in (0xD, 0xE, 0x21, 0x22) or 0x14 <= op <= 0x1F:
                popped, pushed = 2, 1
            elif op == 0:
                popped, pushed = 0, 0
            else:
                return None
            if depth < pushed:
                return None
            depth += popped-pushed
        return None

    calls = []
    for pc in sorted(visited):
        if data[pc] != 0x25:
            continue
        count = be16(data, pc+3)
        if not 1 <= count <= 9:
            raise ValueError("Original native argument count exceeds runtime limit")
        native = native_index(pc, count-1)
        if native is not None and native >= 0xf7:
            raise ValueError("Original native ID exceeds admitted function table")
        calls.append({"offset": pc, "native": None if native is None else f"{native:02x}",
                      "script_argument_words": count-1})
    return {"id": f"{bank:04x}", "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "globals": (min(functions)-globals_at)//4,
            "functions": [{"index": i, "offset": at} for i, at in enumerate(functions)],
            "reachable_instructions": len(visited),
            "opcodes": [f"{op:02x}" for op in sorted(opcodes)],
            "native_calls": calls,
            "required_natives": sorted({c["native"] for c in calls if c["native"] is not None}),
            "unresolved_native_sites": sum(c["native"] is None for c in calls),
            "native_callbacks": "Required callbacks only; implementation is owned and checked by the host scene"}


def encode_pack(floor, banks, messages):
    header = bytearray(64)
    count = len(banks) + len(messages)
    offset = 64 + 16*count
    directory, payload = bytearray(), bytearray()
    for data in banks + messages:
        directory.extend(struct.pack("<4I", be16(data, 0), offset+len(payload), len(data), 0))
        payload.extend(data)
    size = offset + len(payload)
    if size > 1048576:
        raise ValueError("Story pack exceeds runtime allocation limit")
    struct.pack_into("<4s7I", header, 0, b"C3S1", 1, size, floor,
                     len(banks), len(messages), 64, 64+16*len(banks))
    return bytes(header + directory + payload)


def extract_area_metadata(originals):
    """Retain source event records, floor hooks, and actor name/talk policy.

    floor_character.c dispatches the common event table by kind/floor/source
    ID. floor_bios.c consumes the lifecycle table. char_name_bios.c resolves
    the actor row's name index through the common name/heard-flag table.
    All values and all four event arguments are read from the original data.
    """
    rel = originals["common_rel.rel"]
    common = rel_data(rel, 125)
    base = u32(rel, u32(rel, 16) + 4*8) & ~3
    event_count, floor_count, name_count = (u32(common, at) for at in
                                          (0x14A704, 0x14A670, 0x14A660))
    if (event_count, floor_count, name_count) != (594, 196, 607):
        raise ValueError("Unexpected original event/floor/name table counts")
    events, lifecycle, names = [], [], []
    event_rows = extent(common, 0x146364, event_count * 28)
    for index in range(event_count):
        kind, _pad, floor, source_id, script, *arguments = struct.unpack_from(
            ">BBH6I", event_rows, index * 28)
        if floor in AREAS:
            events.append({"index": index, "kind": kind, "floor": floor,
                           "source_id": source_id, "script": hex(script),
                           "args": arguments,
                           "file_offset": hex(base + 0x146364 + index * 28)})
    floor_rows = extent(common, 0x8BCF4, floor_count * 0x4C)
    for index in range(floor_count):
        words = list(struct.unpack_from(">19I", floor_rows, index * 0x4C))
        if words[3] in AREAS:
            lifecycle.append({"floor": words[3], "resource_group": words[1],
                              "file_offset": hex(base + 0x8BCF4 + index * 0x4C),
                              "index": index, "raw_words": words,
                              **dict(zip(("init_script", "pre_script", "main_script",
                                          "post_script", "pre_native", "main_native",
                                          "post_native"), words[11:18]))})
    if sorted(row["floor"] for row in lifecycle) != sorted(AREAS):
        raise ValueError("Missing or duplicate original area lifecycle")
    name_rows = extent(common, 0x1A34, name_count * 8)
    for floor, (archive, identity, module, start, count) in ACTOR_TABLES.items():
        actors = extent(rel_data(member(originals[archive], identity), module),
                        start, count * 36)
        for index in range(count):
            at = index * 36
            name_index = be16(actors, at + 8)
            if name_index >= name_count:
                raise ValueError("Original actor name index is out of bounds")
            policy = actors[at + 1]
            names.append({"floor": floor, "actor": index, "name_index": name_index,
                          "hear_flag": be16(name_rows, name_index * 8),
                          "name_message": u32(name_rows, name_index * 8 + 4),
                          "talk_start": (policy >> 6) & 3,
                          "talk_end": (policy >> 4) & 3})
    return {"area-events.json": events, "area-lifecycle.json": lifecycle,
            "actor-names.json": names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        disc, output = verify_disc(args.disc), check_output(args.output)
        output.mkdir(parents=True, exist_ok=True)
        originals = {name: admit(disc / "files" / name) for name in PROFILES}
        area_metadata = extract_area_metadata(originals)
        rel = originals["common_rel.rel"]
        # REL125 data+0x89CFC is common GSVM bank0596. It ends at the next
        # published table data+0x8BCB4. The admitted binary keeps this precise.
        base = u32(rel, u32(rel, 16) + 4*8) & ~3
        common = rel[base+0x89CFC:base+0x8BCB4]
        # Common English message group (684 entries). Its last GSchar string
        # ends at +0x124DD; alignment bytes and following groups are excluded.
        # Native TV news B6 uses message 0x426E from this original group.
        common_messages = rel[base+0x647B4:base+0x647B4+0x124DD]
        # Original common names/system strings, including charName index1's
        # message1014 and the unknown-speaker glyph message0FA3.
        common_names = rel[base+0x58044:base+0x647B4]
        common_info = inspect_bank(common)
        if (be16(common, 0) != 0x596 or be16(common_messages, 4) != 684
                or be16(common_names, 4) != 2018):
            raise ValueError("Unexpected original common script bank")
        report = {"source_commit": SOURCE_COMMIT, "format": "C3S1", "areas": [],
                  "original_files": {name: {"bytes": p[0], "sha256": p[1]} for name, p in PROFILES.items()},
                  "common_bank": common_info, "metadata_files": [],
                  "common_messages": {"data_offset": "647b4", "bytes": len(common_messages),
                                      "count": be16(common_messages, 4),
                                      "sha256": hashlib.sha256(common_messages).hexdigest()},
                  "common_names": {"data_offset": "58044", "bytes": len(common_names),
                                   "count": be16(common_names, 4),
                                   "sha256": hashlib.sha256(common_names).hexdigest()},
                  "scope": "Original banks/data only. Native engine services are separately ported and checked."}
        for floor, (filename, script_id, message_id, outname) in AREAS.items():
            bank = member(originals[filename], script_id)
            messages = member(originals[filename], message_id)
            info = inspect_bank(bank)
            if be16(bank, 0) != 0x100:
                raise ValueError("Unexpected original field script bank")
            packed = encode_pack(floor, [bank, common], [messages, common_messages, common_names])
            (output / outname).write_bytes(packed)
            report["areas"].append({"floor": floor, "file": outname, "bytes": len(packed),
                "sha256": hashlib.sha256(packed).hexdigest(), "bank": info,
                "script_member": f"{script_id:08x}", "message_member": f"{message_id:08x}",
                "message_sha256": hashlib.sha256(messages).hexdigest(),
                "message_count": be16(messages, 4)})
        for filename, records in area_metadata.items():
            encoded = (json.dumps(records, indent=2)+"\n").encode("utf-8")
            (output / filename).write_bytes(encoded)
            report["metadata_files"].append({"file": filename, "records": len(records),
                                             "bytes": len(encoded),
                                             "sha256": hashlib.sha256(encoded).hexdigest()})
        report["metadata_sources"] = {
            "common_rel_data": {
                "events": {"offset": "146364", "count_offset": "14a704", "stride": 28},
                "lifecycle": {"offset": "8bcf4", "count_offset": "14a670", "stride": 76},
                "names": {"offset": "1a34", "count_offset": "14a660", "stride": 8}},
            "actors": [{"floor": floor, "archive": a[0], "member": f"{a[1]:08x}",
                        "module": a[2], "data_offset": f"{a[3]:x}",
                        "count": a[4], "stride": 36} for floor, a in ACTOR_TABLES.items()]}
        (output / "story-manifest.json").write_text(json.dumps(report, indent=2)+"\n")
        print(json.dumps({"output": str(output),
                          "packs": [{k: a[k] for k in ("file", "bytes", "sha256")}
                                    for a in report["areas"]],
                          "metadata_files": report["metadata_files"]}, indent=2))
    except (OSError, ValueError, struct.error) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
