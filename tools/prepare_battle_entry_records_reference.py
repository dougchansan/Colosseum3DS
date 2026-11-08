#!/usr/bin/env python3
"""Pinned control references for native trainer-entry data services.

This is test-only. Host field getters in the fixture read explicit BE records;
no game assets, original binary code or generated references are distributed.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
FILES = {
    'src/game/pokemon_range_8011F5FC.c': 'e5ad2f1745cea73925404e7feef6cac9a3fb1e2bc4ab87fa42e8022f531b1003',
    'src/game/hero_savedata_exact_80129280.c': '22b3936eb1b63b858a435a14773b5ede7439e4aece37aa4f32c8e56f82cc84c0',
    'src/game/fight_out_pokemon.c': 'fdd85b99d970715a3efb18a1e3913695154590d7edbfc80ec67a51bf9830a03e',
    'src/game/fight_trainer_range_801F7F80.c': '29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f',
    'src/game/fight_side.c': 'e32cbb0dc6e47ed6e4be6096f99e368ee9c22dbaa5b8de21f66284a7650ec2d7',
    'src/game/fight_side_bios.c': '7acfb196fb9a640c6849bd9e06d9438e6dcbbd60b62191502914c0f6871c31f5',
    'src/game/fight_floor.c': '9bdddefb133af3615f2d66575cc687022f66026f835e9f70a582f6b4d0dd7bd7',
    'src/game/fight_floor_bios.c': 'b7f47eaebdd6d9f03f609b03fda7fd9ebd3089080ba649c4658fdccb465b4a81',
    'src/game/fight.c': 'd815d9bf0c3165b4c6dce417c09c064b2de88e16eb8c7dc356d27269715a0d65',
    'src/game/fight_trainer_db_range_801FBD10.c': '9fcc27c3ac276eb922f7365a807a4894d5345f879c2bf9cf5303896476b3412d',
    'src/game/gamedata.c': '2e04c7a259dcadc78edd7adfcd281ce941c8180af837f786c4672847b8b52da8',
    'src/game/fight_target.c': '23b3795b38ebbe6722eff6eb4f7f272ded25b17fa2b570646359d2af658cdcb7',
}


def body(source, signature):
    pattern = re.compile(r"^" + re.escape(signature) + r"\s*\{", re.M)
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError("Ambiguous original definition: " + signature)
    start = matches[0].start()
    end = source.index("{", start) + 1
    depth = 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unbalanced original function")
    return re.sub(r"^\s*extern [^\n]+;\n", "", source[start:end], flags=re.M)


def prepare(output, donor=None):
    donor = Path(donor) if donor else ROOT / "vendor/colosseum"
    output = Path(output)
    if output.resolve().is_relative_to(donor.resolve()):
        raise ValueError("Never generate into dependency")
    if json.loads((ROOT / "source-lock.json").read_text(encoding="utf-8"))["decomp"]["commit"] != PIN:
        raise ValueError("Unsupported source pin")
    sources = {}
    for name, digest in FILES.items():
        raw = (donor / name).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError("Entry-record source drift: " + name)
        sources[name] = raw.decode("utf-8")
    selected = [
        ("src/game/pokemon_range_8011F5FC.c", "u8 pokemonCheckValid(u8* ptr)", "pokemon"),
        ("src/game/pokemon_range_8011F5FC.c", "u32 pokemonCheckFightOut(u8* ptr)", "pokemon"),
        ("src/game/hero_savedata_exact_80129280.c", "u32 heroCheckValid(u8* ptr)", "hero"),
        ("src/game/fight_out_pokemon.c", "static inline void* fightPokemonGetPokemonPtrInline(void* fp)", "fighter"),
        ("src/game/fight_out_pokemon.c", "static inline u8 fightPokemonCheckValidInline(void* fp)", "fighter"),
        ("src/game/fight_out_pokemon.c", "static inline u8 fightPokemonCheckFightOutInline(void* fp)", "fighter"),
        ("src/game/fight_trainer_range_801F7F80.c", "u32 fightTrainerCheckValid(void* context)", "trainer"),
        ("src/game/fight_trainer_range_801F7F80.c", "void* fightTrainerCheckTemotiPokemonFightEntry(void* context, void* moveData)", "trainer"),
        ("src/game/fight_trainer_range_801F7F80.c", "BOOL fightTrainerCheckTrainerDataIdValid(u32 arg0, s32 arg1)", "trainer_scalar"),
        ("src/game/fight_trainer_range_801F7F80.c", "u8 fightTrainerIsGcHero(void* arg0)", "trainer_scalar"),
        ("src/game/fight_side.c", "void fightSideGetFightTrainerGridParam(u32 param_1, u16 param_2, u16 param_3, u8* param_4, s8* param_5)", "grid"),
    ]
    parts = []
    names = [re.search(r"(\w+)\(", sig).group(1) for _, sig, _ in selected]
    for path, sig, kind in selected:
        text = body(sources[path], sig)
        if kind == "pokemon":
            text = text.replace("pokemonGetStatus", "ref_pokemon_scalar")
            # Source's explicit pointer->u32->pointer was a 32-bit ABI identity.
            text = text.replace("*(u32*)(u32)lbl_80478F90", "*(u32*)(uintptr_t)lbl_80478F90")
        elif kind in ("fighter", "trainer"):
            text = text.replace("pokemonGetStatus", "ref_pokemon_pointer")
            text = text.replace("fightTrainerGetStatus", "ref_trainer_pointer")
            text = text.replace("(s32)ref_", "(s32)(intptr_t)ref_")
            # This TU declares the no-argument battle-result global getter
            # with an ignored context argument. Reconcile the host prototype.
            text = text.replace("fn_801EF634(context)", "fn_801EF634()")
        elif kind == "trainer_scalar":
            text = text.replace("fightTrainerGetStatus", "ref_trainer_scalar")
        for original, replacement in {
            "gamedataAttestCheckValid": "ref_attest", "heroGetStatus": "ref_hero_status",
            "GScharCmp": "ref_char_cmp", "fn_801EF634": "ref_result",
            "fightPokemonCheckValid": "ref_fightPokemonCheckValidInline",
            "fightSideDataBiosGetPtr": "ref_grid_row", "fightSideDataBiosGetYrot": "ref_grid_yaw",
        }.items():
            text = re.sub(r"\b" + original + r"\b", replacement, text)
        for name in names:
            text = re.sub(r"\b" + name + r"\b", "ref_" + name, text)
        parts.append(text)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("/* Pinned original predicate/control bodies; test-only host getters. */\n" + "\n\n".join(parts) + "\n", encoding="utf-8")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, default=ROOT / "build/battle_entry_records_reference.h")
    p.add_argument("--donor", type=Path, default=ROOT / "vendor/colosseum")
    args = p.parse_args()
    prepare(args.output, args.donor)
