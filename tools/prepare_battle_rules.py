#!/usr/bin/env python3
"""Extract pinned result, win-predicate, AI selection and one-draw RNG wrapper.

Only symbol isolation and scalar floor-handle lowering change the source.
Generated original code remains in the private/ignored build directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
PINS = {
    "src/game/fight_floor.c": "9bdddefb133af3615f2d66575cc687022f66026f835e9f70a582f6b4d0dd7bd7",
    "src/game/fight_range_80211A00.c": "fd9637901db913df32bae06fef5478fb152dfcb212a82349e099d8ca2d1f338a",
    "src/game/gs_math_random_exact_800E0C54.c": "4f46448db151759635ecba7f14ed90b64cb4bfd60f783966ae27fb109fa43eae",
}
FUNCTIONS = (
    ("src/game/fight_floor.c", "u8 fightFloorSetFightResultId(void* floor, u16 id)"),
    ("src/game/fight_floor.c", "u32 fightFloorIsGcHeroWin(void* floor, u16 result)"),
    ("src/game/gs_math_random_exact_800E0C54.c", "u16 fn_800E0C54(void)"),
    ("src/game/fight_range_80211A00.c", "s32 fightTrainerAiGetValueAryMaxBanme(s32* valueAry, u16 count, u8 useRandom)"),
)
SYMBOLS = (
    "fightFloorSetFightResultId", "fightFloorIsGcHeroWin",
    "fightFloorIsGcHeroHostSide", "fightFloorGetStatus",
    "fn_801EF634", "fn_801EF62C", "fn_800E0C54", "fn_801ADCD8",
    "fightTrainerAiGetValueAryMaxBanme",
)


def function(source, signature):
    match = re.search(r"^" + re.escape(signature) + r"\s*\{", source, re.M)
    if not match:
        raise ValueError("Pinned battle rule definition missing: " + signature)
    start, end, depth = match.start(), match.end(), 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unterminated battle rule: " + signature)
    return source[start:end]


def generate(source_root):
    manifest = json.loads((ROOT / "config/battle-rules-source-files.json").read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if (manifest["commit"] != COMMIT or lock["decomp"]["commit"] != COMMIT or
            {row["path"]: row["sha256"] for row in manifest["files"]} != PINS):
        raise ValueError("Battle rules source manifest/lock pin changed")
    sources = {}
    for path, digest in PINS.items():
        data = (Path(source_root) / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Battle rules source differs from pin: " + path)
        sources[path] = data.decode("utf8")
    bodies = []
    for path, signature in FUNCTIONS:
        body = function(sources[path], signature)
        # The only original pointer here is an opaque floor token. It is never
        # dereferenced; replace its type with u32 rather than narrowing a host
        # pointer. The AI value array remains a genuine native s32 pointer.
        body = body.replace("void*", "u32")
        for symbol in SYMBOLS:
            body = re.sub(r"\b" + symbol + r"\b", "c3r_src_" + symbol, body)
        bodies.append("static " + body)
    return ("/* Generated pinned battle rules from " + COMMIT + "; do not edit.\n"
            " * Opaque floor pointer lowered to scalar handle; arithmetic/control unchanged. */\n" +
            "\n\n".join(bodies) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_rules_original.inc")
    args = parser.parse_args()
    try:
        data = generate(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data, encoding="utf8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Battle rules extraction failed: {error}\n")
    print(f"Extracted pinned battle rules into {args.output}")


if __name__ == "__main__":
    main()
