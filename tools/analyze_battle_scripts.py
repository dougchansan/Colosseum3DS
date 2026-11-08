#!/usr/bin/env python3
"""Disassemble the original move-effect scripts a set of moves can reach.

Reads the pinned decomp for the 255-entry opcode table (lbl_8027A00C) and
derives each handler's script-pointer advances from its source, then walks
the scripts in the canonical GC6E01 main.dol starting at lbl_80379BFF[effect].
Prints a report (opcode closure, per-script listing). Nothing is written into
the checkout; original bytes stay in the private DOL.

usage: analyze_battle_scripts.py MAIN_DOL COMMON_REL [--moves 33,39,...]
"""
import argparse
import hashlib
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "vendor/colosseum/src/game"
DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
REL_SHA1 = "e816761813900aa0fd4f87d012b3b2a19d2f003e"
EFFECT_TABLE = 0x80379BFF
SCRIPT_PC = "lbl_8047B610"
TERMINATORS = ("WS_SEQEND", "WS_WAZAEND", "WS_SEQRET", "WS_SPEABIEND", "WS_ALERTEND",
               "WS_GETEND", "WS_ITEMEND")
# Opcodes that continue in another effect's script chosen at run time
# (lbl_80379BFF[...]) or end the script through a helper; walked as roots.
DYNAMIC = {0x63, 0x9E, 0xB7, 0xCC}
SCRIPT_LO, SCRIPT_HI = 0x80375E60, 0x80379BF4  # script bytecode .data


class Dol:
    def __init__(self, data):
        self.data = data
        self.sections = []
        for i in range(18):
            off, addr, size = (struct.unpack(">I", data[base + 4 * i:base + 4 * i + 4])[0]
                               for base in (0x00, 0x48, 0x90))
            if size:
                self.sections.append((addr, size, off))

    def read(self, addr, n):
        for a, s, o in self.sections:
            if a <= addr and addr + n <= a + s:
                return self.data[o + addr - a:o + addr - a + n]
        raise KeyError(hex(addr))


def opcode_table():
    text = (GAME / "data/rodata_80279B78.c").read_text()
    start = text.index("const void* lbl_8027A00C[255]")
    body = text[text.index("{", start) + 1:text.index("};", start)]
    names = re.findall(r"\(void\*\)(\w+)", body)
    if len(names) != 255:
        raise ValueError(f"opcode table has {len(names)} entries")
    return names


def function_bodies():
    """Map function name -> list of source bodies (all definitions found)."""
    bodies = defaultdict(list)
    head = re.compile(r"^[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;{]*\)\s*\{", re.M)
    for path in GAME.rglob("*.c"):
        text = path.read_text(errors="replace")
        for m in head.finditer(text):
            name = m.group(1)
            if name in ("if", "while", "for", "switch"):
                continue
            i = m.end(); depth = 1
            while depth and i < len(text):
                depth += (text[i] == "{") - (text[i] == "}"); i += 1
            bodies[name].append((path.name, text[m.start():i]))
    return bodies


NUM = r"(0x[0-9a-fA-F]+|\d+)"


def canonical(name, bodies):
    """Prefer definitions in split `*exact*` units; the monolithic range file
    keeps stale bodies under #if guards ("body moved to ...")."""
    srcs = bodies.get(name, [])
    exact = [b for b in srcs if "exact" in b[0]]
    return exact or srcs


def scan(src):
    adv, jumps = set(), set()
    aliases = {SCRIPT_PC}
    aliases.update(re.findall(r"\b(\w+)\s*=\s*\*?\s*\(?[\w\s\*\(\)&]*?\)?\s*&?" + SCRIPT_PC + r"\s*;", src))
    alias = "(?:" + "|".join(re.escape(x) for x in aliases) + ")"
    pc_lhs = r"(?:\*\s*\(\s*u32\s*\*\s*\)\s*&\s*)?" + SCRIPT_PC
    adv.update(int(v, 0) for v in re.findall(pc_lhs + r"\s*\+=\s*" + NUM, src))
    if re.search(SCRIPT_PC + r"\s*\+\+|\+\+\s*" + SCRIPT_PC, src):
        adv.add(1)
    # A right-hand side starting with a dereference is a jump, not an advance.
    plus = r"\s*=(?!\s*\*)\s*\(?[\w\s\*\(\)]*?\)?\s*\(?\s*(?:\(\s*\w+\s*\)\s*)?" + alias + r"\s*\+\s*" + NUM + r"\s*\)?\s*;"
    adv.update(int(v, 0) for v in re.findall(pc_lhs + plus, src))
    # next-PC temporaries assigned to PC later
    for tmp, v in re.findall(r"\b(\w+)" + plus, src):
        if tmp not in aliases and re.search(pc_lhs + r"\s*=\s*" + re.escape(tmp) + r"\s*;", src):
            adv.add(int(v, 0))
    jumps.update(int(v, 0) for v in re.findall(
        # 4-byte operands only: u32/s32/int reads or pointer-to-pointer reads.
        r"\*\s*\(\s*(?:(?:u32|s32|int)\s*\*|\w+\s*\*\s*\*)\s*\)\s*\(\s*(?:\(\s*\w+\s*\*\s*\)\s*)?" + alias + r"\s*\+\s*" + NUM + r"\s*\)", src))
    jump_write = bool(re.search(pc_lhs + r"\s*=\s*\*\s*\(", src)) or bool(re.search(pc_lhs + r"\s*=\s*(?:nxt|jumpTarget)\b", src))
    return adv, jumps, jump_write


def analyse(name, bodies, depth=0, seen=None):
    """Return (advances, jump_operand_offsets, unconditional_jump, found).

    Follows helper calls (two levels) when a handler has no PC write of its own."""
    seen = seen if seen is not None else set()
    seen.add(name)
    srcs = canonical(name, bodies)
    adv, jumps, writes_jump = set(), set(), False
    for _, src in srcs:
        a, j, w = scan(src)
        adv |= a; jumps |= j; writes_jump |= w
    if not adv and not writes_jump and depth < 2:
        for _, src in srcs:
            body = src[src.index("{"):]
            for callee in re.findall(r"\b(\w+)\s*\(", body):
                if callee in bodies and callee not in seen and callee not in ("pokemonGetStatus", "wazaGetStatus"):
                    a, j, u, _ = analyse(callee, bodies, depth + 1, seen)
                    adv |= a; jumps |= j
    unconditional = writes_jump and not adv
    return adv, jumps, unconditional, bool(srcs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dol"); ap.add_argument("rel")
    ap.add_argument("--moves", default="33,39,45,44,93,115,216,269,270,289,290,165")
    ap.add_argument("--sizes", help="override opcode sizes, e.g. 0x1f=6,0x22=7")
    # The interpreter runs the faint sub-script (lbl_80378798) directly.
    ap.add_argument("--extra-roots", default="0x80378798")
    a = ap.parse_args()
    dol_bytes = Path(a.dol).read_bytes(); rel = Path(a.rel).read_bytes()
    if hashlib.sha1(dol_bytes).hexdigest() != DOL_SHA1 or hashlib.sha1(rel).hexdigest() != REL_SHA1:
        sys.exit("input hash mismatch")
    dol = Dol(dol_bytes)
    names = opcode_table(); bodies = function_bodies()
    info = [analyse(n, bodies) for n in names]
    # Lengths read from the handler source where a shared byte-reader helper
    # adds a spurious +1 advance (accuracy check 0x01: jump @1, move @5;
    # 0x7B: jump @1, battler @5).
    override = {0x01: 7, 0x7B: 6}
    if a.sizes:
        for kv in a.sizes.split(","):
            k, v = kv.split("="); override[int(k, 0)] = int(v, 0)

    effects = {}
    for m in map(int, a.moves.split(",")):
        row = rel[0x184C + 0x11C7C4 + m * 56:][:56]
        effects.setdefault(struct.unpack(">H", row[26:28])[0], []).append(m)

    seen_ops = defaultdict(set); problems = []; dynamic = set()
    visited = set(); queue = []
    roots = {}
    for eff in sorted(effects):
        ptr = struct.unpack(">I", dol.read(EFFECT_TABLE + 4 * eff, 4))[0]
        roots[eff] = ptr; queue.append(ptr)
    for r in filter(None, a.extra_roots.split(",")):
        queue.append(int(r, 0))
    listing = defaultdict(list)
    while queue:
        start = queue.pop()
        pc = start
        while pc not in visited:
            visited.add(pc)
            op = dol.read(pc, 1)[0]
            adv, jumps, unconditional, found = info[op]
            size = override.get(op)
            if size is None and unconditional and jumps:
                size = max(jumps) + 4
            if size is None:
                if len(adv) == 1:
                    size = next(iter(adv))
                elif adv:
                    size = max(adv)
                    # A conditional jump also matches its operand read; the
                    # length is then jump offset + 4. Anything else is flagged.
                    if len(adv) > 1 and not (jumps and size == max(jumps) + 4):
                        problems.append(f"{pc:08x} op {op:#04x} {names[op]}: several advances {sorted(adv)}; used max")
            seen_ops[op].add(pc)
            raw = dol.read(pc, size or 1)
            listing[start].append(f"  {pc:08x}: {op:02x} {names[op]:<28} {raw[1:].hex()}")
            for j in sorted(jumps):
                if size and j + 4 <= size:
                    target = struct.unpack(">I", dol.read(pc + j, 4))[0]
                    if SCRIPT_LO <= target < SCRIPT_HI:  # data pointers name variables
                        queue.append(target)
            if not size and names[op] in TERMINATORS:
                break
            if not size and op in DYNAMIC:
                dynamic.add(op)
                break
            if not size:
                problems.append(f"{pc:08x} op {op:#04x} {names[op]}: no static advance (found={found}); stopped")
                break
            if unconditional or (not adv and not jumps):
                break
            if names[op] in TERMINATORS:
                break
            pc += size

    print("effects:", {e: effects[e] for e in sorted(effects)})
    print("roots:", {e: hex(p) for e, p in roots.items()})
    print(f"\nopcode closure ({len(seen_ops)} opcodes):")
    for op in sorted(seen_ops):
        adv, jumps, unconditional, found = info[op]
        print(f"  {op:#04x} {names[op]:<28} advances={sorted(adv)} jump_offsets={sorted(jumps)}{' goto' if unconditional else ''} uses={len(seen_ops[op])}")
    print("\ndynamic continuations:", " ".join(f"{op:#04x} {names[op]}" for op in sorted(dynamic)))
    print(f"scripts walked: {len(listing)}")
    print("\nproblems:")
    for p in problems:
        print(" ", p)
    print("\nlistings:")
    for start in sorted(listing):
        print(f"script {start:08x}")
        print("\n".join(listing[start]))


if __name__ == "__main__":
    main()
