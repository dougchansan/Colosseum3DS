#include "colosseum3ds/battle_general.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The original trainer AI in the general battle (fn_802342CC, fn_8023A308,
 * fn_8023A740, fightTrainerAiGetFightPokemonIrekaeModosuValue) on a
 * synthetic version-3 pack (no original data): two AI rows (0 random target
 * and move, 1 scored), the AI value codes the scorer reads, and the
 * opponent trainer row's AI id (+6). */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

enum { AI_ROWS = 2, CODES = 0x4D };

/* ai_id: the opponent's AI row; singles: one Pokemon a side; switch_pct: row 1's +7. */
static uint8_t *make_ai_pack(unsigned ai_id, int singles, unsigned switch_pct, size_t *size)
{
    size_t n; uint8_t *base = make_pack(2, 50, &n);
    size_t extra = 4 + 4 + AI_ROWS * 40 + 4 + CODES * 4;
    uint8_t *b = calloc(1, n + extra);
    memcpy(b, base, n); free(base);
    uint8_t *r = b + g32(b + 124);
    p16(r + 128 + 6, ai_id);
    if (singles) r[122] = 1;
    uint8_t *s = b + n;
    p32(s, 0); s += 4;                          /* no side-0 trainers */
    p32(s, AI_ROWS); s += 4;
    uint8_t *row0 = s, *row1 = s + 40;
    row0[0xE] = 1; row0[0x11] = 1; row0[0x12] = 1; row0[0x13] = 1; row0[0xB] = 1; row0[0x20] = row0[0x22] = 9;
    row1[7] = (uint8_t)switch_pct; row1[0xE] = 1; row1[0x13] = 1; row1[0xB] = 1; row1[0x20] = row1[0x22] = 9;
    s += AI_ROWS * 40;
    p32(s, CODES); s += 4;
    static const struct { unsigned code; int value; } codes[] = {
        {6, 20}, {7, -30}, {0x3D, -200}, {0x3E, 20}, {0x40, -100}, {0x44, 15}, {0x46, 20}, {0x47, 20}};
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; ++i) p32(s + codes[i].code * 4, (uint32_t)codes[i].value);
    p32(b + 4, 3); p32(b + 8, (uint32_t)(n + extra));
    c3_save_codec_sha1(b + 72, b + 128, n + extra - 128);
    *size = n + extra;
    return b;
}

static C3BattlePack pack;
static void load(unsigned ai_id, int singles, unsigned switch_pct)
{
    size_t n; uint8_t *bytes = make_ai_pack(ai_id, singles, switch_pct, &n);
    c3_battle_pack_free(&pack);
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error) && pack.ai_count == AI_ROWS && pack.ai_value_count == CODES);
    free(bytes);
}

/* Moves: 1 TACKLE (Normal, 40), 3 a Fire hit (40), 4 a Normal 200-power hit. */
static void moves(void)
{
    uint8_t *m = pack.bytes + 128 + 4 * 56;
    m[1] = 10; m[2] = 0; m[3] = 0; m[4] = 100; p16(m + 22, 200);
}

static uint16_t foe_move(const C3GeneralBattle *b)
{
    for (uint32_t i = 0; i < b->event_count; ++i)
        if (b->events[i].kind == C3_GB_EVENT_MOVE && b->events[i].actor >= 2) return b->events[i].move;
    return 0;
}

int main(void)
{
    uint8_t party[6][0x138];
    static C3GeneralBattle b;

    {   /* Scored row: Normal TACKLE has no effect on a Ghost hero (code 0x40, -100), so the foe always uses its Fire move. */
        load(1, 1, 0);
        unsigned fire = 0;
        for (uint32_t seed = 0; seed < 50; ++seed) {
            make_party(party, 1, 50, 1, 0);
            CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
            b.party[0][0].types[0] = b.party[0][0].types[1] = 7;
            C3GeneralMon *f = &b.party[1][b.active[1][0]];
            f->moves[0] = 1; f->pp[0] = 10; f->moves[1] = 3; f->pp[1] = 10;
            C3GeneralCommand c[2] = {{0, 0, 2}, {0}};
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            uint16_t m = foe_move(&b);
            CHECK(m == 3 || m == 0);  /* 0: it fainted or could not move first */
            fire += m == 3;
        }
        CHECK(fire > 25);
    }
    {   /* Random row (+0x11, +0x12): both moves come up, the immune one included. */
        load(0, 1, 0);
        unsigned seen[2] = {0, 0};
        for (uint32_t seed = 0; seed < 60; ++seed) {
            make_party(party, 1, 50, 1, 0);
            CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
            b.party[0][0].types[0] = b.party[0][0].types[1] = 7;
            C3GeneralMon *f = &b.party[1][b.active[1][0]];
            f->moves[0] = 1; f->pp[0] = 10; f->moves[1] = 3; f->pp[1] = 10;
            C3GeneralCommand c[2] = {{0, 0, 2}, {0}};
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            uint16_t m = foe_move(&b);
            seen[0] += m == 1; seen[1] += m == 3;
        }
        CHECK(seen[0] > 5 && seen[1] > 5);
    }
    {   /* Scored row: a move that knocks the target out (0x44, 0x46, 0x47) beats a weak one. */
        load(1, 1, 0); moves();
        unsigned ko = 0, total = 0;
        for (uint32_t seed = 0; seed < 40; ++seed) {
            make_party(party, 1, 50, 1, 0);
            CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
            C3GeneralMon *f = &b.party[1][b.active[1][0]];
            f->moves[0] = 2; f->pp[0] = 10; f->moves[1] = 4; f->pp[1] = 10; f->moves[2] = 0; f->moves[3] = 0;
            f->stats[5] = 999;                     /* it moves first */
            C3GeneralCommand c[2] = {{0, 0, 2}, {0}};
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            uint16_t m = foe_move(&b); ko += m == 4; total += m != 0;
        }
        CHECK(total > 30 && ko == total);
    }
    {   /* Switch (row 1, +7 = 100): no usable move affects the Ghost hero
         * (code 6, +20), so the foe switches to the Pokemon in reserve that
         * has the Fire move; at +7 = 0 it never switches. */
        for (unsigned pct = 0; pct <= 100; pct += 100) {
            load(1, 1, pct);
            unsigned switched = 0;
            for (uint32_t seed = 0; seed < 20; ++seed) {
                make_party(party, 1, 50, 1, 0);
                CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
                b.party[0][0].types[0] = b.party[0][0].types[1] = 7;
                unsigned act = b.active[1][0], bench = 1 - act;
                C3GeneralMon *f = &b.party[1][act], *g = &b.party[1][bench];
                memset(f->moves, 0, sizeof f->moves); f->moves[0] = 1; f->pp[0] = 10;
                memset(g->moves, 0, sizeof g->moves); g->moves[0] = 3; g->pp[0] = 10;
                C3GeneralCommand c[2] = {{0, 0, 2}, {0}};
                CHECK(c3_general_battle_turn(&b, &pack, c, error));
                switched += b.active[1][0] == bench;
            }
            CHECK(pct ? switched == 20 : switched == 0);
        }
    }
    {   /* A spectator battle (side-0 CPU trainer) and an old pack keep working:
         * a version-2 pack has no AI rows and falls back to the uniform policy. */
        size_t n; uint8_t *bytes = make_pack(2, 50, &n); C3BattlePack old = {0};
        CHECK(c3_battle_pack_decode(&old, bytes, n, error) && !old.ai_count);
        make_party(party, 1, 50, 2, 0);
        CHECK(c3_general_battle_begin(&b, &old, 0, party, 3, error));
        C3GeneralCommand c; CHECK(c3_general_battle_choose(&b, &old, 2, &c) && c.kind == 0);
        c3_battle_pack_free(&old); free(bytes);
    }
    c3_battle_pack_free(&pack);
    puts("battle-trainer-ai: scored moves, random rows, KO preference and switching ok");
    return 0;
}
