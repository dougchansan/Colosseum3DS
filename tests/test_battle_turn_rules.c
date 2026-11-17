#include "colosseum3ds/battle_general.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original turn rules in the general battle (src/battle_general.c), on the
 * synthetic pack (no original data):
 * - fightFloorSortFightOutPokemonPtrArySub / ...CmpfightOutPokemonNimbleness:
 *   action bracket (item < switch < move), move priority, effective speed,
 *   a coin (one draw) for exact ties;
 * - fightOutPokemonGetNowNimbleness: paralysis >>2, Macho Brace >>1, Quick
 *   Claw against the turn's shared roll;
 * - fn_8022B2CC range IDs (3 random foe, 4 both foes) and fn_80232110's
 *   both-foes halving and screens;
 * - fn_80213270: screens wear off first, then poison/bad poison per
 *   Pokemon in speed order. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

static C3BattlePack pack;
static uint8_t party[6][0x138];

static void move_row(unsigned id, int priority, unsigned range, unsigned power)
{
    uint8_t *m = pack.bytes + 128 + id * 56;
    memset(m, 0, 56);
    m[0] = (uint8_t)(int8_t)priority; m[1] = 30; m[2] = 0; m[3] = (uint8_t)range; m[4] = power ? 100 : 0;
    p16(m + 22, power); p16(m + 26, power ? 0 : 18);
}

/* A double battle, every Pokemon with the same sturdy stats: HP 999, Atk,
 * Def, SpA, SpD 100, Speed by position; no critical hits. */
static void setup(C3GeneralBattle *b, uint32_t seed, const unsigned speed[4], unsigned party_count)
{
    make_party(party, 1, 50, party_count, 0);
    CHECK(c3_general_battle_begin(b, &pack, 0, party, seed, error));
    b->info.critical = 0;
    for (unsigned s = 0; s < 2; ++s)
        for (unsigned i = 0; i < b->party_count[s]; ++i) {
            C3GeneralMon *m = &b->party[s][i];
            m->hp = m->max_hp = m->stats[0] = 999;
            for (unsigned j = 1; j < 5; ++j) m->stats[j] = 100;
            m->stats[5] = 50; m->types[0] = m->types[1] = 0;
            for (unsigned k = 0; k < 4; ++k) { m->moves[k] = (uint16_t)(k + 1); m->pp[k] = m->pp_max[k] = 30; }
        }
    for (unsigned pos = 0; pos < 4; ++pos) b->party[pos / 2][b->active[pos / 2][pos % 2]].stats[5] = (uint16_t)speed[pos];
}

/* Positions in the order their action began (a move, a ball, a switch,
 * or a "can't move" message). */
static unsigned order_of(const C3GeneralBattle *b, uint8_t out[4])
{
    unsigned n = 0, seen = 0;
    for (uint32_t i = 0; i < b->event_count; ++i) {
        const C3GeneralEvent *e = &b->events[i];
        int begins = e->kind == C3_GB_EVENT_MOVE || strstr(e->text, "can't move") || strstr(e->text, "come back");
        if (begins && e->actor < 4 && !(seen >> e->actor & 1u)) { seen |= 1u << e->actor; out[n++] = e->actor; }
    }
    return n;
}

/* Every position uses move `slot` at foe/hero position `target`. */
static void turn_all(C3GeneralBattle *b, unsigned slot, unsigned hero_target)
{
    C3GeneralCommand c[2] = {{0, (uint8_t)slot, (uint8_t)hero_target}, {0, (uint8_t)slot, (uint8_t)hero_target}};
    /* The foes use the trainer policy; give them only `slot`. */
    for (unsigned i = 0; i < b->party_count[1]; ++i)
        for (unsigned k = 0; k < 4; ++k) b->party[1][i].pp[k] = k == slot ? 30 : 0;
    CHECK(c3_general_battle_turn(b, &pack, c, error));
}

static unsigned hp_lost(const C3GeneralBattle *b, unsigned pos)
{
    unsigned total = 0;
    for (uint32_t i = 0; i < b->event_count; ++i) {
        unsigned v;
        const char *t = b->events[i].kind == C3_GB_EVENT_HP && b->events[i].target == pos ? strstr(b->events[i].text, "took") : NULL;
        if (t && sscanf(t, "took %u", &v) == 1) total += v;
    }
    return total;
}

int main(void)
{
    size_t n; uint8_t *bytes = make_pack(2, 50, &n);
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    move_row(1, 0, 0, 1);    /* TACKLE stand-in: power 1 so nobody faints */
    move_row(2, 0, 0, 1);
    move_row(3, 1, 0, 1);    /* priority +1 */
    move_row(4, 0, 4, 40);   /* both foes, power 40 */
    static C3GeneralBattle b;
    uint8_t o[4];

    {   /* Speed: higher first. */
        const unsigned sp[4] = {10, 20, 30, 40};
        setup(&b, 1, sp, 2); turn_all(&b, 0, 2);
        CHECK(order_of(&b, o) == 4 && o[0] == 3 && o[1] == 2 && o[2] == 1 && o[3] == 0);
    }
    {   /* Paralysis quarters speed: 40 -> 10 falls behind 20 and 30, ahead of 5. */
        const unsigned sp[4] = {5, 20, 30, 40};
        setup(&b, 2, sp, 2); b.party[1][b.active[1][1]].status = C3_GB_PARALYSIS;
        turn_all(&b, 0, 2);
        CHECK(order_of(&b, o) == 4 && o[0] == 2 && o[1] == 1 && o[2] == 3 && o[3] == 0);
    }
    {   /* Move priority beats speed (the slowest uses the +1 move). */
        const unsigned sp[4] = {10, 20, 30, 40};
        setup(&b, 3, sp, 2);
        C3GeneralCommand c[2] = {{0, 2, 2}, {0, 0, 2}};
        for (unsigned i = 0; i < 2; ++i) for (unsigned k = 0; k < 4; ++k) b.party[1][i].pp[k] = k == 0 ? 30 : 0;
        CHECK(c3_general_battle_turn(&b, &pack, c, error));
        CHECK(order_of(&b, o) == 4 && o[0] == 0 && o[1] == 3 && o[2] == 2 && o[3] == 1);
    }
    {   /* Quick Claw: under 20*0xFFFF/100 (13107) on the turn's roll it acts first; not otherwise. */
        const unsigned sp[4] = {10, 20, 30, 40};
        setup(&b, 4, sp, 2); b.party[0][b.active[0][0]].held_item = 183; b.quick_claw_roll = 13106;
        turn_all(&b, 0, 2);
        CHECK(order_of(&b, o) == 4 && o[0] == 0);
        setup(&b, 4, sp, 2); b.party[0][b.active[0][0]].held_item = 183; b.quick_claw_roll = 13107;
        turn_all(&b, 0, 2);
        CHECK(order_of(&b, o) == 4 && o[3] == 0);
    }
    {   /* Macho Brace halves: 41 -> 20 falls behind 21. */
        const unsigned sp[4] = {5, 21, 10, 41};
        setup(&b, 5, sp, 2); b.party[1][b.active[1][1]].held_item = 181;
        turn_all(&b, 0, 2);
        CHECK(order_of(&b, o) == 4 && o[0] == 1 && o[1] == 3 && o[2] == 2 && o[3] == 0);
    }
    {   /* A switch (bracket 3) goes before even a priority move (bracket 4). */
        const unsigned sp[4] = {10, 1, 30, 40};
        setup(&b, 6, sp, 3);
        C3GeneralCommand c[2] = {{0, 2, 2}, {1, 2, 0}};
        for (unsigned i = 0; i < 2; ++i) for (unsigned k = 0; k < 4; ++k) b.party[1][i].pp[k] = k == 0 ? 30 : 0;
        CHECK(c3_general_battle_turn(&b, &pack, c, error));
        CHECK(order_of(&b, o) >= 2 && o[0] == 1 && o[1] == 0);
    }
    {   /* Exact ties: a coin per comparison, deterministic per seed, both ways over seeds. */
        const unsigned sp[4] = {50, 50, 50, 50};
        unsigned first[4] = {0}; static C3GeneralBattle again;
        for (uint32_t seed = 0; seed < 64; ++seed) {
            setup(&b, seed, sp, 2); again = b;
            turn_all(&b, 0, 2); turn_all(&again, 0, 2);
            CHECK(!memcmp(&b, &again, sizeof b));
            CHECK(order_of(&b, o) == 4); ++first[o[0]];
        }
        for (unsigned pos = 0; pos < 4; ++pos) CHECK(first[pos] > 0);
    }
    {   /* Both-foes move: halved while two foes stand, full against one.
         * base = 100*40*22/100/50 = 17; halved 8; +2; STAB x1.5; 85..100%. */
        const unsigned sp[4] = {90, 1, 2, 3};
        unsigned lo = 999, hi = 0;
        for (uint32_t seed = 0; seed < 40; ++seed) {
            setup(&b, seed, sp, 2);
            C3GeneralCommand c[2] = {{0, 3, 2}, {0, 0, 0}}; /* the partner taps its ally */
            for (unsigned i = 0; i < 2; ++i) for (unsigned k = 0; k < 4; ++k) b.party[1][i].pp[k] = k == 0 ? 30 : 0;
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            for (unsigned pos = 2; pos < 4; ++pos) { unsigned v = hp_lost(&b, pos); if (v < lo) lo = v; if (v > hi) hi = v; }
        }
        CHECK(lo >= 12 && hi <= 15);
        lo = 999; hi = 0;
        for (uint32_t seed = 0; seed < 40; ++seed) {
            setup(&b, seed, sp, 2); b.party[1][b.active[1][1]].hp = 0;
            C3GeneralCommand c[2] = {{0, 3, 2}, {0, 0, 0}};
            for (unsigned i = 0; i < 2; ++i) for (unsigned k = 0; k < 4; ++k) b.party[1][i].pp[k] = k == 0 ? 30 : 0;
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            unsigned v = hp_lost(&b, 2); if (v < lo) lo = v; if (v > hi) hi = v;
        }
        CHECK(lo >= 23 && hi <= 28);
    }
    {   /* Reflect against a power-40 physical hit: /3*2 with two defenders, /2 with one. */
        move_row(1, 0, 0, 40);
        const unsigned sp[4] = {90, 1, 2, 3};
        unsigned two_hi = 0, two_lo = 999, one_hi = 0, one_lo = 999;
        for (uint32_t seed = 0; seed < 40; ++seed) for (unsigned alone = 0; alone < 2; ++alone) {
            setup(&b, seed, sp, 2); b.screens[1][0] = 5;
            if (alone) b.party[1][b.active[1][1]].hp = 0;
            C3GeneralCommand c[2] = {{0, 0, 2}, {0, 1, 0}};
            for (unsigned i = 0; i < 2; ++i) for (unsigned k = 0; k < 4; ++k) b.party[1][i].pp[k] = k == 1 ? 30 : 0;
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            unsigned v = 0;
            for (uint32_t i = 0; i < b.event_count; ++i)
                if (b.events[i].kind == C3_GB_EVENT_HP && b.events[i].target == 2) { const char *t = strstr(b.events[i].text, "took"); if (t && sscanf(t, "took %u", &v) == 1) break; }
            if (alone) { if (v < one_lo) one_lo = v; if (v > one_hi) one_hi = v; }
            else { if (v < two_lo) two_lo = v; if (v > two_hi) two_hi = v; }
        }
        /* base 17: two defenders 17/3*2 = 10 (+2, x1.5 = 18: 15..18); one 17/2 = 8 (+2, x1.5 = 15: 12..15). */
        printf("battle-turn-rules: reflect two %u..%u one %u..%u\n", two_lo, two_hi, one_lo, one_hi);
        CHECK(two_lo >= 15 && two_hi <= 18 && one_lo >= 12 && one_hi <= 15 && two_hi > one_hi);
        move_row(1, 0, 0, 1);
    }
    {   /* End of turn: Reflect wears off before poison; poison in speed order;
         * bad poison max/16 x count (999/16 = 62, then 124). */
        const unsigned sp[4] = {10, 20, 30, 40};
        setup(&b, 9, sp, 2);
        b.screens[0][0] = 1;
        b.party[0][b.active[0][0]].status = C3_GB_POISON;
        b.party[1][b.active[1][1]].status = C3_GB_TOXIC;
        turn_all(&b, 0, 2);
        int wore = -1, slow = -1, fast = -1;
        for (uint32_t i = 0; i < b.event_count; ++i) {
            const C3GeneralEvent *e = &b.events[i];
            if (strstr(e->text, "wore off") && wore < 0) wore = (int)i;
            if (strstr(e->text, "hurt by") && e->actor == 0 && slow < 0) slow = (int)i;
            if (strstr(e->text, "hurt by") && e->actor == 3 && fast < 0) fast = (int)i;
        }
        CHECK(wore >= 0 && fast > wore && slow > fast);
        CHECK(b.party[1][b.active[1][1]].hp == 999 - 62);
    }
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-turn-rules: order, priority, speed items, ties, targeting, damage and end of turn ok");
    return 0;
}
