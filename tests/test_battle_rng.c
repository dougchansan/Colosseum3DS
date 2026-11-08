#include "colosseum3ds/battle_rng.h"
#include "colosseum3ds/core.h"
#include <assert.h>
#include <stdio.h>

/* Independent reference lane: compile the complete pinned HSD TU, unchanged,
 * under private symbols. The generated reference modulo body retains its
 * original signature and globals, unlike the context-bound native lane. */
#define fn_801A6990 c3_reference_seed_released
#define lbl_80478C90 c3_reference_seed
#define lbl_80478C94 c3_reference_seed_pointer
#define fn_801ADCD8 c3_reference_rand
#define fn_801ADC7C c3_reference_randf
#define fn_801ADC3C c3_reference_randi
#define _HSD_RandForgetMemory c3_reference_forget
#include "../vendor/colosseum/src/hsd/random.c"
#undef fn_801A6990
#undef lbl_80478C90
#undef lbl_80478C94
#undef fn_801ADCD8
#undef fn_801ADC7C
#undef fn_801ADC3C
#undef _HSD_RandForgetMemory
#define C3_BATTLE_RNG_REFERENCE
#include "battle_rng_source.h"

int c3_reference_seed_released(void *pointer)
{
    return pointer != &c3_reference_seed;
}

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)

static void same_state(const C3BattleRng *rng, const C3BattleRngSnapshot *snapshot)
{
    CHECK(rng->seed == snapshot->seed);
    CHECK(rng->draws == snapshot->draws);
}

static void reference_seed(uint32_t seed)
{
    c3_reference_seed_pointer = &c3_reference_seed;
    c3_reference_seed = seed;
}

static void compare_seed(uint32_t seed)
{
    static const uint32_t bounds[] = {1, 2, 3, 255, 256, 65535, 65536,
                                     0x7fffffffu, 0x80000000u, 0xfffffffeu, UINT32_MAX};
    C3BattleRng rng;
    uint16_t small;
    uint32_t result;
    CHECK(c3_battle_rng_init(&rng, seed));
    reference_seed(seed);
    for (unsigned i = 0; i < 64; ++i) {
        uint16_t expected = (uint16_t)c3_reference_rand();
        CHECK(c3_battle_rng_next16(&rng, &small));
        CHECK(small == expected);
        CHECK(rng.seed == c3_reference_seed);
        CHECK(rng.draws == i + 1u);
    }
    for (unsigned i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i) {
        uint32_t expected = c3_reference_bounded(bounds[i]);
        CHECK(c3_battle_rng_bounded32(&rng, bounds[i], &result));
        CHECK(result == expected && result < bounds[i]);
        CHECK(rng.seed == c3_reference_seed);
        CHECK(rng.draws == 64u + 2u * (i + 1u));
    }
}

static void failures_and_snapshots(void)
{
    C3BattleRng rng, restored;
    C3BattleRngSnapshot snapshot = {0x12345678u, 19};
    uint16_t small = 0xabcdu;
    uint32_t value = 0x12345678u;
    CHECK(!c3_battle_rng_init(NULL, 1));
    CHECK(c3_battle_rng_init(&rng, UINT32_MAX));
    CHECK(!c3_battle_rng_snapshot(NULL, &snapshot));
    CHECK(snapshot.seed == 0x12345678u && snapshot.draws == 19);
    CHECK(c3_battle_rng_snapshot(&rng, &snapshot));
    CHECK(!c3_battle_rng_snapshot(&rng, NULL));
    CHECK(!c3_battle_rng_restore(NULL, &snapshot));
    CHECK(!c3_battle_rng_restore(&rng, NULL));
    CHECK(!c3_battle_rng_next16(NULL, &small));
    CHECK(small == 0xabcdu);
    CHECK(!c3_battle_rng_next16(&rng, NULL));
    same_state(&rng, &snapshot);
    CHECK(!c3_battle_rng_bounded32(NULL, 1, &value));
    CHECK(!c3_battle_rng_bounded32(&rng, 1, NULL));
    CHECK(!c3_battle_rng_bounded32(&rng, 0, &value));
    CHECK(value == 0x12345678u);
    same_state(&rng, &snapshot);
    CHECK(!c3_battle_rng_next16(&rng, (uint16_t *)&rng.seed));
    CHECK(!c3_battle_rng_bounded32(&rng, 99, &rng.seed));
    CHECK(!c3_battle_rng_snapshot(&rng, (C3BattleRngSnapshot *)&rng));
    CHECK(!c3_battle_rng_restore(&rng, (const C3BattleRngSnapshot *)&rng));
    same_state(&rng, &snapshot);
    CHECK(c3_battle_rng_restore(&restored, &snapshot));
    CHECK(c3_battle_rng_next16(&rng, &small));
    { uint16_t other; CHECK(c3_battle_rng_next16(&restored, &other)); CHECK(other == small); }
    CHECK(rng.seed == restored.seed && rng.draws == restored.draws);
    rng.draws = UINT64_MAX;
    CHECK(c3_battle_rng_snapshot(&rng, &snapshot));
    CHECK(!c3_battle_rng_next16(&rng, &small));
    CHECK(!c3_battle_rng_bounded32(&rng, 1, &value));
    CHECK(value == 0x12345678u);
    same_state(&rng, &snapshot);
    rng.draws = UINT64_MAX - 1;
    CHECK(c3_battle_rng_snapshot(&rng, &snapshot));
    CHECK(!c3_battle_rng_bounded32(&rng, UINT32_MAX, &value));
    same_state(&rng, &snapshot);
    CHECK(c3_battle_rng_next16(&rng, &small));
    CHECK(rng.draws == UINT64_MAX);
    rng.draws = UINT64_MAX - 2;
    CHECK(c3_battle_rng_bounded32(&rng, 1, &value));
    CHECK(value == 0 && rng.draws == UINT64_MAX);
}

static void ordering_and_isolation(void)
{
    C3BattleRng a, b, separate;
    C3BattleRngSnapshot before;
    uint16_t low, high, other;
    uint32_t combined;
    CHECK(c3_battle_rng_init(&a, 1));
    CHECK(c3_battle_rng_init(&separate, 1));
    CHECK(c3_battle_rng_next16(&a, &low));
    CHECK(c3_battle_rng_next16(&a, &high));
    CHECK(low != high);
    CHECK(c3_battle_rng_bounded32(&separate, UINT32_MAX, &combined));
    CHECK(combined == (((uint32_t)high << 16) | low) % UINT32_MAX);
    CHECK(a.seed == separate.seed && separate.draws == 2);
    CHECK(c3_battle_rng_init(&a, 0x01234567u));
    CHECK(c3_battle_rng_init(&b, 0x89abcdefu));
    c3_rng_seed(1);
    reference_seed(1);
    CHECK(c3_rng_next() == (uint32_t)c3_reference_rand());
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(c3_battle_rng_snapshot(&b, &before));
        CHECK(c3_battle_rng_bounded32(&a, 0x80000001u, &combined));
        same_state(&b, &before);
        CHECK(c3_battle_rng_next16(&b, &other));
    }
    CHECK(a.draws == 200 && b.draws == 100);
    CHECK(c3_rng_next() == (uint32_t)c3_reference_rand());
    reference_seed(0x01234567u);
    for (unsigned i = 0; i < 200; ++i) (void)c3_reference_rand();
    CHECK(a.seed == c3_reference_seed);
    reference_seed(0x89abcdefu);
    for (unsigned i = 0; i < 100; ++i) (void)c3_reference_rand();
    CHECK(b.seed == c3_reference_seed);
}

int main(void)
{
    const uint32_t seeds[] = {0, 1, UINT32_MAX, 0x80000000u, 0x7fffffffu, 214013,
                              2531011, 0xdeadbeefu, 0x0000ffffu, 0xffff0000u};
    uint32_t fixture = 0x4f524143u;
    for (unsigned i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) compare_seed(seeds[i]);
    for (unsigned i = 0; i < 1000; ++i) {
        fixture ^= fixture << 13; fixture ^= fixture >> 17; fixture ^= fixture << 5;
        compare_seed(fixture);
    }
    failures_and_snapshots();
    ordering_and_isolation();
    printf("Battle RNG: %u source-parity checks passed (bounded source is CANDIDATE; no PPC oracle)\n", checks);
    return 0;
}
