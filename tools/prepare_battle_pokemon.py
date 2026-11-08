#!/usr/bin/env python3
"""Extract the pinned temporary pre-catch constructor and its arithmetic closure.

Generated original code belongs in private ignored build/. Boundary changes:
explicit PPC high-first RNG sequencing, uintptr_t status pointer carriers,
typed attest argument, and no original global-address dereferences.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
MANIFEST = ROOT / "config/battle-pokemon-source-files.json"
PINS = {'include/game/pokemon_data.h': 'e92811673afba62df19788d78199e3a3996a85dcad4d2e4fc851c060be712c70',
 'src/game/data/data_803635C0.c': '2cccf5b0231579b72d4709b32bdbb61f47a5881e6e39b79491ff5ffce498ce78',
 'src/game/data/rodata_80279B78.c': '4093b49d69a020ec94e91f97a7fda05ea29dc5fb2aaf7d8fbbbc67b19655c415',
 'src/game/field_range_80117E58.c': '024f1b87bd348a69e3d4a1b309e268c928c803f6dd36b6a691f0da6cec4c2af4',
 'src/game/fight_trainer_db_range_801FBD10.c': '9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d',
 'src/game/fight_trainer_range_801F7F80.c': '29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f',
 'src/game/fight_trainer_range_801FAA58.c': 'f3418bdfb311fe8e33ca06e773a5e82493310d2bff960fde1323a2933b9f90bc',
 'src/game/gamedata.c': '2e04c7a259dcadc78edd7adfcd281ce941c8180af837f786c4672847b8b52da8',
 'src/game/gamedataBios.c': '363c603cddc9476ffb441226500ffacf1072aec5486f1d7fd6638863ba21373c',
 'src/game/gs_msg.c': '7d36a3f685efbca1743d569691dd5fcda6e3c704afb72e926bb1e778957a0ebc',
 'src/game/pokemon_data.c': 'e40fc24c972135df30f2507b00054185c84bb15dcbf9c5c0b121bfa0c7ee7a45',
 'src/game/pokemon_get_status_exact_8012640C.c': '295ff83903d644add435e7beaf8888c35eed1e05d518329b416303fd3cff4682',
 'src/game/pokemon_range_8011F5FC.c': 'e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003',
 'src/game/pokemon_range_8012795C.c': '686f4dbd1bfb44c0c851b603d6442c1cc6a83cc503ffceabbfd13b35f391d4d8',
 'src/game/pokemon_range_exact_801248C4.c': '3bb6f0b890edf39aba65c03aadc41ca144091b6155802b0cbbd385a30f13655a',
 'src/game/waza.c': '5a333dee057a4e865c172da9379d4aecc882af034a3ba837d6651e46449dc00c'}
FUNCTIONS = {
    "src/game/pokemon_range_8011F5FC.c": [
        "pokemonDoItemSoubi", "inline_fn", "pokemonOboeWaza", "pokemonWazaCheckValidInline",
        "pokemonSearchWazaDataIdInline", "pokemonSearchWrapInline",
        "pokemonWazaGetMaxPPInline", "pokemonWazaInitInline",
        "pokemonWazaCreateInline", "pokemonWazaCopyInline", "pokemonSetWazaStatus",
        "pokemonWazaCreate", "pokemonCreate", "pokemonCreateRndFitRand32",
        "pokemonGetSexKotei", "pokemonGetSexFromRnd", "pokemonCreateRndFitGetSex",
        "pokemonCreateRndFitIsRare", "pokemonCreateRndFit"],
    "src/game/pokemon_range_exact_801248C4.c": [
        "pokemonSetTokuseiFlag", "pokemonInit", "pokemonWazaInit",
        "pokemonResetBasisStatus"],
    "src/game/pokemon_get_status_exact_8012640C.c": ["pokemonGetLevelFromExp"],
    "src/game/pokemon_range_8012795C.c": [
        "pokemonSeikakuAdjustStatus", "pokemonCalcLevelStatus", "pokemonSetLevelBasisStatus"],
    "src/game/fight_trainer_range_801F7F80.c": [
        "fightTrainerCreateFightTrainerPokemonDataIdToPokemon"],
}


def function(source, name):
    match = re.search(r"^(?:static )?(?:inline )?[\w* ]+\b" + re.escape(name) +
                      r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError("Pinned constructor definition missing: " + name)
    end, depth = match.end(), 1
    # Comments do not contain unmatched braces in this pinned selection.
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unterminated constructor definition: " + name)
    return source[match.start():end].strip()


def generate(source_root):
    manifest = json.loads(MANIFEST.read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if (manifest["commit"] != COMMIT or lock["decomp"]["commit"] != COMMIT or
            {row["path"]: row["sha256"] for row in manifest["files"]} != PINS):
        raise ValueError("Constructor source lock changed")
    sources = {}
    for row in manifest["files"]:
        raw = (Path(source_root) / row["path"]).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != row["sha256"]:
            raise ValueError("Constructor source differs from pin: " + row["path"])
        sources[row["path"]] = raw.decode("utf8")
    if not set(FUNCTIONS).issubset(sources):
        raise ValueError("Incomplete constructor manifest")
    bodies = []
    for path, names in FUNCTIONS.items():
        for name in names:
            body = function(sources[path], name)
            body = re.sub(r"\bextern\b[^;]+;", "", body)
            body = re.sub(r"^(?:static )?(?:inline )?", "static ", body, count=1)
            body = body.replace("(fn_800E0C54() & 0xFFFF) | (fn_800E0C54() << 16)",
                                "c3p_personality()")
            body = body.replace("*(u32*)(u32)lbl_80478F90", "context.data->species_count")
            body = body.replace("(u32)&local", "(uintptr_t)&local")
            # The learned-move copy is four bytes, never a native word load.
            body = body.replace("u32*", "u8*")
            body = body.replace("gamedataGetStatus(0, 1)", "(const void*)gamedataGetStatus(0, 1)")
            body = body.replace("pokemonSetWazaStatus(ptr, result, buf)",
                                "pokemonSetWazaStatus(ptr, result, (u8)(uintptr_t)buf)")
            body = body.replace("retry:\n", "retry:\n    if (context.error || ++context.attempts > C3_BATTLE_POKEMON_RETRY_LIMIT) { context.error = 1; return 0; }\n")
            bodies.append(body)
    lexical = re.sub(r"/\*.*?\*/|//[^\n]*", "", "\n".join(bodies), flags=re.S)
    symbols = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", lexical))
    symbols -= {"if", "for", "while", "switch", "sizeof", "return", "void",
                "u8", "u16", "u32", "s8", "s16", "s32", "c3p_personality"}
    result = "\n\n".join(bodies)
    for symbol in sorted(symbols, key=len, reverse=True):
        result = re.sub(r"\b" + symbol + r"\b", "c3p_src_" + symbol, result)
    prototypes = [body[:body.index("{")].strip() + ";" for body in
                  re.split(r"\n\n(?=static [\w* ]+\w+\()", result)]
    return ("/* Generated original constructor from " + COMMIT + ". */\n" +
            "\n".join(prototypes) + "\n\n" + result + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_pokemon_original.inc")
    args = parser.parse_args()
    try:
        output = generate(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Constructor extraction failed: {error}\n")
    print(f"Extracted pinned temporary constructor into {args.output}")


if __name__ == "__main__":
    main()
