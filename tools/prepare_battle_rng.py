#!/usr/bin/env python3
"""Extract two pinned RNG bodies; the bounded helper is explicitly CANDIDATE.

Only native deltas: fixed-width type names, private function names, the HSD
seed-pointer reference replaced by a parameter, and explicit seed threading
through the candidate helper's two calls. No arithmetic is rewritten.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
PINS = {
    "src/hsd/random.c": "5cbf926074e0efc33caafa312333cc7b62b7d4124eaea1af238952feb2ececd5",
    "src/game/gs_math_range_800E09E8.c": "f1b57465550055f817be359aef3e56fca930f06cda9eae883b875899adccc498",
}


def extract(source, signature):
    match = re.search(r"^" + re.escape(signature) + r"\s*\{([^{}]*)\}", source, re.M)
    if not match:
        raise ValueError("Pinned RNG definition missing: " + signature)
    return match.group(1)


def generate(source_root):
    manifest = json.loads((ROOT / "config/battle-rng-source-files.json").read_text())
    if manifest["commit"] != COMMIT or {row["path"]: row["sha256"] for row in manifest["functions"]} != PINS:
        raise ValueError("Battle RNG source manifest pin changed")
    sources = {}
    for path, expected in PINS.items():
        data = (source_root / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Battle RNG source differs from pin: " + path)
        sources[path] = data.decode("utf8")
    next_body = extract(sources["src/hsd/random.c"], "s32 fn_801ADCD8(void)")
    bounded = extract(sources["src/game/gs_math_range_800E09E8.c"], "u32 _fadeEffectGetRandom__FUl(u32 mod)")
    if next_body.count("*lbl_80478C94") != 3 or bounded.count("fn_801ADCD8()") != 2:
        raise ValueError("Pinned RNG context binding changed")
    native_next = next_body.replace("*lbl_80478C94", "*seed")
    native_bounded = bounded.replace("fn_801ADCD8()", "c3_battle_source_next16(seed)")
    native_bounded = re.sub(r"\bu32\b", "uint32_t", native_bounded)
    reference_bounded = bounded.replace("fn_801ADCD8()", "c3_reference_rand()")
    return ("/* Generated from pinned " + COMMIT + "; do not edit.\n"
            " * Modulo source remains CANDIDATE. docs/rng-qualification.md records 72 original\n"
            " * PowerPC instruction cases for this slice; full battle RNG call graphs remain unqualified.\n"
            " * Generate with tools/prepare_battle_rng.py. No arithmetic changes. */\n"
            "#ifdef C3_BATTLE_RNG_REFERENCE\n"
            "static u32 c3_reference_bounded(u32 mod)\n{" + reference_bounded + "}\n"
            "#else\n#include <stdint.h>\n"
            "static int32_t c3_battle_source_next16(uint32_t *seed)\n{" + native_next + "}\n"
            "static uint32_t c3_battle_source_bounded32(uint32_t *seed, uint32_t mod)\n{" + native_bounded + "}\n"
            "#endif\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_rng_source.h")
    args = parser.parse_args()
    try:
        generated = generate(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated, encoding="utf8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Battle RNG extraction failed: {error}\n")
    print(f"Extracted pinned RNG bodies into {args.output}; modulo helper remains CANDIDATE")


if __name__ == "__main__":
    main()
