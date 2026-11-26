#define C3_FIXTURE_MOVES 360
#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/shadow_moves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A Shadow Pokemon's move slots (include/colosseum3ds/shadow_moves.h,
 * pokemonBiosGetPokemonWazaPtr): the gauge -> shown-moves rule, then a
 * Shadow FOE snagged in one synthetic battle and led out in the next. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

enum { SR = 0x164, HIDDEN = 0x165, TACKLE = 1, GROWL = 2, BURN = 3 };

/* Bit per slot 1..3 showing the Pokemon's own move. */
static unsigned open_slots(int shadow, int trainer, int32_t dp, uint32_t initial)
{
    unsigned level = c3_shadow_level(shadow, dp, initial), mask = 0;
    for (unsigned slot = 1; slot < 4; ++slot)
        if (c3_shadow_shown_move(TACKLE, slot, shadow, trainer, level) == TACKLE) mask |= 1u << slot;
    return mask;
}

static void rule(void)
{
    /* pokemonGetDarkPokemonLevel over a full gauge of 30 (3000 in +0xDC). */
    const int32_t dp[] = {3000, 2400, 2399, 1800, 1200, 1199, 600, 599, 1, 0, -100};
    const unsigned level[] = {0, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    /* Slot 1 opens under 80%, slot 2 under 40%, slot 3 under 20%. */
    const unsigned open[] = {0, 0, 2, 2, 2, 6, 6, 14, 14, 14, 14};
    for (unsigned i = 0; i < sizeof dp / sizeof dp[0]; ++i) {
        CHECK(c3_shadow_level(1, dp[i], 30) == level[i]);
        CHECK(open_slots(1, 0, dp[i], 30) == open[i]);
        CHECK(open_slots(1, 1, dp[i], 30) == 14);           /* a trainer's: slots 1..3 as they are */
        for (unsigned t = 0; t < 2; ++t) CHECK(c3_shadow_shown_move(TACKLE, 0, 1, (int)t, level[i]) == SR);
    }
    CHECK(c3_shadow_level(0, 3000, 30) == 7 && open_slots(0, 0, 3000, 30) == 14);
    CHECK(c3_shadow_shown_move(TACKLE, 0, 0, 0, 7) == TACKLE);
    /* An unknown full gauge counts as 1 (pokemonGetDarkPokemonLevel's divisor). */
    CHECK(c3_shadow_level(1, 100, 0) == 0 && c3_shadow_level(1, 99, 0) == 1 && c3_shadow_level(1, 0, 0) == 6);
    /* "????" stands in even for an empty slot. */
    CHECK(c3_shadow_shown_move(0, 3, 1, 0, 0) == HIDDEN && c3_shadow_shown_move(0, 3, 1, 0, 5) == 0);
    /* Records: Shadow while +0xD8 is set and +0xDC is not negative. */
    uint8_t r[0x138]; memset(r, 0, sizeof r);
    p16(r + 0x78, TACKLE); p16(r + 0x7c, GROWL); p16(r + 0x80, BURN);
    p16(r + 0xd8, 1); p32(r + 0xdc, 3000);
    CHECK(c3_shadow_record_is_shadow(r));
    CHECK(c3_shadow_record_move(r, 0, 30) == SR && c3_shadow_record_move(r, 1, 30) == HIDDEN && c3_shadow_record_move(r, 3, 30) == HIDDEN);
    p32(r + 0xdc, 2000);
    CHECK(c3_shadow_record_move(r, 1, 30) == GROWL && c3_shadow_record_move(r, 2, 30) == HIDDEN);
    p16(r + 0xd6, 7);                                         /* fightTrainerPokemonDataId */
    CHECK(c3_shadow_record_move(r, 0, 30) == SR && c3_shadow_record_move(r, 2, 30) == BURN && c3_shadow_record_move(r, 3, 30) == 0);
    p16(r + 0xd6, 0); p32(r + 0xdc, (uint32_t)-100);          /* purified */
    CHECK(!c3_shadow_record_is_shadow(r) && c3_shadow_record_move(r, 0, 30) == TACKLE && c3_shadow_record_move(r, 1, 30) == GROWL);
    puts("shadow-moves: gauge -> shown moves rule (hero, trainer, purified, records) ok");
}

static uint8_t *make_shadow_pack(size_t *size)
{
    uint8_t *b = make_pack(2, 5, size);
    uint32_t entries = g32(b + 124);
    uint8_t *m = b + 128 + SR * 56;      /* Shadow Rush: typeless 9, 90 power */
    m[1] = 5; m[2] = 9; m[3] = 0; m[4] = 100; p16(m + 22, 90);
    m = b + 128 + HIDDEN * 56; m[1] = 5;  /* "????" */
    /* The Shadow FOE (opponent 0) knows TACKLE, GROWL and the burn hit. */
    uint8_t *t = b + entries + 192;
    p32(t + 0x34 + 8, GROWL); p32(t + 0x34 + 16, BURN);
    c3_save_codec_sha1(b + 72, b + 128, *size - 128);
    return b;
}

static void play_out(C3GeneralBattle *b, const C3BattlePack *pack)
{
    for (unsigned t = 0; t < 200 && b->outcome == C3_GB_RUNNING; ++t) {
        C3GeneralCommand c[2] = {{0}};
        for (unsigned a = 0; a < b->active_count; ++a) if (b->active[0][a] != 255) CHECK(c3_general_battle_choose(b, pack, a, &c[a]));
        CHECK(c3_general_battle_turn(b, pack, c, error));
    }
    CHECK(b->outcome != C3_GB_RUNNING);
}

static void snag_then_battle(void)
{
    size_t n; uint8_t *bytes = make_shadow_pack(&n); C3BattlePack pack;
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    static C3GeneralBattle b; uint8_t party[6][0x138], copy[6][0x138]; C3GeneralRewards rw;

    /* Battle 1: the FOE shows Shadow Rush over TACKLE, its other moves as they are. */
    make_party(party, 1, 50, 1, 0);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 21, error));
    const C3GeneralMon *foe = &b.party[1][0];
    CHECK(foe->moves[0] == SR && foe->moves[1] == GROWL && foe->moves[2] == BURN && foe->moves[3] == 0);
    CHECK(foe->own_moves[0] == TACKLE && foe->own_pp[0] == 35 && c3_general_battle_legal_moves(&b, &pack, 2) == 7);
    b.snag = 1; b.balls[1] = 1;   /* a Master Ball */
    {   C3GeneralCommand c[2] = {{2, 1, 2}, {0}};   /* the hero has one Pokemon: position 1 is empty */
        CHECK(c3_general_battle_turn(&b, &pack, c, error) && b.caught[0] == 1); }
    play_out(&b, &pack);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw) && rw.caught_count == 1);
    C3SnagOwner owner; memset(&owner, 0, sizeof owner); owner.trainer_id = 0x12345678u;
    uint8_t rec[0x138];
    CHECK(c3_general_snag_record(&pack, &b, 0, &owner, 30, 9, rec));
    /* The record keeps its own moves; Shadow Rush is only what it shows. */
    CHECK(g16(rec + 0x78) == TACKLE && g16(rec + 0x7c) == GROWL && g16(rec + 0x80) == BURN && !g16(rec + 0x84));
    CHECK(rec[0x7a] == 35 && rec[0x7e] == 40 && rec[0x82] == 25 && !g16(rec + 0xd6) && g16(rec + 0xd8) == 1 && g32(rec + 0xdc) == 3000u);
    CHECK(c3_shadow_record_move(rec, 0, 30) == SR && c3_shadow_record_move(rec, 1, 30) == HIDDEN);

    /* Battle 2: the snagged FOE leads the hero's party. */
    make_party(party, 1, 50, 2, 0); memcpy(party[0], rec, sizeof rec);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 22, error));
    C3GeneralMon *m = &b.party[0][0];
    CHECK(m->species == 2 && m->shadow && !m->trainer && m->dp == 3000 && m->dp_max == 3000);
    CHECK(m->moves[0] == SR && m->moves[1] == HIDDEN && m->moves[2] == HIDDEN && m->moves[3] == HIDDEN && m->hidden == 15);
    CHECK(c3_general_battle_legal_moves(&b, &pack, 0) == 1);   /* only Shadow Rush */
    {   C3GeneralCommand hidden = {0, 1, 2}, rush = {0, 0, 2};
        CHECK(!c3_general_battle_command_valid(&b, &pack, 0, hidden) && c3_general_battle_command_valid(&b, &pack, 0, rush)); }
    /* The trainer AI on the hero's side picks only what it may (the headless policy). */
    for (unsigned k = 0; k < 20; ++k) { C3GeneralCommand c; CHECK(c3_general_battle_choose(&b, &pack, 0, &c)); CHECK(c.kind != 0 || c.slot == 0); }
    for (unsigned i = 0; i < b.party_count[1]; ++i) b.party[1][i].hp = b.party[1][i].max_hp = 999;
    m->hp = m->max_hp = 999; m->stats[5] = 999;   /* it acts first and survives the turn */
    {   C3GeneralCommand c[2] = {{0, 0, 2}, {0}};
        CHECK(c3_general_battle_choose(&b, &pack, 1, &c[1]));
        CHECK(c3_general_battle_turn(&b, &pack, c, error)); }
    for (unsigned i = 0; i < b.party_count[1]; ++i) if (b.party[1][i].hp) b.party[1][i].hp = 1;
    /* Shadow Rush reads its 5 PP again; the use lands in TACKLE's own slot (at most 4). */
    CHECK(m->moves[0] == SR && m->pp[0] == 5 && m->own_pp[0] == 4);
    /* The gauge opening shows each move with its own PP. */
    m->dp = 2100; c3_general_shadow_moves(m);
    CHECK(m->moves[1] == GROWL && m->pp[1] == 40 && m->pp_max[1] == 40 && m->moves[2] == HIDDEN && c3_general_battle_legal_moves(&b, &pack, 0) == 3);
    m->pp[1] = 39;
    m->dp = 1100; c3_general_shadow_moves(m);
    CHECK(m->moves[1] == GROWL && m->pp[1] == 39 && m->moves[2] == BURN && m->pp[2] == 25 && m->moves[3] == HIDDEN);
    m->dp = 500; c3_general_shadow_moves(m);
    CHECK(m->moves[3] == 0 && m->hidden == 1 && c3_general_battle_legal_moves(&b, &pack, 0) == 7);
    /* Back up the gauge (no drop raises it, but the rule is symmetric): GROWL's PP is kept. */
    m->dp = 3000; c3_general_shadow_moves(m);
    CHECK(m->moves[1] == HIDDEN && m->own_pp[1] == 39);
    m->dp = 500; c3_general_shadow_moves(m);
    CHECK(m->pp[1] == 39);
    play_out(&b, &pack);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw) && rw.written);
    CHECK(g16(copy[0] + 0x78) == TACKLE && g16(copy[0] + 0x7c) == GROWL && g16(copy[0] + 0x80) == BURN && !g16(copy[0] + 0x84));
    CHECK(copy[0][0x7a] == 4 && copy[0][0x7e] <= 39 && g32(copy[0] + 0xdc) == 500u);

    /* Purified (+0xDC = -100): its own four moves, all selectable. */
    p32(party[0] + 0xdc, (uint32_t)-100);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 23, error));
    CHECK(!b.party[0][0].shadow && b.party[0][0].moves[0] == TACKLE && b.party[0][0].moves[1] == GROWL && !b.party[0][0].hidden);
    CHECK(c3_general_battle_legal_moves(&b, &pack, 0) == 7);
    c3_battle_pack_free(&pack); free(bytes);
    puts("shadow-moves: snagged FOE keeps its moves, leads with Shadow Rush and \"????\", opens with the gauge ok");
}

int main(void)
{
    rule();
    snag_then_battle();
    return 0;
}
