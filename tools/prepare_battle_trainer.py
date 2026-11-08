#!/usr/bin/env python3
"""Extract the pinned parent trainer/hero/item closure into ignored build/."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import importlib.util
_helper_spec = importlib.util.spec_from_file_location("c3_trainer_extract_helper", Path(__file__).with_name("prepare_battle_pokemon.py"))
_helper = importlib.util.module_from_spec(_helper_spec)
_helper_spec.loader.exec_module(_helper)
function = _helper.function
ROOT = Path(__file__).resolve().parents[1]
COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
MANIFEST = ROOT / "config/battle-trainer-source-files.json"
PINS = {'src/game/fight_trainer_range_801F7F80.c': '29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f', 'src/game/hero_savedata_exact_80129280.c': '22b3936eb1b63b858a435a14773b5ede7439e4aece37aa4f32c8e56f82cc84c0', 'src/game/hero_bios_exact_8012A450.c': '660cdb9802501e82f63ed173a610cbeaec85bb79a046e2b95534654da0135505', 'src/game/pokemon_range_8011F5FC.c': 'e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003', 'src/game/gamedata.c': '2e04c7a259dcadc78edd7adfcd281ce941c8180af837f786c4672847b8b52da8', 'src/game/gs_msg.c': '7d36a3f685efbca1743d569691dd5fcda6e3c704afb72e926bb1e778957a0ebc', 'src/game/people/people_data_exact_80141308.c': '6a027894b1e03c364ceb6d8766a31bfc1b0239939f2e61051a99b3ce43ba385e', 'src/game/people/people_data_exact_80142A88.c': '83e9a7e9c6d0959953e3a0daf14ceaaff550315b675d6560b97492bc4c904e8b', 'src/game/people/people_data.c': 'a5ce74a0ed6f9497bb4452a9ea63055602ac03801debe0d5409992bb6583cfb6', 'src/game/fight_trainer_db_range_801FBD10.c': '9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d', 'src/game/fight_trainer_range_801FAA58.c': 'f3418bdfb311fe8e33ca06e773a5e82493310d2bff960fde1323a2933b9f90bc', 'src/game/pokemon_data.c': 'e40fc24c972135df30f2507b00054185c84bb15dcbf9c5c0b121bfa0c7ee7a45', 'src/game/gamedataBios.c': '363c603cddc9476ffb441226500ffacf1072aec5486f1d7fd6638863ba21373c'}
FUNCTIONS = {
 "src/game/fight_trainer_range_801F7F80.c": ["fightTrainerCheckTrainerDataIdValidInline", "fightTrainerGetTrainerDataKindInline", "fightTrainerCreateFightTrainerDataIdToHero"],
 "src/game/hero_savedata_exact_80129280.c": ["heroCreate", "heroInit", "heroAddPokemonToParty", "heroCatchPokemon", "heroItemGetItemKindToItemAryPtr", "heroItemAddItemDataId"],
 "src/game/pokemon_range_8011F5FC.c": ["pokemonSetCatchStatus", "pokemonCheckValid"],
 "src/game/hero_bios_exact_8012A450.c": ["heroBiosCopy"],
 "src/game/people/people_data_exact_80141308.c": ["peopleItemIdValid", "peopleEntryValid", "peopleEntryIs", "peopleFindEntry", "peopleFindEntryBelow", "peopleFindFreeEntry", "peopleEntryClear", "peopleEntryAddCount", "peopleEntryAdd", "peopleEntryPut", "fn_80141308"],
 "src/game/people/people_data_exact_80142A88.c": ["fn_80142A88"],
 "src/game/people/people_data.c": ["fn_801425E8"],
}

def generate(source_root):
    manifest = json.loads(MANIFEST.read_text())
    lock = json.loads((ROOT / "source-lock.json").read_text())
    if (manifest["commit"] != COMMIT or lock["decomp"]["commit"] != COMMIT or
        {r["path"]:r["sha256"] for r in manifest["files"]} != PINS):
        raise ValueError("Trainer source lock changed")
    sources = {}
    for path, pin in PINS.items():
        raw = (Path(source_root) / path).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != pin:
            raise ValueError("Trainer source differs from pin: " + path)
        sources[path] = raw.decode("utf8")
    bodies=[]
    for path,names in FUNCTIONS.items():
        for name in names:
            source=re.sub(r"asm void \w+\([^{}]*\)\s*\{[^{}]*\}", "", sources[path])
            body=function(source,name)
            body=re.sub(r"\bextern\b[^;]+;", "", body)
            body=re.sub(r"^(?:static )?(?:inline )?", "static ",body,count=1)
            if name=="heroBiosCopy":
                body=body.replace("u32*", "void*")
                body=body.replace("*(HeroCopyData*)dst = *(HeroCopyData*)src;", "memmove(dst, src, 0xB18);")
            body=body.replace("u8 pokemon[0x138];", "u8* pokemon = context.scratch;")
            if name=="heroItemGetItemKindToItemAryPtr":
                # Source default leaves flag2 undefined. Unsupported kinds are
                # rejected before that path instead of supplying a fake flag.
                body=body.replace("switch (kind) {", "switch (kind) {\n    default: context.error = 1; return NULL;")
            body=body.replace("HeroSaveData*", "u8*")
            body=body.replace("heroCreate(hero, name, sex)", "heroCreate(hero, (uintptr_t)name, sex)")
            body=body.replace("name = GSmsgGetGSchar(", "name = (void*)GSmsgGetGSchar(")
            body=body.replace("slot = heroGetStatus(", "slot = (void*)heroGetStatus(")
            body=body.replace("item_array = heroGetStatus(", "item_array = (void*)heroGetStatus(")
            body=body.replace("fn_80142A88(heroGetStatus(", "fn_80142A88((C3ItemWord*)heroGetStatus(")
            body=body.replace("fn_80141308(\n        items,", "fn_80141308(\n        (C3ItemWord*)items,")
            body=body.replace("u32 trainer_name;", "uintptr_t trainer_name;")
            body=body.replace("u32 trainer_id, u8 sex", "uintptr_t trainer_id, u8 sex")
            body=body.replace("u32 arg6, u32 arg7)", "u32 arg6, uintptr_t arg7)")
            body=body.replace("(u32)&empty_name", "(uintptr_t)&empty_name")
            body=body.replace("heroGetStatus(hero, 2, 0)))", "(u32)heroGetStatus(hero, 2, 0)))")
            body=body.replace("*(u32*)(u32)lbl_80478F90", "context.species_count")
            body=body.replace("u32*", "C3ItemWord*")
            if name=="fn_801425E8":
                body=body.replace("u32 temp;", "C3ItemWord temp;")
                body=body.replace("PEOPLE_ENTRY_VALID", "peopleEntryValid")
                body=body.replace("PEOPLE_ENTRY_ID(entry)", "itemGetStatus(entry, 0, 0x1B, 0)")
                body=body.replace("PEOPLE_ENTRY_ID(other)", "itemGetStatus(other, 0, 0x1B, 0)")
            body=body.replace("lbl_80478BD8", "context.data->item_count")
            # Original item words are four bytes; C3ItemWord preserves stride and copies.
            body=body.replace("heroGetStatus(ptr, 2, 0);", "(u32)heroGetStatus(ptr, 2, 0);")
            bodies.append(body)
    text="\n\n".join(bodies)
    lexical=re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    symbols=set(re.findall(r"\b([A-Za-z_]\w*)\s*\(",lexical))
    symbols-={"if","for","while","switch","sizeof","return","void","u8","u16","u32","s8","s16","s32","uintptr_t","memmove"}
    for symbol in sorted(symbols,key=len,reverse=True):
        text=re.sub(r"\b"+symbol+r"\b","c3t_src_"+symbol,text)
    prototypes=[b[:b.index("{")].strip()+";" for b in re.split(r"\n\n(?=static [\w* ]+\w+\()",text)]
    return "/* Generated pinned trainer parent from "+COMMIT+". */\n"+"\n".join(prototypes)+"\n\n"+text+"\n"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,default=ROOT/"vendor/colosseum")
    parser.add_argument("--output",type=Path,default=ROOT/"build/battle_trainer_original.inc")
    args=parser.parse_args()
    try:
        text=generate(args.source)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(text,encoding="utf8",newline="\n")
    except (OSError,ValueError) as e:
        parser.exit(1,str(e)+"\n")
if __name__=="__main__":
    main()
