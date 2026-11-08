#!/usr/bin/env python3
"""Specialize the pinned GPL Dolphin sidecar for two original RNG functions.

The generated source remains private build output. No arithmetic callbacks or
instruction replacements are introduced. Dolphin SingleStepInner executes the
admitted original DOL text; the added instrumentation only observes registers
and memory immediately before the HSD return instruction.
"""
import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "vendor/colosseum/tools/decomp_work/semantic_oracle/gpl_sidecar/dolphin_oracle.cpp"
BASE_SHA256 = "3cff014472c2843c0f5841b5653ed171984086ff3b5d21eb7ce0c284ccd79bb1"


def specialize(data):
    data = data.replace(b"\r\n", b"\n")
    if hashlib.sha256(data).hexdigest() != BASE_SHA256:
        raise ValueError("Dolphin sidecar source differs from the reviewed pin")
    source = data.decode()

    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise ValueError("Dolphin instrumentation anchor changed: " + old[:60])
        source = source.replace(old, new)

    replace("constexpr std::size_t kCodeSandboxBytes = 4096;",
            "constexpr std::size_t kCodeSandboxBytes = 1u << 20;")
    replace('  std::string status = "step_limit";',
            '  JsonArray draw_trace;\n  JsonArray instruction_pcs;\n  std::string status = "step_limit";')
    replace("    if (ppc.pc < request.entry_pc || ppc.pc >= code_end || (ppc.pc & 3u) != 0)",
            "    const bool rng_pc = (ppc.pc >= 0x800e0c04u && ppc.pc < 0x800e0c54u) ||\n"
            "                        (ppc.pc >= 0x801adcd8u && ppc.pc < 0x801add0cu);\n"
            "    if (!rng_pc || ppc.pc < request.entry_pc || ppc.pc >= code_end || (ppc.pc & 3u) != 0)")
    replace("    system.GetInterpreter().SingleStepInner();",
            "    if (ppc.pc == 0x801add08u)\n    {\n"
            "      const auto* seed = CheckedRam(system, 0x81600000u, 4, \"RNG trace seed\");\n"
            "      const std::uint32_t value = (std::uint32_t(seed[0]) << 24) |\n"
            "          (std::uint32_t(seed[1]) << 16) | (std::uint32_t(seed[2]) << 8) | seed[3];\n"
            "      JsonObject draw;\n"
            "      draw.emplace(\"return_pc\", picojson::value(Hex32(ppc.pc)));\n"
            "      draw.emplace(\"caller_return_pc\", picojson::value(Hex32(ppc.spr[SPR_LR])));\n"
            "      draw.emplace(\"value\", picojson::value(Hex32(ppc.gpr[3])));\n"
            "      draw.emplace(\"seed_after\", picojson::value(Hex32(value)));\n"
            "      draw_trace.emplace_back(std::move(draw));\n    }\n"
            "    instruction_pcs.emplace_back(picojson::value(Hex32(ppc.pc)));\n"
            "    system.GetInterpreter().SingleStepInner();")
    replace('  result.emplace("instructions", picojson::value(static_cast<double>(instructions)));',
            '  result.emplace("instructions", picojson::value(static_cast<double>(instructions)));\n'
            '  result.emplace("draw_trace", picojson::value(std::move(draw_trace)));\n'
            '  result.emplace("instruction_pcs", picojson::value(std::move(instruction_pcs)));')
    replace('picojson::value("dolphin-interpreter-from-moderngekko-tree")',
            'picojson::value("dolphin-interpreter-rng-qualification")')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(specialize(BASE.read_bytes()), encoding="utf8", newline="\n")


if __name__ == "__main__":
    main()
