#include "colosseum3ds/battle_rules.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do { ++checks; assert(c); } while (0)

static void result_domain(void)
{
    /* Golden result rows: each column is a requested original result 0..7.
     * The return masks separately capture accepted-but-unchanged precedence.
     * Original-instruction qualification is recorded separately in the JSON
     * corpus and tools/qualification checker, not inferred from these rows. */
    static const uint8_t results[2][8][8] = {
        {{0,1,2,3,4,5,5,3}, {1,1,1,1,1,1,1,1},
         {0,1,2,3,4,5,5,3}, {0,1,3,3,3,3,3,3},
         {0,1,2,3,4,5,5,3}, {0,1,5,5,5,5,5,5},
         {0,1,2,3,4,5,6,3}, {0,1,2,3,4,5,5,7}},
        {{0,1,2,3,4,5,6,7}, {1,1,1,1,1,1,1,1},
         {0,1,2,7,4,5,6,7}, {0,1,7,3,4,5,6,7},
         {0,1,2,3,4,6,6,7}, {0,1,2,3,6,5,6,7},
         {0,1,6,6,6,6,6,6}, {0,1,7,7,7,7,7,7}}
    };
    static const uint8_t accepted[2][8] = {
        {0xfe,0xfd,0xfb,3,0xef,3,0xbf,0x7f},
        {0xfe,0xfd,0xfb,0xf7,0xef,0xdf,3,3}
    };
    for (unsigned draw = 0; draw < 2; ++draw)
        for (unsigned now = 0; now < 8; ++now)
            for (unsigned wanted = 0; wanted < 8; ++wanted) {
                struct { uint32_t before; C3BattleRuleResult out; uint32_t after; } box;
                memset(&box, 0xa5, sizeof(box));
                CHECK(c3_battle_rules_combine_result(now, wanted, draw, &box.out));
                CHECK(box.out.result == results[draw][now][wanted]);
                CHECK(box.out.accepted == ((accepted[draw][now] >> wanted) & 1));
                CHECK(box.before == 0xa5a5a5a5 && box.after == 0xa5a5a5a5);
            }
    for (unsigned result = 0; result < 8; ++result)
        for (unsigned host = 0; host < 2; ++host) {
            uint8_t box[3] = {0xa5,0xa5,0xa5};
            CHECK(c3_battle_rules_hero_win(result, host, &box[1]));
            CHECK(box[1] == (host ? result == 2 : result == 3));
            CHECK(box[0] == 0xa5 && box[2] == 0xa5);
        }
    C3BattleRuleResult out, before;
    memset(&out, 0xa5, sizeof(out)); before = out;
    CHECK(!c3_battle_rules_combine_result(8, 0, 0, &out));
    CHECK(!c3_battle_rules_combine_result(0, UINT16_MAX, 0, &out));
    CHECK(!c3_battle_rules_combine_result(0, 0, 2, &out));
    CHECK(!c3_battle_rules_combine_result(0, 0, 0, NULL));
    CHECK(!memcmp(&out, &before, sizeof(out)));
    uint8_t win = 0xa5;
    CHECK(!c3_battle_rules_hero_win(8, 0, &win));
    CHECK(!c3_battle_rules_hero_win(2, 2, &win));
    CHECK(!c3_battle_rules_hero_win(2, 1, NULL));
    CHECK(win == 0xa5);
}

static void selection(const int32_t *values, uint16_t count, uint8_t random, uint32_t seed)
{
    C3BattleRng actual = {seed, 17}, expected = actual;
    int32_t highest = -200, indices[16];
    unsigned ties = 0;
    for (unsigned i = 0; i < count; ++i)
        if (values[i] > highest) highest = values[i];
    for (unsigned i = 0; i < count; ++i)
        if (values[i] == highest) indices[ties++] = (int32_t)i;
    int32_t wanted = -1;
    if (ties) {
        uint16_t next = 0;
        if (ties > 1 && random == 1) CHECK(c3_battle_rng_next16(&expected, &next));
        wanted = indices[next % ties];
    }
    struct { int32_t before, out, after; } box = {123456,123456,123456};
    int32_t saved[16] = {0};
    if (count) memcpy(saved, values, count * sizeof(*values));
    CHECK(c3_battle_rules_ai_argmax(values, count, random, &actual, &box.out));
    CHECK(box.out == wanted);
    CHECK(actual.seed == expected.seed && actual.draws == expected.draws);
    CHECK(box.before == 123456 && box.after == 123456);
    CHECK(!count || !memcmp(values, saved, count * sizeof(*values)));
}

static void ai_domain(void)
{
    static const int32_t alphabet[] = {INT32_MIN, -201, -200, -199, INT32_MAX};
    int32_t values[16];
    /* All 781 arrays of length 0..4 across sentinel and signed boundaries;
     * every possible useRandom byte includes non-boolean values. */
    unsigned combinations = 1;
    for (uint16_t count = 0; count <= 4; ++count) {
        for (unsigned code = 0; code < combinations; ++code) {
            unsigned digits = code;
            for (unsigned i = 0; i < count; ++i) { values[i] = alphabet[digits % 5]; digits /= 5; }
            for (unsigned random = 0; random <= UINT8_MAX; ++random)
                selection(count ? values : NULL, count, random, code * 0x12345u + random);
        }
        combinations *= 5;
    }
    for (unsigned count = 5; count <= 16; ++count) {
        for (unsigned i = 0; i < 16; ++i) values[i] = -200;
        for (unsigned seed = 0; seed < 64; ++seed)
            selection(values, count, 1, seed * 0x1020304u);
        for (unsigned best = 0; best < count; ++best) {
            values[best] = INT32_MAX;
            selection(values, count, 1, UINT32_MAX);
            values[best] = -200;
        }
    }
}

static void rejection_and_replay(void)
{
    int32_t values[17] = {4,4}, out = 123;
    C3BattleRng rng = {0x12345678, UINT64_MAX}, before = rng;
    CHECK(!c3_battle_rules_ai_argmax(values, 2, 1, &rng, &out));
    CHECK(out == 123 && rng.seed == before.seed && rng.draws == before.draws);
    CHECK(c3_battle_rules_ai_argmax(values, 1, 1, &rng, &out));
    CHECK(out == 0 && rng.seed == before.seed && rng.draws == before.draws);
    CHECK(c3_battle_rules_ai_argmax(values, 2, 2, &rng, &out));
    CHECK(out == 0 && rng.draws == UINT64_MAX);
    out = 123;
    CHECK(!c3_battle_rules_ai_argmax(values, 17, 1, &rng, &out));
    CHECK(!c3_battle_rules_ai_argmax(NULL, 1, 1, &rng, &out));
    CHECK(!c3_battle_rules_ai_argmax(values, 1, 1, NULL, &out));
    CHECK(!c3_battle_rules_ai_argmax(values, 1, 1, &rng, NULL));
    CHECK(!c3_battle_rules_ai_argmax(values, 2, 1, &rng, &values[1]));
    CHECK(!c3_battle_rules_ai_argmax(values, 2, 1, &rng, (int32_t*)&rng.seed));
    CHECK(!c3_battle_rules_ai_argmax((const int32_t*)&rng.seed, 1, 1, &rng, &out));
    CHECK(out == 123 && values[1] == 4 && rng.seed == before.seed && rng.draws == before.draws);
    rng.seed = UINT32_MAX; rng.draws = UINT64_MAX - 1;
    CHECK(c3_battle_rules_ai_argmax(values, 2, 1, &rng, &out));
    CHECK(rng.draws == UINT64_MAX);
    C3BattleRngSnapshot snapshot;
    C3BattleRng a = {123,0}, b = {98765,100};
    int32_t first[64], second[64];
    CHECK(c3_battle_rng_snapshot(&a, &snapshot));
    for (unsigned i = 0; i < 64; ++i) {
        CHECK(c3_battle_rules_ai_argmax(values, 2, 1, &a, &first[i]));
        CHECK(c3_battle_rules_ai_argmax(values, 2, 1, &b, &out));
    }
    C3BattleRng final = a;
    CHECK(c3_battle_rng_restore(&a, &snapshot));
    for (unsigned i = 0; i < 64; ++i)
        CHECK(c3_battle_rules_ai_argmax(values, 2, 1, &a, &second[i]));
    CHECK(!memcmp(first, second, sizeof(first)));
    CHECK(a.seed == final.seed && a.draws == final.draws && b.draws == 164);
}

int main(void)
{
    result_domain();
    ai_domain();
    rejection_and_replay();
    printf("Battle rules: %u checks passed (source-derived isolated rules only)\n", checks);
    return 0;
}
