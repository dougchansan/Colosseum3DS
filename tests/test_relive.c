/* Relic Stone purification rules (src/relive.c). The integration part needs
 * private inputs: C3_RELIVE_SAVE (a save GCI), C3_RELIVE_NEWGAME (prepared
 * newgame.c3g) and C3_RELIVE_PACK (battles.c3x); it is skipped without them.
 * It turns party slot 0 into a heart-opened Shadow Pokemon in memory only. */
#include "colosseum3ds/relive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

int main(void)
{
    /* Purify flags: 0x4EE + id - 1 for ids 1..0x60. */
    CHECK(c3_relive_purify_flag(0) == 0 && c3_relive_purify_flag(1) == 0x4EE && c3_relive_purify_flag(0x60) == 0x54D);
    CHECK(c3_relive_purify_flag(0x61) == 0);

    /* Record predicates. */
    uint8_t r[C3_GB_POKEMON_SIZE] = {0};
    CHECK(!c3_relive_is_shadow(r));
    put16(r + 0xD8, 5); put32(r + 0xDC, 1234);
    CHECK(c3_relive_is_shadow(r) && !c3_relive_pickable(r));
    put32(r + 0xDC, 0); CHECK(c3_relive_pickable(r));
    put32(r + 0xDC, (uint32_t)-100); CHECK(!c3_relive_is_shadow(r) && !c3_relive_pickable(r));

    /* preReliveMain's decision. */
    C3ReliveCounts c = {0};
    CHECK(c3_relive_action(0, &c) == C3_RELIVE_NONE);                 /* no item, none ready: 5 */
    c.ready = 1; c.remaining = 1; CHECK(c3_relive_action(0, &c) == C3_RELIVE_PICK);  /* 4 */
    c.has_item = 1; CHECK(c3_relive_action(0, &c) == C3_RELIVE_PICK);  /* 2 -> 4 for mode 0 */
    c.ready = 0; CHECK(c3_relive_action(0, &c) == C3_RELIVE_NONE);    /* 1 -> 5 */
    CHECK(c3_relive_action(1, &c) == C3_RELIVE_PURIFY);                /* mode 1 always 2 */
    c.remaining = 0; CHECK(c3_relive_action(0, &c) == C3_RELIVE_NONE); /* 3 -> 5 */
    CHECK(c3_relive_action(2, &c) == C3_RELIVE_NONE);                  /* mode 2 keeps 3 */

    const char *save = getenv("C3_RELIVE_SAVE"), *newgame = getenv("C3_RELIVE_NEWGAME"), *packpath = getenv("C3_RELIVE_PACK");
    if (save && newgame && packpath) {
        C3GameState state = {0}; C3BattlePack pack; char error[C3_GAME_ERROR_SIZE];
        memset(&pack, 0, sizeof pack);
        if (!c3_game_state_load(&state, save, newgame, error)) { fprintf(stderr, "relive: %s\n", error); return 1; }
        CHECK(c3_battle_pack_load(&pack, packpath, error));
        uint8_t before[C3_GB_POKEMON_SIZE], after[C3_GB_POKEMON_SIZE];
        CHECK(c3_game_state_pokemon(&state, 0, before) && be16(before));
        /* A Shadow Pokemon whose heart is open, with stored-up experience,
         * friendship and the status 0x3E entry. */
        put16(before + 0xD8, 7); put32(before + 0xDC, 0);
        uint8_t level = before[0x60];
        put32(before + 0xE0, 20000); put16(before + 0xE4, 30); put16(before + 0xB0, 100);
        put16(before + 0xE8, 0x3E); before[0xEF] = 0x5A; /* pad7 kept */
        before[0xC6] = 0;
        CHECK(c3_game_state_set_pokemon(&state, 0, before));
        uint32_t flag = 1;
        CHECK(c3_game_state_set_flag(&state, c3_relive_purify_flag(7), 0));
        C3ReliveCounts counts; CHECK(c3_relive_counts(&state, &counts));
        CHECK(counts.ready >= 1 && counts.remaining >= 1);
        CHECK(c3_relive_action(0, &counts) == C3_RELIVE_PICK);
        C3ReliveResult res;
        CHECK(c3_relive_purify(&state, 0, 0, &pack, &res));
        CHECK(c3_game_state_pokemon(&state, 0, after));
        CHECK((int32_t)be32(after + 0xDC) == -100 && !c3_relive_is_shadow(after));
        CHECK(c3_game_state_get_flag(&state, c3_relive_purify_flag(7), &flag) && flag == 1);
        CHECK(res.pool_exp == 20000 && res.friendship_before == 100 && be16(after + 0xB0) == 200);  /* 100 + 30 + 70 */
        CHECK(be16(after + 0xE4) == 0 && after[0xC6] == 1);
        CHECK(res.status_cleared && be16(after + 0xE8) == 0 && after[0xEF] == 0x5A);
        CHECK(be32(after + 0x5C) >= be32(before + 0x5C));
        if (level < 100) CHECK(be32(after + 0xE0) == 0 || after[0x60] == 100);
        CHECK(after[0x60] == level + res.exp.level_count);
        CHECK(res.move0 == be16(after + 0x78));
        /* A second purification of the same slot: no longer Shadow. */
        CHECK(!c3_relive_purify(&state, 0, 0, &pack, &res));
        /* Mode 1 (forced) opens a closed heart first. */
        put32(before + 0xDC, 5000); put16(before + 0xD8, 8);
        CHECK(c3_game_state_set_pokemon(&state, 0, before));
        CHECK(c3_relive_purify(&state, 0, 1, &pack, &res));
        CHECK(c3_game_state_pokemon(&state, 0, after) && (int32_t)be32(after + 0xDC) == -100);
        printf("relive: purified species %u level %u -> %u, %u moves learned, %u skipped\n",
               (unsigned)res.species, (unsigned)level, (unsigned)after[0x60], res.exp.learned_count, res.exp.skipped_count);
        c3_battle_pack_free(&pack); c3_game_state_free(&state);
    } else {
        puts("relive: integration skipped (C3_RELIVE_SAVE/NEWGAME/PACK unset)");
    }
    printf("relive: %u checks passed\n", checks);
    return 0;
}
