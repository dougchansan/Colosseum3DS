#ifndef C3_SHADOW_MOVES_H
#define C3_SHADOW_MOVES_H

#include <stdint.h>

/* What a Shadow Pokemon's move slots show (pokemonBiosGetPokemonWazaPtr,
 * pokemon_data.c, with mode 1: every move read through pokemonGetStatus
 * 0x7F / 0x80 / 0x81 with a slot 0..3 -- the battle, its menus and AI, the
 * summary and the P*DA). The record keeps the Pokemon's own four moves; mode
 * 0 (slot + 4, and every setter) still reaches them.
 *
 * A Pokemon is Shadow while darkpokemonDataId (+0xD8) is set and dp (+0xDC,
 * the heart gauge x100) is not negative (pokemonBiosGetDarkFlag, status
 * 0xC2); purification sets dp to -100. For a Shadow Pokemon:
 *  - slot 0 always shows Shadow Rush (0x164, lbl_80478B58 = {0x164, 5 PP});
 *  - a trainer's Pokemon (fightTrainerPokemonDataId +0xD6, status 0xC9, set
 *    by fightTrainerCreateFightTrainerPokemonDataIdToPokemon and cleared by
 *    the snag) shows its own moves in slots 1..3;
 *  - the hero's (+0xD6 = 0) shows "????" (0x165, lbl_80478B5C = {0x165, 5
 *    PP}) in slot 1 while the dark level is under 2, slot 2 under 4 and
 *    slot 3 under 5 (pokemonGetDarkPokemonLevel), so its moves open at 80%,
 *    40% and 20% of the gauge. A "????" slot shows even when the slot
 *    behind it is empty (pokemonWazaCheckValid only refuses 0 and 0x163).
 * Both stand-ins read 5 PP; the battle never selects 0x165
 * (_fightMenuFightTrainerGcHeroOpenMenuSubWaza and fightPokemonCountOutOkWaza
 * skip it). */

#define C3_SHADOW_RUSH 0x164u
#define C3_SHADOW_HIDDEN_MOVE 0x165u /* "????" */
#define C3_SHADOW_STAND_IN_PP 5u

/* pokemonGetDarkPokemonLevel: 0 (gauge full) .. 6 (empty) for a Shadow
 * Pokemon, 7 otherwise. dp is the record's +0xDC (gauge x100), initial the
 * Shadow's full gauge in gauge units (status 0xC4, fn_801EEEB8: the save's
 * runtime table, row id*0x18 +0x4A6); 0 counts as 1, as the original does. */
static inline unsigned c3_shadow_level(int shadow, int32_t dp, uint32_t initial)
{
    if (!shadow) return 7;
    int64_t d = initial ? (int64_t)initial : 1, v = dp < 0 ? 0 : dp;
    if (v >= 100 * d) return 0;
    if (v >= 80 * d) return 1;
    if (v >= 60 * d) return 2;
    if (v >= 40 * d) return 3;
    if (v >= 20 * d) return 4;
    return v > 0 ? 5 : 6;
}

/* The move a slot shows: `move` itself, Shadow Rush or "????". */
static inline uint16_t c3_shadow_shown_move(uint16_t move, unsigned slot, int shadow, int trainer, unsigned level)
{
    if (!shadow || slot > 3) return move;
    if (slot == 0) return (uint16_t)C3_SHADOW_RUSH;
    if (trainer) return move;
    if ((slot == 1 && level < 2) || (slot == 2 && level < 4) || (slot == 3 && level < 5))
        return (uint16_t)C3_SHADOW_HIDDEN_MOVE;
    return move;
}

/* The same from a 0x138-byte save record (big-endian PokemonBios). */
static inline int c3_shadow_record_is_shadow(const uint8_t *r)
{
    return r && (r[0xd8] | r[0xd9]) && !(r[0xdc] & 0x80u);
}
static inline int32_t c3_shadow_record_dp(const uint8_t *r)
{
    return (int32_t)((uint32_t)r[0xdc] << 24 | (uint32_t)r[0xdd] << 16 | (uint32_t)r[0xde] << 8 | r[0xdf]);
}
static inline uint16_t c3_shadow_record_move(const uint8_t *r, unsigned slot, uint32_t initial)
{
    uint16_t own = (uint16_t)(r[0x78 + 4 * slot] << 8 | r[0x79 + 4 * slot]);
    int shadow = c3_shadow_record_is_shadow(r);
    return c3_shadow_shown_move(own, slot, shadow, (r[0xd6] | r[0xd7]) != 0,
                                c3_shadow_level(shadow, c3_shadow_record_dp(r), initial));
}

#endif
