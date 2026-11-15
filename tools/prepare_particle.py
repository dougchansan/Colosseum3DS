#!/usr/bin/env python3
"""Bounded source-derived GPT1 dependency preparation; outputs remain private.

The original bank stays big-endian and unrelocated. C3PT carries checked offsets
and top-down RGBA textures; native code must never reinterpret PPC addresses.
"""
import collections
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
MAX_BANK = 1024 * 1024
MAX_GENERATORS = 64
MAX_PROGRAM = 1024
MAX_INSTRUCTIONS = 256
MAX_TEXTURES = 32
SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"


def extent(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("Particle input extent outside bank")
    return data[offset:offset + size]


def word(data, offset):
    return struct.unpack(">I", extent(data, offset, 4))[0]


def source_manifest():
    manifest = json.loads((ROOT / "config/particle-source-files.json").read_text())
    if manifest["commit"] != SOURCE_COMMIT:
        raise ValueError("Particle source manifest pin changed")
    for name, digest in manifest["files"].items():
        path = ROOT / "vendor/colosseum" / name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError("Particle source drift: " + name)
    return manifest


def program(data):
    """Only instructions reached by the admitted closure, from psinterpret.c.

    In particular AC consumes time AND TWO floats; the pinned importer omits
    its second float. Do not use that permissive disassembler for execution.
    """
    if not 1 <= len(data) <= MAX_PROGRAM:
        raise ValueError("Particle program byte bound")
    offset = 0
    result, children, frames = [], [], []
    mark_index = None
    loop_index = None
    while offset < len(data):
        if len(result) >= MAX_INSTRUCTIONS:
            raise ValueError("Particle instruction bound")
        start, opcode = offset, data[offset]
        offset += 1
        args = {}
        def take(count):
            nonlocal offset
            value = extent(data, offset, count)
            offset += count
            return value
        def time():
            value = take(1)[0]
            return ((value & 127) << 8) + take(1)[0] if value & 128 else value
        def floats(count):
            values = list(struct.unpack(">" + "f" * count, take(4 * count)))
            if not all(math.isfinite(x) for x in values):
                raise ValueError("Nonfinite particle instruction operand")
            return values
        if opcode < 128:
            args["wait"] = opcode & 31
            if opcode & 32:
                args["wait"] = (args["wait"] << 8) + take(1)[0]
            name = "WAIT"
            if opcode & 64:
                name = "TEXTURE_WAIT"
                args["pose"] = take(1)[0]
                frames.append(args["pose"])
        elif opcode in (0xF0, 0xF1):
            name = "SPAWN_GENERATOR_REFERENCE" if opcode == 0xF0 else "SPAWN_PARTICLE_REFERENCE"
            args["reference"] = struct.unpack(">H", take(2))[0]
            if opcode == 0xF0:
                args["kind"] = take(1)[0]
            children.append(args["reference"])
        elif opcode in (0xCF, 0xDF):
            name = "PRIMARY_COLOR" if opcode == 0xCF else "ENVIRONMENT_COLOR"
            args["time"] = time()
            args["rgba"] = list(take(4))
        elif opcode in (0xA0, 0xAC):
            name = "SIZE" if opcode == 0xA0 else "RANDOM_SIZE"
            args["time"] = time()
            args["values"] = floats(1 if opcode == 0xA0 else 2)
        elif opcode in (0xBD, 0xE8, 0xED):
            name = {0xBD: "RANDOM_SPEED", 0xE8: "TRAIL", 0xED: "RANDOM_ROTATION"}[opcode]
            args["values"] = floats(1 if opcode == 0xE8 else 2)
            if opcode == 0xED:
                args["steps"] = take(1)[0]
        elif opcode in (0xA3, 0xB6):
            name = "FRICTION" if opcode == 0xA3 else "ROTATION"
            if opcode == 0xB6:
                args["time"] = time()
            args["values"] = floats(1)
        elif opcode in (0xA8, 0x9F):
            name = "RANDOM_POSITION" if opcode == 0xA8 else "ADD_VELOCITY_XYZ"
            args["values"] = floats(3)
        elif opcode == 0xBC:
            name = "RANDOM_TEXTURE"
            args["base"], args["range"] = take(2)
            frames.extend(range(args["base"], args["base"] + max(1, args["range"])))
        elif opcode == 0xF3:
            name = "ROTATION_SWEEP"
            args["direction"] = take(1)[0]
            args["values"] = floats(2)
            args["time"] = time()
        elif opcode == 0xE9:
            name = "RANDOM_COLOR"
            args["flags"], args["steps"] = take(2)
            count = (args["flags"] & 15).bit_count()
            args["signed_ranges"] = list(struct.unpack("b" * count, take(count)))
        elif opcode in (0xFA, 0xFB, 0xFC, 0xFD):
            name = {0xFA: "LOOP_START", 0xFB: "LOOP_END", 0xFC: "MARK", 0xFD: "JUMP_MARK"}[opcode]
            if opcode == 0xFA:
                args["count"] = take(1)[0]
                if not args["count"]:
                    raise ValueError("Zero-count particle loops are unqualified")
                loop_index = len(result) + 1
            elif opcode == 0xFB:
                if loop_index is None:
                    raise ValueError("Particle loop has no start")
                args["target_instruction"] = loop_index
            elif opcode == 0xFC:
                mark_index = len(result) + 1
            else:
                if mark_index is None or not any(i["args"].get("wait", 0) for i in result[mark_index:]):
                    raise ValueError("Unbounded zero-time particle mark loop")
                args["target_instruction"] = mark_index
        elif opcode in (0xB1, 0xAD, 0xE6):
            name = {0xB1: "SET_KIND_0X60", 0xAD: "SET_KIND_0X80", 0xE6: "SET_KIND_0X200000"}[opcode]
        elif opcode == 0xFF:
            name = "END"
        else:
            raise ValueError(f"Unsupported particle opcode 0x{opcode:02X} at program+0x{start:X}")
        result.append({"offset": start, "opcode": opcode, "name": name,
                       "bytes": offset - start, "args": args})
        if opcode == 0xFF:
            if any(value not in (0, 255) for value in data[offset:]):
                raise ValueError("Unexpected bytes after particle program end")
            return result, children, frames
        if opcode == 0xFD and all(value in (0, 255) for value in data[offset:]):
            # Reflect's authored mark loop ends in zero padding, not END.
            # Positive waits bound each traversal; particle life bounds runs.
            return result, children, frames
    raise ValueError("Particle program lacks a bounded END")


def decode_i8(data, width, height):
    """GX I8 8x4 tiled bytes -> top-down RGBA, intensity in all four channels."""
    if (not 8 <= width <= 256 or not 4 <= height <= 256 or width % 8 or height % 4 or
        len(data) != width * height):
        raise ValueError("Unsupported particle I8 texture dimensions/extent")
    result = bytearray(width * height * 4)
    cursor = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 8):
            for y in range(4):
                for x in range(8):
                    at = ((by + y) * width + bx + x) * 4
                    result[at:at + 4] = bytes((data[cursor],)) * 4
                    cursor += 1
    return bytes(result)


def decode_i4(data, width, height):
    """GX I4 8x8 tiled high/low nibbles, expanded to full eight-bit intensity."""
    if (not 8 <= width <= 256 or not 8 <= height <= 256 or width % 8 or height % 8 or
        len(data) * 2 != width * height):
        raise ValueError("Unsupported particle I4 texture dimensions/extent")
    result = bytearray(width * height * 4)
    cursor = 0
    for by in range(0, height, 8):
        for bx in range(0, width, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    value = data[cursor]
                    cursor += 1
                    for dx, intensity in ((0, (value >> 4) * 17), (1, (value & 15) * 17)):
                        at = ((by + y) * width + bx + x + dx) * 4
                        result[at:at + 4] = bytes((intensity,)) * 4
    return bytes(result)


def bank(data, root=3, additional_roots=(), *, profile=None):
    growl = profile == 'growl'
    tackle = profile == 'tackle'
    if profile not in (None, 'growl', 'tackle'):
        raise ValueError('Unknown explicit particle source profile')
    if tackle and (root != 0 or additional_roots or len(data) != 91680 or
                   hashlib.sha256(data).hexdigest() != '40269fb5a7111267999a9e9d9fd72f18c24a4b54a9b452267c782b7123f8bdef'):
        raise ValueError('Tackle original bank/profile binding changed')
    if growl and (root != 18 or additional_roots or len(data) != 365764 or
                  hashlib.sha256(data).hexdigest() != '64867627ce7029307e3064e1af883cafb52995b7e2834980d7722d328ef043da'):
        raise ValueError('Growl original bank/profile binding changed')
    if not 32 <= len(data) <= MAX_BANK or extent(data, 0, 4) != b"GPT1":
        raise ValueError("Invalid GPT1 signature/size")
    ptl, txg, tex_bytes, refs = (word(data, at) for at in (4, 8, 12, 16))
    if not 32 <= ptl < txg < refs <= len(data) or txg + tex_bytes != refs:
        raise ValueError("Invalid GPT1 section ordering")
    if struct.unpack(">H", extent(data, ptl, 2))[0] != 0x43 or word(data, ptl + 4):
        raise ValueError("Only source GPT1 command-bank version43 base0 is admitted")
    count = word(data, ptl + 8)
    if not 1 <= count <= (113 if growl else MAX_GENERATORS) or not 0 <= root < count or refs + count * 4 != len(data):
        raise ValueError("GPT1 generator/reference bound")
    offsets = [ptl + word(data, ptl + 12 + i * 4) for i in range(count)]
    if sorted(set(offsets)) != offsets or offsets[0] < ptl + 12 + count * 4 or offsets[-1] + 60 > txg:
        raise ValueError("Invalid GPT1 generator table")
    references = [word(data, refs + i * 4) for i in range(count)]
    generators, closure, pending = {}, [], [root, *additional_roots]
    while pending:
        index = pending.pop()
        if index in generators:
            continue
        if not 0 <= index < count or len(generators) >= (16 if growl else MAX_GENERATORS):
            raise ValueError("Particle transitive generator reference outside bank")
        start, end = offsets[index], offsets[index + 1] if index + 1 < count else txg
        if end - start < 61:
            raise ValueError("Truncated generator header/program")
        raw = extent(data, start, end - start)
        gen_type, group, gen_life, life, kind = struct.unpack_from(">4HI", raw)
        if growl and bool(kind & 0x30000) != (index in (93,111)):
            raise ValueError('Unqualified Growl application-SRT generator')
        if (gen_type not in (0, 1, 2, 6, 8) and not (tackle and index == 6 and gen_type == 4)) or not 1 <= life <= 1000 or not 1 <= gen_life <= 120:
            raise ValueError("Unqualified particle generator type/lifetime")
        params = list(struct.unpack_from(">12f", raw, 12))
        if not all(math.isfinite(x) for x in params):
            raise ValueError("Nonfinite particle generator operand")
        instructions, children, frames = program(raw[60:])
        mapped = []
        for reference in children:
            if not 0 <= reference < len(references):
                raise ValueError("Particle bank reference exceeds REF table")
            mapped.append(references[reference])
        pending += mapped
        closure.append(index)
        generators[index] = {"index": index, "bank_offset": start, "bytes": end - start,
            "type": gen_type, "texture_group": group, "generator_life": gen_life,
            "particle_life": life, "source_kind": kind,
            "initialized_kind": (kind & 0xF1FFFFFF) | 0x08000000,
            "params": params, "program_offset": start + 60, "program_bytes": end - start - 60,
            "program_hex": raw[60:].hex(), "program_sha256": hashlib.sha256(raw[60:]).hexdigest(),
            "instructions": instructions, "child_generators": mapped, "texture_poses": frames}
    def depth(index, active):
        if index in active or len(active) >= 8:
            raise ValueError("Particle generator recursion/nesting bound")
        for child in generators[index]["child_generators"]:
            depth(child, active + [index])
    for index in (root, *additional_roots):
        depth(index, [])
    if growl and sorted(closure) != [18,93,111,112]:
        raise ValueError('Growl admitted sparse closure changed')
    group_count = word(data, txg)
    if not 1 <= group_count <= 8:
        raise ValueError("Particle texture-group bound")
    textures = []
    groups = sorted(set(g["texture_group"] for g in generators.values()))
    for group in groups:
        if not 0 <= group < group_count:
            raise ValueError("Particle generator texture group missing")
        at = txg + word(data, txg + 4 + group * 4)
        frames, fmt, palflag, width, height, palnum = struct.unpack(">6I", extent(data, at, 24))
        if not 1 <= frames <= (19 if growl and group==1 else 16) or fmt not in (0, 1) or palflag or palnum:
            raise ValueError("Unsupported particle texture format/palette/frame count")
        for pose in range(frames):
            offset = txg + word(data, at + 24 + pose * 4)
            pixels = extent(data, offset, width * height // (2 if fmt == 0 else 1))
            if offset < txg or offset + len(pixels) > refs:
                raise ValueError("Particle texture outside TEX section")
            rgba = decode_i4(pixels, width, height) if fmt == 0 else decode_i8(pixels, width, height)
            textures.append({"group": group, "pose": pose, "width": width, "height": height,
                "source_format": fmt, "bank_offset": offset, "source_bytes": len(pixels),
                "source_sha256": hashlib.sha256(pixels).hexdigest(),
                "rgba_sha256": hashlib.sha256(rgba).hexdigest(), "rgba": rgba})
        for generator in generators.values():
            if generator["texture_group"] == group and any(pose >= frames for pose in generator["texture_poses"]):
                raise ValueError("Particle texture pose outside original frame table")
    if len(textures) > MAX_TEXTURES:
        raise ValueError("Particle texture allocation bound")
    if tackle and (count != 8 or sorted(closure) != list(range(8)) or groups != [0, 2, 3, 4] or len(textures) != 7):
        raise ValueError('Tackle reached generator/texture closure changed')
    return {"root_generator": root, "root_generators": [root, *additional_roots],
            "bank_bytes": len(data), "bank_sha256": hashlib.sha256(data).hexdigest(),
            "generator_count": count, "references": references, "closure": sorted(closure),
            "generators": [generators[i] for i in sorted(generators)], "textures": textures}


def encode(data, report, source_sha256):
    textures = report["textures"]
    bank_offset = (128 + 32 * len(textures) + 31) & ~31
    result = bytearray(bank_offset)
    result.extend(data)
    result.extend(bytes((-len(result)) % 32))
    for index, texture in enumerate(textures):
        at = len(result)
        rgba = texture["rgba"]
        struct.pack_into("<8I", result, 128 + index * 32, texture["group"], texture["pose"],
                         texture["width"], texture["height"], at, len(rgba), texture["source_format"], 0)
        result.extend(rgba)
        result.extend(bytes((-len(result)) % 32))
    struct.pack_into("<4s7I", result, 0, b"C3PT", 1, len(result), report["root_generator"],
                     bank_offset, len(data), len(textures), 128)
    result[32:64] = bytes.fromhex(source_sha256)
    result[64:96] = hashlib.sha256(result[128:]).digest()
    return bytes(result)


def report_json(report):
    return {**report, "textures": [{k: v for k, v in texture.items() if k != "rgba"}
                                    for texture in report["textures"]]}


# ---------------------------------------------------------------------------
# General GPT1 path (C3PT version 2). The legacy bank()/program() contract
# above stays frozen: its packs are hash-pinned by the encounter-10 WZX
# descriptors. The general path admits the psInterpretParticle0 opcode table
# (psinterpret.c, 0x8016F500) and generator shapes 0,1,2,3,4,6,7,8
# (generator.c generateParticle_8017424C), decodes only the generators
# reachable from the requested roots, and fails closed PER GENERATOR: a
# generator the runtime cannot execute is disabled, and a spawn of it becomes
# the source's own NULL-template no-op (psCreateGeneratorID /
# psGenerateParticleID0 return NULL). src/particle.c repeats every check below;
# keep the two in step (tests/test_particle_disc.c compares them on the disc).

GENERAL_VERSION = 2
GENERAL_MAX_BANK = 1024 * 1024     # disc peak 485648
GENERAL_MAX_SOURCE_GENERATORS = 4096
GENERAL_MAX_GENERATORS = 128      # decoded closure slots (disc union peak 109), C3_PARTICLE_MAX_GENERATORS
GENERAL_MAX_PROGRAM = 4096        # bytes per generator program (runtime bound)
GENERAL_MAX_TEXTURE_GROUPS = 64   # C3_PARTICLE_MAX_TEXTURE_GROUPS
GENERAL_MAX_FRAMES = 256
GENERAL_MAX_TEXTURES = 32         # C3_PARTICLE_PACK_MAX_TEXTURES (disc union peak 32)
GENERAL_MAX_EDGE = 64             # screen-sized: larger textures are box-filtered down
GENERAL_MAX_ROOTS = 32            # C3_PARTICLE_PACK_MAX_ROOTS (disc peak 21 per bank)
GENERAL_MAX_RATE = 1536           # C3_PARTICLE_MAX_PARTICLES
GENERAL_SHAPES = (0, 1, 2, 3, 4, 6, 7, 8)
# GX texture formats: bits per pixel and tile size (GX texture tiling).
GX_FORMATS = {0: ("I4", 4, 8, 8), 1: ("I8", 8, 8, 4), 2: ("IA4", 8, 8, 4), 3: ("IA8", 16, 4, 4),
              4: ("RGB565", 16, 4, 4), 5: ("RGB5A3", 16, 4, 4), 6: ("RGBA8", 32, 4, 4)}

# Operand layout of every psInterpretParticle0 case (T time, F float, B byte,
# H big-endian u16). 0x80-0x9F, 0xC0-0xDF, 0xE9, 0xEA and 0xEB are variable.
GENERAL_OPERANDS = {
    0xA0: "TF", 0xA1: "", 0xA2: "F", 0xA3: "F", 0xA4: "H", 0xA5: "H", 0xA6: "HH", 0xA7: "B",
    0xA8: "FFF", 0xA9: "F", 0xAA: "HH", 0xAB: "F", 0xAC: "TFF", 0xAD: "", 0xAE: "", 0xAF: "",
    0xB0: "", 0xB1: "", 0xB2: "", 0xB3: "TBBB", 0xB4: "", 0xB5: "", 0xB6: "TF", 0xB7: "B",
    0xB8: "BFF", 0xB9: "H", 0xBA: "BBBB", 0xBB: "BBBB", 0xBC: "BB", 0xBD: "FF", 0xBE: "FFF",
    0xBF: "B", 0xE0: "BBBB", 0xE2: "", 0xE3: "B", 0xE4: "B", 0xE5: "B", 0xE6: "", 0xE7: "",
    0xE8: "F", 0xED: "FFB", 0xEF: "HB", 0xF0: "HB", 0xF1: "H", 0xF2: "H", 0xF3: "BFFT",
    0xF4: "FFFF", 0xF5: "", 0xF6: "", 0xF7: "", 0xFA: "B", 0xFB: "", 0xFC: "", 0xFD: "",
    0xFE: "", 0xFF: "",
}
# Spawns: REF-mapped (psBankRef) or direct template IDs.
GENERAL_REF_SPAWNS = {0xF0: "generator", 0xF1: "particle", 0xF2: "particle"}
GENERAL_DIRECT_SPAWNS = {0xA4: "particle", 0xA5: "generator", 0xB9: "particle", 0xEF: "generator"}


def general_program(data):
    """Linear decode of one generator program up to its first unconditional
    end (FE/FF, or FD which never falls through). Returns (instructions,
    reference_indices, direct_ids, poses) or raises ValueError with a reason.
    Bytes after the end are unreachable (no forward jumps exist) and ignored."""
    if not 1 <= len(data) <= GENERAL_MAX_PROGRAM:
        raise ValueError("program byte bound")
    offset, result, refs, direct, poses = 0, [], [], [], []
    loop_start = 0  # source cmdLoopPtr/cmdMarkPtr start at 0 (psGenerateParticle0)
    mark = 0

    def take(count):
        nonlocal offset
        if offset + count > len(data):
            raise ValueError("truncated instruction")
        value = data[offset:offset + count]
        offset += count
        return value

    def time():
        value = take(1)[0]
        return ((value & 127) << 8) + take(1)[0] if value & 128 else value

    def floats(count):
        values = list(struct.unpack(">" + "f" * count, take(4 * count)))
        if not all(math.isfinite(x) and abs(x) <= 1000000 for x in values):
            raise ValueError("nonfinite/excessive instruction scalar")
        return values

    while offset < len(data):
        start, op = offset, data[offset]
        offset += 1
        args, wait = {}, 0
        if op < 0x80:
            wait = op & 31
            if op & 32:
                wait = (wait << 8) + take(1)[0]
            if op & 64:
                args["pose"] = take(1)[0]
                poses.append(args["pose"])
            args["wait"] = wait
        elif (op & 0xF8) <= 0x98:
            # 0x80 set pos, 0x88 add pos, 0x90 set vel, 0x98 add vel; bit n
            # selects component n. Retail copies all three components even
            # when only some were read (uninitialised stack). Partial SETs
            # are refused; partial ADDs (8A/8B/8E on the disc) add zero for
            # the unread components -- an approximation of the stack garbage.
            if op & 7 != 7 and op & 0xF8 in (0x80, 0x90):
                raise ValueError("partial vector set reads uninitialised source operands")
            read = iter(floats(bin(op & 7).count("1")))
            args["values"] = [next(read) if op & (1 << c) else 0.0 for c in range(3)]
        elif op & 0xF0 in (0xC0, 0xD0):
            args["time"] = time()
            args["rgba"] = list(take(bin(op & 15).count("1")))
        elif op == 0xE9:
            flags, steps = take(2)
            args.update(flags=flags, steps=steps, ranges=list(take(bin(flags & 15).count("1"))))
        elif op in (0xEA, 0xEB):
            args["time"] = time()
            flags = take(1)[0]
            args.update(flags=flags, values=list(take((flags & 1) + ((flags >> 3) & 1))))
        elif op in GENERAL_OPERANDS:
            values = []
            for kind in GENERAL_OPERANDS[op]:
                if kind == "T":
                    values.append(time())
                elif kind == "F":
                    values.extend(floats(1))
                elif kind == "B":
                    values.append(take(1)[0])
                else:
                    values.append(struct.unpack(">H", take(2))[0])
            args["values"] = values
            if op in GENERAL_REF_SPAWNS:
                refs.append(values[0])
            elif op == 0xAA:
                refs.extend(range(values[0], values[0] + max(1, values[1])))
            elif op in GENERAL_DIRECT_SPAWNS:
                direct.append(values[0])
            elif op == 0xBC:
                poses.extend(range(values[0], values[0] + max(1, values[1])))
            elif op == 0xFA:
                loop_start = len(result) + 1
            elif op == 0xFC:
                mark = len(result) + 1
            elif op == 0xFB:
                # FB without FA jumps to cmdLoopPtr 0 (255 more times; u8 wrap).
                if not any(i["wait"] for i in result[loop_start:]):
                    raise ValueError("zero-time loop")
            elif op == 0xFD:
                if not any(i["wait"] for i in result[mark:]):
                    raise ValueError("zero-time mark loop")
        else:
            raise ValueError(f"opcode 0x{op:02X} outside psInterpretParticle0")
        result.append({"offset": start, "opcode": op, "wait": wait, "args": args})
        if op in (0xFD, 0xFE, 0xFF):
            return result, refs, direct, poses
    raise ValueError("program lacks an end")


def gx_texture_size(fmt, width, height):
    _, bpp, bw, bh = GX_FORMATS[fmt]
    return ((width + bw - 1) // bw * bw) * ((height + bh - 1) // bh * bh) * bpp // 8


def decode_gx(fmt, data, width, height):
    """Any non-palette GX particle texture -> top-down RGBA8. Intensity
    formats replicate I into RGB (and alpha for I4/I8, as GX does)."""
    if fmt not in GX_FORMATS:
        raise ValueError("palette/unknown texture format")
    _, bpp, bw, bh = GX_FORMATS[fmt]
    if not 1 <= width <= 1024 or not 1 <= height <= 1024 or len(data) != gx_texture_size(fmt, width, height):
        raise ValueError("texture dimensions/extent")
    out = bytearray(width * height * 4)
    tile_bytes = bw * bh * bpp // 8
    cursor = 0
    for by in range(0, height, bh):
        for bx in range(0, width, bw):
            t = data[cursor:cursor + tile_bytes]
            cursor += tile_bytes
            for i in range(bw * bh):
                x, y = bx + i % bw, by + i // bw
                if x >= width or y >= height:
                    continue
                if fmt == 0:
                    v = t[i // 2]
                    v = (v >> 4 if i % 2 == 0 else v & 15) * 17
                    px = (v, v, v, v)
                elif fmt == 1:
                    v = t[i]
                    px = (v, v, v, v)
                elif fmt == 2:
                    v = t[i]
                    a, l = (v >> 4) * 17, (v & 15) * 17
                    px = (l, l, l, a)
                elif fmt == 3:
                    a, l = t[2 * i], t[2 * i + 1]
                    px = (l, l, l, a)
                elif fmt == 4:
                    v = (t[2 * i] << 8) | t[2 * i + 1]
                    r, g, b = v >> 11, (v >> 5) & 63, v & 31
                    px = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255)
                elif fmt == 5:
                    v = (t[2 * i] << 8) | t[2 * i + 1]
                    if v & 0x8000:
                        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
                        px = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255)
                    else:
                        a, r, g, b = (v >> 12) & 7, (v >> 8) & 15, (v >> 4) & 15, v & 15
                        px = (r * 17, g * 17, b * 17, (a << 5) | (a << 2) | (a >> 1))
                else:
                    px = (t[2 * i + 1], t[32 + 2 * i], t[32 + 2 * i + 1], t[2 * i])
                at = (y * width + x) * 4
                out[at:at + 4] = bytes(px)
    return bytes(out)


def fit_texture(rgba, width, height):
    """PICA wants power-of-two edges >= 8. Box-filter by halves down to
    GENERAL_MAX_EDGE, then nearest-resample any other shape to power-of-two
    edges in [8, GENERAL_MAX_EDGE]. Returns (rgba, width, height)."""
    def pow2(n):
        p = 8
        while p < n:
            p *= 2
        return min(p, GENERAL_MAX_EDGE)
    while (width > GENERAL_MAX_EDGE or height > GENERAL_MAX_EDGE) and not width % 2 and not height % 2:
        w, h = width // 2, height // 2
        half = bytearray(w * h * 4)
        for y in range(h):
            r0, r1 = 2 * y * width * 4, (2 * y + 1) * width * 4
            for x in range(w):
                for c in range(4):
                    s = (rgba[r0 + 8 * x + c] + rgba[r0 + 8 * x + 4 + c] +
                         rgba[r1 + 8 * x + c] + rgba[r1 + 8 * x + 4 + c])
                    half[(y * w + x) * 4 + c] = (s + 2) // 4
        rgba, width, height = bytes(half), w, h
    w2, h2 = pow2(width), pow2(height)
    if (w2, h2) != (width, height):
        out = bytearray(w2 * h2 * 4)
        for y in range(h2):
            sy = y * height // h2
            for x in range(w2):
                sx = x * width // w2
                out[(y * w2 + x) * 4:(y * w2 + x) * 4 + 4] = rgba[(sy * width + sx) * 4:(sy * width + sx) * 4 + 4]
        rgba, width, height = bytes(out), w2, h2
    return rgba, width, height


def general_header(data):
    if not 32 <= len(data) <= GENERAL_MAX_BANK or data[:4] != b"GPT1":
        raise ValueError("bank signature/size")
    ptl, txg, tex_bytes, refs = (word(data, at) for at in (4, 8, 12, 16))
    if not 32 <= ptl < txg < refs <= len(data) or txg + tex_bytes != refs or any(data[20:32]):
        raise ValueError("bank section ordering")
    if struct.unpack(">H", extent(data, ptl, 2))[0] != 0x43 or word(data, ptl + 4):
        raise ValueError("bank version")
    count = word(data, ptl + 8)
    if not 1 <= count <= GENERAL_MAX_SOURCE_GENERATORS or refs + count * 4 != len(data):
        raise ValueError("bank generator/reference table")
    offsets = [ptl + word(data, ptl + 12 + i * 4) for i in range(count)]
    if offsets[0] < ptl + 12 + count * 4 or any(b <= a for a, b in zip(offsets, offsets[1:])) or offsets[-1] >= txg:
        raise ValueError("bank generator offsets")
    groups = word(data, txg)
    if not 1 <= groups <= GENERAL_MAX_TEXTURE_GROUPS or txg + 4 + 4 * groups > refs:
        raise ValueError("bank texture-group table")
    return {"ptl": ptl, "txg": txg, "refs": refs, "count": count, "offsets": offsets,
            "references": [word(data, refs + 4 * i) for i in range(count)], "groups": groups}


def general_group_info(data, header, group):
    """(frames, fmt, width, height, size, frame_offsets) or raise ValueError."""
    txg = header["txg"]
    rel = word(data, txg + 4 + 4 * group)
    at = txg + rel
    if rel < 4 + 4 * header["groups"] or at + 24 > header["refs"]:
        raise ValueError("texture group offset")
    frames, fmt, palflag, width, height, palnum = struct.unpack(">6I", extent(data, at, 24))
    if not 1 <= frames <= GENERAL_MAX_FRAMES or at + 24 + 4 * frames > header["refs"]:
        raise ValueError("texture frame count")
    if palflag or palnum or fmt not in GX_FORMATS:
        raise ValueError(f"texture format {fmt}" + (" palette" if palflag else ""))
    if not 1 <= width <= 1024 or not 1 <= height <= 1024:
        raise ValueError("texture dimensions")
    size = gx_texture_size(fmt, width, height)
    offsets = []
    for pose in range(frames):
        offset = txg + word(data, at + 24 + 4 * pose)
        if offset + size > header["refs"]:
            raise ValueError("texture pixels outside TEX section")
        offsets.append(offset)
    return frames, fmt, width, height, size, offsets


def general_group(data, header, group, cache, decode=True):
    """Decode one texture group (every frame) or raise ValueError."""
    key = (group, decode)
    if key not in cache:
        try:
            frames, fmt, width, height, size, offsets = general_group_info(data, header, group)
            out = []
            for pose, offset in enumerate(offsets):
                pixels = data[offset:offset + size]
                entry = {"group": group, "pose": pose, "source_width": width, "source_height": height,
                         "source_format": fmt, "bank_offset": offset, "source_bytes": size,
                         "source_sha256": hashlib.sha256(pixels).hexdigest()}
                if decode:
                    rgba, w, h = fit_texture(decode_gx(fmt, pixels, width, height), width, height)
                    entry.update(width=w, height=h, rgba=rgba, rgba_sha256=hashlib.sha256(rgba).hexdigest())
                out.append(entry)
            cache[key] = out
        except ValueError as error:
            cache[key] = error
    if isinstance(cache[key], Exception):
        raise cache[key]
    return cache[key]


def general_generator(data, header, index):
    """Decode one generator or raise ValueError(reason)."""
    start = header["offsets"][index]
    end = header["offsets"][index + 1] if index + 1 < header["count"] else header["txg"]
    if end - start < 61 or end - start - 60 > GENERAL_MAX_PROGRAM:
        raise ValueError("generator header/program extent")
    raw = data[start:end]
    gen_type, group, gen_life, life, kind = struct.unpack_from(">4HI", raw)
    params = list(struct.unpack_from(">12f", raw, 12))
    if not all(math.isfinite(x) and abs(x) <= 1000000 for x in params):
        raise ValueError("nonfinite/excessive generator scalar")
    if gen_type & 0xF not in GENERAL_SHAPES or gen_type & ~0x8F:
        raise ValueError(f"generator shape {gen_type:#x}")
    # kind 0x10000 (camera emission basis) and 0x20000 (AppSRT) are admitted:
    # the runtime takes the camera from c3_particle_set_camera.
    if group >= header["groups"]:
        raise ValueError("generator texture group outside table")
    if abs(params[7]) > GENERAL_MAX_RATE:
        raise ValueError("generator rate bound")
    instructions, refs, direct, poses = general_program(raw[60:])
    children = []
    for reference in refs:
        if reference >= header["count"]:
            raise ValueError("spawn reference outside REF table")
        children.append(header["references"][reference])
    children.extend(direct)
    return {"index": index, "bank_offset": start, "bytes": end - start, "type": gen_type,
            "texture_group": group, "generator_life": gen_life, "particle_life": life,
            "source_kind": kind, "initialized_kind": (kind & 0xF1FFFFFF) | 0x08000000,
            "params": params, "program_offset": start + 60, "program_bytes": end - start - 60,
            "program_sha256": hashlib.sha256(raw[60:]).hexdigest(),
            "opcodes": sorted({i["opcode"] for i in instructions if i["opcode"] >= 0x80}),
            # Source NULL template (id >= count): the spawn is a no-op, not a child.
            "child_generators": sorted({c for c in children if c < header["count"]}),
            "texture_poses": sorted(set(poses))}


def general_bank(data, roots, decode=True):
    """Closure of `roots` with per-generator admission. Raises ValueError only
    when the bank itself, a root or a pack bound is unusable."""
    roots = list(dict.fromkeys(roots))
    if not 1 <= len(roots) <= GENERAL_MAX_ROOTS:
        raise ValueError("root count")
    header = general_header(data)
    cache, generators, disabled, pending = {}, {}, {}, list(roots)
    while pending:
        index = pending.pop(0)
        if index in generators or index in disabled:
            continue
        if not 0 <= index < header["count"]:
            raise ValueError("root outside bank")
        try:
            g = general_generator(data, header, index)
            textures = general_group(data, header, g["texture_group"], cache, decode)
            if any(p >= len(textures) for p in g["texture_poses"]):
                raise ValueError("texture pose outside frame table")
        except ValueError as error:
            if index in roots:
                raise ValueError("root " + str(error))
            disabled[index] = str(error)
            continue
        generators[index] = g
        pending += [c for c in g["child_generators"] if c not in generators and c not in disabled]
    if len(generators) > GENERAL_MAX_GENERATORS:
        raise ValueError("closure exceeds decoded generator slots")
    groups = sorted({g["texture_group"] for g in generators.values()})
    textures = [t for group in groups for t in cache[(group, decode)]]
    if len(textures) > GENERAL_MAX_TEXTURES:
        raise ValueError("texture count exceeds pack bound")
    return {"version": GENERAL_VERSION, "root_generator": roots[0], "root_generators": roots,
            "bank_bytes": len(data), "bank_sha256": hashlib.sha256(data).hexdigest(),
            "generator_count": header["count"], "closure": sorted(generators),
            "disabled": {str(k): v for k, v in sorted(disabled.items())},
            "generators": [generators[i] for i in sorted(generators)], "textures": textures}


def encode_general(data, report, source_sha256):
    """C3PT version 2: v1 layout plus a root table. Header +96 root_count;
    the root table (root_count u32) follows the texture records; the bank is
    32-byte aligned after it. Textures are RGBA8, power-of-two edges 8..64."""
    textures, roots = report["textures"], report["root_generators"]
    table_end = 128 + 32 * len(textures) + 4 * len(roots)
    bank_offset = (table_end + 31) & ~31
    result = bytearray(bank_offset)
    struct.pack_into("<%dI" % len(roots), result, 128 + 32 * len(textures), *roots)
    result.extend(data)
    result.extend(bytes((-len(result)) % 32))
    for index, texture in enumerate(textures):
        at = len(result)
        rgba = texture["rgba"]
        struct.pack_into("<8I", result, 128 + index * 32, texture["group"], texture["pose"],
                         texture["width"], texture["height"], at, len(rgba), texture["source_format"], 0)
        result.extend(rgba)
        result.extend(bytes((-len(result)) % 32))
    struct.pack_into("<4s7I", result, 0, b"C3PT", GENERAL_VERSION, len(result), roots[0],
                     bank_offset, len(data), len(textures), 128)
    result[32:64] = bytes.fromhex(source_sha256)
    struct.pack_into("<I", result, 96, len(roots))
    result[64:96] = hashlib.sha256(result[128:]).digest()
    return bytes(result)


def general_report_json(report):
    return {**report, "textures": [{k: v for k, v in t.items() if k != "rgba"} for t in report["textures"]]}


def read_batch_entries(source, default_roots):
    """Yield (label, bank_bytes, roots) from a directory of GPT1 banks (every
    file starting with GPT1, roots = default_roots) or a JSON list of
    {"fsys": path, "member": id, "offset": n, "bytes": n, "roots": [..]} /
    {"file": path, "offset": n, "bytes": n, "roots": [..]}."""
    path = Path(source)
    if path.is_dir():
        for file in sorted(p for p in path.iterdir() if p.is_file()):
            raw = file.read_bytes()
            if raw[:4] == b"GPT1":
                yield file.name, raw, list(default_roots)
        return
    from prepare_outskirt import member
    archives = {}
    for entry in json.loads(path.read_text()):
        if "fsys" in entry:
            key = (entry["fsys"], entry["member"])
            if key not in archives:
                archives.clear()
                archives[key] = member(Path(entry["fsys"]).read_bytes(), int(entry["member"]))
            blob, label = archives[key], f'{Path(entry["fsys"]).name}:{int(entry["member"]):#x}'
        else:
            blob, label = Path(entry["file"]).read_bytes(), Path(entry["file"]).name
        offset = int(entry.get("offset", 0))
        size = int(entry.get("bytes", len(blob) - offset))
        if offset < 0 or size < 0 or offset + size > len(blob):
            raise ValueError(f"{label}: bank extent outside member")
        yield f"{label}+{offset:#x}", blob[offset:offset + size], [int(r) for r in entry.get("roots", default_roots)]


def batch(source, out_dir, default_roots=(0,)):
    """Convert every bank; one wxp_<sha1(GPT1):12>.c3p per distinct bank with
    the union of its admitted roots. Returns the JSON report."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    banks, launches, failures = {}, [], collections.Counter()
    for label, raw, roots in read_batch_entries(source, default_roots):
        digest = hashlib.sha1(raw).hexdigest()
        banks.setdefault(digest, {"data": raw, "roots": [], "labels": []})["labels"].append(label)
        for root in roots:
            try:
                general_bank(raw, [root], decode=False)
                status = "ok"
                if root not in banks[digest]["roots"]:
                    banks[digest]["roots"].append(root)
            except ValueError as error:
                status = str(error)
                failures[status] += 1
            launches.append({"source": label, "bank_sha1": digest, "root": root, "status": status})
    packs = {}
    for digest, bank_info in sorted(banks.items()):
        roots = bank_info["roots"]
        if not roots:
            continue
        name = f"wxp_{digest[:12]}.c3p"
        try:
            if len(roots) > GENERAL_MAX_ROOTS:
                raise ValueError("more admitted roots than one pack holds")
            report = general_bank(bank_info["data"], roots)
        except ValueError as error:
            failures["pack: " + str(error)] += len(roots)
            for launch in launches:
                if launch["bank_sha1"] == digest and launch["status"] == "ok":
                    launch["status"] = "pack: " + str(error)
            continue
        packed = encode_general(bank_info["data"], report, hashlib.sha256(bank_info["data"]).hexdigest())
        (out / name).write_bytes(packed)
        packs[name] = {"bank_sha1": digest, "pack_sha1": hashlib.sha1(packed).hexdigest(),
                       "pack_bytes": len(packed), "roots": roots, "closure": report["closure"],
                       "disabled": report["disabled"], "textures": len(report["textures"]),
                       "sources": bank_info["labels"]}
    ok = sum(1 for launch in launches if launch["status"] == "ok")
    result = {"format": "C3PT", "version": GENERAL_VERSION, "launches": len(launches),
              "convertible_launches": ok, "packs": packs,
              "failures_by_reason": dict(failures.most_common()), "launch_results": launches}
    (out / "particle_batch_report.json").write_bytes((json.dumps(result, indent=1) + "\n").encode())
    # Plain-text closure list for the C host check (tests/test_particle_disc.c):
    # "<pack> <root_count> <roots...> <closure_count> <closure ids...>".
    with open(out / "particle_batch_closures.txt", "w", newline="\n") as lines:
        for name, info in sorted(packs.items()):
            lines.write(" ".join(map(str, [name, len(info["roots"]), *info["roots"],
                                            len(info["closure"]), *info["closure"]])) + "\n")
    return result


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Convert original GPT1 particle banks to C3PT v2 packs (private outputs).")
    parser.add_argument("--batch", required=True, help="directory of GPT1 banks, or JSON list of bank locations")
    parser.add_argument("--out", required=True, help="output directory (keep it under private/)")
    parser.add_argument("--root", type=int, action="append", help="root for directory mode (repeatable; default 0)")
    args = parser.parse_args()
    result = batch(args.batch, args.out, args.root or (0,))
    print(f'{result["convertible_launches"]}/{result["launches"]} launches, {len(result["packs"])} packs')
    for reason, count in result["failures_by_reason"].items():
        print(f"  {count:5d} {reason}")


if __name__ == "__main__":
    main()
