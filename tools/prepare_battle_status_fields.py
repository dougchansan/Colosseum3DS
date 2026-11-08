#!/usr/bin/env python3
"""Generate battle status field tables from pinned source.

Original status dispatchers (pokemonGetStatus, wazaGetStatus,
fightFloorGetStatus and their setters) route each selector to a one-line
*Bios accessor that reads or writes one fixed record offset. This tool
resolves every selector to its accessor and the accessor to (offset, width,
signedness, index bound/stride, out-of-range value), asserts that getter and
setter agree, and writes one C table per dispatcher. Selectors whose case
calls other functions, or whose accessor has an unrecognised form, are
emitted as COMPUTED (listed on stderr) and must be lowered by hand; at run
time they are refused, never guessed.
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "vendor/colosseum/src/game"
PINS = {
    "pokemon_get_status_exact_8012640C.c": "295ff83903d644add435e7beaf8888c35eed1e05d518329b416303fd3cff4682",
    "pokemon_range_8011F5FC.c": "e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003",
    "fight_trainer_db_range_801FBD10.c": "9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d",
    "waza.c": "5a333dee057a4e865c172da9379d4aecc882af034a3ba837d6651e46449dc00c",
    "fight_floor.c": "9bdddefb133af3615f2d66575cc687022f66026f835e9f70a582f6b4d0dd7bd7",
    "fight_floor_bios.c": "b7f47eaebdd6d9f03f609b03fda7fd9ebd3089080ba649c4658fdccb465b4a81",
    "fight_out_pokemon_exact_802096E8.c": "ad5a5ca4a6aa8c10a2bd1d6bd6ad6a01f9107df69d8e124e86544ee580de0c03",
}
DISPATCHERS = (
    dict(name="pokemon", output="battle_status_fields.inc", first=0xCB, last=0x123, data_below=0,
         get=("pokemon_get_status_exact_8012640C.c", "u32 pokemonGetStatus(u8* obj, u32 id, u32 selector, u32 d)\n{"),
         set=("pokemon_range_8011F5FC.c", "void pokemonSetStatus(u8* obj, u32 id, u32 selector, u32 subindex, u32 value)\n{"),
         prefix=r"fight(?:Out)?PokemonBios", bios=("fight_trainer_db_range_801FBD10.c",)),
    dict(name="waza", output="battle_waza_fields.inc", first=0x01, last=0x34, data_below=0x25,
         get=("waza.c", "u32 wazaGetStatus(void* ptr, u16 dataId, u16 status, u32 index) {"),
         set=("waza.c", "void wazaSetStatus(void* ptr, u16 dataId, u16 status, u32 index, u32 value) {"),
         prefix=r"(?:wazaDataBios|fightWazaBios)", bios=("waza.c", "fight_out_pokemon_exact_802096E8.c")),
    dict(name="floor", output="battle_floor_fields.inc", first=0x01, last=0x5F, data_below=0x08,
         get=("fight_floor.c", "s32 fightFloorGetStatus(u8* pkm, u32 slot, u32 field, u32 arg) {"),
         set=("fight_floor.c", "u8 fightFloorSetStatus(u32 p1, u16 p2, u32 p3, u16 p4, u32 p5) {"),
         prefix=r"fightFloor(?:Data)?Bios", bios=("fight_floor_bios.c",)),
)
WIDTH = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4}
KEYWORDS = ("return", "sizeof", "if", "while", "switch", "for")
NUM = r"(0x[0-9A-Fa-f]+|\d+)"


def source(name):
    data = (GAME / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != PINS[name]:
        raise ValueError(f"status source drift: {name}")
    return data.decode()


def function_body(text, start):
    i = text.index("{", start); depth = 1; j = i + 1
    while depth:
        depth += (text[j] == "{") - (text[j] == "}"); j += 1
    return text[i:j]


def switch_cases(body, prefix, verb, first, last):
    """selector -> accessor for single-accessor-call cases; None = computed."""
    out = {}
    for m in re.finditer(r"case (0x[0-9A-Fa-f]+|\d+):(.*?)(?=\bcase (?:0x[0-9A-Fa-f]+|\d+):|\bdefault:|\Z)", body, re.S):
        sel = int(m.group(1), 0)
        if not first <= sel <= last:
            continue
        text = m.group(2)
        names = re.findall(r"\b(" + prefix + verb + r"\w+)\s*\(", text)
        others = [o for o in re.findall(r"\b(\w+)\s*\(", text)
                  if o not in KEYWORDS and not re.fullmatch(prefix + r"\w+", o)]
        out[sel] = names[0] if len(names) == 1 and not others and "extern" not in text else None
    return out


ACC = (r"\*\s*\(\s*(u8|s8|u16|s16|u32|s32)\s*\*\s*\)\s*\(\s*(?:&\s*ptr\s*\[\s*" + NUM +
       r"\s*\]|\(\s*u8\s*\*\s*\)\s*ptr\s*\+\s*" + NUM + r"|ptr\s*\+\s*" + NUM + r")\s*\)")


def accessor(bodies, name, setter):
    """Resolve one accessor; None for an unrecognised form."""
    if name not in bodies:
        return None
    params, body = bodies[name]
    flat = " ".join(body.split())
    names = [p.split()[-1].lstrip("*") for p in params.split(",") if p.strip() and p.strip() != "void"]
    if names:  # the record parameter becomes ptr; an index parameter becomes idx
        flat = re.sub(r"\b" + re.escape(names[0]) + r"\b", "ptr", flat)
    if len(names) == 3 or (len(names) == 2 and not setter):
        flat = re.sub(r"\b" + re.escape(names[1]) + r"\b", "idx", flat)
    d = {"limit": 0, "stride": 0, "limit_value": 0}
    m = re.search(r"if \(idx >= " + NUM + r"\) \{? ?return\s*(-?\w+)?\s*; ?\}?", flat)
    if m:
        d["limit"] = int(m.group(1), 0)
        if m.group(2) and m.group(2) not in ("NULL", "0") and not setter:
            d["limit_value"] = int(m.group(2), 0)
    m = re.search(r"ptr \+= idx \* " + NUM + ";", flat)
    if m:
        d["stride"] = int(m.group(1), 0)
    m = re.search(ACC + (r"\s*=" if setter else ""), flat)
    if m:
        offset = int(next(g for g in m.groups()[1:] if g), 0)
        if setter or re.search(r"return\s*(?:\(\s*\w+\s*\)\s*)?" + re.escape(m.group(0)), flat):
            d.update(kind="value", type=m.group(1), offset=offset)
            return d
    # Byte reads: return (s8)ptr[0xN]; / return ptr[0xN];
    m = re.search(r"return\s*(?:\((u8|s8)\)\s*)?ptr\s*\[\s*" + NUM + r"\s*\]\s*;", flat)
    if m and not setter:
        d.update(kind="value", type=m.group(1) or "u8", offset=int(m.group(2), 0))
        return d
    # Arrays: base = (T*)(ptr + 0xN); return base[idx]; / base[idx] = val;
    m = re.search(r"(\w+) = \(\s*(u8|s8|u16|s16|u32|s32)\s*\*\s*\)\s*\(\s*ptr\s*\+\s*" + NUM + r"\s*\)\s*;", flat)
    if m and re.search(re.escape(m.group(1)) + r"\s*\[\s*idx\s*\]" + (r"\s*=" if setter else r"\s*;"), flat):
        d.update(kind="value", type=m.group(2), offset=int(m.group(3), 0), stride=WIDTH[m.group(2)])
        return d
    if not setter:
        cast = r"\(?(?:u8\s*\*\s*\)\s*)?"
        for rx, off_g, stride_g in ((r"return " + cast + r"ptr \+ idx \* " + NUM + r" \+ " + NUM + ";", 2, 1),
                                    (r"return " + cast + r"ptr \+ " + NUM + r" \+ idx \* " + NUM + ";", 1, 2),
                                    (r"return " + cast + r"ptr \+ " + NUM + ";", 1, None)):
            m = re.search(rx, flat)
            if m:
                d.update(kind="address", type="u32", offset=int(m.group(off_g), 0))
                if stride_g:
                    d["stride"] = int(m.group(stride_g), 0)
                return d
    return None


def build(dispatch):
    get_src, set_src = source(dispatch["get"][0]), source(dispatch["set"][0])
    get_body = function_body(get_src, get_src.index(dispatch["get"][1]))
    set_body = function_body(set_src, set_src.index(dispatch["set"][1]))
    bodies = {}
    for name in dispatch["bios"]:
        text = source(name)
        for m in re.finditer(r"^[\w\*\s]+?\b(" + dispatch["prefix"] + r"(?:Get|Set)\w+)\s*\(([^;{)]*)\)\s*\{", text, re.M):
            bodies.setdefault(m.group(1), (m.group(2), function_body(text, m.start())))
    gets = switch_cases(get_body, dispatch["prefix"], "Get", dispatch["first"], dispatch["last"])
    sets = switch_cases(set_body, dispatch["prefix"], "Set", dispatch["first"], dispatch["last"])
    rows, irregular, mismatched = [], [], []
    for sel in range(dispatch["first"], dispatch["last"] + 1):
        g, s = gets.get(sel, "absent"), sets.get(sel, "absent")
        gd = accessor(bodies, g, False) if g not in (None, "absent") else None
        sd = accessor(bodies, s, True) if s not in (None, "absent") else None
        if g not in (None, "absent") and gd is None:
            irregular.append(g); g = None
        if s not in (None, "absent") and sd is None:
            irregular.append(s); s = None
        if gd and sd and gd["kind"] == "value":
            # The original can route a selector's setter to a different field
            # (wazaSetStatus case 10 calls the case-11 setter). Such a setter
            # is not modelled: it becomes COMPUTED and is reported.
            if any(gd[k] != sd[k] for k in ("offset", "limit", "stride")) or WIDTH[gd["type"]] != WIDTH[sd["type"]]:
                mismatched.append(f"{sel:#x} ({g} / {s})"); s, sd = None, None
        rows.append((sel, g, gd, s, sd))
    if mismatched:
        print(f"{dispatch['name']}: setter not modelled (getter/setter fields differ): {', '.join(mismatched)}",
              file=sys.stderr)
    return rows, irregular


def kind_name(name, d):
    if name == "absent":
        return "C3_FIELD_NONE"
    if name is None or d is None:
        return "C3_FIELD_COMPUTED"
    return "C3_FIELD_ADDRESS" if d["kind"] == "address" else "C3_FIELD_VALUE"


def emit(dispatch, rows):
    lines = [f"/* Generated by tools/prepare_battle_status_fields.py ({dispatch['name']}) from pinned decomp source.",
             " * {selector, get kind, set kind, offset, width, signed, index limit, stride,",
             " *  value returned for an out-of-range index, reads a data row} */"]
    for sel, g, gd, s, sd in rows:
        d = gd or sd or {"offset": 0, "type": "u8", "limit": 0, "stride": 0}
        row = 1 if sel < dispatch["data_below"] else 0
        label = g if g not in (None, "absent") else ("absent" if g == "absent" else "computed")
        lines.append(f"    {{{sel:#05x}, {kind_name(g, gd)}, {kind_name(s, sd)}, {d['offset']:#06x}, "
                     f"{WIDTH[d['type']]}, {1 if d['type'].startswith('s') else 0}, {d['limit']}, {d['stride']}, "
                     f"{(gd or {}).get('limit_value', 0)}, {row}}}, /* {label} */")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output-dir", default=str(ROOT / "build"))
    ap.add_argument("--only", choices=[d["name"] for d in DISPATCHERS])
    a = ap.parse_args()
    out = Path(a.output_dir); out.mkdir(parents=True, exist_ok=True)
    for dispatch in DISPATCHERS:
        if a.only and dispatch["name"] != a.only:
            continue
        rows, irregular = build(dispatch)
        (out / dispatch["output"]).write_text(emit(dispatch, rows), newline="\n")
        if irregular:
            print(f"{dispatch['name']}: computed (unrecognised accessor form): {', '.join(sorted(set(irregular)))}",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
