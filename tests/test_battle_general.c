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

#include "battle_pack_fixture.h"
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
