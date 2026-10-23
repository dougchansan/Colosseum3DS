#!/usr/bin/env python3
"""Bounded source-derived GPT1 dependency preparation; outputs remain private.

The original bank stays big-endian and unrelocated. C3PT carries checked offsets
and top-down RGBA textures; native code must never reinterpret PPC addresses.
"""
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


def bank(data, root=3, additional_roots=()):
    if not 32 <= len(data) <= MAX_BANK or extent(data, 0, 4) != b"GPT1":
        raise ValueError("Invalid GPT1 signature/size")
    ptl, txg, tex_bytes, refs = (word(data, at) for at in (4, 8, 12, 16))
    if not 32 <= ptl < txg < refs <= len(data) or txg + tex_bytes != refs:
        raise ValueError("Invalid GPT1 section ordering")
    if struct.unpack(">H", extent(data, ptl, 2))[0] != 0x43 or word(data, ptl + 4):
        raise ValueError("Only source GPT1 command-bank version43 base0 is admitted")
    count = word(data, ptl + 8)
    if not 1 <= count <= MAX_GENERATORS or not 0 <= root < count or refs + count * 4 != len(data):
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
        if not 0 <= index < count or len(generators) >= MAX_GENERATORS:
            raise ValueError("Particle transitive generator reference outside bank")
        start, end = offsets[index], offsets[index + 1] if index + 1 < count else txg
        if end - start < 61:
            raise ValueError("Truncated generator header/program")
        raw = extent(data, start, end - start)
        gen_type, group, gen_life, life, kind = struct.unpack_from(">4HI", raw)
        if gen_type not in (0, 1, 2, 6, 8) or not 1 <= life <= 1000 or not 1 <= gen_life <= 120:
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
        if not 1 <= frames <= 16 or fmt not in (0, 1) or palflag or palnum:
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
