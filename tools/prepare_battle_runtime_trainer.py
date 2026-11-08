#!/usr/bin/env python3
"""Extract pinned deterministic runtime trainer reset/create and status closure."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
COMMIT="51bfe4314a218dfaf9592fc80723c50498d0ee55"
MANIFEST=ROOT/"config/battle-runtime-trainer-source-files.json"
PINS = {'src/game/fight_trainer_range_801F7F80.c': '29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f',
 'src/game/fight_trainer_range_801FAA58.c': 'f3418bdfb311fe8e33ca06e773a5e82493310d2bff960fde1323a2933b9f90bc',
 'src/game/fight_trainer_db_range_801FBD10.c': '9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d',
 'src/game/fight_out_pokemon_exact_80206A04.c': '8d07c7557d6e267d2560e8b91e9367cdf46b73b8ea147633b831cfe9fa963d9c',
 'src/game/fight_out_pokemon_exact_802096E8.c': 'ad5a5ca4a6aa8c10a2bd1d6bd6ad6a01f9107df69d8e124e86544ee580de0c03',
 'src/game/fight_trainer.c': 'af36fe5a633618c36fff3d142e897e07642e596e60f20f926b2cb932f9879cc9',
 'src/game/fight_action_range_801F0898.c': 'cfd768bfa86adc1ed507a2e3eb65ffcc741b09768ca89c99659b1ecd6007418c',
 'src/game/fight_action_exact_8020D868.c': '5d46dc1ca755c106823e797d69d4d75659fc435ae18211f0234df3d03d9e4b66',
 'src/game/fight_action_exact_8020D844.c': '32da02079dac801f7a6cbf84d6b00608cef8480061e577a280ad9b2c67d12514',
 'src/game/fight_action_exact_8020D784.c': '10b4f185688e7db9f5380d0718a9a6956981b8a919087d6b1f71d930f9d381eb',
 'include/game/fight_action.h': 'da46b8f3d557e62bd41de62442b6a9fc0a6ae25f3e710e000a1a22d75a765eac',
 'src/game/fight_floor.c': '9bdddefb133af3615f2d66575cc687022f66026f835e9f70a582f6b4d0dd7bd7',
 'src/game/field_range_80117E58.c': '024f1b87bd348a69e3d4a1b309e268c928c803f6dd36b6a691f0da6cec4c2af4',
 'src/game/kaisuu.c': '6d135f8766d7c1122cc9dbdb907f7828b0035190de5ed57ab4adb7fba1196153',
 'src/game/data/data_80315690.c': '4f09d0d7e73dc3cd5afb915bada4d8db2e485cb1f41df53e0ab901b154947a22',
 'src/game/data/data_8035BB50.c': '202cbf5c0c51cfe215d08ee88d075b778649c134e2fb65be339fb0e924d13f4e',
 'src/game/data/rodata_80279B78.c': '4093b49d69a020ec94e91f97a7fda05ea29dc5fb2aaf7d8fbbbc67b19655c415',
 'src/game/status.c': '22085553f727751524861f80d2db09a896fb5e056887b5df25b33a1a12011eaf',
 'src/game/waza.c': '5a333dee057a4e865c172da9379d4aecc882af034a3ba837d6651e46449dc00c',
 'src/game/people/people_data.c': 'a5ce74a0ed6f9497bb4452a9ea63055602ac03801debe0d5409992bb6583cfb6',
 'src/game/pokemon_range_8011F5FC.c': 'e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003'}
FUNCTIONS={
 "src/game/fight_trainer_range_801F7F80.c":["fightTrainerCreate","fightTrainerInit"],
 "src/game/fight_out_pokemon_exact_80206A04.c":["fightOutPokemonInitAry","fightOutPokemonInit"],
 "src/game/fight_action_range_801F0898.c":["fightActionInit"],
 "src/game/fight_trainer.c":["fightTrainerEnemyPokemonInitAry","fightOutPokemonEnemyInitAry"],
 "src/game/fight_out_pokemon_exact_802096E8.c":["fightWazaInit","fightItemInit"],
 "src/game/field_range_80117E58.c":["fn_80119D90","fn_80119DD0","fn_80119E50","fn_80119E90","fn_80119ED0","fn_80119F10","fieldEntryGet","fn_8011B2C0","fn_8011B950"],
 "src/game/kaisuu.c":["kaisuuBiosGetMax","kaisuuBiosGetMin","kaisuuGetKaisuu"],
 "src/game/fight_floor.c":["fightFloorSetShadow"],
}
_spec=importlib.util.spec_from_file_location("c3_runtime_extract_helper",Path(__file__).with_name("prepare_battle_pokemon.py"))
_helper=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_helper)

def array(source,name):
    match=re.search(r"\b"+name+r"\[\d+\]\s*=\s*\{(.*?)\};",source,re.S)
    if not match:raise ValueError("Pinned runtime table missing: "+name)
    text=re.sub(r"/\*.*?\*/|//[^\n]*","",match[1],flags=re.S)
    return bytes(int(n,0) for n in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b",text))

def generate(source_root):
    manifest=json.loads(MANIFEST.read_text());lock=json.loads((ROOT/"source-lock.json").read_text())
    if(manifest["commit"]!=COMMIT or lock["decomp"]["commit"]!=COMMIT or
       {r["path"]:r["sha256"] for r in manifest["files"]}!=PINS):
        raise ValueError("Runtime trainer source lock changed")
    sources={}
    for path,pin in PINS.items():
        raw=(Path(source_root)/path).read_bytes().replace(b"\r\n",b"\n")
        if hashlib.sha256(raw).hexdigest()!=pin:raise ValueError("Runtime trainer source differs from pin: "+path)
        sources[path]=raw.decode("utf8")
    status=array(sources["src/game/data/data_8035BB50.c"],"lbl_8035BBA8")
    kaisuu=array(sources["src/game/data/data_80315690.c"],"lbl_80315690")
    stages=array(sources["src/game/data/rodata_80279B78.c"],"lbl_80279C60")
    if(len(status)!=1720 or status[63*20:64*20].hex()!="0600010000000000002a00000000000000000000" or
       len(kaisuu)!=80 or kaisuu[:8]!=b"\xff"*8 or len(stages)!=16):
        raise ValueError("Runtime no-draw table boundary changed")
    tables="/* Pinned original table bytes; counts equal original initialized globals. */\n"
    for name,data in [("lbl_8035BBA8",status),("lbl_80315690",kaisuu)]:
        tables+="static const u8 "+name+"[]={"+",".join(str(n) for n in data)+"};\n"
    tables+="#define lbl_80478B48 (sizeof(lbl_8035BBA8)/20u)\n#define lbl_80478B20 (sizeof(lbl_80315690)/8u)\n"
    tables+="static const StatusIdTable7 lbl_80279C60={{"+",".join(str(int.from_bytes(stages[i:i+2],"big")) for i in range(0,14,2))+"}};\n"
    bodies=[]
    for path,names in FUNCTIONS.items():
        for name in names:
            body=_helper.function(sources[path],name)
            body=re.sub(r"\bextern\b[^;]+;","",body)
            body=re.sub(r"^(?:static )?(?:inline )?","static ",body,count=1)
            body=body.replace("fightOutPokemonInitAry(u32 param_1", "fightOutPokemonInitAry(u8* param_1")
            body=body.replace("param_1 != 0", "param_1 != NULL")
            body=body.replace("fightOutPokemonInit(int r3)", "fightOutPokemonInit(void* r3)")
            body=body.replace("u32 uVar2;", "uintptr_t uVar2;")
            body=body.replace("fn_8011B950(uVar2", "fn_8011B950((u8*)uVar2")
            body=body.replace("fightOutPokemonEnemyInitAry(uVar2", "fightOutPokemonEnemyInitAry((C3OutEnemy*)uVar2")
            body=body.replace("(u32)pokemonGetStatus", "(uintptr_t)pokemonGetStatus")
            body=body.replace("(int)pokemonGetStatus", "(uintptr_t)pokemonGetStatus")
            body=body.replace("*puVar3 = 0;", "put32((u8*)puVar3, 0);")
            body=body.replace("(void*)0xffffffff", "(uintptr_t)UINT32_MAX")
            body=body.replace("(void *)0xffffffff", "(uintptr_t)UINT32_MAX")
            body=body.replace("(void*)((u32)ctx + (u32)i * 0x14)", "(void*)((u8*)ctx + (u32)i * 0x14)")
            body=body.replace("FightOutPokemonEnemyEntry", "C3OutEnemy")
            body=body.replace("u8* entry;", "const u8* entry;") if name.startswith("fn_80119") else body
            body=body.replace("return *(u16*)(entry + 0x8);", "return be16(entry + 0x8);")
            if name=="fieldEntryGet":
                body=body.replace("base = fn_80119F10(id);", "uintptr_t category = fn_80119F10(id);")
                body=body.replace("base = statusGetStatus(base, obj, 0, fn_80119ED0(id), 0);", "base = (u8*)statusGetStatus((u32)category, obj, 0, fn_80119ED0(id), 0);")
            if name=="kaisuuBiosGetMax":
                body=re.sub(r"struct Entry\s*\{[^{}]*\};", "",body)
                body=body.replace("struct Entry* table;", "")
                body=body.replace("table = (struct Entry*)lbl_80315690;", "")
                body=body.replace("return table[index].end;", "return signed32(lbl_80315690 + index * 8 + 4);")
            if name=="kaisuuBiosGetMin":
                body=body.replace("return *(s32*)(lbl_80315690 + index * 8);", "return signed32(lbl_80315690 + index * 8);")
            bodies.append(body)
    text="\n\n".join(bodies)
    lexical=re.sub(r"/\*.*?\*/|//[^\n]*","",text,flags=re.S)
    symbols=set(re.findall(r"\b([A-Za-z_]\w*)\s*\(",lexical))
    symbols-={"if","for","while","switch","sizeof","return","void","u8","u16","u32","s8","s16","s32","uintptr_t","put32","signed32","be16"}
    for symbol in sorted(symbols,key=len,reverse=True):text=re.sub(r"\b"+symbol+r"\b","c3r_src_"+symbol,text)
    prototypes=[b[:b.index("{")].strip()+";" for b in re.split(r"\n\n(?=static [\w* ]+\w+\()",text)]
    return "/* Generated original runtime trainer from "+COMMIT+". */\n"+tables+"\n"+"\n".join(prototypes)+"\n\n"+text+"\n"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,default=ROOT/"vendor/colosseum")
    parser.add_argument("--output",type=Path,default=ROOT/"build/battle_runtime_trainer_original.inc")
    args=parser.parse_args()
    try:
        text=generate(args.source);args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(text,encoding="utf8",newline="\n")
    except (OSError,ValueError) as error:parser.exit(1,str(error)+"\n")
if __name__=="__main__":main()
