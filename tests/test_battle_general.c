#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/battle_general_view.h"
#include "colosseum3ds/battle_presentation.h"
#include "colosseum3ds/game_state.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for the general story battle (src/battle_general.c).
 * Part 1 builds a small synthetic C3B2 pack (no original data) and checks
 * decoding, encounter info, a won and a lost battle, determinism, the story
 * result codes and the party/prize writeback.
 * Part 2 (PRIVATE, optional): with <battles.c3x> <newgame dir> <save.gci> it
 * runs every admitted encounter of the prepared pack against that save's
 * party with the opponent policy on both sides, and fails if any battle
 * cannot be started or a turn is rejected. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];

enum { MOVES = 166, SPECIES = 3, ENCOUNTERS = 2, ENTRY = 672 };
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static unsigned g16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static size_t pack_size(void)
{ return 128 + MOVES * 56 + SPECIES * 284 + 18 * 44 + 8 * 0x194 + ENCOUNTERS * ENTRY + (SPECIES + MOVES + 2 * ENCOUNTERS) * 16; }

/* opponent_level/species configure encounter 0's two-Pokemon party. */
static uint8_t *make_pack(unsigned opponent_species, unsigned opponent_level, size_t *size)
{
    size_t n = pack_size();
    uint8_t *b = calloc(1, n);
    uint32_t moves = 128, species = moves + MOVES * 56, types = species + SPECIES * 284,
             growth = types + 18 * 44, entries = growth + 8 * 0x194, names = entries + ENCOUNTERS * ENTRY;
    memcpy(b, "C3B2", 4); p32(b + 4, 1); p32(b + 8, (uint32_t)n); p32(b + 12, ENCOUNTERS);
    p32(b + 16, MOVES); p32(b + 20, SPECIES); p32(b + 24, 18); p32(b + 28, 8);
    p32(b + 112, species); p32(b + 116, types); p32(b + 120, growth); p32(b + 124, entries);
    /* move 1 TACKLE (hit), 2 GROWL (Attack down, both foes), 3 EMBER-like
     * burn hit, 165 STRUGGLE (recoil). Row: pri, pp, type, range, acc, sec, .., power +22, effect +26. */
    uint8_t *m = b + moves + 1 * 56; m[1] = 35; m[2] = 0; m[3] = 0; m[4] = 100; p16(m + 22, 40); p16(m + 26, 0);
    m = b + moves + 2 * 56; m[1] = 40; m[2] = 0; m[3] = 4; m[4] = 100; p16(m + 26, 18);
    m = b + moves + 3 * 56; m[1] = 25; m[2] = 10; m[3] = 0; m[4] = 100; m[5] = 10; p16(m + 22, 40); p16(m + 26, 4);
    m = b + moves + 165 * 56; m[1] = 1; m[2] = 0; m[3] = 0; p16(m + 22, 50); p16(m + 26, 48);
    for (unsigned s = 1; s < SPECIES; ++s) {
        uint8_t *r = b + species + s * 284;
        r[0] = 0; p16(r + 6, s == 1 ? 100 : 50); r[0x30] = r[0x31] = s == 1 ? 0 : 10;
        for (unsigned j = 0; j < 6; ++j) p16(r + 0x84 + 2 * j, s == 1 ? 100 : 40);
        r[0xba] = 1; p16(r + 0xbc, 1); /* level-up: TACKLE at 1 */
    }
    for (unsigned t = 0; t < 18; ++t) for (unsigned d = 0; d < 18; ++d) p16(b + types + t * 44 + 8 + d * 2, 0x3f);
    p16(b + types + 0 * 44 + 8 + 7 * 2, 0x43);      /* Normal vs Ghost: immune */
    p16(b + types + 10 * 44 + 8 + 0 * 2, 0x41);     /* Fire vs Normal: 2x (synthetic) */
    for (unsigned g = 0; g < 8; ++g) for (unsigned l = 0; l <= 100; ++l) p32(b + growth + g * 0x194 + l * 4, l * l * l);
    for (unsigned e = 0; e < ENCOUNTERS; ++e) {
        uint8_t *r = b + entries + e * ENTRY; p16(r, e);
        uint8_t *enc = r + 8;
        enc[0] = 2; enc[1] = 1; enc[2] = 1; p16(enc + 4, 5);
        p16(enc + 24, 1); p32(enc + 28, 1); p16(enc + 32, 10);
        p16(r + 64 + 2, 200); r[120] = 1; r[121] = 6; r[122] = 2;
        p16(r + 128 + 2, 26); p16(r + 180, 5);
        if (e == 0) {
            r[2] = 2;
            for (unsigned k = 0; k < 2; ++k) {
                uint8_t *t = r + 192 + k * 80;
                t[4] = (uint8_t)opponent_level; p16(t + 10, opponent_species); t[3] = k == 0;
                p32(t + 0x34, 1); /* TACKLE */
            }
        } else p16(r + 4, 3);
    }
    const char *sn[SPECIES] = {"", "HERO", "FOE"};
    for (unsigned s = 0; s < SPECIES; ++s) strcpy((char *)b + names + s * 16, sn[s]);
    strcpy((char *)b + names + (SPECIES + 1) * 16, "TACKLE");
    strcpy((char *)b + names + (SPECIES + MOVES) * 16, "WILLIE");
    strcpy((char *)b + names + (SPECIES + MOVES + 1) * 16, "RIDER");
    c3_save_codec_sha1(b + 72, b + 128, n - 128);
    *size = n;
    return b;
}

static void make_party(uint8_t party[6][0x138], unsigned species, unsigned level, unsigned count, int shadow)
{
    memset(party, 0, 6 * 0x138);
    for (unsigned i = 0; i < count; ++i) {
        uint8_t *r = party[i];
        unsigned base = species == 1 ? 100 : 40;
        p16(r, species); r[0x60] = (uint8_t)level; p32(r + 0x5c, level * level * level);
        for (unsigned j = 0; j < 6; ++j) {
            unsigned core = (2 * base + 31) * level / 100;
            p16(r + 0xa4 + 2 * j, 31);
            p16(r + 0x8c + 2 * j, j ? core + 5 : core + level + 10);
        }
        p16(r + 0x8a, g16(r + 0x8c));
        p16(r + 0x78, 1); r[0x7a] = 35; /* TACKLE */
        p16(r + 0x7c, 3); r[0x7e] = 25; /* burn hit */
        if (shadow) p16(r + 0xd8, 1);
    }
}

/* The 3D presentation of a log (src/battle_general_view.c): every logged
 * event becomes exactly one presentation event, batches are contiguous and
 * accepted by the presentation timeline, and each send-out starts a batch
 * with the species it brings in. */
static C3GeneralView view;
static C3BattlePresentation timeline;
static unsigned send_outs, hits_with_actor;
static void check_view(const C3GeneralBattle *b)
{
    static C3BattlePreviewEvent out[C3_PREVIEW_EVENT_LIMIT];
    uint32_t first = 0, expected = 0, n;
    c3_general_view_restart(&view, b);
    while ((n = c3_general_view_batch(&view, b, out, C3_PREVIEW_EVENT_LIMIT, &first))) {
        CHECK(first == expected);
        expected += n;
        CHECK(c3_battle_presentation_submit(&timeline, out, n));
        for (uint32_t i = 0; i < n; ++i) {
            unsigned position; uint16_t species;
            const C3BattlePreviewEvent *e = &out[i];
            int send = c3_general_view_send_out(b, first + i, &position, &species);
            CHECK(send == (e->kind == C3_PREVIEW_EVENT_SEND_OUT));
            if (send) { CHECK(i == 0 || first + i == 0); CHECK(species && position < 4 && e->target == position); ++send_outs; }
            if (e->kind == C3_PREVIEW_EVENT_HP) { CHECK(e->target < 4); hits_with_actor += e->actor < 4; }
            if (e->kind == C3_PREVIEW_EVENT_FAINT) CHECK(e->target < 4 && b->events[first + i].hp[e->target] == 0);
        }
        /* Play the batch out so the next submit is accepted. */
        timeline.cursor = timeline.count; timeline.last_sequence = out[n - 1].sequence;
    }
    CHECK(expected == b->event_count);
}
static void view_begin(const C3GeneralBattle *b)
{
    static const uint16_t hp[4];
    memset(&view, 0, sizeof view);
    c3_battle_presentation_init(&timeline, hp);
    check_view(b);
}

static C3GeneralOutcome play(C3GeneralBattle *b, const C3BattlePack *p, unsigned *turns)
{
    view_begin(b);
    while (b->outcome == C3_GB_RUNNING) {
        C3GeneralCommand c[2] = {{0}};
        for (unsigned a = 0; a < b->active_count; ++a)
            if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp) CHECK(c3_general_battle_choose(b, p, a, &c[a]));
        CHECK(c3_general_battle_turn(b, p, c, error));
        check_view(b);
        ++*turns;
    }
    return b->outcome;
}

static void synthetic(void)
{
    size_t n; uint8_t *bytes = make_pack(2, 5, &n);
    C3BattlePack pack = {0}; C3BattleEncounterInfo info;
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    CHECK(c3_battle_pack_info(&pack, 0, &info));
    CHECK(info.reason == 0 && info.party_count == 2 && info.active_per_side == 2 && info.wipeout == 1);
    CHECK(info.trainer_id == 10 && info.prize_base == 5 && info.shadow_count == 1);
    CHECK(!strcmp(info.trainer, "WILLIE") && !strcmp(info.title, "RIDER"));
    CHECK(c3_battle_pack_info(&pack, 1, &info) && info.reason == 3);
    CHECK(!c3_battle_pack_info(&pack, 2, &info));
    char name[16]; CHECK(c3_battle_pack_move_name(&pack, 1, name) && !strcmp(name, "TACKLE"));
    C3BattlePack bad = {0};
    bytes[200] ^= 1; CHECK(!c3_battle_pack_decode(&bad, bytes, n, error)); bytes[200] ^= 1;
    CHECK(!c3_battle_pack_decode(&bad, bytes, n - 1, error));
    CHECK(c3_general_effect_modelled(0) && c3_general_effect_modelled(18) && !c3_general_effect_modelled(9));

    uint8_t party[6][0x138], copy[6][0x138];
    static C3GeneralBattle b, again;
    /* Encounter 1 has no opponent party and is refused with its reason. */
    make_party(party, 1, 50, 2, 0);
    CHECK(!c3_general_battle_begin(&b, &pack, 1, party, 7, error) && strstr(error, "no Pokemon"));

    /* Won: two level-50 heroes against two level-5 foes. */
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 7, error));
    CHECK(b.active_count == 2 && b.party_count[0] == 2 && b.party_count[1] == 2 && b.event_count >= 3);
    CHECK(b.party[1][0].moves[0] == 1 && b.party[1][0].pp[0] == 35 && b.party[1][0].shadow);
    again = b;
    unsigned turns = 0, turns_again = 0;
    CHECK(play(&b, &pack, &turns) == C3_GB_WON);
    CHECK(play(&again, &pack, &turns_again) == C3_GB_WON && turns == turns_again);
    CHECK(!memcmp(&b, &again, sizeof b)); /* deterministic for one seed */
    C3GeneralRewards rw;
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(rw.result_code == 2 && !rw.wipeout && rw.prize == 5u * 5u);
    /* Each foe: 50 * 5 / 7 * 3 / 2 = 52 split over the heroes that faced it. */
    CHECK(rw.exp[0] + rw.exp[1] > 0 && rw.exp[0] <= 104 && rw.exp[1] <= 104);
    CHECK(g32(copy[0] + 0x5c) == 125000u + rw.exp[0]);
    CHECK(g16(copy[0] + 0x8a) == b.party[0][0].hp && copy[0][0x7a] == b.party[0][0].pp[0]);

    /* Level up: a level-5 hero with almost enough experience. */
    make_party(party, 1, 5, 1, 0); p32(party[0] + 0x5c, 215); /* 6^3 = 216 */
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 3, error));
    turns = 0; C3GeneralOutcome o = play(&b, &pack, &turns);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    if (rw.exp[0]) {
        CHECK(copy[0][0x60] > 5 && rw.levels_gained[0] == copy[0][0x60] - 5);
        CHECK(g16(copy[0] + 0x8c) > g16(party[0] + 0x8c));
    }
    CHECK(rw.result_code == (o == C3_GB_WON ? 2u : 3u));

    /* Shadow Pokemon bank experience instead of levelling. */
    make_party(party, 1, 50, 1, 1);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 11, error));
    turns = 0; CHECK(play(&b, &pack, &turns) == C3_GB_WON);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(rw.exp[0] && g32(copy[0] + 0xe0) == rw.exp[0] && g32(copy[0] + 0x5c) == 125000u && copy[0][0x60] == 50);
    c3_battle_pack_free(&pack); free(bytes);

    /* Lost: one level-3 hero against two level-90 foes; wipeout is reported. */
    bytes = make_pack(1, 90, &n);
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    make_party(party, 2, 3, 1, 0);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 5, error));
    turns = 0; CHECK(play(&b, &pack, &turns) == C3_GB_LOST);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(rw.result_code == 3 && rw.wipeout && rw.prize == 0 && g16(copy[0] + 0x8a) == 0);
    /* A finished battle refuses further turns; a mismatched party is refused. */
    C3GeneralCommand c[2] = {{0}};
    CHECK(!c3_general_battle_turn(&b, &pack, c, error));
    p16(copy[0], 1); CHECK(!c3_general_battle_writeback(&b, &pack, copy, &rw));
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-general: synthetic pack, win/loss, determinism and writeback ok");
}

static void private_sweep(const char *pack_path, const char *newgame_dir, const char *save)
{
    C3BattlePack pack = {0}; C3GameState game = {0}; char file[1024], gerror[C3_GAME_ERROR_SIZE] = {0};
    CHECK(c3_battle_pack_load(&pack, pack_path, error));
    snprintf(file, sizeof file, "%s/newgame.c3g", newgame_dir);
    CHECK(c3_game_state_load(&game, save, file, gerror));
    uint8_t party[6][0x138];
    for (unsigned i = 0; i < 6; ++i) CHECK(c3_game_state_pokemon(&game, i, party[i]));
    static C3GeneralBattle b;
    unsigned admitted = 0, won = 0, lost = 0, draw = 0, refused = 0;
    unsigned long total_turns = 0;
    for (uint32_t e = 0; e < pack.encounters; ++e) {
        C3BattleEncounterInfo info;
        CHECK(c3_battle_pack_info(&pack, e, &info));
        if (info.reason) { ++refused; continue; }
        ++admitted;
        CHECK(c3_general_battle_begin(&b, &pack, e, party, 0x1000u + e, error));
        unsigned turns = 0;
        C3GeneralOutcome o = play(&b, &pack, &turns);
        total_turns += turns;
        uint8_t copy[6][0x138]; C3GeneralRewards rw;
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
        printf("battle encounter=%lu trainer=%s/%s party=%u active=%u turns=%u outcome=%s result=%lu prize=%lu\n",
               (unsigned long)e, info.title, info.trainer, info.party_count, info.active_per_side, turns,
               o == C3_GB_WON ? "won" : o == C3_GB_LOST ? "lost" : "draw",
               (unsigned long)rw.result_code, (unsigned long)rw.prize);
        won += o == C3_GB_WON; lost += o == C3_GB_LOST; draw += o == C3_GB_DRAW;
    }
    printf("battle-general: view send_outs=%u hits_with_attacker=%u\n", send_outs, hits_with_actor);
    printf("battle-general: private sweep %u admitted (%u refused): %u won, %u lost, %u draw, %lu turns\n",
           admitted, refused, won, lost, draw, total_turns);
    c3_game_state_free(&game); c3_battle_pack_free(&pack);
}

int main(int argc, char **argv)
{
    synthetic();
    if (argc >= 4) private_sweep(argv[1], argv[2], argv[3]);
    else puts("battle-general: private sweep skipped (needs <battles.c3x> <newgame dir> <save.gci>)");
    return 0;
}
