#!/usr/bin/env python3
"""Extract pinned trainer sequence loader control into a test-only reference.

The generated file is not a production resource implementation. Original
archive/model/particle loading remains behind explicitly supplied services.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
HASHES = {
    "src/game/sequence.c": "5197f33a164dd2698f04c01272f0deaa7f02629f010856b535684277f5daa1dd",
    "src/game/fight_trainer_range_801F7F80.c": "29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f",
}


def extract(source, signature):
    if source.count(signature) != 1:
        raise ValueError(f"Expected one source body: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unbalanced pinned source body")
    return source[start:end]


def prepare(output, donor=None):
    donor = Path(donor) if donor is not None else ROOT / "vendor/colosseum"
    lock = json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))
    if lock["decomp"]["commit"] != PIN:
        raise ValueError("Unsupported source pin; qualification must be renewed")
    sources = {}
    for path, digest in HASHES.items():
        raw = (donor / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError(f"Source drift: {path}")
        sources[path] = raw.decode("utf-8")
    sequence = sources["src/game/sequence.c"]
    trainer = sources["src/game/fight_trainer_range_801F7F80.c"]
    wait = extract(sequence, "static inline void sequenceWaitResource(")
    load = extract(sequence, "void* fn_801DE418(u16 index)")
    wrapper = extract(trainer, "void* fightTrainerCreateSequence(void* arg0)")
    # Only the pointer-width conversion used for a mocked scalar accessor is
    # widened. Source order, branches, return values and writes are unchanged.
    old = "(u16)(u32)fightTrainerGetStatus(0, arg0, 9, 0)"
    if wrapper.count(old) != 1:
        raise ValueError("Trainer selector conversion changed")
    wrapper = wrapper.replace(old, "(u16)(uintptr_t)fightTrainerGetStatus(0, arg0, 9, 0)")
    # The original wrapper TU declares a u32 argument but passes a narrowed
    # u16; the definition is u16. Reconcile that cross-TU declaration so the
    # isolated reference can compile in one host TU. Do not change the call.
    prototype = "extern void* fn_801DE418(u32);"
    if wrapper.count(prototype) != 1:
        raise ValueError("Trainer loader declaration changed")
    wrapper = wrapper.replace(prototype, "extern void* fn_801DE418(u16);")
    output = Path(output)
    if output.resolve().is_relative_to(donor.resolve()):
        raise ValueError("Never generate into the source dependency")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("/* Generated test-only pinned control reference. */\n" +
                      wait + "\n" + load + "\n" + wrapper + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/battle_sequence_reference.h")
    args = parser.parse_args()
    prepare(args.output, args.donor)
