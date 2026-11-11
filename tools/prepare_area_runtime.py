#!/usr/bin/env python3
"""Encode reviewed original actor/CCD/event metadata as bounded C3A1 packs.

Input is the private area-actors.json emitted by prepare_area.py. Event links
must come from the reviewed original floor/event tables, not inferred triangle
numbers. No original coordinates, dialogue, or assets are stored in this tool.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import os
import shutil
import sys
import tempfile

try:
    from floor_profiles import area_pack, story_floors
except ImportError:  # Loaded by file path: the profile table sits beside this tool.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from floor_profiles import area_pack, story_floors

PIN = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
# Original common REL125 floor rows and the C3A1 file each floor publishes,
# one entry per floor_profiles.py story row.
GROUPS = {row["floor"]: row["resource_group"] for row in story_floors().values()}
PACKS = {row["floor"]: area_pack(row) for row in story_floors().values()}


def _record(value, label):
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _field(record, key):
    try:
        return record[key]
    except KeyError as error:
        raise ValueError(f"Missing original metadata field: {key}") from error


def _list(value, label, maximum=None, exact=None):
    if not isinstance(value, list) or (maximum is not None and len(value) > maximum) or (exact is not None and len(value) != exact):
        raise ValueError(f"Invalid {label} table length")
    return value


def _uint(value, label, maximum=0xFFFFFFFF, minimum=0, text=False):
    if text and isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as error:
            raise ValueError(f"Invalid {label}") from error
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"Invalid {label}")
    return value


def _float(value, label, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not minimum <= value <= maximum:
        raise ValueError(f"Invalid {label}")
    try:
        result = struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error) as error:
        raise ValueError(f"Invalid {label}") from error
    if not math.isfinite(result) or not minimum <= result <= maximum:
        raise ValueError(f"Invalid {label}")
    return result


def _row(rows, floor, label, actor=None):
    matches = []
    for row in _list(rows, label):
        _record(row, label)
        if _uint(_field(row, "floor"), "floor", 65535) == floor:
            if actor is None or _uint(_field(row, "actor"), "actor index", 255) == actor:
                matches.append(row)
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one {label} record for floor {floor}" + (f" actor {actor}" if actor is not None else ""))
    return matches[0]


def encode(area, links, lifecycle, names):
    _record(area, "area")
    floor = _uint(_field(area, "floor"), "floor", 65535, 1)
    if floor not in GROUPS:
        raise ValueError("Unsupported original floor")
    actor_rows = _list(_field(area, "actors"), "actors", maximum=32)
    surface_rows = [_list(_field(area, key), key, maximum=1024)
                    for key in ("transition_surfaces", "interaction_surfaces")]
    selected_links = []
    for link in _list(links, "event links"):
        _record(link, "event link")
        if _uint(_field(link, "floor"), "event floor", 65535) == floor:
            selected_links.append(link)
    _list(selected_links, "event links", maximum=128)
    life = _row(lifecycle, floor, "lifecycle")
    if _uint(_field(life, "resource_group"), "resource group", 65535, 1) != GROUPS[floor] or any(
            _uint(_field(life, key), key) for key in ("pre_native", "main_native", "post_native")):
        raise ValueError("Unsupported original floor lifecycle")
    life_words = _list(_field(life, "raw_words"), "lifecycle words")
    if len(life_words) < 11:
        raise ValueError("Original lifecycle load script is missing")
    scripts = [_uint(life_words[10], "load script")]
    scripts.extend(_uint(_field(life, key), key) for key in ("init_script", "pre_script", "main_script", "post_script"))

    actors = bytearray()
    seen_actors = set()
    for a in actor_rows:
        _record(a, "actor")
        index = _uint(_field(a, "index"), "actor index", 255)
        if index in seen_actors:
            raise ValueError("Duplicate original actor index")
        seen_actors.add(index)
        filename = _field(a, "file")
        if filename is None:
            filename = ""
        if not isinstance(filename, str):
            raise ValueError("Invalid prepared actor basename")
        try:
            name = filename.encode("ascii")
        except UnicodeEncodeError as error:
            raise ValueError("Invalid prepared actor basename") from error
        if len(name) >= 64 or (name and not re.fullmatch(rb"[A-Za-z0-9_-][A-Za-z0-9_.-]*\.c3t", name)) or b".." in name:
            raise ValueError("Invalid prepared actor basename")
        raw = _list(_field(a, "raw_words"), "actor words")
        if len(raw) < 4:
            raise ValueError("Original actor flags are missing")
        position = _list(_field(a, "position"), "actor position", exact=3)
        yaw = _field(a, "yaw_degrees")
        if isinstance(yaw, bool) or not isinstance(yaw, (int, float)) or not -math.degrees(100) <= yaw <= math.degrees(100):
            raise ValueError("Invalid actor yaw")
        values = [_float(x, "actor position", -100000, 100000) for x in position]
        values.extend((_float(math.radians(yaw), "actor yaw", -100, 100),
                       _float(_field(a, "radius"), "actor radius", 0, 100),
                       _float(_field(a, "walk_distance"), "actor walk distance", 0, 100),
                       _float(_field(a, "run_distance"), "actor run distance", 0, 100)))
        if values[4] <= 0:
            raise ValueError("Actor radius must be positive after float32 encoding")
        motions = [_uint(x, "actor motion", 255) for x in _list(_field(a, "motion_indices"), "actor motions", exact=9)]
        words = (index, _uint(_field(a, "model_resource"), "actor resource", minimum=1),
                 _uint(_field(a, "people_info"), "people info", 149), _uint(raw[0], "actor flags"),
                 _uint(_field(a, "move_script"), "move script"), _uint(_field(a, "talk_script"), "talk script"),
                 _uint(raw[2], "actor auxiliary word"), _uint(raw[3], "actor auxiliary word"))
        n = _row(names, floor, "actor name", index)
        name_values = [_uint(_field(n, key), key, limit) for key, limit in
                       (("name_index", 65535), ("hear_flag", 65535), ("name_message", 0xFFFFF),
                        ("talk_start", 3), ("talk_end", 3))]
        row = bytearray(160)
        struct.pack_into("<8I7f", row, 0, *words, *values)
        row[60:69] = bytes(motions)
        info = _field(a, "people_info_raw")
        if not isinstance(info, str) or not re.fullmatch(r"[0-9a-fA-F]{88}", info):
            raise ValueError("Original PeopleInfo record must contain exactly 44 bytes")
        row[69] = (int(info[:2], 16) >> 2) & 3
        row[72:72+len(name)] = name
        struct.pack_into("<5I", row, 136, *name_values)
        parts = _record(a.get("source_part_bones", {}), "prepared source part mapping")
        if any(key != "1" for key in parts):
            raise ValueError("Only reviewed original attachment part1 is prepared")
        if "1" in parts:
            if not name:
                raise ValueError("Prepared attachment part requires its actor model")
            struct.pack_into("<I", row, 156, _uint(parts["1"], "prepared part1 bone", 63)+1)
        actors.extend(row)

    surfaces = []
    for rows in surface_rows:
        table = bytearray()
        for t in rows:
            _record(t, "surface")
            vertices = [_float(x, "surface vertex", -100000, 100000) for x in
                        _list(_field(t, "vertices"), "surface vertices", exact=9)]
            normal = [_float(x, "surface normal", -2, 2) for x in
                      _list(_field(t, "normal"), "surface normal", exact=3)]
            # Match the runtime's float32 accumulation, including boundary rounding.
            norm = 0.0
            for x in normal:
                norm = _float(norm + _float(x*x, "normal square", 0, 4), "normal length", 0, 12)
            if not _float(.99, "normal minimum", 0, 2) <= norm <= _float(1.01, "normal maximum", 0, 2):
                raise ValueError("Surface normal must be a unit vector")
            table.extend(struct.pack("<12f4H", *vertices, *normal,
                                     _uint(_field(t, "flags"), "surface flags", 65535),
                                     _uint(_field(t, "source_id"), "surface source ID", 65535),
                                     _uint(_field(t, "object"), "surface object", 255), 0))
        surfaces.append(table)

    encoded_links = bytearray()
    seen_links = set()
    for link in selected_links:
        kind = _uint(_field(link, "kind"), "event kind", 4, 1)
        source_id = _uint(_field(link, "source_id"), "event source ID")
        if (kind, source_id) in seen_links:
            raise ValueError("Duplicate event kind and source ID")
        seen_links.add((kind, source_id))
        args = _list(_field(link, "args"), "event arguments", exact=4)
        words = []
        for arg in args:
            if isinstance(arg, bool) or not isinstance(arg, int) or not -(1 << 31) <= arg <= 0xFFFFFFFF:
                raise ValueError("Invalid event argument word")
            words.append(arg & 0xFFFFFFFF)
        encoded_links.extend(struct.pack("<8I", kind, source_id,
                                         _uint(_field(link, "script"), "event script", minimum=1, text=True),
                                         *words, _uint(_field(link, "index"), "event index")))

    header = bytearray(128)
    at_trans = 128 + len(actors)
    at_interact = at_trans + len(surfaces[0])
    at_links = at_interact + len(surfaces[1])
    struct.pack_into("<4s15I", header, 0, b"C3A1", 1, at_links+len(encoded_links), floor,
                     GROUPS[floor], len(actors)//160, len(surfaces[0])//56, len(surfaces[1])//56,
                     128, at_trans, at_interact, 160, 56, 0, len(encoded_links)//32, at_links)
    header[64:84] = bytes.fromhex(PIN)
    struct.pack_into("<5I", header, 84, *scripts)
    return bytes(header + actors + surfaces[0] + surfaces[1] + encoded_links)


def prepare(areas, links, lifecycle, names):
    """Validate every requested pack before opening any output file."""
    _list(areas, "areas", maximum=len(GROUPS))
    if not areas:
        raise ValueError("No areas were supplied")
    files, reports, seen = {}, [], set()
    for area in areas:
        data = encode(area, links, lifecycle, names)
        floor = area["floor"]
        if floor in seen:
            raise ValueError("Duplicate original floor")
        seen.add(floor)
        filename = PACKS[floor]
        files[filename] = data
        reports.append({"floor": floor, "file": filename, "bytes": len(data),
                        "sha256": hashlib.sha256(data).hexdigest()})
    return files, reports


def publish(output, files):
    """Publish one complete new directory; leave no partial result on failure."""
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"Output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent))
    try:
        for name, data in files.items():
            (staging / name).write_bytes(data)
        if output.exists() or output.is_symlink():
            raise FileExistsError(f"Output already exists: {output}")
        os.rename(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--links", type=Path, required=True)
    parser.add_argument("--lifecycle", type=Path, required=True)
    parser.add_argument("--names", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        # Hash the same byte snapshots that were parsed, even if a producer is
        # concurrently replacing the input metadata files.
        raw = {key: getattr(args, key).read_bytes() for key in ("metadata", "links", "lifecycle", "names")}
        decoded = {key: json.loads(value) for key, value in raw.items()}
        files, reports = prepare(decoded["metadata"], decoded["links"], decoded["lifecycle"], decoded["names"])
        manifest = {"source_commit": PIN, **{key+"_sha256": hashlib.sha256(value).hexdigest() for key, value in raw.items()},
                    "packs": reports}
        files["area-runtime-manifest.json"] = (json.dumps(manifest, indent=2)+"\n").encode("utf-8")
        publish(args.output, files)
        print(json.dumps(reports, indent=2))
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
