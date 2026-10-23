"""Verify field-item provenance and extract the unmodified HP recovery routine."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = "hpRecover__FP20ITEMUSE2POKEMON_LOG1PsP7PokemonUcbUsP12FightPokemon"


def extract(root=ROOT):
    spec = json.loads((root / "config/field-items-source-files.json").read_text())
    lock = json.loads((root / "source-lock.json").read_text())
    if spec["source_commit"] != lock["decomp"]["commit"]:
        raise ValueError("Field-item source pin differs from source-lock.json")
    for entry in spec["files"]:
        data = (root / "vendor/colosseum" / entry["path"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError("Field-item source drift: " + entry["path"])
    source = (root / "vendor/colosseum/src/game/people/people_data.c").read_text()
    start = source.index("typedef struct ItemUsePokemonLog {")
    helper_end = source.index("#if !defined(PEOPLE_DATA_80142EF8_ONLY)", start)
    func_start = source.index("void " + NAME + "(", helper_end)
    end = source.index("\n}\n#endif", func_start) + 3
    code = source[start:helper_end] + source[func_start:end]
    # Only namespace identifiers. Arithmetic and control flow are unchanged.
    names = ("ItemUsePokemonLog", "peopleItemUseLogAdd", NAME,
             "pokemonBiosGetHp", "pokemonBiosGetMaxHp", "pokemonBiosSetHp",
             "fightFloorGetFightPokemonPtrToFightTrainerPtr",
             "fightTrainerCheckFightPokemonFightOut", "fn_802331A4")
    import re
    for name in names:
        code = re.sub(r"\b" + name + r"\b", "c3fi_src_" + name, code)
    code = code.replace("void c3fi_src_" + NAME + "(", "static void c3fi_src_" + NAME + "(", 1)
    return "/* Generated from hash-verified upstream; do not edit. */\n" + code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/field_items_original.h")
    args = parser.parse_args()
    code = extract()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != code:
        args.output.write_text(code)


if __name__ == "__main__":
    main()
