#!/usr/bin/env python3
"""Prepare private WZX (battle move/special effect sequence) packs (sdmc boot/battle3d/).

Every wzx_*.fsys member is parsed generically (header, node table, payloads)
and written as one wx_<group>_<member>.c3w; embedded model DATs become
wxm_<sha1:12>.c3t (C3T3 via prepare_field_geometry, C3Z2), embedded GPT1
banks wxp_<sha1:12>.c3p (tools/prepare_particle.py), embedded camera DATs
a sampled camera block. wzx.c3n indexes the move/override/variant/special
tables of the DOL (lbl_8036E150, lbl_80373750, lbl_80373210, lbl_803727C8)
and waza_sequence_global.fsys. The formats are described in the session
note wzx-formats.md; a node the pipeline cannot convert keeps its record
with a nonzero status (fail closed per node, never substituted).
No game data is in this file; outputs belong under private/.

    python3 tools/prepare_battle_wzx.py --disc <extraction> \
        --common-rel <canonical common_rel.rel> --parser <pinned hsd importer> \
        --output private/battle3d --approximate [--jobs 24] [--skip-existing]
    python3 tools/prepare_battle_wzx.py --verify private/battle3d
"""
import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import struct
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]

# DOL tables (RAM addresses, read through prepare_game_state.Dol).
KIND_TABLE, KIND_STRIDE, KIND_COUNT = 0x8036E150, 28, 0x80478CC0
SPECIAL_TABLE, SPECIAL_COUNT = 0x803727C8, 0x80478CE0
VARIANT_TABLE, VARIANT_STRIDE, VARIANT_COUNT = 0x80373210, 32, 0x80478CE8
OVERRIDE_TABLE, OVERRIDE_ROWS = 0x80373750, 5872 // 16
# fn_801DDEE4 search bands of lbl_80373750 (first, last exclusive) by type.
OVERRIDE_BANDS = {1: (1, 0x11C), 2: (0x11E, 0x12A), 3: (0x12C, 0x15E), 4: (0x160, 0x160)}
OVERRIDE_VARIANT_BAND = (0x162, 0x16E)
MOVE_ROWS, MOVE_STRIDE, MOVE_COUNT, MOVE_WZX = 0x11C7C4, 56, 356, 0x30  # common REL section 125
GLOBAL_ARCHIVE = "waza_sequence_global.fsys"

# Status codes in the node record (+124). 0 = converted / payload ready.
STATUS = {
    0: "ok",
    1: "model_conversion_failed",
    2: "model_no_geometry",
    3: "model_no_dat",
    4: "particle_conversion_failed",
    5: "particle_producer_missing",
    6: "effect_dat_conversion_failed",
    7: "conversion_timeout",
}
KIND_NAMES = {1: "control", 2: "model", 3: "particle", 4: "effect", 5: "sound", 6: "action"}
NODE_BYTES, HEADER_BYTES, CAMERA_SAMPLE = 160, 64, 36
MODEL_RAM_BUDGET = 4_000_000
MAX_SAMPLES = 4096          # C3T LIMITS total frames
MODEL_FPS = 30.0            # GSmodelSetAnimRate 0.5 per 60 Hz tick (wazaSequenceEntry.c)
CAMERA_RATE = 0.5           # GScameraSetAnimRate 0.5 (camera.c cameraPlayOffsetAnime)
TASK_TIMEOUT = 900
EFFECT_DAT_TYPES = (5, 11)
# C3T batch flags (include/colosseum3ds/title.h) that valid_batch() checks.
LIT, SPECULAR, TEV_LERP, DUAL_MODULATE, REFLECTION_BLEND = 16, 128, 256, 1024, 8192
BLEND_FLAGS = 8 | 2048 | 4096  # ADDITIVE, SUBTRACT, ADDITIVE_ONE


def batch_problem(batch, vertices, textures):
    """None when a C3T3/C3T4 batch tuple passes title_renderer.c valid_batch()
    (the stage renderer skips the whole pack otherwise), else the reason."""
    first, count, texture, flags, color, alpha_mode = batch[:6]
    shininess = batch[17]
    ref0, ref1, compare0, compare1, alpha_op, depth, texture2 = batch[20:27]
    blend = flags & BLEND_FLAGS
    second_always = compare1 == 7 or (compare1 == 3 and ref1 == 255)
    if alpha_op != 0 or not ((compare0 == 7 and second_always) or
                             (compare0 == 4 and (second_always or (compare1 == 4 and ref1 <= ref0)))):
        return "alpha test op %d compare %d/%d ref %d/%d" % (alpha_op, compare0, compare1, ref0, ref1)
    if not count or count % 3 or first > vertices or count > vertices - first or texture >= textures:
        return "geometry"
    if color not in (0, 3, 4) or alpha_mode not in (0, 2, 3) or depth > 7 or ref0 > 255 or ref1 > 255:
        return "texture/depth mode"
    if blend & (blend - 1):
        return "blend flags %#x" % blend
    if flags & SPECULAR and (not flags & LIT or flags & (TEV_LERP | DUAL_MODULATE) or blend or
                             not math.isfinite(shininess) or not 0 < shininess <= 512):
        return "specular with flags %#x" % flags
    if flags & REFLECTION_BLEND and (texture2 >= textures or color != 4 or alpha_mode != 0 or not flags & LIT or
                                     flags & (TEV_LERP | DUAL_MODULATE) or not 0 <= batch[27] <= 1):
        return "reflection blend with flags %#x" % flags
    if flags & DUAL_MODULATE and (texture2 >= textures or color != 4 or alpha_mode != 0 or
                                  not flags & TEV_LERP or flags & LIT):
        return "dual modulate with flags %#x" % flags
    return None


def native_specular(meshes):
    """The 3DS lit-specular pass has no TEV-lerp/dual/blend variant: drop the
    specular highlight there (the texture/colour expression is kept)."""
    dropped = 0
    for mesh in meshes:
        batch = mesh["batch"]
        flags = batch[3]
        if flags & SPECULAR and (not flags & LIT or flags & (TEV_LERP | DUAL_MODULATE | BLEND_FLAGS) or
                                 not math.isfinite(batch[17]) or not 0 < batch[17] <= 512):
            batch = list(batch)
            batch[3] = flags & ~SPECULAR
            mesh["batch"] = type(mesh["batch"])(batch)
            dropped += 1
    return dropped


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def bes32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def align32(x):
    return (x + 0x1F) & ~0x1F


# ---------------------------------------------------------------------------
# Generic WZX member parser (grammar: wzx-research.md section 1; parses all 1098).

def node_header(d, o):
    ver = be32(d, o + 0x68)
    if ver not in (1, 2):
        raise ValueError("bad node version %d at %#x" % (ver, o))
    n = dict(off=o, key=bes32(d, o), kind=bes32(d, o + 4), attach=bes32(d, o + 8),
             postype=bes32(d, o + 0xC), link=bes32(d, o + 0x10), src=bes32(d, o + 0x14),
             tgt=bes32(d, o + 0x18), tidx=bes32(d, o + 0x1C),
             timing=[bes32(d, o + 0x20 + 4 * i) for i in range(16)],
             flags=be32(d, o + 0x60), part=bes32(d, o + 0x64), ver=ver,
             state=(bes32(d, o + 0x6C) if ver == 2 else 0))
    return n, o + (0x70 if ver == 2 else 0x6C)


def effect_payload(d, p):
    """Effect (kind 4) payload: (info, blob (offset, size) or None, end)."""
    t = be32(d, p)
    q = p + 0xC
    info = dict(type=t, frames=bes32(d, p + 4))
    blob = None
    if t == 0:
        cnt = bes32(d, q + 4)
        mode = bes32(d, q + 8)
        info.update(target=be32(d, q), keys=cnt)
        end = q + (-4 if mode in (1, 2) else 0) + 0x10 + cnt * 0x10
    elif t in (1, 3):
        blob = (align32(q + 0x54), be32(d, q + 0x4C))
    elif t == 2:
        end = q + 0x1C
    elif t == 4:
        blob = (align32(q + 0x18), be32(d, q + 0x10))
    elif t == 5:
        blob = (align32(q + 0x3C), be32(d, q + 0x34))
    elif t == 6:
        off = -4 if be32(d, q + 0x3C) == 1 else 0
        blob = (align32(q + off + 0x44), be32(d, q + 0x38))
    elif t == 7:
        blob = (align32(q + 0x34), be32(d, q + 0x2C))
    elif t == 8:
        cnt, mode = bes32(d, q + 8), bes32(d, q + 0xC)
        info.update(mode=mode)
        end = q + 0x10 + (cnt * 0x10 if mode == 2 else 0)
    elif t == 9:
        cnt, mode = bes32(d, q), bes32(d, q + 4)
        info.update(mode=mode)
        end = q + {3: 0xC + cnt * 16, 2: 8 + cnt * 16}.get(mode, 8)
    elif t == 10:
        end = q + 0x28
    elif t == 11:
        size = be32(d, q)
        blob = (align32(q + 0x18), size)
    elif t == 12:
        off = -4 if be32(d, q + 0x14) == 1 else 0
        blob = (align32(q + off + 0x1C), be32(d, q + 0x10))
    else:
        raise ValueError("effect type %d" % t)
    if blob is not None:
        end = blob[0] + align32(blob[1])
        if not blob[1]:
            blob = None
    return info, blob, end


def parse_member(d):
    """Parse one sequence member to its exact end; offsets index into d."""
    root, p = node_header(d, 0)
    h = [be32(d, p + 4 * i) for i in range(6)]
    camera = None
    if h[4] == 1:
        nxt = p + 0x10
    elif h[4] == 2:
        nxt = p + 0x14
    else:
        nxt = align32(p + 0x18)
        if h[5]:
            camera = (nxt, h[5])
            nxt += align32(h[5])
    raw_flags = flags = h[2]
    if h[4] <= 3:
        flags |= 0x78
    if h[4] <= 5:
        flags &= ~0x1F8000  # wazaSequenceLoadData clears bits 0x8000..0x100000
    seq = dict(entry=h[0], count=h[1] - 1, raw_flags=raw_flags, flags=flags, part_sel=h[3],
               ver=h[4], camera=camera, nodes=[], root_ver=root["ver"])
    if not 0 <= seq["count"] <= 256:
        raise ValueError("node count %d" % seq["count"])
    for _ in range(seq["count"]):
        n, q = node_header(d, nxt)
        k = n["kind"]
        n["payload_at"] = q
        if k == 1:
            n["sub"], n["value"] = bes32(d, q), bes32(d, q + 4)
            nxt = q + 0xC + (n["value"] * 8 if n["sub"] == 3 else 0)
            n["payload_end"] = nxt
        elif k == 2:
            n.update(anim=bes32(d, q), animtype=bes32(d, q + 4), tex=bes32(d, q + 8),
                     textype=bes32(d, q + 0xC), drive=bes32(d, q + 0x10),
                     drive_part=bes32(d, q + 0x14), mflags=be32(d, q + 0x18))
            size = be32(d, q + 0x1C)
            dat_at = (q + 0x43) & ~0x1F
            n["payload_end"] = q + 0x24
            n["dat"] = (dat_at, size) if size else None
            nxt = dat_at + align32(size)
        elif k == 3:
            n.update(root=bes32(d, q), pflags=be32(d, q + 4), pver=bes32(d, q + 0xC))
            size = be32(d, q + 8)
            nxt = q + (4 if n["pver"] == 3 else 0) + 0x10
            n["payload_end"] = nxt
            n["gpt"] = None
            if n["state"] == 0:
                n["gpt"] = (nxt, size)
                nxt += align32(size)
        elif k == 4:
            info, blob, nxt = effect_payload(d, q)
            n["eff"] = info
            if info["type"] in EFFECT_DAT_TYPES:
                n["dat"] = blob
                n["payload_end"] = blob[0] if blob else nxt
            else:
                n["dat"] = None
                n["payload_end"] = nxt
        elif k == 5:
            n["se"], n["smode"] = be32(d, q), bes32(d, q + 4)
            nxt = q + (-4 if n["smode"] in (1, 2) else 0) + 0xC
            n["payload_end"] = nxt
        elif k == 6:
            n["action"] = bes32(d, q)
            nxt = q + 8
            n["payload_end"] = nxt
        else:
            raise ValueError("node kind %d" % k)
        if nxt > len(d):
            raise ValueError("node overruns member")
        seq["nodes"].append(n)
    seq["end"], seq["size"] = nxt, len(d)
    if align32(nxt) < len(d) - 32 or nxt > len(d):
        raise ValueError("parse ended at %#x of %#x" % (nxt, len(d)))
    return seq


def fsys_identities(archive):
    count = be32(archive, 12)
    table = be32(archive, be32(archive, 24))
    return [be32(archive, be32(archive, table + i * 4)) for i in range(count)]


def member_name(group, resource):
    return "wx_%03X_%08X.c3w" % (group, resource)


# ---------------------------------------------------------------------------
# Binary writers.

def encode_member(seq, records, camera_samples):
    """records: per node dict(payload bytes, file, status, clips, bones, joint_map)."""
    nodes_at = HEADER_BYTES
    cursor = nodes_at + NODE_BYTES * len(records)
    camera_at = cursor if camera_samples else 0
    cursor += CAMERA_SAMPLE * len(camera_samples)
    payload_at = []
    for r in records:
        cursor = (cursor + 3) & ~3
        payload_at.append(cursor)
        cursor += len(r["payload"])
    map_at = []
    for r in records:
        cursor = (cursor + 3) & ~3
        map_at.append(cursor if r["joint_map"] else 0)
        cursor += 2 * len(r["joint_map"])
    cursor = (cursor + 3) & ~3
    strings = bytearray(b"\0")
    name_off = []
    for r in records:
        if r["file"]:
            name_off.append(len(strings))
            strings += r["file"].encode("ascii") + b"\0"
        else:
            name_off.append(0)
    strings_at = cursor
    total = (strings_at + len(strings) + 3) & ~3
    out = bytearray(total)
    struct.pack_into("<4s11I", out, 0, b"C3WX", 1, total, len(records), seq["entry"], seq["flags"],
                     seq["part_sel"], seq["ver"], nodes_at, strings_at, camera_at, len(camera_samples))
    struct.pack_into("<2I", out, 48, seq["raw_flags"], seq["size"])
    for i, (n, r) in enumerate(zip(seq["nodes"], records)):
        at = nodes_at + i * NODE_BYTES
        struct.pack_into("<8I", out, at, *[x & 0xFFFFFFFF for x in (
            n["key"], n["kind"], n["attach"], n["postype"], n["link"], n["src"], n["tgt"], n["tidx"])])
        struct.pack_into("<16i", out, at + 32, *n["timing"])
        struct.pack_into("<4I", out, at + 96, n["flags"], n["part"] & 0xFFFFFFFF, n["ver"],
                         n["state"] & 0xFFFFFFFF)
        struct.pack_into("<4I", out, at + 112, payload_at[i], len(r["payload"]), name_off[i], r["status"])
        struct.pack_into("<4I", out, at + 128, r["clips"], r["bones"], map_at[i], len(r["joint_map"]))
        out[payload_at[i]:payload_at[i] + len(r["payload"])] = r["payload"]
        if r["joint_map"]:
            struct.pack_into("<%dH" % len(r["joint_map"]), out, map_at[i], *r["joint_map"])
    for i, sample in enumerate(camera_samples):
        struct.pack_into("<9f", out, camera_at + i * CAMERA_SAMPLE, *sample[:8], 0.)
    out[strings_at:strings_at + len(strings)] = strings
    return bytes(out)


def encode_index(rows):
    """rows: (category, kind, id, pkx, variant, pack name)."""
    rows = sorted(set(rows), key=lambda r: (r[0], r[2], r[1], r[4], r[3]))
    names, offsets = bytearray(), {}
    for r in rows:
        if r[5] not in offsets:
            offsets[r[5]] = len(names)
            names += r[5].encode("ascii") + b"\0"
    rows_at = 32
    names_at = rows_at + 16 * len(rows)
    out = bytearray(names_at + len(names))
    struct.pack_into("<4s4I", out, 0, b"C3WI", 1, len(rows), rows_at, names_at)
    for i, (cat, kind, ident, pkx, variant, name) in enumerate(rows):
        struct.pack_into("<BBHHHII", out, rows_at + 16 * i, cat, kind, ident, pkx, variant, offsets[name], 0)
    out[names_at:] = names
    return bytes(out)


# ---------------------------------------------------------------------------
# Readers (reusable for checks; no game knowledge needed).

def read_index(data):
    magic, version, count, rows_at, names_at = struct.unpack_from("<4s4I", data, 0)
    if magic != b"C3WI" or version != 1 or rows_at + 16 * count > names_at or names_at > len(data):
        raise ValueError("Invalid wzx.c3n header")
    rows = []
    for i in range(count):
        cat, kind, ident, pkx, variant, name_off, _ = struct.unpack_from("<BBHHHII", data, rows_at + 16 * i)
        at = names_at + name_off
        end = data.index(b"\0", at)
        rows.append(dict(category=cat, kind=kind, id=ident, pkx=pkx, variant=variant,
                         pack=data[at:end].decode("ascii")))
    return rows


def read_member(data):
    (magic, version, total, count, entry, flags, part_sel, seq_ver, nodes_at, strings_at,
     camera_at, camera_count) = struct.unpack_from("<4s11I", data, 0)
    if magic != b"C3WX" or version != 1 or total != len(data):
        raise ValueError("Invalid C3WX header")
    if nodes_at != HEADER_BYTES or nodes_at + count * NODE_BYTES > len(data) or strings_at > len(data):
        raise ValueError("C3WX node/string table outside file")
    if camera_count and camera_at + camera_count * CAMERA_SAMPLE > len(data):
        raise ValueError("C3WX camera block outside file")
    raw_flags, member_bytes = struct.unpack_from("<2I", data, 48)

    def string(off):
        if not off:
            return ""
        at = strings_at + off
        return data[at:data.index(b"\0", at)].decode("ascii")
    nodes = []
    for i in range(count):
        at = nodes_at + i * NODE_BYTES
        key, kind, attach, postype, link, src, tgt, tidx = struct.unpack_from("<8i", data, at)
        timing = list(struct.unpack_from("<16i", data, at + 32))
        nflags, part, node_ver, state = struct.unpack_from("<IiIi", data, at + 96)
        payload_at, payload_bytes, name_off, status = struct.unpack_from("<4I", data, at + 112)
        clips, bones, map_at, map_count = struct.unpack_from("<4I", data, at + 128)
        if payload_at + payload_bytes > len(data) or (map_count and map_at + 2 * map_count > len(data)):
            raise ValueError("C3WX node %d extent outside file" % i)
        nodes.append(dict(key=key, kind=kind, attach=attach, postype=postype, link=link, src=src,
                          tgt=tgt, tidx=tidx, timing=timing, flags=nflags, part=part, node_ver=node_ver,
                          state=state, payload=data[payload_at:payload_at + payload_bytes],
                          file=string(name_off), status=status, clips=clips, bones=bones,
                          joint_map=list(struct.unpack_from("<%dH" % map_count, data, map_at))
                          if map_count else []))
    camera = [struct.unpack_from("<9f", data, camera_at + i * CAMERA_SAMPLE)[:8] for i in range(camera_count)]
    return dict(entry=entry, flags=flags, raw_flags=raw_flags, part_sel=part_sel, seq_version=seq_ver,
                member_bytes=member_bytes, nodes=nodes, camera=camera)


def unpack_c3z(data):
    if data[:4] == b"C3Z2":
        import subprocess
        raw = subprocess.run(["zstd", "-q", "-d", "-c"], input=data[8:], stdout=subprocess.PIPE,
                             check=True).stdout
    elif data[:4] == b"C3Z1":
        import zlib
        raw = zlib.decompress(data[8:])
    else:
        return data
    if len(raw) != struct.unpack_from("<I", data, 4)[0]:
        raise ValueError("Compressed pack raw size mismatch")
    return raw


def read_c3t_header(data):
    raw = unpack_c3z(data)
    magic, version, total = struct.unpack_from("<4s2I", raw, 0)
    bones, vertices, batches, textures, clips, frames, lights = struct.unpack_from("<7I", raw, 12)
    fps = struct.unpack_from("<f", raw, 40)[0]
    offsets = struct.unpack_from("<7I", raw, 44)
    if magic not in (b"C3T3", b"C3T4") or total != len(raw):
        raise ValueError("Invalid C3T header/size")
    if not (0 < bones <= 64 and 0 < vertices <= 32768 and 0 < batches <= 64 and 0 < textures <= 32 and
            0 < clips <= 32 and 0 < frames <= MAX_SAMPLES and lights <= 8 and 1 <= fps <= 120):
        raise ValueError("C3T counts outside bounds")
    if list(offsets) != sorted(offsets) or offsets[-1] > total:
        raise ValueError("C3T section offsets outside pack")
    next_vertex = 0
    for i, batch in enumerate(struct.iter_unpack("<8I12f7If", raw[offsets[1]:offsets[1] + 112 * batches])):
        problem = batch_problem(batch, vertices, textures) or (batch[0] != next_vertex and "partition")
        if problem:
            raise ValueError("C3T batch %d unsupported by the stage renderer: %s" % (i, problem))
        next_vertex += batch[1]
    if next_vertex != vertices:
        raise ValueError("C3T batches do not partition the vertices")
    return dict(magic=magic.decode(), raw_bytes=len(raw), bones=bones, vertices=vertices, batches=batches,
                textures=textures, clips=clips, frames=frames, lights=lights, fps=fps)


def read_c3p_header(data):
    magic, version, total, root, bank_at, bank_bytes, textures, table = struct.unpack_from("<4s7I", data, 0)
    roots = struct.unpack_from("<I", data, 96)[0] if version == 2 else 1
    if magic != b"C3PT" or version not in (1, 2) or total != len(data) or bank_at + bank_bytes > total or \
            data[bank_at:bank_at + 4] != b"GPT1" or not 1 <= roots <= 32 or \
            table + 32 * textures + (4 * roots if version == 2 else 0) > bank_at:
        raise ValueError("Invalid C3PT header/extent")
    for i in range(textures):
        _, _, w, h, at, size, _, _ = struct.unpack_from("<8I", data, table + 32 * i)
        if at + size > total or size != w * h * 4:
            raise ValueError("C3PT texture outside pack")
    return dict(version=version, root=root, roots=roots, bank_bytes=bank_bytes, textures=textures)


def verify(output):
    """Re-read every file wzx.json lists; returns (problems, counts)."""
    output = Path(output)
    report = json.loads((output / "wzx.json").read_text())
    problems, counts = [], collections.Counter()
    rows = read_index((output / "wzx.c3n").read_bytes())
    counts["index_rows"] = len(rows)
    for row in rows:
        if not (output / row["pack"]).is_file():
            problems.append("index row %r: missing %s" % (row, row["pack"]))
    checked = {}
    for name, item in report["members"].items():
        path = output / name
        if not path.is_file():
            problems.append("missing member " + name)
            continue
        try:
            member = read_member(path.read_bytes())
        except ValueError as error:
            problems.append("%s: %s" % (name, error))
            continue
        counts["members"] += 1
        if len(member["nodes"]) != item["node_count"]:
            problems.append("%s: node count %d != parse %d" % (name, len(member["nodes"]), item["node_count"]))
        if len(member["camera"]) != item.get("camera_samples", 0):
            problems.append("%s: camera samples differ" % name)
        for i, node in enumerate(member["nodes"]):
            counts["nodes"] += 1
            if node["status"] not in STATUS:
                problems.append("%s node %d: unknown status %d" % (name, i, node["status"]))
            if not node["file"]:
                continue
            if node["file"] not in checked:
                fpath = output / node["file"]
                if not fpath.is_file():
                    checked[node["file"]] = "missing"
                else:
                    try:
                        data = fpath.read_bytes()
                        if node["file"].endswith(".c3t"):
                            checked[node["file"]] = read_c3t_header(data)
                            counts["model_packs"] += 1
                        else:
                            checked[node["file"]] = read_c3p_header(data)
                            counts["particle_packs"] += 1
                    except (ValueError, struct.error) as error:
                        checked[node["file"]] = "invalid: %s" % error
            state = checked[node["file"]]
            if node["status"] == 0 and isinstance(state, str):
                problems.append("%s node %d: %s %s" % (name, i, node["file"], state))
            if node["status"] == 0 and node["file"].endswith(".c3t") and isinstance(state, dict):
                if node["bones"] != state["bones"] or node["clips"] != state["clips"]:
                    problems.append("%s node %d: bone/clip count differs from pack" % (name, i))
                if any(b != 0xFFFF and b >= state["bones"] for b in node["joint_map"]):
                    problems.append("%s node %d: joint map outside pack bones" % (name, i))
    return problems, counts


# ---------------------------------------------------------------------------
# Workers (fork: every process imports the pinned parser once).

CTX = {}


def worker_init(parser, approximate):
    import prepare_field_geometry as geometry
    import prepare_title as title
    import prepare_battle_stages as stages
    geometry.import_parser(parser)
    geometry.APPROXIMATE["enabled"] = approximate
    if approximate:
        stages.install_singular_sampler(geometry)
    title.APPROXIMATE_HOOK = geometry.approximate if approximate else None
    CTX.update(geometry=geometry, title=title, stages=stages, approximate=approximate, shape_sets=0)
    install_shape_set_reader()


def install_shape_set_reader():
    """The pinned parser's ShapeSet (POBJ shape animation) reader builds array
    types bounded by field names its type parser cannot resolve ("Array field
    with unknown variable name: shape_count"), failing the whole DAT. The
    describe phase attaches SHAPEANIM POBJs rigidly and never reads the shape
    targets, so keep the header fields only: the base shape is drawn (shape
    blending is not converted) and each such PObj is counted per model.
    Its ShapeAnimationJoint tree also skips HSD's ShapeAnimDObj level (it
    reads the ShapeAnim pointer as an AObjDesc and follows garbage as a
    Joint); nothing reads shape animations, so that pointer stays raw."""
    from shared.Nodes.Classes.Shape.ShapeSet import ShapeSet
    from shared.Nodes.Classes.Shape.ShapeAnimation import ShapeAnimation
    from shared.Nodes.Node import Node
    if getattr(ShapeSet, "_wzx_header_only", False):
        return
    ShapeAnimation.fields = [(n, "uint" if n == "animation" else t) for n, t in ShapeAnimation.fields]

    def load(self, parser):
        Node.loadFromBinary(self, parser)
        self.vertex_set, self.normal_set = [], []
        CTX["shape_sets"] = CTX.get("shape_sets", 0) + 1
    ShapeSet.loadFromBinary = load
    ShapeSet._wzx_header_only = True


class Timeout(Exception):
    pass


def _alarm(signum, frame):
    raise Timeout()


def run_task(task):
    signal.signal(signal.SIGALRM, _alarm)
    signal.alarm(TASK_TIMEOUT)
    started = time.time()
    try:
        if task["type"] == "model":
            result = convert_model(task)
        else:
            result = convert_bank(task)
    except Timeout:
        result = dict(ok=False, status=7, reason="timeout after %d s" % TASK_TIMEOUT)
    except Exception as error:  # noqa: BLE001 - recorded per node, never substituted
        result = dict(ok=False, status=1 if task["type"] == "model" else 4,
                      reason="%s: %s" % (type(error).__name__, error),
                      trace=traceback.format_exc(limit=4))
    finally:
        signal.alarm(0)
    result.update(name=task["name"], type=task["type"], seconds=round(time.time() - started, 2))
    return result


def parse_dat(dat):
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    options = dict(filepath="wzx.dat", game="COLO_XD", max_frame=1000000000, section_names=[], ik_hack=False)
    sections = parse_sections(dat, route_sections(dat), options)
    scenes = [s for s in sections if s.section_name == "scene_data"]
    if len(scenes) != 1:
        raise ValueError("Expected one scene_data root, found %s" % [s.section_name for s in sections])
    return scenes[0].root_node, describe_scene(scenes, options)


def material_end(raw_model):
    title = CTX["title"]
    ends = [0.]
    for root in raw_model.animated_material_joints or []:
        for node in title.tree_nodes(root):
            material = node.animation
            while material:
                if material.animation:
                    ends.append(material.animation.end_frame)
                texture = material.texture_animation
                while texture:
                    if texture.animation:
                        ends.append(texture.animation.end_frame)
                    texture = texture.next
                material = material.next
    return max(ends)


def valid_joints(model, states):
    import numpy as np
    good = []
    for j in range(len(model.joints)):
        track = np.asarray([world[j] for world, _ in states])
        if np.isfinite(track).all() and np.min(np.abs(np.linalg.det(track[:, :3, :3]))) >= 1e-9 \
                and np.max(np.abs(track)) < 1e5:
            good.append(j)
    return good


def convert_model(task):
    """One embedded HSD DAT -> one C3T3 pack (clip 0 = selected joint anim with
    the selected texture/material animation), with retained source joints."""
    geometry, stages, approximate = CTX["geometry"], CTX["stages"], CTX["approximate"]
    from prepare_pda_models import omit_collapsed_meshes
    output = Path(task["output"])
    name = task["name"]
    CTX["shape_sets"] = 0
    root, scene = parse_dat(task["dat"])
    shape_sets = CTX["shape_sets"]
    if not scene.models:
        return dict(ok=False, status=2, reason="DAT has no model (cameras %d, lights %d)" % (
            len(scene.cameras), len(scene.lights)))
    notes = []
    if len(scene.models) > 1:
        notes.append("%d models in DAT, model 0 converted" % len(scene.models))
    raw_model = root.models[0]
    joints_all = list(raw_model.animated_joints or [])
    materials_all = list(raw_model.animated_material_joints or [])
    anim, tex = task["anim"], task["tex"]
    raw_model.animated_joints = [joints_all[anim]] if 0 <= anim < len(joints_all) else []
    raw_model.animated_material_joints = [materials_all[tex]] if 0 <= tex < len(materials_all) else []
    if anim >= len(joints_all):
        notes.append("joint animation %d of %d: rest pose" % (anim, len(joints_all)))
    if tex >= len(materials_all):
        notes.append("texture animation %d of %d: none" % (tex, len(materials_all)))
    before = collections.Counter(geometry.APPROXIMATIONS)
    fixes = stages.actor_source_fixes(scene.models[0], raw_model) if approximate else collections.Counter()
    options = ({"frozen_material_colors": True, "allow_paths": True, "allow_instances": True}
               if approximate else {})
    model = None
    for _ in range(3):
        try:
            model = geometry.Model(scene.models[0], raw_model, allow_image_swaps=True, **options)
            break
        except ValueError as error:
            if approximate and "path" in str(error):
                fixes = collections.Counter(fixes) + stages.strip_joint_paths(raw_model)
            elif approximate and "channel: 40" in str(error):
                # A repeated type-40 particle callback track (not a transform).
                fixes = collections.Counter(fixes) + strip_joint_channel(raw_model, 40)
            else:
                raise
    if model is None:
        raise ValueError("model rejected after channel fixes")
    approximations = collections.Counter(fixes)
    if shape_sets:
        approximations["shape_animation_base_shape_only"] += shape_sets
    held = stages.hold_texture_blend(model) if approximate else 0
    if held:
        approximations["texture_blend_animation_held_at_rest"] += held
    if getattr(model, "frozen_material_tracks", None):
        approximations["frozen_material_colors"] += 1
    if not model.meshes:
        return joint_only_pack(task, model, raw_model, approximations, notes)
    built, material_notes = [], []
    for i in range(len(model.meshes)):
        try:
            mesh, note = stages.build_actor_mesh(geometry, model, i, approximate)
        except ValueError as error:
            if not approximate:
                raise
            approximations["mesh_dropped"] += 1
            material_notes.append("mesh %d dropped: %s" % (i, error))
            continue
        built.append(mesh)
        if note:
            material_notes.append(note)
    for note in material_notes:
        for phrase, label in (("source material", "source_material_dropped"),
                              ("base texture layer only", "base_texture_layer_only"),
                              ("second texture stage dropped", "second_texture_stage_dropped")):
            if phrase in note:
                approximations[label] += 1
    if not built:
        return dict(ok=False, status=1, reason="no mesh could be built: " + "; ".join(material_notes[:3]))
    specular = native_specular(built)
    if specular:
        if not approximate:
            raise ValueError("specular material outside the native lit-specular subset")
        approximations["specular_dropped_on_tev_lerp_or_blend"] += specular
    joint_end = model.ends[0] if model.ends else 0.
    end = max(joint_end, material_end(raw_model))
    camera, lights = task["camera"], task["lights"]
    referenced = sorted(task["referenced"])
    rates = [1, 0.5, 0.25, 0.125]
    reductions, result = [], None
    for attempt, rate in enumerate(rates):
        frames, uneven = stages.resample(end, rate)
        if len(frames) > MAX_SAMPLES:
            reductions.append({"rate": rate, "samples": len(frames), "reason": "sample bound"})
            continue
        model.__dict__.pop("_collapsed_world_descriptors", None)
        stages.HIDDEN_SINGULAR.clear()
        schedules = [(0, frames, False)]
        pairs = [(0, f) for f in frames]
        states = [model.world(0, f) for f in frames]
        meshes = geometry.expand_image_swaps([dict(m) for m in built])
        meshes, omitted = omit_collapsed_meshes(meshes, states)
        if not meshes:
            return dict(ok=False, status=2, reason="every mesh collapsed over the clip", notes=notes)
        batches = len(meshes)
        if batches > 64:
            meshes = stages.merge_batches(meshes)
        hidden = 0
        if approximate:
            meshes, hidden = stages.hide_singular(meshes, states, pairs)
        merged = stages.merge_tracks(geometry, meshes, states, approximate)
        stages.mark_collapsed(meshes, states, pairs)
        for mesh in meshes:
            mesh["collapsed_samples"] = mesh.get("collapsed_samples", set()) | mesh.pop("singular_samples", set())
        good = valid_joints(model, states)
        candidates = [("all", good), ("referenced", [j for j in referenced if j in good]), ("none", [])]
        written, kept, last_error = None, None, None
        for label, joints in candidates:
            if label == "referenced" and joints == good:
                continue
            for path in output.glob(name + "*"):
                path.unlink()
            try:
                written = geometry.prepare_pack_set(
                    meshes, schedules, MODEL_FPS * rate, [camera] * len(frames), lights,
                    task["dat_sha256"], output, name, expected_count=1, format_version=3,
                    retained_parts=[(model, j, j) for j in joints], fit_first_chunk=approximate)
                kept = label
                break
            except ValueError as error:
                last_error = error
                if "partition" not in str(error) and "Retained" not in str(error) and \
                        "bound" not in str(error) and "exceeds" not in str(error):
                    raise
        if written is None:
            raise last_error
        pack = written[0]
        path = output / pack["file"]
        raw_bytes = path.stat().st_size
        if raw_bytes > MODEL_RAM_BUDGET and attempt < len(rates) - 1 and approximate:
            reductions.append({"rate": rate, "raw_bytes": raw_bytes, "reason": "ram budget"})
            path.unlink()
            continue
        result = dict(pack=pack, rate=rate, frames=len(frames), kept=kept, joints=good, merged=merged,
                      hidden=hidden, omitted=omitted, batches=batches, uneven=uneven, raw_bytes=raw_bytes)
        break
    if result is None:
        raise ValueError("model exceeds the sample bound at every rate")
    approximations += stages_used(geometry, before)
    if result["merged"]:
        approximations["skin_tracks_merged"] += 1
    if result["hidden"]:
        approximations["singular_joints_hidden"] += result["hidden"]
    if result["rate"] != 1:
        approximations["model_sampled_below_authored_rate"] += 1
    if referenced and result["kept"] == "none":
        approximations["referenced_joints_not_retained"] += 1
    elif result["kept"] == "referenced" or (result["kept"] == "all" and
                                             any(j not in result["joints"] for j in referenced)):
        if any(j not in result["joints"] for j in referenced):
            approximations["referenced_joint_singular_not_retained"] += 1
    import compress_packs
    path = output / result["pack"]["file"]
    compress_packs.compress_directory(output, pattern=path.name, max_texture=task["max_texture"])
    stored = path.read_bytes()
    decoded = compress_packs.decompress_pack(stored)
    pack = result["pack"]
    joint_map = [0xFFFF] * len(model.joints)
    for part in pack["retained_parts"]:
        joint_map[part["source_joint"]] = part["sampled_bone"]
    return dict(ok=True, status=0, file=path.name, raw_bytes=result["raw_bytes"],
                repacked_bytes=len(decoded), stored_bytes=len(stored),
                sha256=hashlib.sha256(stored).hexdigest(), bones=pack["sampled_bones"], clips=1,
                vertices=pack["vertices"], batches=pack["batches"], textures=pack["textures"],
                sample_fps=pack["sample_fps"], samples=result["frames"], authored_end=end,
                joint_end=joint_end, joints=len(model.joints), joint_map=joint_map,
                retained=result["kept"], referenced=referenced, notes=notes + material_notes,
                omitted_zero_surface_meshes=result["omitted"], size_reductions=reductions,
                approximations=dict(approximations))


def strip_joint_channel(raw_model, channel):
    title = CTX["title"]
    used = collections.Counter()
    for root in getattr(raw_model, "animated_joints", None) or []:
        for node in title.tree_nodes(root):
            aobj = node.animation
            if aobj is None:
                continue
            previous, fobj = None, aobj.frame
            while fobj:
                if fobj.type == channel:
                    used["joint_channel_%d_dropped" % channel] += 1
                    if previous is None:
                        aobj.frame = fobj.next
                    else:
                        previous.next = fobj.next
                else:
                    previous = fobj
                fobj = fobj.next
    return used


def joint_only_pack(task, model, raw_model, approximations, notes):
    """A DAT without meshes (owner-driving / motion resources): a C3T3 whose
    bones are the retained source joints (model-space world 3x4 per sample) and
    whose one batch is a hidden degenerate triangle (C3T needs one)."""
    import numpy as np
    title, stages, geometry = CTX["title"], CTX["stages"], CTX["geometry"]
    import compress_packs
    end = max(model.ends[0] if model.ends else 0., material_end(raw_model))
    for rate in (1, 0.5, 0.25, 0.125):
        frames, _ = stages.resample(end, rate)
        if len(frames) <= MAX_SAMPLES:
            break
    states = [model.world(0, f) for f in frames]
    joints = valid_joints(model, states)[:64]
    tracks = [np.asarray([world[j] for world, _ in states])[:, :3, :].astype("<f4") for j in joints]
    if not joints and CTX["approximate"]:
        # Joints singular only in some samples (a scale-in from 0): those
        # samples keep the nearest invertible sample's axes at 1/100 scale.
        for j in range(len(model.joints)):
            track = np.asarray([world[j] for world, _ in states])[:, :3, :].astype("<f8")
            dets = np.abs(np.linalg.det(track[:, :, :3])) if np.isfinite(track).all() else None
            if dets is None or np.max(np.abs(track)) >= 1e5 or not (dets >= 1e-9).any():
                continue
            good = np.flatnonzero(dets >= 1e-9)
            for i in np.flatnonzero(dets < 1e-9):
                nearest = good[np.argmin(np.abs(good - i))]
                axes = track[nearest, :, :3]
                track[i, :, :3] = axes * (0.01 / np.cbrt(abs(np.linalg.det(axes))))
            joints.append(j)
            tracks.append(track.astype("<f4"))
            approximations["singular_joint_samples_scaled"] += len(frames) - len(good)
            if len(joints) == 64:
                break
    if not joints:
        return dict(ok=False, status=2, reason="no meshes and no invertible joint (%d joints)" % len(model.joints),
                    joints=len(model.joints), notes=notes)
    vertex = (0., 0., 0., 0., 1., 0., 0., 0., 255, 255, 255, 0, 0, 0., 0.)
    # Alpha test ALWAYS/ALWAYS and LEQUAL depth (prepare_title's defaults): the
    # stage renderer rejects a pack whose batch has an unsupported alpha test.
    batch = (0, 3, 0, 0, 0, 0, 0, 0, 1., 1., 1., 1., 1., 1., 0., 0., 0., 0., 0., 1., 0, 0, 7, 7, 0, 3, 0, 0.)
    camera = struct.pack("<12f", *task["camera"])
    sampled = bytearray()
    for i in range(len(frames)):
        for track in tracks:
            sampled += track[i].tobytes()
        source = tracks[0][i][:, 3]
        sampled += struct.pack("<24f", 1., 1., 0., 0., 0., 0., 0., 0., 0., 1., 1., 1., 1., 1., 0., 0.,
                               0., 0., 0., 0., float(source[0]), float(source[1]), float(source[2]), 0.)
        sampled += camera
    pack = title.encode_pack(len(tracks), [vertex] * 3, [batch], [(8, 8, bytes(256), 0, 0)],
                             [(0, len(frames), 0, 0)], task["lights"], bytes(sampled),
                             fps=MODEL_FPS * rate, format_version=3, archive_sha256=task["dat_sha256"])
    path = Path(task["output"]) / (task["name"] + ".c3t")
    path.write_bytes(pack)
    compress_packs.compress_directory(path.parent, pattern=path.name, max_texture=task["max_texture"])
    stored = path.read_bytes()
    joint_map = [0xFFFF] * len(model.joints)
    for index, j in enumerate(joints):
        joint_map[j] = index
    if rate != 1:
        approximations["model_sampled_below_authored_rate"] += 1
    return dict(ok=True, status=0, file=path.name, raw_bytes=len(pack),
                repacked_bytes=len(compress_packs.decompress_pack(stored)), stored_bytes=len(stored),
                sha256=hashlib.sha256(stored).hexdigest(), bones=len(tracks), clips=1, vertices=3, batches=1,
                textures=1, sample_fps=MODEL_FPS * rate, samples=len(frames), authored_end=end,
                joint_end=model.ends[0] if model.ends else 0., joints=len(model.joints), joint_map=joint_map,
                retained="all", referenced=sorted(task["referenced"]), joint_only=True,
                notes=notes + ["no meshes: joint-only pack (one hidden degenerate triangle)"],
                approximations=dict(approximations))


def stages_used(geometry, since):
    now = geometry.APPROXIMATIONS
    return collections.Counter({k: now[k] - since.get(k, 0) for k in now if now[k] != since.get(k, 0)})


def convert_bank(task):
    """One GPT1 bank -> C3PT. All roots launched from it are admitted together;
    when the closure fails, each root is tried alone and the pack keeps the
    roots that pass (the others' nodes carry the failure)."""
    import inspect
    import prepare_particle as particle
    data, roots = task["gpt"], sorted(task["roots"])
    if hasattr(particle, "general_bank"):
        return convert_bank_general(task, particle, data, roots)
    extra = "additional_roots" in inspect.signature(particle.bank).parameters

    def admit(selected):
        if extra:
            return particle.bank(data, selected[0], tuple(selected[1:]))
        if len(selected) != 1:
            raise ValueError("converter admits one root per bank")
        return particle.bank(data, selected[0])
    failures, passing, report = {}, [], None
    try:
        report = admit(roots)
        passing = list(roots)
    except Exception as error:  # noqa: BLE001
        if len(roots) == 1:
            failures[roots[0]] = "%s" % error
        else:
            for root in roots:
                try:
                    admit([root])
                    passing.append(root)
                except Exception as single:  # noqa: BLE001
                    failures[root] = "%s" % single
            if passing:
                try:
                    report = admit(passing)
                except Exception as error:  # noqa: BLE001 - combined closure over a bound
                    for root in passing[1:]:
                        failures[root] = "closure with root %d: %s" % (passing[0], error)
                    passing = passing[:1]
                    report = admit(passing)
    if not passing:
        return dict(ok=False, status=4, reason=failures[roots[0]], failures=failures, roots=roots)
    packed = particle.encode(data, report, hashlib.sha256(data).hexdigest())
    path = Path(task["output"]) / task["name"]
    path.write_bytes(packed)
    return dict(ok=True, status=0, file=path.name, stored_bytes=len(packed), roots=roots,
                passing_roots=passing, failures=failures, closure=report["closure"],
                textures=len(report["textures"]), sha256=hashlib.sha256(packed).hexdigest())


def convert_bank_general(task, particle, data, roots):
    """prepare_particle's general path (C3PT v2, per-generator fail-closed),
    as its batch(): each root admitted alone, then one pack for the union."""
    failures, passing = {}, []
    for root in roots:
        try:
            particle.general_bank(data, [root], decode=False)
            passing.append(root)
        except ValueError as error:
            failures[root] = str(error)
    if not passing:
        return dict(ok=False, status=4, reason=failures[roots[0]], failures=failures, roots=roots,
                    converter="general")
    try:
        report = particle.general_bank(data, passing)
    except ValueError as error:
        for root in passing[1:]:
            failures[root] = "pack with root %d: %s" % (passing[0], error)
        passing = passing[:1]
        report = particle.general_bank(data, passing)
    packed = particle.encode_general(data, report, hashlib.sha256(data).hexdigest())
    path = Path(task["output"]) / task["name"]
    path.write_bytes(packed)
    return dict(ok=True, status=0, file=path.name, stored_bytes=len(packed), roots=roots,
                passing_roots=passing, failures=failures, closure=report["closure"],
                disabled_generators=report.get("disabled", {}), textures=len(report["textures"]),
                converter="general", sha256=hashlib.sha256(packed).hexdigest())


# ---------------------------------------------------------------------------
# Driver.

class Run:
    def __init__(self, args):
        sys.path.insert(0, str(ROOT / "tools"))
        from prepare_boot_media import verify_disc
        from prepare_game_state import Dol
        from prepare_battle_scene import archive_names
        from prepare_outskirt import rel_data
        import prepare_battle_stages as stages
        import prepare_title as title
        self.args = args
        self.disc = verify_disc(args.disc)
        self.parser = title.verify_parser(args.parser)
        self.common = stages.admit_common(args.common_rel)
        self.output = Path(args.output).resolve()
        if (self.output == ROOT or ROOT in self.output.parents) and ROOT / "private" not in self.output.parents:
            raise ValueError("Output inside the repository must be under ignored private/")
        self.output.mkdir(parents=True, exist_ok=True)
        dol_bytes = (self.disc / "sys/main.dol").read_bytes()
        self.dol = Dol(dol_bytes)
        self.toc = (self.disc / "files/GSfsys.toc").read_bytes()
        self.names = archive_names(self.toc)
        self.groups = {v: k for k, v in self.names.items()}
        self.rel = rel_data(self.common, 125)
        self.stages = stages
        self.dol_bytes = dol_bytes

    def word(self, address):
        return be32(self.dol.read(address, 4), 0)

    # Tables ------------------------------------------------------------------
    def tables(self):
        kind_count, special_count = self.word(KIND_COUNT), self.word(SPECIAL_COUNT)
        variant_count = self.word(VARIANT_COUNT)
        kinds = {}
        for w in range(1, kind_count):
            row = self.dol.read(KIND_TABLE + w * KIND_STRIDE, KIND_STRIDE)
            kinds[w] = {k: struct.unpack_from(">II", row, 4 + (k - 1) * 8) for k in (1, 2, 3)}
        moves = {}
        for mv in range(1, MOVE_COUNT):
            at = MOVE_ROWS + mv * MOVE_STRIDE
            if at + MOVE_STRIDE > len(self.rel):
                break
            moves[mv] = be32(self.rel, at + MOVE_WZX)
        variants = []
        for i in range(variant_count):
            row = self.dol.read(VARIANT_TABLE + i * VARIANT_STRIDE, VARIANT_STRIDE)
            w = struct.unpack(">8I", row)
            variants.append(dict(row=i, group=w[0], variant=w[1],
                                 kinds={k: (w[2 * k], w[2 * k + 1]) for k in (1, 2, 3)}))
        overrides = []
        for i in range(OVERRIDE_ROWS):
            row = self.dol.read(OVERRIDE_TABLE + i * 16, 16)
            variant, pkx, group = row[0], struct.unpack_from(">H", row, 2)[0], struct.unpack_from(">H", row, 4)[0]
            g, r = struct.unpack_from(">II", row, 8)
            overrides.append(dict(row=i, variant=variant, pkx=pkx, group=group, resource=(g, r)))
        specials = []
        for i in range(special_count):
            g, r, msg = struct.unpack(">III", self.dol.read(SPECIAL_TABLE + i * 12, 12))
            specials.append(dict(id=i, resource=(g, r), message=msg))
        return dict(kind_count=kind_count, special_count=special_count, variant_count=variant_count,
                    kinds=kinds, moves=moves, variants=variants, overrides=overrides, specials=specials)

    def resolve(self, t, members):
        """Index rows exactly as fn_801DDEE4 / fn_801DDC10 resolve resources."""
        rows, unresolved = [], []
        by_group = collections.defaultdict(list)
        for mv, w in t["moves"].items():
            if w:
                by_group[w].append(mv)

        def pack(g, r, where):
            name = self.names.get(g)
            if name is None or (name, r) not in members:
                unresolved.append(dict(where=where, group=g, resource="%08x" % r, archive=name))
                return None
            return members[(name, r)]

        def count(group, kind):  # fn_801DDC10
            c = 0
            if group in t["kinds"] and all(t["kinds"][group][kind]):
                c = 1
            return c + sum(1 for v in t["variants"] if v["group"] == group and all(v["kinds"][kind]))
        for w, kinds in t["kinds"].items():
            for kind, (g, r) in kinds.items():
                if g and r:
                    name = pack(g, r, "move wz %d kind %d" % (w, kind))
                    for mv in by_group.get(w, []):
                        if name:
                            rows.append((0, kind, mv, 0xFFFF, 0, name))
        for v in t["variants"]:
            for kind, (g, r) in v["kinds"].items():
                if v["group"] and g and r:
                    name = pack(g, r, "variant row %d kind %d" % (v["row"], kind))
                    for mv in by_group.get(v["group"], []):
                        if name:
                            rows.append((2, kind, mv, 0xFFFF, v["variant"], name))
        seen = set()
        bands = [(kind, band) for kind, band in OVERRIDE_BANDS.items()]
        for kind, (first, last) in bands + [(None, OVERRIDE_VARIANT_BAND)]:
            for o in t["overrides"][first:last]:
                g, r = o["resource"]
                if not (g and r):
                    continue
                kinds = [kind] if kind else [k for k in (1, 3) if o["variant"] and o["variant"] < count(o["group"], k)]
                name = pack(g, r, "override row %#x" % o["row"])
                for k in kinds:
                    key = (k, o["group"], o["pkx"], o["variant"], kind is None)
                    if key in seen or not name:
                        continue
                    seen.add(key)
                    for mv in by_group.get(o["group"], []):
                        rows.append((1, k, mv, o["pkx"], o["variant"], name))
        for s in t["specials"][1:]:
            g, r = s["resource"]
            if g and r:
                name = pack(g, r, "special %#x" % s["id"])
                if name:
                    rows.append((3, 4, s["id"], 0xFFFF, 0, name))
        return rows, unresolved

    # Members -----------------------------------------------------------------
    def parse_all(self):
        from prepare_outskirt import member
        parsed, skipped = {}, []
        for path in sorted((self.disc / "files").glob("wzx_*.fsys")):
            archive = path.read_bytes()
            group = self.groups.get(path.name)
            for rid in fsys_identities(archive):
                d = member(archive, rid)
                if len(d) < 0x80 or be32(d, 0x68) not in (1, 2):
                    skipped.append(dict(archive=path.name, resource="%08x" % rid, reason="not a sequence member"))
                    continue
                try:
                    seq = parse_member(d)
                except (ValueError, struct.error) as error:
                    skipped.append(dict(archive=path.name, resource="%08x" % rid, reason="parse: %s" % error))
                    continue
                if group is None:
                    group = be32(archive, 8)  # FSYS header group id (archive absent from GSfsys.toc)
                    seq["group_from_fsys_header"] = True
                seq.update(archive=path.name, group=group, resource=rid, data=d,
                           archive_sha256=hashlib.sha256(archive).hexdigest())
                parsed[(path.name, rid)] = seq
        return parsed, skipped

    def reference(self):
        """Battle floor 5 arena camera + lights, as the actor packs use."""
        stages = self.stages
        import prepare_field_geometry as geometry
        _, floor, _, _ = stages.sources(self.common, self.dol_bytes, self.toc)
        source = floor(stages.REFERENCE_FLOOR)
        archive = (self.disc / "files" / source["archive"]).read_bytes()
        scene_id = [i for i in fsys_identities(archive) if (i >> 8) & 0xFF == stages.SCENE_TYPE][0]
        data, raw, scene = geometry.load_scene(archive, scene_id)
        return stages.Run.camera(scene), geometry.fixed_lights(raw, data)

    def camera_samples(self, seq):
        import prepare_title as title
        at, size = seq["camera"]
        root, scene = parse_dat(seq["data"][at:at + size])
        if len(scene.cameras) != 1:
            raise ValueError("camera DAT has %d cameras" % len(scene.cameras))
        camera = scene.cameras[0]
        end = camera.animations[0].end_frame if camera.animations else 0.
        count = int(math.floor(end / CAMERA_RATE + 1e-6)) + 1
        samples = []
        for i in range(count):
            v = title.camera_values(camera, i * CAMERA_RATE)
            if not all(math.isfinite(x) for x in v[:8]):
                raise ValueError("nonfinite camera sample")
            samples.append(v[:8])
        return samples, dict(end_frame=end, samples=count, near=camera.near, far=camera.far,
                             aspect=camera.aspect)


def model_selection(n):
    """(anim, tex) the loader starts: joint anim index and texture anim index
    (GSmodelSetTexAnimIndex only when tex != anim; equal indices are treated as
    the same material animation). Effect DATs: anim 0 / tex 0 (not traced)."""
    if n["kind"] == 2:
        return n["anim"], n["tex"]
    return 0, 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", type=Path)
    parser.add_argument("--common-rel", type=Path)
    parser.add_argument("--parser", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-texture", type=int, default=128, choices=(128, 256, 512, 1024))
    parser.add_argument("--approximate", action="store_true")
    parser.add_argument("--skip-existing", action="store_true",
                        help="reuse model/particle results recorded in wzx-work.json whose files exist")
    parser.add_argument("--retry-failed", action="store_true",
                        help="with --skip-existing: convert failed models/banks again")
    parser.add_argument("--redo-particles", action="store_true",
                        help="with --skip-existing: convert every particle bank again")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument("--members", help="comma list of archive names (testing)")
    parser.add_argument("--verify", type=Path, help="re-read an output directory and exit")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / "tools"))
    if args.verify:
        problems, counts = verify(args.verify)
        print(json.dumps(dict(counts), indent=1))
        for p in problems[:200]:
            print("PROBLEM", p)
        print("%d problems" % len(problems))
        sys.exit(1 if problems else 0)
    for name in ("disc", "common_rel", "parser", "output"):
        if getattr(args, name) is None:
            parser.error("--%s is required" % name.replace("_", "-"))
    run = Run(args)
    out = run.output
    tables = run.tables()
    parsed, skipped = run.parse_all()
    if args.members:
        wanted = set(args.members.split(","))
        parsed = {k: v for k, v in parsed.items() if k[0] in wanted}
    print("parsed %d members, %d skipped" % (len(parsed), len(skipped)), flush=True)
    worker_init(run.parser, args.approximate)
    camera, lights = run.reference()

    # Global GPT banks (actions 0/7).
    from prepare_outskirt import member as fsys_member
    global_archive = (run.disc / "files" / GLOBAL_ARCHIVE).read_bytes()
    global_banks = {rid: fsys_member(global_archive, rid) for rid in fsys_identities(global_archive)}

    # Tasks: unique model inputs and GPT banks.
    model_tasks, bank_tasks = {}, {}
    node_model, node_bank = {}, {}
    for key, seq in parsed.items():
        d = seq["data"]
        producers = {}
        for i, n in enumerate(seq["nodes"]):
            if n["kind"] == 3 and n["gpt"]:
                producers[n["key"]] = i
        for i, n in enumerate(seq["nodes"]):
            if n["kind"] in (2, 4) and n.get("dat"):
                at, size = n["dat"]
                dat = d[at:at + size]
                anim, tex = model_selection(n)
                ident = hashlib.sha1(dat + struct.pack(">ii", anim, tex)).hexdigest()
                name = "wxm_%s" % ident[:12]
                task = model_tasks.setdefault(name, dict(
                    type="model", name=name, dat=dat, anim=anim, tex=tex, referenced=set(),
                    dat_sha256=hashlib.sha256(dat).hexdigest(), camera=camera, lights=lights,
                    output=str(out), max_texture=args.max_texture, users=0))
                task["users"] += 1
                if n["kind"] == 2:
                    if n["drive"] and n["drive_part"] >= 0:
                        task["referenced"].add(n["drive_part"])
                    for other in seq["nodes"]:
                        if other["flags"] & 1 and other["link"] == n["key"] and other["part"] >= 0:
                            task["referenced"].add(other["part"])
                node_model[(key, i)] = name
            elif n["kind"] == 3:
                if n["gpt"]:
                    src = n
                else:
                    j = producers.get(n["state"])
                    src = seq["nodes"][j] if j is not None else None
                if src is None:
                    node_bank[(key, i)] = None
                    continue
                at, size = src["gpt"]
                gpt = d[at:at + size]
                name = "wxp_%s.c3p" % hashlib.sha1(gpt).hexdigest()[:12]
                task = bank_tasks.setdefault(name, dict(type="bank", name=name, gpt=gpt, roots=set(),
                                                        output=str(out), users=0))
                task["roots"].add(n["root"])
                task["users"] += 1
                node_bank[(key, i)] = (name, n["root"])
    global_names = {}
    for rid, gpt in global_banks.items():
        name = "wxp_%s.c3p" % hashlib.sha1(gpt).hexdigest()[:12]
        task = bank_tasks.setdefault(name, dict(type="bank", name=name, gpt=gpt, roots=set(),
                                                output=str(out), users=0))
        task["roots"].add(0)
        global_names[rid] = name
    print("%d model inputs, %d particle banks" % (len(model_tasks), len(bank_tasks)), flush=True)

    work_path = out / "wzx-work.json"
    work = {}
    if args.skip_existing and work_path.exists():
        work = json.loads(work_path.read_text())
    results = {}
    for name, item in work.items():
        if args.redo_particles and item.get("type") == "bank":
            continue
        if name in model_tasks or name in bank_tasks:
            if (not item.get("ok") and not args.retry_failed) or (item.get("ok") and (out / item["file"]).exists()):
                results[name] = item
    pending = [t for n, t in list(model_tasks.items()) + list(bank_tasks.items()) if n not in results]
    # Largest inputs first so the long ones start early.
    pending.sort(key=lambda t: -len(t.get("dat") or t.get("gpt")))
    for t in pending:
        t["referenced"] = sorted(t.get("referenced", ()))
        t["roots"] = sorted(t.get("roots", ()))
    print("%d tasks pending (%d reused)" % (len(pending), len(results)), flush=True)
    if pending:
        import multiprocessing
        context = multiprocessing.get_context("fork")
        done = 0
        with context.Pool(args.jobs, initializer=worker_init, initargs=(run.parser, args.approximate),
                          maxtasksperchild=40) as pool:
            for result in pool.imap_unordered(run_task, pending):
                results[result["name"]] = result
                done += 1
                print("[%d/%d] %s %s %s" % (done, len(pending), result["name"],
                                            "ok" if result.get("ok") else "FAIL",
                                            result.get("reason", "")[:120]), flush=True)
                if done % 25 == 0:
                    work_path.write_text(json.dumps(results, indent=0))
        work_path.write_text(json.dumps(results, indent=0))

    # Members.
    report_members, approximations = {}, collections.Counter()
    member_names = {}
    for key, seq in parsed.items():
        member_names[key] = member_name(seq["group"], seq["resource"])
    for name in list(out.glob("wx_*.c3w")) if not args.members else []:
        if name.name not in member_names.values():
            name.unlink()
    for key, seq in sorted(parsed.items()):
        d = seq["data"]
        records, nodes_report = [], []
        for i, n in enumerate(seq["nodes"]):
            rec = dict(payload=d[n["payload_at"]:n["payload_end"]], file="", status=0, clips=0, bones=0,
                       joint_map=[])
            reason = None
            if n["kind"] == 2 or (n["kind"] == 4 and n["eff"]["type"] in EFFECT_DAT_TYPES):
                if not n.get("dat"):
                    if n["kind"] == 2:
                        rec["status"], reason = 3, "no embedded DAT"
                else:
                    name = node_model[(key, i)]
                    res = results.get(name, {})
                    rec["file"] = name + ".c3t"
                    if res.get("ok"):
                        rec.update(clips=res["clips"], bones=res["bones"], joint_map=res["joint_map"])
                    else:
                        rec["status"] = res.get("status", 1)
                        if n["kind"] == 4 and rec["status"] in (1, 2):
                            rec["status"] = 6
                        reason = res.get("reason", "not converted")
            elif n["kind"] == 3:
                bank = node_bank.get((key, i))
                if bank is None:
                    rec["status"], reason = 5, "reused bank key %d not in member" % n["state"]
                else:
                    name, root = bank
                    res = results.get(name, {})
                    rec["file"] = name
                    if not res.get("ok") or root not in res.get("passing_roots", ()):
                        rec["status"] = 4
                        reason = (res.get("failures") or {}).get(root) or res.get("reason", "not converted")
            records.append(rec)
            entry = dict(key=n["key"], kind=KIND_NAMES[n["kind"]], status=STATUS[rec["status"]])
            if rec["file"]:
                entry["file"] = rec["file"]
            if reason:
                entry["reason"] = reason[:300]
            if n["kind"] == 4:
                entry["effect_type"] = n["eff"]["type"]
            nodes_report.append(entry)
        cam, cam_info = [], None
        if seq["camera"]:
            try:
                cam, cam_info = run.camera_samples(seq)
            except Exception as error:  # noqa: BLE001
                cam_info = dict(error="%s: %s" % (type(error).__name__, error))
        data = encode_member(seq, records, cam)
        name = member_names[key]
        (out / name).write_bytes(data)
        by_kind = collections.Counter(e["kind"] for e in nodes_report)
        by_status = collections.Counter(e["status"] for e in nodes_report)
        report_members[name] = dict(archive=seq["archive"], group="%03x" % seq["group"],
                                    resource="%08x" % seq["resource"], member_bytes=seq["size"],
                                    member_sha256=hashlib.sha256(d).hexdigest(), bytes=len(data),
                                    entry=seq["entry"], flags="%08x" % seq["flags"], seq_version=seq["ver"],
                                    node_count=len(records), nodes_by_kind=dict(by_kind),
                                    nodes_by_status=dict(by_status),
                                    converted=sum(1 for r in records if r["status"] == 0),
                                    camera_samples=len(cam), camera=cam_info, nodes=nodes_report)

    # Index.
    members_by_source = {key: member_names[key] for key in parsed}
    rows, unresolved = run.resolve(tables, members_by_source)
    for rid, name in global_names.items():
        if results.get(name, {}).get("ok"):
            rows.append((4, 4, rid >> 16, 0xFFFF, 0, name))
        else:
            unresolved.append(dict(where="global %08x" % rid, reason=results.get(name, {}).get("reason")))
    index = encode_index(rows)
    (out / "wzx.c3n").write_bytes(index)

    # Coverage per move (default roots; variants/overrides listed separately).
    def member_cov(name):
        item = report_members[name]
        return item["converted"], item["node_count"]
    moves = {}
    for cat, kind, ident, pkx, variant, name in sorted(set(rows)):
        if cat != 0:
            continue
        m = moves.setdefault(str(ident), dict(roots={}, converted=0, nodes=0))
        c, t = member_cov(name)
        m["roots"][str(kind)] = dict(pack=name, converted=c, nodes=t)
        m["converted"] += c
        m["nodes"] += t
    for m in moves.values():
        m["coverage"] = "full" if m["converted"] == m["nodes"] else ("none" if not m["converted"] else "partial")
    for name, res in results.items():
        for k, v in (res.get("approximations") or {}).items():
            approximations[k] += v
    files = {}
    for res in results.values():
        if res.get("ok"):
            files[res["file"]] = res.get("stored_bytes", 0)
    member_bytes = sum(m["bytes"] for m in report_members.values())
    status_total = collections.Counter()
    kind_status = collections.defaultdict(collections.Counter)
    for m in report_members.values():
        for e in m["nodes"]:
            status_total[e["status"]] += 1
            kind_status[e["kind"]][e["status"]] += 1
    model_res = [r for r in results.values() if r["type"] == "model"]
    bank_res = [r for r in results.values() if r["type"] == "bank"]
    report = dict(
        schema=1, format="wzx v1 (wzx-formats.md)", approximate=args.approximate,
        max_texture=args.max_texture,
        sampling=dict(models="1 sample per authored frame, pack fps 30 (GSmodelSetAnimRate 0.5 per 60 Hz "
                             "tick); halved while a pack exceeds %d bytes or %d samples" % (
                                 MODEL_RAM_BUDGET, MAX_SAMPLES),
                      camera="60 Hz samples, 0.5 authored frame per sample (GScameraSetAnimRate 0.5); "
                             "importer units (original * 0.1), camera DAT space",
                      model_selection="clip 0 = node joint anim index (-1 rest) with texture anim index; "
                                      "effect type 5/11 DATs use joint anim 0 + texture anim 0 (not traced)",
                      lights_camera="battle floor %d arena (as the actor packs)" % run.stages.REFERENCE_FLOOR),
        status_codes=STATUS,
        totals=dict(members=len(report_members), member_bytes=member_bytes,
                    model_inputs=len(model_res), model_packs=sum(1 for r in model_res if r.get("ok")),
                    model_failed=sum(1 for r in model_res if not r.get("ok")),
                    particle_banks=len(bank_res), particle_packs=sum(1 for r in bank_res if r.get("ok")),
                    particle_failed=sum(1 for r in bank_res if not r.get("ok")),
                    pack_bytes=sum(files.values()), total_bytes=sum(files.values()) + member_bytes + len(index),
                    index_rows=len(set(rows)), nodes_by_status=dict(status_total),
                    nodes_by_kind_status={k: dict(v) for k, v in kind_status.items()},
                    moves=len(moves), moves_full=sum(1 for m in moves.values() if m["coverage"] == "full"),
                    moves_partial=sum(1 for m in moves.values() if m["coverage"] == "partial"),
                    moves_none=sum(1 for m in moves.values() if m["coverage"] == "none")),
        approximations=dict(approximations),
        index=dict(file="wzx.c3n", bytes=len(index), sha256=hashlib.sha256(index).hexdigest(),
                   unresolved=unresolved),
        skipped_members=skipped, moves=moves, members=report_members,
        models={k: v for k, v in sorted(results.items()) if v["type"] == "model"},
        particles={k: v for k, v in sorted(results.items()) if v["type"] == "bank"})
    (out / "wzx.json").write_text(json.dumps(report, indent=1) + "\n")
    t = report["totals"]
    print("wzx: %d members, %d/%d model packs, %d/%d particle packs, %d index rows, %.1f MB; moves full %d "
          "partial %d none %d" % (t["members"], t["model_packs"], t["model_inputs"], t["particle_packs"],
                                  t["particle_banks"], t["index_rows"], t["total_bytes"] / 1e6,
                                  t["moves_full"], t["moves_partial"], t["moves_none"]), flush=True)


if __name__ == "__main__":
    main()
