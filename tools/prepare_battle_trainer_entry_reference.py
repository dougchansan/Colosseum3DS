#!/usr/bin/env python3
"""Extract the pinned original trainer-entry action into a host-only oracle.

Original control is retained. Test services supply its dependencies. Host
pointer conversions are explicitly widened and the original uninitialized
stack image is seeded by a test hook, never read as indeterminate C storage.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
FILE = "src/game/fight_action.c"
HASH = "df209c49abae9f308eb3c5bec9fd53212064191e69f9e36fb2ff5e7eee22950f"
SIGNATURE = "u32 fightActionFlowKaisiNyuujouTrainer(void* action)"


def prepare(output, donor=None):
    donor = Path(donor) if donor is not None else ROOT / "vendor/colosseum"
    if json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))["decomp"]["commit"] != PIN:
        raise ValueError("Unsupported source pin")
    raw = (donor / FILE).read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(raw).hexdigest() != HASH:
        raise ValueError("Trainer-entry source drift")
    source = raw.decode("utf-8")
    if source.count(SIGNATURE) != 1:
        raise ValueError("Ambiguous trainer-entry signature")
    start = source.index(SIGNATURE)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unbalanced source body")
    body = source[start:end]
    # The test supplies typed dependency declarations before this include.
    # Drop only the obsolete, mutually inconsistent local extern prototypes.
    body, count = re.subn(r"^    extern [^\n]+;\n", "", body, flags=re.M)
    if count != 31:
        raise ValueError(f"Unexpected extern declaration count {count}")
    # Narrow-to-wide cast fixes only for 32-bit scalar reference returns.
    for text in ("(void*)fightEncountDataBiosGetPtr", "(void*)fightTargetGetPtr",
                 "(void*)fightSideGetStatus", "(void*)fightTrainerGetStatus",
                 "(void*)heroGetStatus", "(void*)fightSideGetValidFightTrainerPtr"):
        if text not in body:
            raise ValueError(f"Missing expected reference conversion: {text}")
        body = body.replace(text, text.replace("(void*)", "(void*)(uintptr_t)"))
    needle = "    buff = fightActionDataBiosGetBuff"
    if body.count(needle) != 1:
        raise ValueError("Missing stack setup boundary")
    body = body.replace(needle, "    oracle_seed_stack(hero, sizeof hero);\n" + needle)
    body = body.replace(SIGNATURE, "static u32 original_trainer_entry(void* action)", 1)
    output = Path(output)
    if output.resolve().is_relative_to(donor.resolve()):
        raise ValueError("Never generate into dependency")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("/* Test-only pinned source; no production scene stubs. */\n" + body + "\n", encoding="utf-8")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, default=ROOT / "build/battle_trainer_entry_reference.h")
    p.add_argument("--donor", type=Path, default=ROOT / "vendor/colosseum")
    a = p.parse_args()
    prepare(a.output, a.donor)
