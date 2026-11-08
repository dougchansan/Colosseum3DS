#ifndef C3_BATTLE_RNG_H
#define C3_BATTLE_RNG_H

#include <stdint.h>

/* Per-battle state, independent of the existing global c3_rng_* stream.
 * These are host values, not a serialized save format. */
typedef struct C3BattleRng {
    uint32_t seed;
    uint64_t draws;
} C3BattleRng;

typedef struct C3BattleRngSnapshot {
    uint32_t seed;
    uint64_t draws;
} C3BattleRngSnapshot;

int c3_battle_rng_init(C3BattleRng *rng, uint32_t seed);
int c3_battle_rng_snapshot(const C3BattleRng *rng, C3BattleRngSnapshot *snapshot);
int c3_battle_rng_restore(C3BattleRng *rng, const C3BattleRngSnapshot *snapshot);

/* Return 1 on success. NULL, output/context overlap, zero modulus, or draw
 * counter overflow returns 0 without changing context or output. next16 uses
 * one pinned HSD draw. bounded32 uses two draws (first low, second high),
 * followed by unsigned modulo, from the pinned GS CANDIDATE helper. The source
 * has undefined behavior for modulus zero; this wrapper explicitly rejects it.
 * The source remains CANDIDATE. docs/rng-qualification.md records 72 original
 * PowerPC instruction cases for this arithmetic slice; the full battle call
 * graph and its RNG consumption remain unqualified. */
int c3_battle_rng_next16(C3BattleRng *rng, uint16_t *value);
int c3_battle_rng_bounded32(C3BattleRng *rng, uint32_t modulus, uint32_t *value);

#endif
