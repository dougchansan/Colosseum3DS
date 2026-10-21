#include "colosseum3ds/pokemon.h"
#include "dolphin/types.h"

#include <stdatomic.h>
#include <stddef.h>

/*
 * The retail accessor interface has no context for species/nature lookups.
 * Serialize this adapter's context and reject contention instead of spinning.
 * All changes are made to a private copy and committed only on success.
 */
static atomic_flag c3_pokemon_in_use = ATOMIC_FLAG_INIT;
static C3Pokemon *c3_pokemon_context;
static int c3_pokemon_accessor_error;

static int c3_stat_from_selector(u32 selector, u32 first)
{
    static const int order[6] = {
        C3_STAT_HP, C3_STAT_ATTACK, C3_STAT_DEFENSE,
        C3_STAT_SP_ATTACK, C3_STAT_SP_DEFENSE, C3_STAT_SPEED
    };
    if (selector < first || selector >= first + 6) {
        c3_pokemon_accessor_error = 1;
        return 0;
    }
    return order[selector - first];
}

static u32 c3_native_pokemon_get(u8 *obj, u32 id, u32 selector, u32 index)
{
    C3Pokemon *p = c3_pokemon_context;
    if (!p || index != 0) {
        c3_pokemon_accessor_error = 1;
        return 0;
    }
    if (!obj) {
        if (id != p->species_id || selector < 3 || selector > 8) {
            c3_pokemon_accessor_error = 1;
            return 0;
        }
        return p->base_stats[c3_stat_from_selector(selector, 3)];
    }
    if (obj != (u8 *)p || id != 0) {
        c3_pokemon_accessor_error = 1;
        return 0;
    }
    switch (selector) {
    case 0x6E: return p->species_id;
    case 0xBF: return 0; /* One explicitly supplied nature record. */
    case 0x83: return p->current_hp;
    default:
        if (selector >= 0x87 && selector <= 0x8C)
            return p->stats[c3_stat_from_selector(selector, 0x87)];
        if (selector >= 0x8D && selector <= 0x92)
            return p->ev[c3_stat_from_selector(selector, 0x8D)];
        if (selector >= 0x93 && selector <= 0x98)
            return p->iv[c3_stat_from_selector(selector, 0x93)];
        c3_pokemon_accessor_error = 1;
        return 0;
    }
}

static void c3_native_pokemon_set(u8 *obj, u32 id, u32 selector,
                                 u32 index, u32 value)
{
    C3Pokemon *p = c3_pokemon_context;
    if (!p || obj != (u8 *)p || id != 0 || index != 0) {
        c3_pokemon_accessor_error = 1;
        return;
    }
    if (selector == 0x7A) {
        p->level = (uint8_t)value;
    } else if (selector == 0x83) {
        p->current_hp = (uint16_t)value;
    } else if (selector >= 0x87 && selector <= 0x8C) {
        p->stats[c3_stat_from_selector(selector, 0x87)] = (uint16_t)value;
    } else {
        c3_pokemon_accessor_error = 1;
    }
}

static u8 *c3_native_nature_data(u8 nature)
{
    if (!c3_pokemon_context || nature != 0) {
        c3_pokemon_accessor_error = 1;
        return NULL;
    }
    return (u8 *)c3_pokemon_context;
}

static u8 c3_native_nature_index(u8 *data, u8 index)
{
    if (data != (u8 *)c3_pokemon_context) {
        c3_pokemon_accessor_error = 1;
    }
    return index;
}

static u8 c3_native_attack_rate(u8 *data)
{ return c3_native_nature_index(data, C3_STAT_ATTACK); }
static u8 c3_native_defense_rate(u8 *data)
{ return c3_native_nature_index(data, C3_STAT_DEFENSE); }
static u8 c3_native_speed_rate(u8 *data)
{ return c3_native_nature_index(data, C3_STAT_SPEED); }
static u8 c3_native_sp_attack_rate(u8 *data)
{ return c3_native_nature_index(data, C3_STAT_SP_ATTACK); }
static u8 c3_native_sp_defense_rate(u8 *data)
{ return c3_native_nature_index(data, C3_STAT_SP_DEFENSE); }

static u8 *c3_native_nature_rate(u8 rate)
{
    if (!c3_pokemon_context || rate >= C3_STAT_COUNT) {
        c3_pokemon_accessor_error = 1;
        return NULL;
    }
    return (u8 *)&c3_pokemon_context->nature[rate];
}

static u8 c3_native_rate_numerator(u8 *rate)
{ return ((C3NatureRate *)rate)->numerator; }
static u8 c3_native_rate_denominator(u8 *rate)
{ return ((C3NatureRate *)rate)->denominator; }

static void c3_decomp_set_level_stats(u8 *obj, u32 level);

/*
 * Compile the pinned, unmodified game routine. These names exist only in this
 * translation unit; the adapter is not a replacement for PokemonBios/RELs.
 */
#define pokemonGetStatus c3_native_pokemon_get
#define pokemonSetStatus c3_native_pokemon_set
#define pokemonSeikakuDataBiosGetPtr c3_native_nature_data
#define pokemonSeikakuDataBiosGetPhyAtkRateDataId c3_native_attack_rate
#define pokemonSeikakuDataBiosGetPhyDefRateDataId c3_native_defense_rate
#define pokemonSeikakuDataBiosGetNimblenessRateDataId c3_native_speed_rate
#define pokemonSeikakuDataBiosGetSpeAtkRateDataId c3_native_sp_attack_rate
#define pokemonSeikakuDataBiosGetSpeDefRateDataId c3_native_sp_defense_rate
#define pokemonSeikakuRateDataBiosGetPtr c3_native_nature_rate
#define pokemonSeikakuRateDataBiosGetKake c3_native_rate_numerator
#define pokemonSeikakuRateDataBiosGetWaru c3_native_rate_denominator
#define pokemonSetLevelBasisStatus c3_decomp_set_level_stats
#include "../vendor/colosseum/src/game/pokemon_range_8012795C.c"
#undef pokemonGetStatus
#undef pokemonSetStatus
#undef pokemonSeikakuDataBiosGetPtr
#undef pokemonSeikakuDataBiosGetPhyAtkRateDataId
#undef pokemonSeikakuDataBiosGetPhyDefRateDataId
#undef pokemonSeikakuDataBiosGetNimblenessRateDataId
#undef pokemonSeikakuDataBiosGetSpeAtkRateDataId
#undef pokemonSeikakuDataBiosGetSpeDefRateDataId
#undef pokemonSeikakuRateDataBiosGetPtr
#undef pokemonSeikakuRateDataBiosGetKake
#undef pokemonSeikakuRateDataBiosGetWaru
#undef pokemonSetLevelBasisStatus

void c3_pokemon_neutral_nature(C3Pokemon *pokemon)
{
    if (!pokemon) return;
    for (unsigned i = 0; i < C3_STAT_COUNT; ++i) {
        pokemon->nature[i].numerator = 1;
        pokemon->nature[i].denominator = 1;
    }
}

static int c3_pokemon_valid(const C3Pokemon *pokemon, uint8_t level)
{
    unsigned ev_total = 0, increased = 0, decreased = 0;
    if (!pokemon || !pokemon->species_id || level == 0 || level > 100 ||
        pokemon->level > 100 ||
        pokemon->current_hp > pokemon->stats[C3_STAT_HP]) {
        return 0;
    }
    for (unsigned i = 0; i < C3_STAT_COUNT; ++i) {
        unsigned n = pokemon->nature[i].numerator;
        unsigned d = pokemon->nature[i].denominator;
        if (pokemon->base_stats[i] == 0 || pokemon->base_stats[i] > 255 ||
            pokemon->iv[i] > 31 || pokemon->ev[i] > 255 || !n || !d) {
            return 0;
        }
        ev_total += pokemon->ev[i];
        if (n == d) continue;
        if (i == C3_STAT_HP) return 0;
        if (n * 10 == d * 11) ++increased;
        else if (n * 10 == d * 9) ++decreased;
        else return 0;
    }
    return ev_total <= 510 && increased == decreased && increased <= 1;
}

C3PokemonResult c3_pokemon_recalculate(C3Pokemon *pokemon,
                                      uint8_t target_level)
{
    C3Pokemon working;
    C3PokemonResult result = C3_POKEMON_OK;
    if (!c3_pokemon_valid(pokemon, target_level)) {
        return C3_POKEMON_INVALID_ARGUMENT;
    }
    if (atomic_flag_test_and_set_explicit(&c3_pokemon_in_use,
                                         memory_order_acquire)) {
        return C3_POKEMON_BUSY;
    }
    working = *pokemon;
    c3_pokemon_context = &working;
    c3_pokemon_accessor_error = 0;
    c3_decomp_set_level_stats((u8 *)&working, target_level);
    if (c3_pokemon_accessor_error) {
        result = C3_POKEMON_ACCESSOR_ERROR;
    } else if (working.current_hp > working.stats[C3_STAT_HP]) {
        result = C3_POKEMON_RESULT_RANGE;
    }
    c3_pokemon_context = NULL;
    atomic_flag_clear_explicit(&c3_pokemon_in_use, memory_order_release);
    if (result == C3_POKEMON_OK) *pokemon = working;
    return result;
}
