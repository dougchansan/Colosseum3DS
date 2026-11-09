#ifndef C3_BATTLE_DAMAGE_STATE_H
#define C3_BATTLE_DAMAGE_STATE_H

#include "colosseum3ds/battle_state.h"

/* Original tables the damage arithmetic reads (borrowed). */
typedef struct {
    const uint8_t *stage_ratios;  /* lbl_80375D10: (multiplier, divisor) x13 */
    uint32_t stage_count;
    const uint8_t *item_types;    /* lbl_80279ED0 (0x24 bytes) */
    const uint8_t *types;         /* type table rows (44 bytes; +0 = physical/special) */
    uint32_t type_count;
} C3BattleDamageTables;

/* fn_80232110(attacker, defender, defender_side, move, power, type) over the
 * battle state: the pinned original arithmetic with arena adapters. Returns
 * 1 with *out set, or 0 with reason set when a service it reaches is not
 * modelled yet (badges, spread moves, weather abilities, held items, ...). */
int c3_battle_state_damage(const C3BattleState *state, const C3BattleDamageTables *tables,
                           uint32_t attacker, uint32_t defender, uint32_t defender_side,
                           uint16_t move, uint16_t power, uint16_t type, int32_t *out,
                           char *reason, size_t reason_size);
#endif
