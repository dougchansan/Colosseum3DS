#!/usr/bin/env python3
"""Pin-check and extract the original queue control into a test-only C oracle.

No original source is rewritten or installed in the runtime. The test harness
supplies byte-record accessors, table references, and explicit callbacks so
the unmodified queue functions can execute on a 64-bit host.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def prepare(output, donor=None):
    donor = Path(donor) if donor is not None else ROOT / "vendor/colosseum"
    manifest = json.loads((ROOT / "config/battle-action-source-files.json").read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if manifest["commit"] != lock["decomp"]["commit"]:
        raise ValueError("battle-action source pin differs from source lock")
    for entry in manifest["files"]:
        digest = hashlib.sha256((donor / entry["path"]).read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise ValueError(f"battle-action source drift: {entry['path']}")
    source = (donor / "src/game/fight_action_range_801F0898.c").read_text()
    first = source.index("static inline void fightActionClear(")
    last = source.index("/* 0x801F108C fightActionFifoInit lives")
    queue = source[first:last].replace("#ifndef FIGHT_ACTION_RANGE_801F1170_ONLY\n", "")
    initializer = function((donor / "src/game/fight_action_exact_801F108C.c").read_text(),
                           "void fightActionFifoInit(void)")
    # The original initializer is a separate TU with a byte-array declaration;
    # reconcile that declaration when combining it with the queue TU. Its
    # pointer setter prototypes use u32 for literal NULL; use the queue TU's
    # pointer types on a 64-bit host. No control flow or field values change.
    initializer = initializer.replace("extern u8 lbl_8046D790[];", "")
    initializer = initializer.replace("&lbl_8046D790[(u16)i * 0x30]",
                                      "(u8*)lbl_8046D790 + (u16)i * 0x30")
    for name in ("FightActionDataPtr", "BuffDataPtr", "ActorFightTargetPtr",
                 "MotoFightActionDataPtr"):
        initializer = initializer.replace(f"fightActionBiosSet{name}(void*, u32)",
                                          f"fightActionBiosSet{name}(void*, void*)")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("/* Test-only source oracle; generated from pinned upstream. */\n" +
                      queue + "\n" + initializer + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_action_original.inc")
    args = parser.parse_args()
    prepare(args.output)
