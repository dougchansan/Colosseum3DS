#!/usr/bin/env python3
"""Extract pinned party selection/sorting; generated original code stays in build/."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
MANIFEST = ROOT / "config/battle-party-sort-source-files.json"
PINS = {
 "include/game/pokemon_data.h": "e92811673afba62df19788d78199e3a3996a85dcad4d2e4fc851c060be712c70",
 "src/game/pokemon_data.c": "e40fc24c972135df30f2507b00054185c84bb15dcbf9c5c0b121bfa0c7ee7a45",
 "src/game/gamedata.c": "2e04c7a259dcadc78edd7adfcd281ce941c8180af837f786c4672847b8b52da8",
 "src/game/fight_range_80211A00.c": "fd9637901db913df32bae06fef5478fb152dfcb212a82349e099d8ca2d1f338a",
 "src/game/fight_range_exact_80233DB0.c": "087e9e8ec64c1aef70291833902084b8ca416f1044de31218428e3b02f93d3df",
 "src/game/fight_trainer_range_801F7F80.c": "29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f",
 "src/game/fight_trainer_db_range_801FBD10.c": "9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d",
 "src/game/pokemon_range_8011F5FC.c": "e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003",
 "src/game/pokemon_get_status_exact_8012640C.c": "295ff83903d644add435e7beaf8888c35eed1e05d518329b416303fd3cff4682",
 "src/game/hero_savedata_exact_80129280.c": "22b3936eb1b63b858a435a14773b5ede7439e4aece37aa4f32c8e56f82cc84c0",
 "src/game/gs_colsys.c": "5b9bb5ec1c6ebb3561ed34a18bc0745c305430bfd6136ed3b9e5d966b55f8951",
}
FUNCTIONS = {
 "src/game/fight_trainer_range_801F7F80.c": ["fightTrainerCheckValidInline", "fightTrainerGetHeroKindInline", "fightTrainerSortFightTrainerDataIdToHeroTemotiPokemon"],
 "src/game/fight_range_80211A00.c": ["sumPower", "fn_802331F4"],
 "src/game/fight_range_exact_80233DB0.c": ["fn_80233DB0"],
 "src/game/pokemon_range_8011F5FC.c": ["pokemonCheckValid", "pokemonCheckFightOut"],
 "src/game/hero_savedata_exact_80129280.c": ["heroCheckValid"],
 "src/game/gs_colsys.c": ["fn_8010C508", "fn_8010C54C"],
}
ALIASES = {"fn_801233F4": "pokemonCheckFightOut", "fn_80123FBC": "pokemonCheckValid",
 "fn_8012640C": "pokemonGetStatus", "fn_8012A5B0": "heroGetStatus",
 "fn_801F4804": "fightFloorGetFightPokemonEntryCntInc", "fn_801F54A4": "fightFloorGetStatus",
 "fn_801FB1C0": "fightTrainerGetStatus", "fn_80205BE8": "fightPokemonGetPokemonPtr",
 "fn_80206AEC": "fightPokemonCreate"}
_spec = importlib.util.spec_from_file_location("c3_sort_helper", Path(__file__).with_name("prepare_battle_pokemon.py"))
_helper = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_helper)


def lower(body, name):
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    body = re.sub(r"\bextern\b[^;]+;", "", body)
    body = re.sub(r"^(?:static )?(?:inline )?", "static ", body, count=1)
    for old, new in ALIASES.items():
        body = re.sub(r"\b" + old + r"\b", new, body)
    if name == "fightTrainerCheckValidInline":
        body = re.sub(r"\bcontext\b", "trainer", body)
    body = body.replace("void*", "uintptr_t").replace("u8* ptr", "uintptr_t ptr")
    body = body.replace("NULL", "0")
    body = body.replace("*(u32*)(u32)lbl_80478F90", "context->data->counts[C3_PARTY_SPECIES]")
    body = body.replace("PokemonRangeObjHeader* header;", "")
    body = body.replace("(header = (PokemonRangeObjHeader*)lbl_80478F90)->count", "context->data->counts[C3_PARTY_SPECIES]")
    if name == "fightTrainerSortFightTrainerDataIdToHeroTemotiPokemon":
        body = body.replace("uintptr_t selected[6];", "uintptr_t *selected = context->selected;")
        body = body.replace("FightTrainerPokemonCopy sortedPokemon[6];", "FightTrainerPokemonCopy *sortedPokemon = (FightTrainerPokemonCopy*)context->state->scratch;")
        body = body.replace("pokemon = selected[i];", "pokemon = (FightTrainerPokemonCopy*)selected[i];")
        body = body.replace("pokemon = heroGetStatus(hero, 3, i);", "pokemon = (FightTrainerPokemonCopy*)heroGetStatus(hero, 3, i);")
        body = body.replace("pokemonInit(&sortedPokemon[i])", "pokemonInit((uintptr_t)&sortedPokemon[i])")
        body = body.replace("pokemonCheckValid(pokemon)", "pokemonCheckValid((uintptr_t)pokemon)")
        body = body.replace("sortedPokemon[validCount] = *pokemon;", "memcpy(&sortedPokemon[validCount], pokemon, 312);")
        body = body.replace("*pokemon = sortedPokemon[i];", "memcpy(pokemon, &sortedPokemon[i], 312);")
        body = body.replace("fn_802331F4(trainer, hero, selected,", "fn_802331F4(trainer, hero, (uintptr_t)selected,")
    if name in ("sumPower", "fn_802331F4", "fn_80233DB0"):
        body = body.replace("u32 r3", "uintptr_t r3").replace("u32 r4", "uintptr_t r4")
        body = body.replace("int r5", "uintptr_t r5").replace("int r8", "uintptr_t r8")
    if name == "sumPower":
        body = body.replace("int pk", "uintptr_t pk").replace("u32 poke;", "uintptr_t poke;")
    if name == "fn_802331F4":
        body = re.sub(r"typedef struct TrainerSnapshot\s*\{[^}]*\} TrainerSnapshot;", "", body)
        body = re.sub(r"struct\s*\{\s*u32 \*value;\s*\} puVar3;", "", body)
        body = body.replace("u32 auStack_310 [5];", "uintptr_t *auStack_310 = context->candidates;")
        body = body.replace("u32 savedTrainer0[86];", "u8 savedTrainer0[340];")
        body = body.replace("u32 savedTrainer1[85];", "u8 savedTrainer1[340];")
        for num, var in ((0, "iVar6"), (1, "iVar7")):
            body = body.replace(f"*(TrainerSnapshot *)(savedTrainer{num} + 1) = *(TrainerSnapshot *){var};", f"memcpy(savedTrainer{num}, (void*){var}, 340);")
            body = body.replace(f"*(TrainerSnapshot *){var} = *(TrainerSnapshot *)(savedTrainer{num} + 1);", f"memcpy((void*){var}, savedTrainer{num}, 340);")
        for var in ("iVar6", "iVar7"):
            body = body.replace("int " + var + ";", "uintptr_t " + var + ";")
            body = body.replace(var + " = (int)fightTrainerGetStatus", var + " = fightTrainerGetStatus")
        body = body.replace("u32 uVar5;", "uintptr_t uVar5;").replace("u32 swapTmp;", "uintptr_t swapTmp;")
        body = body.replace("int iVar25;", "int iVar25;\n  uintptr_t swapPointer;")
        body = body.replace("iVar25 = *(u32 *)(r5 + (uVar20 & 0xffff) * 4);",
                            "swapPointer = *(u32 *)(r5 + (uVar20 & 0xffff) * 4);")
        body = body.replace("*(u32 *)(r5 + (uVar22.value & 0xffff) * 4) = iVar25;",
                            "*(u32 *)(r5 + (uVar22.value & 0xffff) * 4) = swapPointer;")
        body = body.replace("iVar26 = (uVar20 & 0xffff) * 4;", "iVar26 = (uVar20 & 0xffff) * sizeof(uintptr_t);")
        body = body.replace(",auStack_310)", ",(uintptr_t)auStack_310)")
        body = re.sub(r"local_318\[([^\]]+)\]", r"(*c3s_category(context, local_318, \1))", body)
        body = body.replace("u8 (*c3s_category(context, local_318, 8));", "u8 local_318[8];")
        body = re.sub(r"(r3|uVar20) \+ 1 & 0xffff", r"(\1 + 1) & 0xffff", body)
    if name == "fn_80233DB0":
        body = re.sub(r"struct cpy85 \{[^}]*\};", "", body)
        body = body.replace("struct cpy85 *iVar5;", "uintptr_t iVar5;").replace("struct cpy85 *iVar7;", "uintptr_t iVar7;")
        body = body.replace("struct cpy85 uStack_198;", "u8 uStack_198[340];").replace("struct cpy85 uStack_2ec;", "u8 uStack_2ec[340];")
        body = body.replace("(struct cpy85 *)", "")
        for buf, var in (("uStack_198", "iVar5"), ("uStack_2ec", "iVar7")):
            body = body.replace(f"{buf} = *{var};", f"memcpy({buf}, (void*){var}, 340);")
            body = body.replace(f"*{var} = {buf};", f"memcpy((void*){var}, {buf}, 340);")
        body = body.replace("int iVar16;", "uintptr_t iVar16;").replace("int iVar8;", "uintptr_t iVar8;")
        body = body.replace("u32 uVar4;", "uintptr_t uVar4;")
        body = body.replace("pokemonCheckValid()", "pokemonCheckValid(iVar16)")
        body = body.replace("(u32)iVar8", "iVar8").replace("(u32)iVar16", "iVar16")
        body = body.replace("= iVar16;", "= iVar16;")
        body = body.replace("return uVar6;", "context->candidate_count = (u16)uVar6; return uVar6;")
        body = body.replace("{", "{\n    context->candidate_count = 0;", 1)
    # Source pointer-array words become checked host-sized slots; serialized
    # references are mapped separately by the public boundary.
    if name in ("fn_802331F4", "fn_80233DB0"):
        body = body.replace("u32 *", "uintptr_t *")
        body = body.replace("*(uintptr_t *)(r5 + iVar26)", "(*c3s_slot(context, r5, iVar26 / sizeof(uintptr_t)))")
        body = re.sub(r"\*\((?:uintptr_t|u32|int)\s*\*\)\((r[58]) \+ (.*?) \* 4\)", r"(*c3s_slot(context, \1, \2))", body)
        body = re.sub(r"\(\(uintptr_t \*\)r5\)\[([^\]]+)\]", r"(*c3s_slot(context, r5, \1))", body)
    if name in ("fn_8010C508", "fn_8010C54C"):
        body = body.replace("lbl_80478B38", "context->data->counts[C3_PARTY_TYPE]")
        body = body.replace("*(u16*)((u8*)lbl_8035B500 + (typeA * 0x2C) + (typeB * 2) + 8)", "c3s_type_value(context, typeA, typeB)")
    body = body.replace("(u32)&terminator", "&terminator")
    # Remove only provably unused decompiler locals, not source control flow.
    for match in list(re.finditer(r"^\s*(?:u32|int|u8|u16|s32|uintptr_t)\s+\*?\s*(\w+);\s*$", body, re.M)):
        var = match[1]
        if len(re.findall(r"\b" + var + r"\b", body)) == 1:
            body = body.replace(match[0], "")
    return body


def generate(source_root):
    manifest = json.loads(MANIFEST.read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if (manifest["commit"] != COMMIT or lock["decomp"]["commit"] != COMMIT or
        {r["path"]: r["sha256"] for r in manifest["files"]} != PINS):
        raise ValueError("Party sort source lock changed")
    sources = {}
    for path, pin in PINS.items():
        raw = (Path(source_root) / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != pin:
            raise ValueError("Party sort source differs from pin: " + path)
        sources[path] = raw.decode("utf8")
    bodies = [lower(_helper.function(sources[path], name), name)
              for path, names in FUNCTIONS.items() for name in names]
    text = "\n\n".join(bodies)
    symbols = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", text))
    symbols -= {"if", "for", "while", "switch", "sizeof", "return", "void", "u8", "u16", "u32", "s8", "s16", "s32", "uintptr_t", "intptr_t", "memcpy", "c3s_slot", "c3s_category", "c3s_type_value"}
    for symbol in sorted(symbols, key=len, reverse=True):
        # Context is explicit and stack-local, including every extracted call.
        text = re.sub(r"\b" + symbol + r"\(", "c3s_src_" + symbol + "(context, ", text)
    for name in [n for names in FUNCTIONS.values() for n in names]:
        text = re.sub(r"^(static [\w* ]+c3s_src_" + name + r")\(context, ",
                      r"\1(C3SortContext *context, ", text, flags=re.M)
    text = text.replace("context, void)", "C3SortContext *context)")
    text = text.replace("context, )", "context)")
    prototypes = [body[:body.index("{")].strip() + ";" for body in re.split(r"\n\n(?=static [\w* ]+\w+\()", text)]
    return "/* Generated pinned party sorting from " + COMMIT + ". */\n" + "\n".join(prototypes) + "\n\n" + text + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_party_sort_original.inc")
    args = parser.parse_args()
    try:
        text = generate(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
