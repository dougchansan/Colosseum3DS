#!/usr/bin/env python3
"""Specialize the pinned Dolphin interpreter for admitted original battle leaves.

Generated GPL code and all original instruction bytes remain private. This adds
observation and strict PC guards only; SingleStepInner remains the executor.
"""
from pathlib import Path
import argparse
import hashlib

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'vendor/colosseum/tools/decomp_work/semantic_oracle/gpl_sidecar/dolphin_oracle.cpp'
BASE_SHA256 = '3cff014472c2843c0f5841b5653ed171984086ff3b5d21eb7ce0c284ccd79bb1'
AI_RANGES = ((0x802397B8, 0x802398E4), (0x800E0C54, 0x800E0C78), (0x801ADCD8, 0x801ADD0C))
FLOOR_RANGES = ((0x801F3984, 0x801F3B24), (0x801EF62C, 0x801EF63C),
                (0x801F54A4, 0x801F61BC), (0x801F6B48, 0x801F6B54),
                (0x801F6AE8, 0x801F6B00), (0x8020E0F8, 0x8020E124),
                (0x8020E0E0, 0x8020E0F8), (0x8020E488, 0x8020E4B4),
                (0x8020E3E0, 0x8020E3F8))

def specialize(data):
    data = data.replace(b'\r\n', b'\n')
    if hashlib.sha256(data).hexdigest() != BASE_SHA256:
        raise ValueError('Dolphin sidecar source differs from reviewed pin')
    source = data.decode()
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError('Dolphin instrumentation anchor changed: ' + old[:60])
        source = source.replace(old, new)
    replace('constexpr std::size_t kCodeSandboxBytes = 4096;',
            'constexpr std::size_t kCodeSandboxBytes = 2u << 20;')
    replace('  std::uint32_t entry_pc = 0;', '  std::uint32_t entry_pc = 0;\n  std::uint32_t code_base = 0;')
    replace('  request.max_instructions = AsUnsigned',
            '  request.code_base = AsU32(Required(function, "code_base"), "function.code_base");\n'
            '  if ((request.code_base & 3u) != 0) Fail("unaligned code base");\n'
            '  request.max_instructions = AsUnsigned')
    replace('CheckedRam(system, request.entry_pc, kCodeSandboxBytes, "function code sandbox")',
            'CheckedRam(system, request.code_base, request.code.size(), "function code sandbox")')
    replace('static_cast<std::uint64_t>(request.entry_pc) + kCodeSandboxBytes',
            'static_cast<std::uint64_t>(request.code_base) + request.code.size()')
    replace('patch_end > request.entry_pc', 'patch_end > request.code_base')
    replace('const std::size_t sandbox_begin = request.entry_pc - kMem1Base;',
            'const std::size_t sandbox_begin = request.code_base - kMem1Base;')
    replace('const std::size_t sandbox_end_offset = sandbox_begin + kCodeSandboxBytes;',
            'const std::size_t sandbox_end_offset = sandbox_begin + request.code.size();')
    replace('  std::string status = "step_limit";',
            '  JsonArray draw_trace;\n  JsonArray instruction_pcs;\n  std::string status = "step_limit";')
    replace('static_cast<std::uint64_t>(request.entry_pc) + request.code.size()',
            'static_cast<std::uint64_t>(request.code_base) + request.code.size()')
    condition = lambda ranges: ' ||\n        '.join(f'(ppc.pc >= 0x{lo:08x}u && ppc.pc < 0x{hi:08x}u)' for lo, hi in ranges)
    replace('    if (ppc.pc < request.entry_pc || ppc.pc >= code_end || (ppc.pc & 3u) != 0)',
            '    const bool admitted = request.function == "ai_argmax" ?\n        (' + condition(AI_RANGES) + ') :\n'
            '        (request.function == "combine_result" && (' + condition(FLOOR_RANGES) + '));\n'
            '    if (!admitted || ppc.pc < request.code_base || ppc.pc >= code_end || (ppc.pc & 3u) != 0)')
    replace('    system.GetInterpreter().SingleStepInner();',
            '''    if (ppc.pc == 0x801add08u)
    {
      const auto* seed = CheckedRam(system, 0x81600000u, 4, "RNG trace seed");
      const std::uint32_t value = (std::uint32_t(seed[0]) << 24) |
          (std::uint32_t(seed[1]) << 16) | (std::uint32_t(seed[2]) << 8) | seed[3];
      JsonObject draw;
      draw.emplace("return_pc", picojson::value(Hex32(ppc.pc)));
      draw.emplace("caller_return_pc", picojson::value(Hex32(ppc.spr[SPR_LR])));
      draw.emplace("value", picojson::value(Hex32(ppc.gpr[3])));
      draw.emplace("seed_after", picojson::value(Hex32(value)));
      draw_trace.emplace_back(std::move(draw));
    }
    instruction_pcs.emplace_back(picojson::value(Hex32(ppc.pc)));
    system.GetInterpreter().SingleStepInner();''')
    replace('  result.emplace("instructions", picojson::value(static_cast<double>(instructions)));',
            '  result.emplace("instructions", picojson::value(static_cast<double>(instructions)));\n'
            '  result.emplace("draw_trace", picojson::value(std::move(draw_trace)));\n'
            '  result.emplace("instruction_pcs", picojson::value(std::move(instruction_pcs)));')
    replace('picojson::value("dolphin-interpreter-from-moderngekko-tree")',
            'picojson::value("dolphin-interpreter-battle-rules-qualification")')
    return source

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(specialize(BASE.read_bytes()), encoding='utf8', newline='\n')

if __name__ == '__main__':
    main()
