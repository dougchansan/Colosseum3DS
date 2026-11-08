#ifndef C3_BATTLE_RULES_H
#define C3_BATTLE_RULES_H

#include "colosseum3ds/battle_rng.h"
#include <stdint.h>

typedef struct C3BattleRuleResult {
    uint16_t result;
    uint8_t accepted;
} C3BattleRuleResult;

/* Source result combination, not terminal-state validation or reward authority.
 * current/requested must be 0..7 and draw_enabled must be 0 or 1.
 * accepted is the original setter's return value, not a victory predicate. */
int c3_battle_rules_combine_result(uint16_t current, uint16_t requested,
                                 uint8_t draw_enabled, C3BattleRuleResult *out);

/* Source win predicate with an explicitly resolved host-side identity (0/1).
 * Does not discover the hero/trainer or resolve original target handles. */
int c3_battle_rules_hero_win(uint16_t result, uint8_t hero_is_host_side,
                            uint8_t *out);

/* Exact original 16-slot selection primitive. count<=16; values may be NULL
 * only for count==0. Empty/all-below -200 returns -1 successfully. Only
 * use_random==1 with multiple maxima consumes one HSD draw. rng is required.
 * Invalid input, overlap, busy context or RNG overflow leaves outputs and RNG
 * unchanged. The caller must own rng exclusively throughout the call. */
int c3_battle_rules_ai_argmax(const int32_t *values, uint16_t count,
                             uint8_t use_random, C3BattleRng *rng, int32_t *out);

#endif
