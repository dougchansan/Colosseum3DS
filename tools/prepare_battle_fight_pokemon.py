#!/usr/bin/env python3
"""Extract the pinned deterministic FightPokemon create/reset into ignored build/."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
MANIFEST = ROOT / "config/battle-fight-pokemon-source-files.json"
PINS = {
    "src/game/fight_out_pokemon_exact_80206A04.c": "8d07c7557d6e267d2560e8b91e9367cdf46b73b8ea147633b831cfe9fa963d9c",
    "src/game/fight_trainer_db_range_801FBD10.c": "9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d",
    "src/game/pokemon_get_status_exact_8012640C.c": "295ff83903d644add435e7beaf8888c35eed1e05d518329b416303fd3cff4682",
    "src/game/pokemon_range_8011F5FC.c": "e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003",
    "src/game/field_range_80117E58.c": "024f1b87bd348a69e3d4a1b309e268c928c803f6dd36b6a691f0da6cec4c2af4",
}
FUNCTIONS = {"src/game/fight_out_pokemon_exact_80206A04.c":
             ["fightPokemonCreate", "fightPokemonInit"]}
DEPENDENCIES = ("pokemonGetStatus", "pokemonSetStatus", "pokemonInit",
                "fn_8011B950", "pokemonBiosCopy")

_spec = importlib.util.spec_from_file_location(
    "c3_fight_pokemon_extract_helper", Path(__file__).with_name("prepare_battle_pokemon.py"))
_helper = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_helper)


def generate(source_root):
    manifest = json.loads(MANIFEST.read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if (manifest["commit"] != COMMIT or lock["decomp"]["commit"] != COMMIT or
            {row["path"]: row["sha256"] for row in manifest["files"]} != PINS):
        raise ValueError("FightPokemon source lock changed")
    sources = {}
    for path, pin in PINS.items():
        raw = (Path(source_root) / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != pin:
            raise ValueError("FightPokemon source differs from pin: " + path)
        sources[path] = raw.decode("utf8")
    bodies = []
    for path, names in FUNCTIONS.items():
        for name in names:
            body = _helper.function(sources[path], name)
            body = re.sub(r"\bextern\b[^;]+;", "", body)
            body = re.sub(r"^void\b", "static void", body, count=1)
            body = body.replace(name + "(",
                                "c3fp_src_" + name + "(C3FightPokemonContext *context, ", 1)
            # DOL GetStatus return in r3 feeds the immediately following Init.
            pattern = r"pokemonGetStatus\(([^;]+)\);\s*pokemonInit\(\);"
            body, count = re.subn(pattern, r"pokemonInit(pokemonGetStatus(\1));", body)
            if count != 1:
                raise ValueError("FightPokemon implicit initializer argument changed")
            body = body.replace("u32 uVar1;", "uintptr_t uVar1;")
            body = body.replace("(u32)pokemonGetStatus", "(uintptr_t)pokemonGetStatus")
            body = body.replace("(void*)0xffffffff", "(uintptr_t)UINT32_MAX")
            body = body.replace("0xcb, 0, p2)", "0xcb, 0, (uintptr_t)p2)")
            for symbol in DEPENDENCIES:
                body = re.sub(r"\b" + symbol + r"\(",
                              "c3fp_src_" + symbol + "(context, ", body)
            bodies.append(body)
    return ("/* Generated original FightPokemon closure from " + COMMIT + ". */\n" +
            "\n\n".join(bodies) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_fight_pokemon_original.inc")
    args = parser.parse_args()
    try:
        output = generate(args.source)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf8", newline="\n")
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
