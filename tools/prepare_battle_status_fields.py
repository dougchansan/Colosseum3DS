#!/usr/bin/env python3
"""Generate the FightPokemon/FightOutPokemon status field table from pinned source.

pokemonGetStatus / pokemonSetStatus (selectors 0xCB..0x123) dispatch to
fightPokemonBios* / fightOutPokemonBios* accessors that read or write one
fixed record offset. This tool resolves every selector to its accessor and the
accessor to (offset, width, signedness, index bound/stride, NULL default),
asserts that getter and setter agree, and writes a C table. Selectors that
call other functions are emitted as COMPUTED and must be lowered by hand.
"""
import argparse
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "vendor/colosseum/src/game"
PINS = {
    "pokemon_get_status_exact_8012640C.c": "295ff83903d644add435e7beaf8888c35eed1e05d518329b416303fd3cff4682",
    "pokemon_range_8011F5FC.c": "e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003",
    "fight_trainer_db_range_801FBD10.c": "9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d",
}
FIRST, LAST = 0xCB, 0x123
WIDTH = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4}


def source(name):
    data = (GAME / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != PINS[name]:
        raise ValueError(f"status source drift: {name}")
    return data.decode()


def function_body(text, signature_start):
    i = text.index("{", signature_start); depth = 1; j = i + 1
    while depth:
        depth += (text[j] == "{") - (text[j] == "}"); j += 1
    return text[i:j]


def switch_cases(body, call):
    """selector -> accessor name for `case 0xNN: <call>(...)` single-call cases;
    anything else in range becomes None (computed)."""
    out = {}
    for m in re.finditer(r"case (0x[0-9A-Fa-f]+):(.*?)(?=case 0x|default:|\Z)", body, re.S):
        sel = int(m.group(1), 16)
        if not FIRST <= sel <= LAST:
            continue
        text = m.group(2)
        names = re.findall(r"\b(fight(?:Out)?PokemonBios" + call + r"\w+)\s*\(", text)
        others = re.findall(r"\b(\w+)\s*\(", text)
        others = [o for o in others if o not in ("return", "sizeof", "if", "while", "switch")
                  and (not o.startswith("fight") or "Bios" not in o)]
        out[sel] = names[0] if len(names) == 1 and not others else None
    return out


ACC = r"\*\s*\(\s*(u8|s8|u16|s16|u32|s32)\s*\*\s*\)\s*\(\s*(?:&\s*ptr\s*\[\s*(0x[0-9A-Fa-f]+)\s*\]|ptr\s*\+\s*(0x[0-9A-Fa-f]+))\s*\)"


def accessor(bodies, name, setter):
    body = bodies.get(name)
    if body is None:
        raise ValueError(f"accessor body missing: {name}")
    flat = " ".join(body.split())
    d = {"null": 0, "limit": 0, "stride": 0, "kind": None}
    m = re.search(r"if \(ptr == NULL\) \{ return (-?\w+); \}", flat)
    if m and not setter:
        d["null"] = 0 if m.group(1) == "NULL" else int(m.group(1), 0)
    d["limit_value"] = 0
    m = re.search(r"if \(idx >= (0x[0-9A-Fa-f]+|\d+)\) \{ return\s*(-?\w+)?\s*;", flat)
    if m:
        d["limit"] = int(m.group(1), 0)
        if m.group(2) and m.group(2) != "NULL" and not setter:
            d["limit_value"] = int(m.group(2), 0)
    m = re.search(r"ptr \+= idx \* (0x[0-9A-Fa-f]+|\d+);", flat)
    if m:
        d["stride"] = int(m.group(1), 0)
    m = re.search(ACC + (r" = " if setter else ""), flat)
    if m and (setter or "return " + m.group(0) in flat or re.search(r"return \(?\w*\)?\s*" + re.escape(m.group(0)), flat)):
        d.update(kind="value", type=m.group(1), offset=int(m.group(2) or m.group(3), 16))
        return d
    m = re.search(r"return ptr \+ idx \* (0x[0-9A-Fa-f]+) \+ (0x[0-9A-Fa-f]+);", flat)
    if m and not setter:
        d.update(kind="address", type="u32", offset=int(m.group(2), 16), stride=int(m.group(1), 16))
        return d
    m = re.search(r"return ptr \+ (0x[0-9A-Fa-f]+);", flat)
    if m and not setter:
        d.update(kind="address", type="u32", offset=int(m.group(1), 16))
        return d
    raise ValueError(f"irregular accessor {name}: {flat[:160]}")


def build():
    get_src = source("pokemon_get_status_exact_8012640C.c")
    set_src = source("pokemon_range_8011F5FC.c")
    bios_src = source("fight_trainer_db_range_801FBD10.c")
    # Anchor on the definition: the file also declares a prototype earlier.
    get_body = function_body(get_src, get_src.index("u32 pokemonGetStatus(u8* obj, u32 id, u32 selector, u32 d)\n{"))
    set_body = function_body(set_src, set_src.index("void pokemonSetStatus(u8* obj, u32 id, u32 selector, u32 subindex, u32 value)\n{"))
    bodies = {}
    for m in re.finditer(r"^[\w\*\s]+?\b(fight(?:Out)?PokemonBios(?:Get|Set)\w+)\s*\([^;{]*\)\s*\{", bios_src, re.M):
        bodies[m.group(1)] = function_body(bios_src, m.start())
    gets, sets = switch_cases(get_body, "Get"), switch_cases(set_body, "Set")
    rows = []
    for sel in range(FIRST, LAST + 1):
        g = gets.get(sel, "absent"); s = sets.get(sel, "absent")
        gd = accessor(bodies, g, False) if g not in (None, "absent") else None
        sd = accessor(bodies, s, True) if s not in (None, "absent") else None
        if gd and sd and gd["kind"] == "value":
            for key in ("offset", "limit", "stride"):
                if gd[key] != sd[key]:
                    raise ValueError(f"selector {sel:#x}: getter/setter disagree on {key}")
            if WIDTH[gd["type"]] != WIDTH[sd["type"]]:
                raise ValueError(f"selector {sel:#x}: getter/setter disagree on width")
        rows.append((sel, g, gd, s, sd))
    return rows


def kind_name(name, d):
    if name == "absent":
        return "C3_FIELD_NONE"
    if name is None:
        return "C3_FIELD_COMPUTED"
    return "C3_FIELD_ADDRESS" if d["kind"] == "address" else "C3_FIELD_VALUE"


def emit(rows):
    lines = ["/* Generated by tools/prepare_battle_status_fields.py from pinned decomp source.",
             " * {selector, get kind, set kind, offset, width, signed, index limit, stride,",
             " *  value returned for an out-of-range index} */"]
    for sel, g, gd, s, sd in rows:
        d = gd or sd or {"offset": 0, "type": "u8", "limit": 0, "stride": 0, "null": 0}
        lines.append(f"    {{{sel:#05x}, {kind_name(g, gd)}, {kind_name(s, sd)}, {d['offset']:#06x}, "
                     f"{WIDTH[d['type']]}, {1 if d['type'].startswith('s') else 0}, {d['limit']}, {d['stride']}, "
                     f"{(gd or {}).get('limit_value', 0)}}}, /* {g if g else 'computed'} */")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default=str(ROOT / "build/battle_status_fields.inc"))
    a = ap.parse_args()
    out = Path(a.output); out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(emit(build()), newline="\n")


if __name__ == "__main__":
    main()
