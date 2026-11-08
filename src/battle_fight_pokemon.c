#include "colosseum3ds/battle_fight_pokemon.h"
#include "dolphin/types.h"
#include <string.h>

typedef struct C3FightPokemonContext {
    u8 *backing;
    const u8 *source;
    u32 source_reference;
    int error;
} C3FightPokemonContext;

static u16 be16(const u8 *p)
{
    return (u16)((u16)p[0] << 8 | p[1]);
}

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void put16(u8 *p, u16 value)
{
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static void put32(u8 *p, u32 value)
{
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

static int overlaps(const void *a, size_t a_size, const void *b, size_t b_size)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < b_size : y - x < a_size;
}

static int status_target(C3FightPokemonContext *context, const void *target,
                         u32 slot, u32 index)
{
    if (target == context->backing && slot == 0 && index == 0)
        return 1;
    context->error = 1;
    return 0;
}

static uintptr_t c3fp_src_pokemonGetStatus(C3FightPokemonContext *context,
    void *target, u32 slot, u32 field, u32 index)
{
    if (!status_target(context, target, slot, index))
        return 0;
    u8 *p = context->backing;
    switch (field) {
    case 0xcb: return be32(p);
    case 0xcc: return (uintptr_t)(p + 4);
    case 0xcd: return (uintptr_t)(p + 0x13c);
    case 0xce: return (uintptr_t)(s32)(s16)be16(p + 0x14c);
    case 0xcf: case 0xd0: case 0xd1: case 0xd2:
        return p[0x14e + field - 0xcf];
    default: context->error = 1; return 0;
    }
}

static void c3fp_src_pokemonSetStatus(C3FightPokemonContext *context,
    void *target, u32 slot, u32 field, u32 index, uintptr_t value)
{
    if (!status_target(context, target, slot, index))
        return;
    u8 *p = context->backing;
    switch (field) {
    case 0xcb:
        if (!value)
            put32(p, 0);
        else if (value == (uintptr_t)context->source)
            put32(p, context->source_reference);
        else
            context->error = 1;
        return;
    case 0xce: put16(p + 0x14c, (u16)value); return;
    case 0xcf: case 0xd0: case 0xd1: case 0xd2:
        p[0x14e + field - 0xcf] = (u8)value;
        return;
    default: context->error = 1; return;
    }
}

static void c3fp_src_pokemonInit(C3FightPokemonContext *context, uintptr_t target)
{
    if (target != (uintptr_t)(context->backing + 4) ||
        !c3_battle_pokemon_init((u8*)target))
        context->error = 1;
}

static void c3fp_src_fn_8011B950(C3FightPokemonContext *context,
                               uintptr_t target, u16 count)
{
    if (target != (uintptr_t)(context->backing + 0x13c) || count != 1) {
        context->error = 1;
        return;
    }
    /* Original seven setters preserve the byte at +7 and halfword at +E. */
    u8 *p = (u8*)target;
    memset(p, 0, 7);
    memset(p + 8, 0, 6);
}

static void c3fp_src_pokemonBiosCopy(C3FightPokemonContext *context,
                                   uintptr_t target, const void *source)
{
    if (target != (uintptr_t)(context->backing + 4) || source != context->source) {
        context->error = 1;
        return;
    }
    memcpy((void*)target, source, C3_BATTLE_POKEMON_BYTES);
}

#include "battle_fight_pokemon_original.inc"

int c3_battle_fight_pokemon_init(u8 backing[C3_BATTLE_FIGHT_POKEMON_BYTES])
{
    if (!backing)
        return 1;
    u8 staged[C3_BATTLE_FIGHT_POKEMON_BYTES];
    memcpy(staged, backing, sizeof(staged));
    C3FightPokemonContext context = {staged, NULL, 0, 0};
    c3fp_src_fightPokemonInit(&context, staged);
    if (context.error)
        return 0;
    memcpy(backing, staged, sizeof(staged));
    return 1;
}

int c3_battle_fight_pokemon_create(u8 backing[C3_BATTLE_FIGHT_POKEMON_BYTES],
    const u8 source[C3_BATTLE_POKEMON_BYTES], u32 source_reference, s16 entry_id)
{
    if (!backing || !source)
        return 1;
    if (!source_reference || overlaps(backing, C3_BATTLE_FIGHT_POKEMON_BYTES,
                                      source, C3_BATTLE_POKEMON_BYTES))
        return 0;
    u8 staged[C3_BATTLE_FIGHT_POKEMON_BYTES];
    memcpy(staged, backing, sizeof(staged));
    C3FightPokemonContext context = {staged, source, source_reference, 0};
    c3fp_src_fightPokemonCreate(&context, staged, (void*)source, entry_id);
    if (context.error)
        return 0;
    memcpy(backing, staged, sizeof(staged));
    return 1;
}
