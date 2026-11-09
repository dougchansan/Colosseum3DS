#!/usr/bin/env python3
"""Extract the pinned original damage body without changing its arithmetic."""
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PIN = "fd9637901db913df32bae06fef5478fb152dfcb212a82349e099d8ca2d1f338a"
SOURCE = "src/game/fight_range_80211A00.c"


def extract(source, prefix="c3p_src_"):
    text = source.read_text(encoding="utf8")
    data = text.replace("\r\n", "\n").encode()
    if hashlib.sha256(data).hexdigest() != PIN:
        raise ValueError("Original battle damage source differs from pin")
    start = text.index("s32 fn_80232110(u32 attacker")
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    body = text[start:end]
    # All external engine services are explicit native adapters in one TU;
    # private symbols cannot collide with other imported routines.
    names = set(re.findall(r"extern\s+\w+\s+(\w+)\s*(?:\(|\[)", body))
    names.add("fn_80232110")
    for name in sorted(names, key=len, reverse=True):
        body = re.sub(r"\b"+name+r"\b", prefix+name, body)
    return "/* Generated pinned damage arithmetic; do not edit. */\n" + body + "\n"


if __name__ == "__main__":
    # Two bindings of the same pinned arithmetic: the preview's (c3p_src_) and
    # the battle state's (c3s_src_), so both can link into one program.
    for name, prefix in (("battle_damage_original.inc", "c3p_src_"), ("battle_damage_state.inc", "c3s_src_")):
        output = ROOT / "build" / name
        data = extract(ROOT / "vendor/colosseum" / SOURCE, prefix)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(data, encoding="utf8", newline="\n")
