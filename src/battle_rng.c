#include "colosseum3ds/battle_rng.h"
#include <stddef.h>
#include "battle_rng_source.h"

static int overlaps(const void *first, size_t first_size,
                    const void *second, size_t second_size)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return a >= b ? a - b < second_size : b - a < first_size;
}

int c3_battle_rng_init(C3BattleRng *rng, uint32_t seed)
{
    if (!rng) return 0;
    rng->seed = seed;
    rng->draws = 0;
    return 1;
}

int c3_battle_rng_snapshot(const C3BattleRng *rng, C3BattleRngSnapshot *snapshot)
{
    if (!rng || !snapshot || overlaps(rng, sizeof(*rng), snapshot, sizeof(*snapshot))) return 0;
    snapshot->seed = rng->seed;
    snapshot->draws = rng->draws;
    return 1;
}

int c3_battle_rng_restore(C3BattleRng *rng, const C3BattleRngSnapshot *snapshot)
{
    if (!rng || !snapshot || overlaps(rng, sizeof(*rng), snapshot, sizeof(*snapshot))) return 0;
    rng->seed = snapshot->seed;
    rng->draws = snapshot->draws;
    return 1;
}

int c3_battle_rng_next16(C3BattleRng *rng, uint16_t *value)
{
    uint32_t seed;
    uint16_t result;
    if (!rng || !value || rng->draws == UINT64_MAX ||
        overlaps(rng, sizeof(*rng), value, sizeof(*value))) return 0;
    seed = rng->seed;
    result = (uint16_t)c3_battle_source_next16(&seed);
    rng->seed = seed;
    ++rng->draws;
    *value = result;
    return 1;
}

int c3_battle_rng_bounded32(C3BattleRng *rng, uint32_t modulus, uint32_t *value)
{
    uint32_t seed, result;
    if (!rng || !value || !modulus || rng->draws > UINT64_MAX - 2 ||
        overlaps(rng, sizeof(*rng), value, sizeof(*value))) return 0;
    seed = rng->seed;
    result = c3_battle_source_bounded32(&seed, modulus);
    rng->seed = seed;
    rng->draws += 2;
    *value = result;
    return 1;
}
