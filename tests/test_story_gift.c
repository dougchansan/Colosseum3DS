/* Script native D9 (fn_801CADA8): the gift Pokemon records and where they
 * are stored (c3_battle_story_gift, c3_battle_pack_gift_record).
 *
 * Optional PRIVATE-DATA test: C3_GIFT_NEWGAME (boot newgame.c3g),
 * C3_GIFT_PACK (battles.c3x) and C3_GIFT_PDA (pda.c3pd); skipped without
 * them. Every run starts a fresh WES game. */
#include "colosseum3ds/battle_story.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void text(const uint8_t *gschar, char out[32]) { c3_game_text_ascii(gschar, 11, out, 32); }

static C3BattlePack pack;
static C3PdaAssets assets;

static void new_game(C3GameState *game, const char *newgame, uint32_t seed)
{
    char error[C3_GAME_ERROR_SIZE] = "";
    c3_game_state_free(game);
    if (!c3_game_state_new(game, "WES", seed, newgame, error)) { fprintf(stderr, "%s\n", error); exit(1); }
}
static unsigned box_count(const C3GameState *game)
{
    uint8_t r[0x138]; unsigned n = 0;
    for (unsigned box = 0; box < 3; ++box)
        for (unsigned i = 0; i < 30; ++i) if (c3_game_state_box_pokemon(game, box, i, r) && rd16(r)) ++n;
    return n;
}
/* The pack's level-up rows (species +0xBA) that pokemonCreate teaches up
 * to `level`: the last four distinct moves, an independent reading. */
static void expected_moves(uint16_t species, unsigned level, uint16_t out[4])
{
    const uint8_t *s = pack.bytes + pack.species_at + species * 284u; unsigned n = 0;
    memset(out, 0, 4 * sizeof out[0]);
    for (unsigned j = 0; j < 20; ++j) {
        const uint8_t *row = s + 0xba + 4 * j; uint16_t id = rd16(row + 2);
        if (!row[0] || row[0] > level || !id) continue;
        int known = 0; for (unsigned k = 0; k < n; ++k) known |= out[k] == id;
        if (known) continue;
        if (n == 4) { memmove(out, out + 1, 3 * sizeof out[0]); n = 3; }
        out[n++] = id;
    }
}
static void check_stats(const uint8_t *r)
{
    static const uint8_t raise[5] = { 1, 2, 5, 3, 4 };
    const uint8_t *s = pack.bytes + pack.species_at + rd16(r) * 284u;
    unsigned level = r[0x60], nature = rd32(r + 4) % 25u;
    for (unsigned j = 0; j < 6; ++j) {
        unsigned base = rd16(s + 0x84 + 2 * j), iv = rd16(r + 0xa4 + 2 * j), v = (2 * base + iv) * level / 100u;
        if (!j) v += level + 10u;
        else { v += 5u; if (nature / 5 != nature % 5) { if (raise[nature / 5] == j) v = v * 11u / 10u; if (raise[nature % 5] == j) v = v * 9u / 10u; } }
        CHECK(iv < 32 && rd16(r + 0x8c + 2 * j) == v);
    }
    CHECK(rd16(r + 0x8a) == rd16(r + 0x8c));
}
static int not_shiny(const uint8_t *r)
{
    uint32_t t = rd32(r + 0x14), p = rd32(r + 4);
    return ((t >> 16) ^ (t & 0xffff) ^ (p >> 16) ^ (p & 0xffff)) >= 8;
}

int main(void)
{
    const char *newgame = getenv("C3_GIFT_NEWGAME"), *pack_path = getenv("C3_GIFT_PACK"), *pda_path = getenv("C3_GIFT_PDA");
    if (!newgame || !pack_path || !pda_path) { puts("story-gift: skipped (C3_GIFT_NEWGAME/PACK/PDA unset)"); return 0; }
    char error[C3_GB_ERROR_SIZE] = "", name[32];
    CHECK(c3_battle_pack_load(&pack, pack_path, error));
    FILE *f = fopen(pda_path, "rb"); CHECK(f);
    CHECK(fseek(f, 0, SEEK_END) == 0); long size = ftell(f); CHECK(size > 0 && fseek(f, 0, SEEK_SET) == 0);
    uint8_t *pda = malloc((size_t)size); CHECK(pda && fread(pda, 1, (size_t)size, f) == (size_t)size); fclose(f);
    CHECK(c3_pda_assets_open(&assets, pda, (size_t)size));

    C3GameState game = { 0 }; uint8_t r[0x138], starter[0x138]; uint16_t species = 0; unsigned where = 99, memo = 0;
    size_t bytes = 0; const uint8_t *body;
    new_game(&game, newgame, 0x12345678u);
    body = c3_game_state_body(&game, &bytes);
    CHECK(c3_game_state_memo_count(&game, &memo) && memo == 2);
    CHECK(c3_game_state_pokemon(&game, 0, starter));

    /* Kind 3, Duking's Plusle: party slot 2 (the two starters fill 0, 1). */
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, &species, &where) == 2 && where == 2 && species == 0x161);
    CHECK(c3_game_state_pokemon(&game, 2, r));
    CHECK(rd16(r) == 0x161 && r[0x60] == 13 && rd32(r + 0x5c) == 2197); /* medium-fast level 13 */
    text(r + 0x2e, name); CHECK(!strcmp(name, "PLUSLE")); CHECK(!memcmp(r + 0x2e, r + 0x44, 0x16));
    text(r + 0x18, name); CHECK(!strcmp(name, "DUKING"));
    CHECK(rd32(r + 0x14) == 0x911d && r[0x10] == 0 && rd16(r + 0x0c) == 0xfe && r[0x0e] == 13 && r[0x0f] == 4);
    CHECK(!memcmp(r + 8, body, 4));                                   /* the save's attest */
    CHECK(rd16(r + 0xb0) == 70 && !rd16(r + 0x88) && !rd16(r + 0xd8)); /* friendship, no item, not Shadow */
    {   uint16_t want[4]; expected_moves(0x161, 13, want);
        CHECK(want[0] == 45 && want[1] == 86 && want[2] == 98 && want[3] == 270); /* Growl, Thunder Wave, Quick Attack, Helping Hand */
        for (unsigned k = 0; k < 4; ++k) {
            C3GeneralMoveInfo mv; CHECK(c3_battle_pack_move(&pack, want[k], &mv));
            CHECK(rd16(r + 0x78 + 4 * k) == want[k] && r[0x7a + 4 * k] == mv.pp && !r[0x7b + 4 * k]);
        } }
    check_stats(r); CHECK(not_shiny(r));
    CHECK(r[0xcc] <= 1);
    /* pokemonInit's defaults, as on the starters the original initialised. */
    CHECK(r[0xd0] == starter[0xd0] && !memcmp(r + 0xdc, starter + 0xdc, 4) && rd32(r + 0xdc) == (uint32_t)-100);
    {   C3GameMemo m; CHECK(c3_game_state_memo_count(&game, &memo) && memo == 3);
        CHECK(c3_game_state_memo(&game, 2, &m) && m.species == 0x161 && (m.raw & 0x8000u));
        CHECK(m.trainer_id == 0x911d && m.personality == rd32(r + 4)); }
    printf("story-gift: kind 3 PLUSLE party=2 pid=%08lx ivs=%u/%u/%u/%u/%u/%u hp=%u ability=%u\n", (unsigned long)rd32(r + 4),
           rd16(r + 0xa4), rd16(r + 0xa6), rd16(r + 0xa8), rd16(r + 0xaa), rd16(r + 0xac), rd16(r + 0xae), rd16(r + 0x8c), r[0xcc]);

    /* The same seed draws the same Plusle. */
    {   uint8_t again[0x138]; C3GameState other = { 0 };
        new_game(&other, newgame, 0x12345678u);
        CHECK(c3_battle_story_gift(&other, &pack, &assets, 3, NULL, NULL) == 2 && c3_game_state_pokemon(&other, 2, again));
        CHECK(!memcmp(again, r, sizeof again)); c3_game_state_free(&other); }

    /* A full party sends it to the PC (heroGetPokemon -> pcboxAddPokemon):
     * -1, box 0 entry 0; the memo already lists Plusle. */
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, NULL, &where) == 3 && where == 3);
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, NULL, &where) == 4);
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, NULL, &where) == 5);
    CHECK(box_count(&game) == 0);
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, &species, &where) == -1 && where == 100 && species == 0x161);
    CHECK(c3_game_state_box_pokemon(&game, 0, 0, r) && rd16(r) == 0x161 && r[0x60] == 13 && rd32(r + 0x14) == 0x911d);
    text(r + 0x18, name); CHECK(!strcmp(name, "DUKING"));
    CHECK(c3_game_state_memo_count(&game, &memo) && memo == 3);
    /* Kinds 1 and 2 (heroCatchPokemon, no PC box): -2 with a full party. */
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 1, NULL, NULL) == -2 && box_count(&game) == 1);
    /* Every box full: -2, nothing written. */
    for (unsigned n = box_count(&game); n < 90; ++n) CHECK(c3_game_state_receive_pokemon(&game, r, r, &where) && where >= 100);
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 3, NULL, NULL) == -2);
    CHECK(c3_battle_story_gift(&game, &pack, &assets, 7, NULL, NULL) == -3 && c3_battle_story_gift(&game, &pack, &assets, 0, NULL, NULL) == -3);

    /* Kind 2, Espeon: the hero's own (name, ID, sex), male, its moves,
     * experience and friendship as the New Game starter has them. */
    new_game(&game, newgame, 0x2468aceu);
    body = c3_game_state_body(&game, &bytes);
    CHECK(c3_game_state_pokemon(&game, 1, starter) && rd16(starter) == 0xc4);
    CHECK(c3_battle_story_gift(&game, &pack, NULL, 2, &species, &where) == 2 && species == 0xc4);
    CHECK(c3_game_state_pokemon(&game, 2, r));
    CHECK(!memcmp(r, starter, 2) && r[0x60] == starter[0x60] && rd32(r + 0x5c) == 0x3ff4);
    CHECK(!memcmp(r + 0x0c, starter + 0x0c, 0x0e - 0x0c) && r[0x0e] == 25 && r[0x0f] == 4 && r[0x10] == body[0x70 + 0xa80]);
    CHECK(!memcmp(r + 0x14, starter + 0x14, 4 + 0x16));               /* trainer ID and name */
    for (unsigned k = 0; k < 4; ++k) CHECK(rd16(r + 0x78 + 4 * k) == rd16(starter + 0x78 + 4 * k) && r[0x7a + 4 * k] == starter[0x7a + 4 * k]);
    CHECK(rd16(r + 0xb0) == 0xdc && rd16(starter + 0xb0) == 0xdc);
    CHECK((rd32(r + 4) & 255u) >= pack.bytes[pack.species_at + 0xc4 * 284u + 2]); /* male */
    check_stats(r); CHECK(not_shiny(r));
    CHECK(c3_game_state_memo_count(&game, &memo) && memo == 2);       /* heroCatchPokemon: no memo */
    /* Kind 1, Umbreon, level 26 from its experience. */
    CHECK(c3_battle_story_gift(&game, &pack, NULL, 1, &species, &where) == 3 && species == 0xc5);
    CHECK(c3_game_state_pokemon(&game, 3, r) && r[0x60] == 26 && rd32(r + 0x5c) == 0x4a6f);
    check_stats(r);
    /* Kinds 4..6 (bonus disc): their own trainers and attest. */
    {   static const struct { unsigned kind; uint16_t species; uint8_t level, attest; uint32_t id; const char *ot; } bonus[] = {
            { 4, 0xfa, 70, 8, 0x2740, "MATTLE" }, { 5, 0x19, 10, 9, 0x7991, "COLOS" }, { 6, 0xfb, 10, 9, 0x7991, "AGATE" } };
        for (unsigned i = 0; i < 3; ++i) {
            new_game(&game, newgame, 0x1000u + i);
            body = c3_game_state_body(&game, &bytes);
            CHECK(c3_battle_story_gift(&game, &pack, &assets, bonus[i].kind, &species, &where) == 2 && species == bonus[i].species);
            CHECK(c3_game_state_pokemon(&game, 2, r) && r[0x60] == bonus[i].level && rd32(r + 0x14) == bonus[i].id);
            text(r + 0x18, name); CHECK(!strcmp(name, bonus[i].ot));
            CHECK(r[8] == bonus[i].attest && r[9] == 3 && r[10] == body[2] && r[11] == body[3] && rd16(r + 0x0c) == 0xff);
            CHECK(rd16(r + 0x88) == (bonus[i].kind == 5 ? 0xca : 0) && r[0x10] == (bonus[i].kind == 6));
            check_stats(r); CHECK(not_shiny(r));
            CHECK(c3_game_state_memo_count(&game, &memo) && memo == 3);
        } }
    /* Without the P*DA names kind 3 cannot be built (its trainer's name). */
    CHECK(c3_battle_story_gift(&game, &pack, NULL, 3, NULL, NULL) == -3);

    c3_game_state_free(&game); c3_battle_pack_free(&pack); free(pda);
    printf("story-gift: %u checks passed\n", checks);
    return 0;
}
