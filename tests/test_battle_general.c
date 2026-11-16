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
    CHECK(rw.written);
    /* Effort: 2 Speed per foe faced, written to +0x98 (Speed is the sixth). */
    CHECK(b.party[0][0].ev_gain[5] + b.party[0][1].ev_gain[5] >= 4 && b.party[0][0].ev_gain[5] <= 4);
    for (unsigned i = 0; i < 2; ++i) CHECK(g16(copy[i] + 0x98 + 10) == b.party[0][i].ev_gain[5]);
    {   /* backSaveDataFlag clear: result only, the party is untouched. */
        C3BattlePack k = pack; k.bytes = malloc(pack.size); CHECK(k.bytes);
        memcpy(k.bytes, pack.bytes, pack.size); k.bytes[k.entries_at + 88] = 0;
        static C3GeneralBattle nb; unsigned t = 0;
        CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &k, copy, &rw) && rw.result_code == 2 && !rw.written && !rw.prize);
        CHECK(!memcmp(copy, party, sizeof copy));
        /* getOkane clear: no prize; getExp clear: no experience or effort. */
        k.bytes[k.entries_at + 88] = 1; k.bytes[k.entries_at + 97] = 0; k.bytes[k.entries_at + 96] = 0;
        CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &k, copy, &rw) && rw.written && !rw.prize && !rw.exp[0] && !rw.exp[1]);
        CHECK(g16(copy[0] + 0x98 + 10) == 0);
        /* doCriticalAttackFlag clear: no critical hit in many battles. */
        k.bytes[k.entries_at + 102] = 0;
        for (unsigned seed = 0; seed < 40; ++seed) {
            CHECK(c3_general_battle_begin(&nb, &k, 0, party, seed, error));
            while (nb.outcome == C3_GB_RUNNING) {
                C3GeneralCommand c[2] = {{0}};
                for (unsigned a = 0; a < nb.active_count; ++a)
                    if (nb.active[0][a] != 255 && nb.party[0][nb.active[0][a]].hp) CHECK(c3_general_battle_choose(&nb, &k, a, &c[a]));
                CHECK(c3_general_battle_turn(&nb, &k, c, error));
                for (unsigned e = 0; e < nb.event_count; ++e) CHECK(!strstr(nb.events[e].text, "critical"));
            }
        }
        free(k.bytes);
    }
    {   /* A persistent burn enters the battle and is written back. */
        uint8_t burned[6][0x138]; memcpy(burned, party, sizeof burned); p16(burned[0] + 0x64, 6);
        static C3GeneralBattle nb; unsigned t = 0;
        CHECK(c3_general_battle_begin(&nb, &pack, 0, burned, 7, error) && nb.party[0][0].status == C3_GB_BURN);
        CHECK(play(&nb, &pack, &t) == C3_GB_WON);
        memcpy(copy, burned, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &pack, copy, &rw));
        CHECK(g16(copy[0] + 0x64) == (nb.party[0][0].hp ? 6u : 0u));
    }

    {   /* Snagging: no Snag Machine or no ball, no throw; a Master Ball snags
         * the Shadow FOE (opponent 0), which then gives no experience. */
        static C3GeneralBattle nb; C3GeneralCommand c[2] = {{0}}; C3ShadowInfo sh;
        CHECK(c3_battle_pack_shadow(&pack, 1, &sh) && sh.catch_rate == 45 && sh.gauge == 30 && sh.snag_flag == 0x84e);
        CHECK(c3_general_battle_begin(&nb, &pack, 0, party, 21, error));
        CHECK(!c3_general_battle_can_snag(&nb, &pack, 0, 1, 2));
        nb.snag = 1; CHECK(!c3_general_battle_can_snag(&nb, &pack, 0, 1, 2));
        nb.balls[1] = 1; nb.balls[4] = 3;
        CHECK(c3_general_battle_can_snag(&nb, &pack, 0, 1, 2) && !c3_general_battle_can_snag(&nb, &pack, 0, 1, 0));
        CHECK(!c3_general_battle_can_snag(&nb, &pack, 0, 1, 3)); /* opponent 1 is not Shadow */
        c[0].kind = 2; c[0].slot = 1; c[0].target = 2;
        CHECK(c3_general_battle_choose(&nb, &pack, 1, &c[1]));
        CHECK(c3_general_battle_turn(&nb, &pack, c, error));
        CHECK(nb.caught[0] == 1 && nb.party[1][0].hp == 0 && nb.balls[1] == 0 && nb.balls_used[1] == 1);
        int gotcha = 0; for (unsigned e = 0; e < nb.event_count; ++e) gotcha |= strstr(nb.events[e].text, "Gotcha!") != NULL;
        CHECK(gotcha);
        unsigned t = 0; CHECK(play(&nb, &pack, &t) == C3_GB_WON);
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &pack, copy, &rw) && rw.caught_count == 1 && rw.caught[0] == 0 && rw.balls_used[1] == 1);
        /* Only opponent 1 (level 5, 50 base EXP) paid: 35 at most per hero. */
        CHECK(rw.exp[0] + rw.exp[1] <= 2u * (50u * 5u / 7u * 3u / 2u));
        C3SnagOwner owner; memset(&owner, 0, sizeof owner); owner.name[1] = 'W'; owner.trainer_id = 0x12345678u; owner.floor = 8; memcpy(owner.attest, "ATST", 4);
        uint8_t rec[0x138];
        CHECK(!c3_general_snag_record(&pack, &nb, 1, &owner, 30, 9, rec));
        CHECK(c3_general_snag_record(&pack, &nb, 0, &owner, 30, 9, rec));
        CHECK(g16(rec) == 2 && rec[0x60] == 5 && rec[0x0f] == 1 && g16(rec + 0xd8) == 1 && g32(rec + 0xdc) == 3000u);
        CHECK(g32(rec + 0x14) == 0x12345678u && g16(rec + 0x0c) == 8 && !memcmp(rec + 8, "ATST", 4) && rec[0x19] == 'W');
        CHECK(g32(rec + 4) % 25u == nb.party[1][0].nature && g16(rec + 0x8a) == g16(rec + 0x8c) && g16(rec + 0x78) == 1);
        CHECK(g16(rec + 0x2e) == 'F' && g16(rec + 0x30) == 'O' && g16(rec + 0x32) == 'E' && !g16(rec + 0x34));
        /* Poke Balls against a full-HP rate-45 Shadow: shakes follow the
         * original formula; the throw always uses the ball up. */
        unsigned caught = 0, thrown = 0;
        for (uint32_t seed = 0; seed < 200; ++seed) {
            CHECK(c3_general_battle_begin(&nb, &pack, 0, party, seed, error));
            nb.snag = 1; nb.balls[4] = 1;
            C3GeneralCommand d[2] = {{2, 4, 2}, {0}};
            CHECK(c3_general_battle_choose(&nb, &pack, 1, &d[1]));
            CHECK(c3_general_battle_turn(&nb, &pack, d, error));
            ++thrown; caught += nb.caught[0] == 4;
            CHECK(nb.balls[4] == 0 && nb.balls_used[4] == 1);
        }
        /* odds = 45*10/10*(3M-2M)/(3M) = 15: 0xFFFF0/sqrt(sqrt(0xFF0000/15)) = 0xFFFF0/32 = 32767.5 -> 32767,
         * so each shake passes about half the time: four in a row ~ 1/16. */
        CHECK(caught > 0 && caught < 40);
        printf("battle-general: snag synthetic %u of %u Poke Balls caught\n", caught, thrown);
    }
    /* Level up: a level-5 hero with almost enough experience. */
    make_party(party, 1, 5, 1, 0); p32(party[0] + 0x5c, 215); /* 6^3 = 216 */
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 3, error));
    turns = 0; C3GeneralOutcome o = play(&b, &pack, &turns);
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    if (rw.exp[0]) {
        CHECK(copy[0][0x60] > 5 && rw.levels_gained[0] == copy[0][0x60] - 5);
        CHECK(g16(copy[0] + 0x8c) > g16(party[0] + 0x8c));
        /* Level 6 teaches GROWL into the free third slot with full PP. */
        CHECK(rw.learned_count[0] == 1 && rw.learned[0][0] == 2 && !rw.skipped_count[0]);
        CHECK(g16(copy[0] + 0x80) == 2 && copy[0][0x82] == 40);
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

/* Writeback invariants for one finished story battle against the
 * untouched party: nothing changes without backSaveDataFlag; otherwise
 * species and identity stay, HP/PP/levels/effort stay legal, experience
 * never drops and matches the level, and the result follows the outcome. */
static unsigned checked_writebacks;
static void check_writeback(const C3BattlePack *pack, const C3GeneralBattle *b, const C3BattleEncounterInfo *info,
                            uint8_t before[6][0x138], uint8_t after[6][0x138], const C3GeneralRewards *rw)
{
    CHECK(rw->result_code == (b->outcome == C3_GB_WON ? 2u : 3u));
    CHECK(rw->wipeout == (b->outcome != C3_GB_WON && info->wipeout));
    if (!info->back_save) { CHECK(!rw->written && !memcmp(before, after, 6 * 0x138)); return; }
    CHECK(rw->written);
    CHECK(info->get_money || !rw->prize);
    CHECK(b->outcome == C3_GB_WON || !rw->prize);
    for (unsigned i = 0; i < 6; ++i) {
        const uint8_t *x = before[i], *y = after[i];
        CHECK(g16(x) == g16(y) && !memcmp(x + 4, y + 4, 0x58));
        if (!g16(x)) { CHECK(!memcmp(x, y, 0x138)); continue; }
        CHECK(g16(y + 0x8a) <= g16(y + 0x8c));
        CHECK(g32(y + 0x5c) >= g32(x + 0x5c) && y[0x60] >= x[0x60] && y[0x60] <= 100);
        CHECK(info->get_exp || g32(y + 0x5c) == g32(x + 0x5c));
        unsigned total = 0;
        for (unsigned j = 0; j < 6; ++j) { unsigned ev = g16(y + 0x98 + 2 * j); CHECK(ev <= 255 && ev >= g16(x + 0x98 + 2 * j)); total += ev; }
        CHECK(total <= 510 || total == 0);
        unsigned status = g16(y + 0x64); CHECK(status == 0 || (status >= 3 && status <= 8));
        if (!g16(y + 0x8a)) CHECK(status == 0);
        for (unsigned j = 0; j < 4; ++j) if (g16(y + 0x78 + 4 * j)) {
            C3GeneralMoveInfo mv; CHECK(c3_battle_pack_move(pack, g16(y + 0x78 + 4 * j), &mv));
            CHECK(y[0x7a + 4 * j] <= mv.pp * (5u + (y[0x7b + 4 * j] > 3 ? 3u : y[0x7b + 4 * j])) / 5u);
        }
    }
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
        check_writeback(&pack, &b, &info, party, copy, &rw);
        unsigned levels = 0, learned = 0, skipped = 0; unsigned long exp = 0;
        for (unsigned i = 0; i < 6; ++i) { levels += rw.levels_gained[i]; learned += rw.learned_count[i]; skipped += rw.skipped_count[i]; exp += rw.exp[i]; }
        printf("battle encounter=%lu trainer=%s/%s kind=%u party=%u active=%u turns=%u outcome=%s result=%lu prize=%lu written=%d exp=%lu levels=%u learned=%u skipped=%u wipeout=%d\n",
               (unsigned long)e, info.title, info.trainer, info.kind, info.party_count, info.active_per_side, turns,
               o == C3_GB_WON ? "won" : o == C3_GB_LOST ? "lost" : "draw",
               (unsigned long)rw.result_code, (unsigned long)rw.prize, rw.written, exp, levels, learned, skipped, rw.wipeout);
        ++checked_writebacks;
        won += o == C3_GB_WON; lost += o == C3_GB_LOST; draw += o == C3_GB_DRAW;
    }
    /* Snag pass: every encounter with a Shadow Pokemon again, the hero
     * throwing Ultra Balls at it whenever it may; each snag becomes a record
     * and is received into the save (party, else PC box). */
    unsigned shadow_battles = 0, snagged = 0, stored_party = 0, stored_box = 0, full = 0, shadow_total = 0;
    for (uint32_t e = 0; e < pack.encounters; ++e) {
        C3BattleEncounterInfo info;
        CHECK(c3_battle_pack_info(&pack, e, &info));
        if (info.reason || !info.shadow_count) continue;
        ++shadow_battles; shadow_total += info.shadow_count;
        CHECK(c3_general_battle_begin(&b, &pack, e, party, 0x2000u + e, error));
        b.snag = 1; b.balls[2] = 99;
        while (b.outcome == C3_GB_RUNNING) {
            C3GeneralCommand c[2] = {{0}};
            for (unsigned a = 0; a < b.active_count; ++a) {
                if (b.active[0][a] == 255 || !b.party[0][b.active[0][a]].hp) continue;
                int thrown = 0;
                for (unsigned t = 2; t < 2u + b.active_count && !thrown; ++t)
                    if (c3_general_battle_can_snag(&b, &pack, a, 2, t)) { c[a].kind = 2; c[a].slot = 2; c[a].target = (uint8_t)t; thrown = 1; }
                if (!thrown) CHECK(c3_general_battle_choose(&b, &pack, a, &c[a]));
            }
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
        }
        uint8_t copy[6][0x138]; C3GeneralRewards rw;
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
        check_writeback(&pack, &b, &info, party, copy, &rw);
        C3SnagOwner owner; memset(&owner, 0, sizeof owner); owner.trainer_id = 1; owner.floor = info.floor_data_id;
        for (unsigned k = 0; k < rw.caught_count; ++k) {
            uint8_t rec[0x138]; unsigned where = 0; C3ShadowInfo sh;
            const C3GeneralMon *m = &b.party[1][rw.caught[k]];
            CHECK(c3_battle_pack_shadow(&pack, m->shadow, &sh) && sh.species == m->species);
            CHECK(c3_general_snag_record(&pack, &b, rw.caught[k], &owner, sh.gauge, e, rec));
            CHECK(g16(rec) == m->species && g16(rec + 0xd8) == m->shadow && g32(rec + 0xdc) == sh.gauge * 100u);
            CHECK(g16(rec + 0x8a) == g16(rec + 0x8c) && rec[0x60] == m->level);
            ++snagged;
            if (!c3_game_state_receive_pokemon(&game, rec, rec, &where)) { ++full; continue; }
            if (where < 6) ++stored_party; else ++stored_box;
            printf("snag encounter=%lu species=%u shadow=%u level=%u rate=%u gauge=%u stored=%u\n", (unsigned long)e,
                   m->species, m->shadow, m->level, sh.catch_rate, sh.gauge, where);
        }
    }
    printf("battle-general: snag sweep %u Shadow battles (%u Shadow Pokemon): %u snagged, %u to the party, %u to boxes, %u with no room\n",
           shadow_battles, shadow_total, snagged, stored_party, stored_box, full);
    printf("battle-general: view send_outs=%u hits_with_attacker=%u\n", send_outs, hits_with_actor);
    printf("battle-general: private sweep %u admitted (%u refused): %u won, %u lost, %u draw, %lu turns, %u writebacks checked\n",
           admitted, refused, won, lost, draw, total_turns, checked_writebacks);
    c3_game_state_free(&game); c3_battle_pack_free(&pack);
}

int main(int argc, char **argv)
{
    synthetic();
    if (argc >= 4) private_sweep(argv[1], argv[2], argv[3]);
    else puts("battle-general: private sweep skipped (needs <battles.c3x> <newgame dir> <save.gci>)");
    return 0;
}
