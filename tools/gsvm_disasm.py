#!/usr/bin/env python3
"""Print the structure of one original GSVM script bank (developer aid).

Reads a script member from a private archive and lists instructions with
function boundaries, branch targets and native call sites. Immediate values
are shown as raw words (and as a float when the type byte says so). It never
prints message text and writes nothing; redirect output outside the checkout
or into ignored private/ only.

    python3 tools/gsvm_disasm.py --fsys <files>/S1_out.fsys --member 01430E00 --function 9
"""
import argparse
from pathlib import Path
import struct
import sys

from prepare_outskirt import member, u32
from prepare_story import OPCODE_SIZES, be16

NAMES = {0: "nop", 1: "end", 2: "push -1", 3: "drop", 4: "call", 5: "ret",
         6: "jmp", 7: "jz", 8: "imm", 0xD: "op0d", 0xE: "op0e", 0xF: "addr",
         0x10: "pushn", 0x11: "not", 0x12: "neg", 0x13: "load", 0x14: "mul",
         0x15: "div", 0x16: "mod", 0x17: "add", 0x18: "sub", 0x19: "lt",
         0x1A: "le", 0x1B: "gt", 0x1C: "ge", 0x1D: "eq", 0x1E: "ne",
         0x1F: "store", 0x20: "copy", 0x21: "and", 0x22: "or", 0x23: "print",
         0x24: "wait", 0x25: "native"}


def operand(desc):
    """Operand descriptor: low six bits type (2 int, else float), bits 6/7 scope."""
    kind = {2: "i", 3: "f"}.get(desc & 0x3F, f"t{desc & 0x3F}")
    scope = ("stack", "local", "global", "g+idx")[(desc >> 6) & 3] if desc & 0xC0 else "stack"
    return f"{kind}:{scope}"


def disassemble(data, wanted=None):
    count = be16(data, 4)
    functions = [u32(data, 24 + i * 4) for i in range(count)]
    starts = {at: i for i, at in enumerate(functions) if at}
    ordered = sorted(starts)
    lines = []
    for position, start in enumerate(ordered):
        index = starts[start]
        if wanted is not None and index not in wanted:
            continue
        end = ordered[position + 1] if position + 1 < len(ordered) else len(data)
        lines.append(f"; function {index} at {start:04x}..{end:04x}")
        pc, stack = start, []
        while pc < end:
            op = data[pc]
            size = OPCODE_SIZES.get(op)
            if size is None or pc + size > len(data):
                lines.append(f"{pc:04x}: ?? {op:02x} (stop)")
                break
            text = NAMES.get(op, f"op{op:02x}")
            if op == 8:
                value = u32(data, pc + 2)
                kind = data[pc + 1]
                shown = f"{value:#x}"
                if kind & 0x3F != 2:
                    shown += f" ({struct.unpack('>f', struct.pack('>I', value))[0]:g})"
                elif value >= 0x80000000:
                    shown += f" ({value - (1 << 32)})"
                else:
                    shown += f" ({value})"
                text += f" t{kind:02x} {shown}"
                stack.append(value)
            elif op == 4:
                text += f" fn{u32(data, pc + 1)} args={be16(data, pc + 5)} locals={be16(data, pc + 7)}"
            elif op in (6, 7):
                text += f" {u32(data, pc + 1):04x}"
            elif op == 3:
                text += f" {be16(data, pc + 2)}"
            elif op in (0xF, 0x11, 0x12, 0x13):
                text += f" {operand(data[pc + 1])}"
            elif op == 0x10:
                text += f" {operand(data[pc + 1])} n={be16(data, pc + 2)}"
            elif op == 0x1F:
                text += f" {operand(data[pc + 1])} -> {operand(data[pc + 2])}"
            elif op == 0x20:
                text += f" {operand(data[pc + 1])} -> {operand(data[pc + 2])} n={be16(data, pc + 3)}"
            elif op in (0x23, 0x24, 0x25):
                words = be16(data, pc + 3)
                text += f" words={words}"
                if op == 0x25 and len(stack) >= words:
                    native, *arguments = stack[-words:]
                    text += f"  ; native {native:02x}(" + ", ".join(f"{a:#x}" for a in arguments) + ")"
                elif op == 0x25:
                    text += "  ; native id/args produced dynamically"
            elif size == 3:
                text += f" {operand(data[pc + 1])}, {operand(data[pc + 2])}"
            if op != 8:
                # Only straight runs of immediates feed the native annotation.
                stack = []
            lines.append(f"{pc:04x}: {text}")
            pc += size
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--bank", type=Path, help="raw original script bank")
    source.add_argument("--fsys", type=Path, help="original archive")
    parser.add_argument("--member", type=lambda v: int(v, 16), help="member ID for --fsys")
    parser.add_argument("--function", type=int, action="append")
    args = parser.parse_args()
    if args.fsys:
        if args.member is None:
            parser.error("--fsys needs --member")
        data = member(args.fsys.read_bytes(), args.member)
    else:
        data = args.bank.read_bytes()
    sys.stdout.write("\n".join(disassemble(data, set(args.function) if args.function else None)) + "\n")


if __name__ == "__main__":
    main()
