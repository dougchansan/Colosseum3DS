#!/usr/bin/env python3
"""Index admitted original story dependencies into a NEW PRIVATE directory.

This emits metadata, never scripts, dialogue, playable packs or a route claim.
All archive groups in the admitted TOC are inspected, without floor filtering.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

from prepare_boot_media import MAIN_DOL_SHA1
from prepare_battle_scene import archive_names, PROFILES as SCENE_PROFILES
from prepare_model import check_output
from prepare_outskirt import extent, member, rel_data, u32
from prepare_story import SOURCE_COMMIT, PROFILES, OPCODE_SIZES, be16, inspect_bank

# Canonical TOC group/name/size/SHA256 catalog, sorted by group, compact JSON.
# Discovered from the private GC6E01 stage; admission checks the entire catalog
# before parsing any archive. The individual hashes are retained in the index.
ARCHIVE_CATALOG_SHA256 = "92352de701a5f2decf3db2ab835998f07ecf0b6a72ed8f00a39d06fff2a604e9"
TABLES = {"lifecycle": (0x8BCF4, 76, 0x14A670, 196),
          "events": (0x146364, 28, 0x14A704, 594),
          "names": (0x1A34, 8, 0x14A660, 607)}
MAX_ARCHIVE = 32 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compact(value):
    return json.dumps(value, separators=(",", ":")).encode("utf-8")


def safe_file(path, limit):
    path = Path(path).absolute()
    if any(p.is_symlink() or (hasattr(p, "is_junction") and p.is_junction())
           for p in (path, *path.parents)):
        raise ValueError("Source path contains a link")
    if not path.is_file() or not 0 < path.stat().st_size <= limit:
        raise ValueError("Invalid source file extent: " + path.name)
    return path.read_bytes()


def pinned(path, size, digest, algorithm="sha256"):
    data = safe_file(path, size)
    if len(data) != size or hashlib.new(algorithm, data).hexdigest() != digest:
        raise ValueError("Source differs from admitted GC6E01: " + Path(path).name)
    return data


def sections(rel):
    count, table = u32(rel, 12), u32(rel, 16)
    if not 1 <= count <= 32 or table < 0x40:
        raise ValueError("Invalid REL section table")
    extent(rel, table, count * 8)
    rows, spans = [], [(0, table + count * 8)]
    for index in range(count):
        offset, size = struct.unpack_from(">II", rel, table + index * 8)
        row = {"section": index, "file_offset": offset & ~3,
               "bytes": size, "executable": bool(offset & 1)}
        rows.append(row)
        if offset & ~3 and size:
            extent(rel, offset & ~3, size)
            spans.append((offset & ~3, (offset & ~3) + size))
    reject_overlap(spans, "REL sections")
    return rows


def reject_overlap(spans, label):
    ordered = sorted(spans)
    if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Overlapping " + label)


def relocations(rel):
    """Retain import module/target section/addend without loading the REL."""
    sec = sections(rel)
    offset, size = u32(rel, 0x28), u32(rel, 0x2C)
    if size % 8 or size > 4096 * 8:
        raise ValueError("Invalid REL import extent")
    extent(rel, offset, size)
    result, streams = {}, []
    for at in range(offset, offset + size, 8):
        module, cursor = struct.unpack_from(">II", rel, at)
        start, target_section, target = cursor, None, 0
        for _ in range(len(rel) // 8):
            delta, kind, source_section, addend = struct.unpack(">HBBI", extent(rel, cursor, 8))
            record_at = cursor
            cursor += 8
            if kind == 203:
                break
            if kind == 202:
                if source_section >= len(sec):
                    raise ValueError("Invalid REL relocation section")
                target_section, target = source_section, 0
                continue
            if target_section is None:
                raise ValueError("REL relocation before section selection")
            target += delta
            if kind == 201:
                continue
            if kind not in (0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 109):
                raise ValueError("Unsupported REL relocation type")
            if target + (4 if kind in (1, 2, 10, 11, 12, 13) else 2) > sec[target_section]["bytes"]:
                raise ValueError("REL relocation exceeds section")
            record = {"record_file_offset": record_at, "type": kind,
                      "import_module": module, "source_section": source_section,
                      "addend": addend, "target_section": target_section,
                      "target_section_offset": target}
            result.setdefault((target_section, target), []).append(record)
        else:
            raise ValueError("Unterminated REL relocations")
        streams.append((start, cursor))
    reject_overlap(streams, "REL import streams")
    return result


def metadata(rel):
    common = rel_data(rel, 125)
    base = sections(rel)[4]["file_offset"]
    reloc = relocations(rel)
    result = {}
    for name, (offset, stride, count_at, expected) in TABLES.items():
        count = u32(common, count_at)
        if count != expected:
            raise ValueError("Unexpected original " + name + " count")
        table = extent(common, offset, stride * count)
        rows = []
        for index in range(count):
            at = index * stride
            record = extent(table, at, stride)
            row = {"index": index, "section": 4, "section_offset": offset + at,
                   "file_offset": base + offset + at, "sha256": sha(record)}
            if name == "lifecycle":
                words = list(struct.unpack(">19I", record))
                row.update(floor=words[3], resource_group=words[1], words=words)
                row.update(zip(("init_script", "pre_script", "main_script", "post_script"), words[11:15]))
                callbacks = {}
                for field, word in zip(("pre_native", "main_native", "post_native"), range(15, 18)):
                    rs = reloc.get((4, offset + at + word * 4), [])
                    callbacks[field] = {"raw_word": words[word], "relocations": rs,
                                        "status": "relocated" if rs else "raw-pointer" if words[word] else "null"}
                row["native_callbacks"] = callbacks
            elif name == "events":
                kind, pad, floor, source, script, *args = struct.unpack(">BBH6I", record)
                row.update(kind=kind, padding=pad, floor=floor, source_id=source, script=script, arguments=args)
            else:
                heard, pad, message = struct.unpack(">HHI", record)
                row.update(hear_flag=heard, padding=pad, name_message=message)
            rows.append(row)
        result[name] = rows
    return result


def archive_entries(data):
    """Validate every FSYS directory and payload before selecting script types."""
    if len(data) < 64 or data[:4] != b"FSYS" or u32(data, 32) != len(data):
        raise ValueError("Invalid FSYS header")
    count = u32(data, 12)
    if not 0 <= count <= 4096:
        raise ValueError("Invalid FSYS count")
    if not count:
        return []  # Four admitted archives are intentionally empty.
    outer = u32(data, 24)
    extent(data, outer, 12)
    table = u32(data, outer)
    extent(data, table, count * 4)
    rows, spans, ids = [], [(0, 64), (outer, outer + 12), (table, table + count * 4)], set()
    for index in range(count):
        at = u32(data, table + index * 4)
        extent(data, at, 40)
        identity, start, unpacked = struct.unpack_from(">III", data, at)
        packed = u32(data, at + 20)
        if identity in ids or not 0 < unpacked <= 16 * 1024 * 1024 or not packed:
            raise ValueError("Duplicate FSYS identity or invalid member extent")
        ids.add(identity)
        payload = extent(data, start, packed)
        compressed = bool(u32(data, at + 12) & 0x80000000)
        if compressed:
            if (len(payload) < 16 or payload[:4] != b"LZSS" or u32(payload, 4) != unpacked
                    or not 16 <= u32(payload, 8) <= packed or u32(payload, 12)):
                raise ValueError("Invalid FSYS compressed member header")
        elif packed != unpacked:
            raise ValueError("Invalid FSYS raw member length")
        spans.extend(((at, at + 40), (start, start + packed)))
        rows.append({"index": index, "resource_id": identity, "entry_file_offset": at,
                     "payload_file_offset": start, "packed_bytes": packed, "bytes": unpacked,
                     "compressed": bool(u32(data, at + 12) & 0x80000000)})
    reject_overlap(spans, "FSYS directory/payload")
    return rows


def toc_names(toc):
    names = archive_names(toc)
    table, count = u32(toc, 16), u32(toc, 8)
    if table < 32:
        raise ValueError("Invalid archive name table")
    for index in range(count):
        at = u32(toc, table + index * 8 + 4)
        if not 32 <= at < table:
            raise ValueError("Archive name overlaps TOC directory")
        if b"\0" not in extent(toc, at, min(128, table - at)):
            raise ValueError("Unterminated archive name")
    return names


def bank_index(data):
    """Use the existing all-entry CFG validator, then recover local literals.

    Literal recovery stops at every join/function boundary/call. Variables,
    native results, computed expressions and cross-function arguments remain
    unknown. No branch condition is claimed true or false.
    """
    if len(data) < 28 or len(data) > 262144 or data[10]:
        raise ValueError("Invalid original GSVM bank")
    count, globals_at = be16(data, 4), u32(data, 16)
    if not 0 < count <= 2048 or globals_at < 24 + count * 4 or globals_at % 4:
        raise ValueError("Invalid GSVM function/global table")
    functions = [u32(data, 24 + i * 4) for i in range(count)]
    present = [pc for pc in functions if pc]
    if not present or min(present) < globals_at or (min(present) - globals_at) % 4:
        raise ValueError("Invalid GSVM globals")
    report = {"id": f"{be16(data, 0):04x}", "bytes": len(data), "sha256": sha(data),
              "globals": (min(present) - globals_at) // 4,
              "functions": [{"index": i, "offset": pc, "status": "present" if pc else "null-entry"}
                            for i, pc in enumerate(functions)]}
    # The existing two-floor inspector rejects valid retail null function
    # slots. Retain its independent validation where its admitted subset fits.
    try:
        inspect_bank(data)
        report["existing_inspector"] = "passed"
    except ValueError as exc:
        report["existing_inspector"] = str(exc)
    pending, visited, edges = list(present), {}, []
    while pending:
        pc = pending.pop()
        if pc in visited:
            continue
        if not min(present) <= pc < len(data):
            raise ValueError("GSVM branch/function out of bounds")
        op, size = data[pc], OPCODE_SIZES.get(data[pc])
        if size is None or pc + size > len(data):
            raise ValueError("Unsupported/truncated GSVM instruction")
        visited[pc] = size
        if op in (6, 7):
            target = u32(data, pc + 1)
            edges.append({"offset": pc, "kind": "conditional-branch" if op == 7 else "branch",
                          "target_offset": target, "condition": "unknown" if op == 7 else None})
            pending.append(target)
        if op == 4:
            target = u32(data, pc + 1)
            if target >= count or not functions[target]:
                raise ValueError("GSVM call to invalid/null function")
            edges.append({"offset": pc, "kind": "function-call", "function": target,
                          "target_offset": functions[target]})
            pending.append(functions[target])
        if op not in (1, 5, 6):
            pending.append(pc + size)
    reject_overlap([(pc, pc + size) for pc, size in visited.items()], "GSVM instructions")
    report["reachable_instructions"] = len(visited)
    report["opcodes"] = sorted({data[pc] for pc in visited})
    owners = {}
    for index, root in enumerate(functions):
        pending, seen = [root] if root else [], set()
        while pending:
            pc = pending.pop()
            if pc in seen:
                continue
            seen.add(pc)
            owners.setdefault(pc, []).append(index)
            op = data[pc]
            if op in (6, 7):
                pending.append(u32(data, pc + 1))
            # Callee instructions belong to their own entry, not its caller.
            if op not in (1, 5, 6):
                pending.append(pc + visited[pc])
    predecessors = {pc + size: pc for pc, size in visited.items()}
    boundaries = set(functions) | {e["target_offset"] for e in edges}
    # Fallthrough after a conditional branch is also a block boundary.
    boundaries.update(pc + size for pc, size in visited.items() if data[pc] == 7)

    def literal(pc, depth, budget=128):
        while budget:
            budget -= 1
            if pc in boundaries or pc not in predecessors:
                return {"value": None, "reason": "block-boundary"}
            pc = predecessors[pc]
            op = data[pc]
            if op in (1, 4, 5, 6, 7):
                return {"value": None, "reason": "control-transfer"}
            if op == 8:
                if depth == 0:
                    return {"value": u32(data, pc + 2), "producer_offset": pc}
                popped, pushed = 0, 1
            elif op == 2:
                if depth == 0:
                    return {"value": 0xFFFFFFFF, "producer_offset": pc}
                popped, pushed = 0, 1
            elif op == 3:
                popped, pushed = be16(data, pc + 2), 0
            elif op == 0x13 and data[pc + 1] & 0x80:
                continue  # immediate operand conversion preserves the word
            elif op in (0x0F, 0x11, 0x12, 0x13):
                popped, pushed = 1, 1
            elif op == 0x10:
                popped, pushed = 1, be16(data, pc + 2)
            elif op == 0x20:
                pushed = be16(data, pc + 3)
                popped = 1 + pushed if data[pc + 1] & 0x80 else 2
            elif op in (0x23, 0x24, 0x25):
                if op == 0x25 and depth == 0:
                    return {"value": None, "reason": "native-return"}
                popped, pushed = be16(data, pc + 3), 0
            elif op in (0x0D, 0x0E, 0x21, 0x22) or 0x14 <= op <= 0x1F:
                popped, pushed = 2, 1
            elif op == 0:
                continue
            else:
                return {"value": None, "reason": "unsupported-producer"}
            if depth < pushed:
                result = {"value": None, "reason": "dynamic-producer", "producer_offset": pc, "opcode": op}
                if op in (0x0F, 0x10, 0x13):
                    result["operand_descriptor"] = data[pc + 1]
                    result["slot_index"] = literal(pc, 0, budget)
                return result
            depth += popped - pushed
        return {"value": None, "reason": "analysis-bound"}

    for edge in edges:
        if edge["kind"] != "function-call":
            continue
        pc = edge["offset"]
        count = be16(data, pc + 7)
        if count > 256:
            raise ValueError("Invalid GSVM function argument count")
        edge["caller_functions"] = owners.get(pc, [])
        edge["return_slots"] = be16(data, pc + 5)
        edge["arguments"] = [literal(pc, count - 1 - i) for i in range(count)]
    sites = []
    for pc in sorted(visited):
        if data[pc] != 0x25:
            continue
        count = be16(data, pc + 3)
        if not 1 <= count <= 256:
            raise ValueError("Invalid native argument count")
        values = [literal(pc, count - 1 - i) for i in range(count)]
        native, args = values[0]["value"], values[1:]
        if native is not None and native >= 0xF7:
            raise ValueError("Native ID exceeds admitted table")
        site = {"offset": pc, "functions": owners.get(pc, []), "native": native,
                "native_producer": values[0], "arguments": args}
        kind = {3: "start-script", 0x4E: "flag-set-one", 0x4F: "flag-set-zero",
                0x50: "flag-write", 0x51: "flag-read", 0x5F: "floor-link",
                0x7D: "floor-link-with-se", 0x98: "battle"}.get(native)
        if kind:
            site["dependency"] = {"kind": kind, "target": args[0]["value"] if args else None,
                                  "condition": "unknown; static site is not a taken route"}
        sites.append(site)
    report["native_calls"] = sites
    report["required_natives"] = sorted({s["native"] for s in sites if s["native"] is not None})
    report["unresolved_native_sites"] = sum(s["native"] is None for s in sites)
    report["unresolved_argument_words"] = sum(a["value"] is None for s in sites for a in s["arguments"])
    report["control_flow_edges"] = edges
    return report


def build_index(dol_path, common_path, files):
    dol = pinned(dol_path, 3779808, MAIN_DOL_SHA1, "sha1")
    common = pinned(common_path, *PROFILES["common_rel.rel"])
    files = Path(files)
    toc = pinned(files / "GSfsys.toc", *SCENE_PROFILES["GSfsys.toc"])
    names = toc_names(toc)
    catalog = []
    for group, name in sorted(names.items()):
        data = safe_file(files / name, MAX_ARCHIVE)
        catalog.append([group, name, len(data), sha(data)])
    if sha(compact(catalog)) != ARCHIVE_CATALOG_SHA256:
        raise ValueError("Archive catalog differs from admitted GC6E01 stage")
    report = {"schema": 1, "source_commit": SOURCE_COMMIT, "scope": "unfiltered structural index; mandatory route unresolved",
              "inputs": {"dol_sha1": hashlib.sha1(dol).hexdigest(), "dol_sha256": sha(dol),
                         "common_sha256": sha(common), "toc_sha256": sha(toc),
                         "archive_catalog_sha256": ARCHIVE_CATALOG_SHA256},
              **metadata(common), "archives": [], "banks": []}
    for group, name, size, digest in catalog:
        data = safe_file(files / name, MAX_ARCHIVE)
        if len(data) != size or sha(data) != digest:
            raise ValueError("Archive changed during indexing")
        entries = archive_entries(data)
        report["archives"].append({"group": group, "name": name, "bytes": size,
                                   "sha256": digest, "members": entries})
        for entry in entries:
            if (entry["resource_id"] >> 8) & 255 != 0x0E:
                continue
            bank = bank_index(member(data, entry["resource_id"]))
            bank.update(archive_group=group, archive=name, resource_id=entry["resource_id"],
                        archive_member_index=entry["index"], payload_file_offset=entry["payload_file_offset"])
            report["banks"].append(bank)
    source = rel_data(common, 125)
    bank = bank_index(extent(source, 0x89CFC, 0x8BCB4 - 0x89CFC))
    bank.update(archive=None, archive_group=0, section=4, section_offset=0x89CFC,
                file_offset=sections(common)[4]["file_offset"] + 0x89CFC)
    report["banks"].append(bank)
    for life in report["lifecycle"]:
        life["archive"] = names.get(life["resource_group"])
        life["bank_candidates"] = [i for i, b in enumerate(report["banks"])
                                   if b["archive_group"] == life["resource_group"]]
        life["script_dependencies"] = {field: script_reference(report["banks"], life["resource_group"], life[field])
                                       for field in ("init_script", "pre_script", "main_script", "post_script")}
    floors = {}
    for life in report["lifecycle"]:
        floors.setdefault(life["floor"], []).append(life)
    for event in report["events"]:
        event["script_dependencies"] = [
            {"lifecycle_index": life["index"],
             **script_reference(report["banks"], life["resource_group"], event["script"])}
            for life in floors.get(event["floor"], [])]
        event["binding_status"] = "candidate-contexts" if event["script_dependencies"] else "unknown-floor-context"
    report["unknowns"] = ["All conditional branches retain unknown outcome.",
        "Variable/computed/cross-block native arguments and script destinations require original execution.",
        "Native callbacks are relocation references, not analyzed native call graphs.",
        "Static battle sites are not proof of final-encounter order or mandatory route.",
        "Original flag 0x476 writer must be correlated with taken branches and battle outcomes."]
    report["summary"] = summary(report)
    return report


def script_reference(banks, group, script):
    candidates = [{"bank_index": i, "function": script & 0xFFFF}
                  for i, b in enumerate(banks)
                  if b["archive_group"] in (0, group) and int(b["id"], 16) == script >> 16
                  and (script & 0xFFFF) < len(b["functions"])
                  and b["functions"][script & 0xFFFF]["offset"]]
    return {"script": script, "candidates": candidates,
            "status": "null" if not script else "candidate" if candidates else "unresolved"}


def summary(report):
    banks = report["banks"]
    calls = [s for b in banks for s in b["native_calls"]]
    return {**{k + "_rows": len(report[k]) for k in TABLES},
            "archives": len(report["archives"]), "banks": len(banks),
            "functions": sum(len(b["functions"]) for b in banks), "native_sites": len(calls),
            "unresolved_native_sites": sum(b["unresolved_native_sites"] for b in banks),
            "unresolved_argument_words": sum(b["unresolved_argument_words"] for b in banks),
            "conditional_branches": sum(e["kind"] == "conditional-branch" for b in banks for e in b["control_flow_edges"]),
            "relocated_lifecycle_callbacks": sum(bool(c["relocations"]) for r in report["lifecycle"] for c in r["native_callbacks"].values()),
            "unresolved_lifecycle_script_references": sum(ref["status"] == "unresolved" for life in report["lifecycle"]
                                                         for ref in life["script_dependencies"].values()),
            "flag_476_sites": [{"bank_index": i, "offset": s["offset"], "functions": s["functions"], "native": s["native"],
                                "arguments": s["arguments"]} for i, b in enumerate(banks) for s in b["native_calls"]
                               if s.get("dependency", {}).get("kind", "").startswith("flag-")
                               and s["dependency"]["target"] == 0x476]}


def write_index(output, report):
    if any(p.is_symlink() or (hasattr(p, "is_junction") and p.is_junction())
           for p in (Path(output).absolute(), *Path(output).absolute().parents)):
        raise ValueError("Output path contains a link")
    output = check_output(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".story-index-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        (stage / "story-source-index.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (stage / "summary.json").write_text(json.dumps(report["summary"], indent=2) + "\n", encoding="utf-8")
        if output.exists():
            raise ValueError("Output appeared during indexing")
        os.rename(stage, output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dol", "common", "files", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        check_output(args.output)
        report = build_index(args.dol, args.common, args.files)
        output = write_index(args.output, report)
        print(json.dumps({"output": str(output), "summary": report["summary"]}, indent=2))
    except (OSError, ValueError, struct.error) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
