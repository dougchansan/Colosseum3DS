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
/* The 3D battle's message loop (platform/3ds/battle_general_3d.c run)
 * without the stage: whenever the timeline is idle it takes the view's next
 * batch, steps 33 ms frames, and the message box shows log event first +
 * cursor. A held event is released (the player's A) once it has been held
 * for press_after frames. The command phase opens when the log runs out. */
typedef struct {
    uint32_t frames[C3_GB_EVENT_LIMIT]; /* frames each log event was the one on screen */
    uint32_t held, held_frames, released_at, commands_at, releases;
} Shown;
static void present_log(const C3GeneralBattle *b, uint32_t press_after, Shown *o)
{
    static C3BattlePreviewEvent batch[C3_PREVIEW_EVENT_LIMIT]; static C3BattlePresentation p; C3GeneralView v;
    uint32_t first = 0, count = 0, waited = 0; uint16_t hp[4];
    memset(&v, 0, sizeof v); memset(o, 0, sizeof *o); o->held = UINT32_MAX;
    c3_general_view_restart(&v, b); memcpy(hp, v.hp, sizeof hp); c3_battle_presentation_init(&p, hp);
    for (uint32_t frame = 1; frame < 100000; ++frame) {
        if (p.cursor >= p.count) {
            uint32_t n = c3_general_view_batch(&v, b, batch, C3_PREVIEW_EVENT_LIMIT, &first);
            if (!n) { o->commands_at = frame; return; }
            CHECK(c3_battle_presentation_submit(&p, batch, n)); count = n;
        }
        if (p.held) {
            if (o->held == UINT32_MAX) o->held = first + p.cursor;
            ++o->held_frames;
            if (++waited >= press_after) { c3_battle_presentation_release(&p); o->released_at = frame; ++o->releases; waited = 0; }
        }
        CHECK(c3_battle_presentation_step(&p, 33, NULL, NULL));
        uint32_t at = first + (p.cursor < count ? p.cursor : count - 1);
        CHECK(at < b->event_count); ++o->frames[at];
    }
    CHECK(!"the log never ended");
}
/* A lead Shadow Pokemon's notice closes the opening log; on screen it must
 * stay until the player presses A (here after 10 s), and the command phase
 * opens only after that press. */
static void check_notice_presented(const C3GeneralBattle *b, uint32_t notice, const char *text)
{
    Shown s; const uint32_t press = 300; /* 10 s of 33 ms frames: far longer than any timed event */
    CHECK(notice + 1 == b->event_count && b->events[notice].hold && b->events[notice].cutin && b->events[notice].special == 0x3A);
    CHECK(!strcmp(b->events[notice].text, text));
    present_log(b, press, &s);
    CHECK(s.held == notice && s.releases == 1 && s.held_frames == press);
    CHECK(s.frames[notice] >= press);           /* the notice is the message on screen the whole time */
    CHECK(s.released_at && s.commands_at > s.released_at);
    for (uint32_t i = 0; i < notice; ++i) CHECK(s.frames[i] > 0 && s.frames[i] < press); /* the rest are timed, each shown */
}
static void view_begin(const C3GeneralBattle *b)
{
    static const uint16_t hp[4];
    memset(&view, 0, sizeof view);
    c3_battle_presentation_init(&timeline, hp);
    check_view(b);
}

static void note_foes(const C3GeneralBattle *b);
static C3GeneralOutcome play(C3GeneralBattle *b, const C3BattlePack *p, unsigned *turns)
{
    view_begin(b); note_foes(b);
    while (b->outcome == C3_GB_RUNNING) {
        C3GeneralCommand c[2] = {{0}};
        for (unsigned a = 0; a < b->active_count; ++a)
            if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp) CHECK(c3_general_battle_choose(b, p, a, &c[a]));
        CHECK(c3_general_battle_turn(b, p, c, error));
        check_view(b); note_foes(b);
        ++*turns;
    }
    return b->outcome;
}

/* The prize line (fight_common message 0x7546) of the battle's last turn:
 * the message right after the outcome, or NULL when the turn has none. */
static const char *prize_line(const C3GeneralBattle *b)
{
    for (uint32_t i = 0; i < b->event_count; ++i) {
        if (strstr(b->events[i].text, " for winning!")) {
            CHECK(i > 0 && b->events[i - 1].kind == C3_GB_EVENT_OUTCOME && b->events[i].kind == C3_GB_EVENT_MESSAGE);
            CHECK(i + 1 == b->event_count);
            return b->events[i].text;
        }
    }
    return NULL;
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
    /* fn_8021E754: mult 2 (two Pokemon a side) x base 5 x best level 5 x
     * OkaneBai 1. */
    CHECK(b.okane_bai == 1 && rw.result_code == 2 && !rw.wipeout && rw.prize == 2u * 5u * 5u && !rw.premium);
    /* The won trainer battle ends with the prize line; without a hero name
     * (c3_battle_story_prepare sets it) the fight text's "Player" is used. */
    CHECK(prize_line(&b) && !strcmp(prize_line(&b), "Player got $50 for winning!"));
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
        /* The prize opcode pays the fight hero copy and prints its line
         * whatever backSaveDataFlag says (only heroBiosCopy depends on it). */
        CHECK(prize_line(&nb) && !strcmp(prize_line(&nb), "Player got $50 for winning!"));
        /* getOkane clear: no prize; getExp clear: no experience or effort. */
        k.bytes[k.entries_at + 88] = 1; k.bytes[k.entries_at + 97] = 0; k.bytes[k.entries_at + 96] = 0;
        CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &k, copy, &rw) && rw.written && !rw.prize && !rw.exp[0] && !rw.exp[1]);
        CHECK(g16(copy[0] + 0x98 + 10) == 0);
        CHECK(!prize_line(&nb)); /* no getOkane: the prize opcode skips the line */
        /* The hero's name and msgctrlMoney's thousands commas: 2 x base
         * 50000 x level 5, the same amount the writeback pays. */
        k.bytes[k.entries_at + 97] = 1; p16(k.bytes + k.entries_at + 180, 50000);
        CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error));
        strcpy(nb.side_name[0], "WES");
        CHECK(play(&nb, &k, &t) == C3_GB_WON);
        CHECK(prize_line(&nb) && !strcmp(prize_line(&nb), "WES got $500,000 for winning!"));
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&nb, &k, copy, &rw) && rw.prize == 500000u);
        p16(k.bytes + k.entries_at + 180, 5);
        {   /* OkaneBai 2: a hero Pokemon holding the Amulet Coin (item 189,
             * soubi 0x20) came out; one left in the party does nothing. */
            uint8_t coin[6][0x138]; memcpy(coin, party, sizeof coin);
            p16(coin[1] + 0x88, 189);
            CHECK(c3_general_battle_begin(&nb, &k, 0, coin, 7, error) && nb.okane_bai == 2);
            CHECK(play(&nb, &k, &t) == C3_GB_WON);
            CHECK(prize_line(&nb) && !strcmp(prize_line(&nb), "Player got $100 for winning!"));
            CHECK(c3_general_battle_writeback(&nb, &k, coin, &rw) && rw.prize == 100u);
            /* A single battle (one Pokemon a side): mult 1, and the holder in
             * the second slot never comes out against two level-5 foes. */
            memcpy(coin, party, sizeof coin); p16(coin[1] + 0x88, 189);
            k.bytes[k.entries_at + 122] = 1;
            CHECK(c3_general_battle_begin(&nb, &k, 0, coin, 7, error) && nb.active_count == 1);
            CHECK(play(&nb, &k, &t) == C3_GB_WON && nb.okane_bai == 1);
            CHECK(prize_line(&nb) && !strcmp(prize_line(&nb), "Player got $25 for winning!"));
            k.bytes[k.entries_at + 122] = 2;
            /* okanePool (the colosseum kinds): mult 4 x base 5 x level 5 goes
             * to the Premium pool, with no money and no prize line; the
             * Amulet Coin doubles it too. */
            k.bytes[k.entries_at + 98] = 1;
            CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
            memcpy(coin, party, sizeof coin);
            CHECK(c3_general_battle_writeback(&nb, &k, coin, &rw) && rw.written && !rw.prize && !prize_line(&nb));
            CHECK(rw.premium == 4u * 5u * 5u);
            memcpy(coin, party, sizeof coin); p16(coin[1] + 0x88, 189);
            CHECK(c3_general_battle_begin(&nb, &k, 0, coin, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
            CHECK(c3_general_battle_writeback(&nb, &k, coin, &rw) && rw.premium == 200u && !rw.prize);
            /* scriptAddPremium writes the save flag, not the fight hero copy:
             * backSaveDataFlag clear keeps the pool's share. */
            k.bytes[k.entries_at + 88] = 0;
            CHECK(c3_general_battle_begin(&nb, &k, 0, party, 7, error) && play(&nb, &k, &t) == C3_GB_WON);
            memcpy(coin, party, sizeof coin);
            CHECK(c3_general_battle_writeback(&nb, &k, coin, &rw) && !rw.written && rw.premium == 100u);
            k.bytes[k.entries_at + 88] = 1; k.bytes[k.entries_at + 98] = 0;
        }
        k.bytes[k.entries_at + 97] = 0;
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
    CHECK(!prize_line(&b)); /* a loss has no prize line */
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(rw.result_code == 3 && rw.wipeout && rw.prize == 0 && !rw.premium && g16(copy[0] + 0x8a) == 0);
    /* A finished battle refuses further turns; a mismatched party is refused. */
    C3GeneralCommand c[2] = {{0}};
    CHECK(!c3_general_battle_turn(&b, &pack, c, error));
    p16(copy[0], 1); CHECK(!c3_general_battle_writeback(&b, &pack, copy, &rw));
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-general: synthetic pack, win/loss, determinism and writeback ok");
}

/* Which opponent party members were ever sent out, from the log. */
static unsigned foes_seen, foe_order_len;
static char foe_order[C3_GB_PARTY + 1];
static void note_foes(const C3GeneralBattle *b)
{
    for (uint32_t i = 0; i < b->event_count; ++i)
        for (unsigned pos = 2; pos < 4; ++pos) {
            unsigned m = b->events[i].mon[pos];
            if (m == 255 || m >= C3_GB_PARTY || (foes_seen >> m & 1u)) continue;
            foes_seen |= 1u << m;
            if (foe_order_len < C3_GB_PARTY) { foe_order[foe_order_len++] = (char)('0' + m); foe_order[foe_order_len] = 0; }
        }
}

/* A trainer with more Pokemon than positions: a fainted foe is replaced by
 * the next one in reserve at the end of the turn (doubles and singles), the
 * battle goes on while any is left, and the Shadow Pokemon kept in reserve
 * comes out and can be snagged. */
static void reserve_replacement(void)
{
    for (unsigned per_side = 1; per_side <= 2; ++per_side) {
        size_t n; uint8_t *bytes = make_pack(2, 5, &n);
        uint8_t *r = bytes + g32(bytes + 124);             /* encounter 0 */
        r[2] = 3; r[122] = (uint8_t)per_side;
        for (unsigned k = 0; k < 3; ++k) {                  /* FOE x3, the Shadow one last */
            uint8_t *t = r + 192 + k * 80;
            memset(t, 0, 80); t[4] = 5; p16(t + 10, 2); t[3] = k == 2; p32(t + 0x34, 1);
        }
        c3_save_codec_sha1(bytes + 72, bytes + 128, n - 128);
        C3BattlePack pack = {0};
        CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
        uint8_t party[6][0x138]; make_party(party, 1, 50, 2, 0);
        static C3GeneralBattle b;
        CHECK(c3_general_battle_begin(&b, &pack, 0, party, 77, error));
        CHECK(b.party_count[1] == 3 && b.active_count == per_side && b.party[1][2].shadow == 1);
        CHECK(b.active[1][0] == 0 && (per_side == 1 || b.active[1][1] == 1));
        view_begin(&b);
        b.snag = 1; b.balls[1] = 1;
        unsigned turns = 0, third_turn = 0; int snagged = 0;
        while (b.outcome == C3_GB_RUNNING && turns < 40) {
            C3GeneralCommand c[2] = {{0}};
            for (unsigned a = 0; a < b.active_count; ++a) {
                if (b.active[0][a] == 255 || !b.party[0][b.active[0][a]].hp) continue;
                /* Snag the Shadow Pokemon once it is out, else TACKLE the
                 * first live foe. */
                int thrown = 0;
                for (unsigned t = 2; t < 2u + b.active_count && !thrown; ++t)
                    if (c3_general_battle_can_snag(&b, &pack, a, 1, t)) { c[a].kind = 2; c[a].slot = 1; c[a].target = (uint8_t)t; thrown = 1; }
                if (!thrown) {
                    c[a].kind = 0; c[a].slot = 0; c[a].target = 2;
                    for (unsigned t = 2; t < 2u + b.active_count; ++t)
                        if (b.active[1][t - 2] != 255 && b.party[1][b.active[1][t - 2]].hp) { c[a].target = (uint8_t)t; break; }
                }
            }
            CHECK(c3_general_battle_turn(&b, &pack, c, error));
            check_view(&b); ++turns;
            for (uint32_t i = 0; i < b.event_count; ++i) {
                if (strstr(b.events[i].text, "sent out FOE") && b.events[i].mon[b.events[i].actor] == 2) third_turn = turns;
                snagged |= strstr(b.events[i].text, "was snagged!") != NULL;
            }
            /* While a foe is left in reserve the side is never empty. */
            unsigned reserve = 0, active = 0;
            for (unsigned i = 0; i < 3; ++i) reserve += b.party[1][i].hp != 0;
            for (unsigned a = 0; a < b.active_count; ++a) active += b.active[1][a] != 255 && b.party[1][b.active[1][a]].hp;
            if (reserve) CHECK(b.outcome == C3_GB_RUNNING && active == (reserve < per_side ? reserve : per_side));
        }
        CHECK(third_turn && snagged && b.caught[2] == 1);
        CHECK(b.outcome == C3_GB_WON);
        C3GeneralRewards rw; uint8_t copy[6][0x138]; memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
        CHECK(rw.caught_count == 1 && rw.caught[0] == 2 && rw.balls_used[1] == 1 && rw.result_code == 2);
        c3_battle_pack_free(&pack); free(bytes);
        printf("battle-general: reserve replacement (%u a side): third foe out on turn %u, snagged, won in %u turns\n", per_side, third_turn, turns);
    }
}

/* fn_8022E6F0 mode 0: an opposing Shadow Pokemon's first appearance, the
 * partner's notice with the aura special, once per Pokemon, only with the
 * fight kind's doHizukiAiFlag (+15); encounter 0x11's snag tutorial. */
static unsigned find_text(const C3GeneralBattle *b, const char *text, uint32_t from)
{
    for (uint32_t i = from; i < b->event_count; ++i) if (!strcmp(b->events[i].text, text)) return i;
    return UINT32_MAX;
}
static void shadow_appearance(void)
{
    size_t n; uint8_t *bytes = make_pack(2, 5, &n);
    uint8_t *r = bytes + g32(bytes + 124);
    r[2] = 3;
    for (unsigned k = 0; k < 3; ++k) { uint8_t *t = r + 192 + k * 80; memset(t, 0, 80); t[4] = 5; p16(t + 10, 2); t[3] = k == 2; p32(t + 0x34, 1); }
    r[88 + 15] = 1;
    c3_save_codec_sha1(bytes + 72, bytes + 128, n - 128);
    C3BattlePack pack = {0}; CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    C3BattleEncounterInfo info; CHECK(c3_battle_pack_info(&pack, 0, &info) && info.partner_aura == 1);
    uint8_t party[6][0x138]; make_party(party, 1, 50, 2, 0);
    static C3GeneralBattle b;
    static const char aura[] = "RUI: Oh! WES!\nThere's a POK\xc3\xa9MON with a black aura!";
    static const char named[] = "RUI: Oh!\nThere's a SHADOW POK\xc3\xa9MON!";
    for (unsigned variant = 0; variant < 3; ++variant) {   /* 0 black aura (0x7717), 1 flag 0x9A0 (0x770A), 2 kind flag clear */
        pack.bytes[pack.entries_at + 88 + 15] = variant == 2 ? 0 : 1;
        CHECK(c3_general_battle_begin(&b, &pack, 0, party, 31, error));
        strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI"); b.shadow_named = variant == 1; b.balls[4] = 3;
        uint32_t before = b.event_count;
        c3_general_battle_appear_checks(&b); CHECK(b.event_count == before); /* the Shadow one waits in reserve */
        view_begin(&b);
        int noticed = 0;
        while (b.outcome == C3_GB_RUNNING) {
            C3GeneralCommand c[2] = {{0, 0, 2}, {0, 0, 2}};
            for (unsigned a = 0; a < 2; ++a) for (unsigned t = 3; t >= 2; --t) if (b.active[1][t - 2] != 255 && b.party[1][b.active[1][t - 2]].hp) c[a].target = (uint8_t)t;
            CHECK(c3_general_battle_turn(&b, &pack, c, error)); check_view(&b);
            uint32_t i = find_text(&b, variant == 1 ? named : aura, 0);
            if (variant == 2) { CHECK(i == UINT32_MAX && find_text(&b, named, 0) == UINT32_MAX); continue; }
            if (i == UINT32_MAX) continue;
            const C3GeneralEvent *e = &b.events[i];
            CHECK(!noticed && i > 0 && b.events[i - 1].kind == C3_GB_EVENT_SWITCH && strstr(b.events[i - 1].text, "sent out FOE"));
            CHECK(e->kind == C3_GB_EVENT_MESSAGE && e->special == 0x3A && e->cutin == 1 && e->actor >= 2 && b.events[i].mon[e->actor] == 2);
            CHECK(b.party[1][2].gauge_dropped == 1);
            /* A replacement's notice ends the turn's log: shown before the next command. */
            for (uint32_t j = i + 1; j < b.event_count; ++j) CHECK(b.events[j].kind == C3_GB_EVENT_MESSAGE || b.events[j].kind == C3_GB_EVENT_SWITCH);
            noticed = 1;
        }
        CHECK(variant == 2 ? !noticed : noticed);
    }
    pack.bytes[pack.entries_at + 88 + 15] = 1;
    {   /* A Shadow lead: noticed at the start (fightActionFlowKaisiPost), once. */
        uint8_t *t0 = pack.bytes + pack.entries_at + 192, *t2 = t0 + 2 * 80;
        t0[3] = 1; t2[3] = 0;
        CHECK(c3_general_battle_begin(&b, &pack, 0, party, 32, error));
        strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI"); b.balls[4] = 3;
        uint32_t before = b.event_count;
        c3_general_battle_appear_checks(&b);
        CHECK(b.event_count == before + 1 && !strcmp(b.events[before].text, aura) && b.events[before].special == 0x3A && b.events[before].actor == 2);
        c3_general_battle_appear_checks(&b); CHECK(b.event_count == before + 1);
        /* Order: the notice closes the opening log, after every send-out, so
         * both presentations (the 3D view's batches, the plain scene's event
         * walk) show it before the command phase they open once the log is
         * exhausted; the first turn's log does not repeat it. */
        CHECK(before + 1 == b.event_count && b.events[before - 1].kind == C3_GB_EVENT_SWITCH);
        {
            static C3BattlePreviewEvent out[C3_PREVIEW_EVENT_LIMIT]; uint32_t first, k, emitted = 0; C3GeneralView v; memset(&v, 0, sizeof v);
            while ((k = c3_general_view_batch(&v, &b, out, C3_PREVIEW_EVENT_LIMIT, &first))) emitted = first + k;
            CHECK(emitted == b.event_count); /* the command phase opens only here, after the notice */
            static C3GeneralBattle t; t = b;
            C3GeneralCommand c[2] = {{0, 0, 2}, {0, 0, 2}};
            CHECK(c3_general_battle_turn(&t, &pack, c, error));
            for (uint32_t i = 0; i < t.event_count; ++i) CHECK(!t.events[i].special && !t.events[i].cutin && strcmp(t.events[i].text, aura));
        }
        view_begin(&b);
        {   /* the 3D view: the aura is a battle special on the Pokemon (a status-style event) */
            static C3BattlePreviewEvent out[C3_PREVIEW_EVENT_LIMIT]; uint32_t first, k; C3GeneralView v; memset(&v, 0, sizeof v);
            int found = 0;
            while ((k = c3_general_view_batch(&v, &b, out, C3_PREVIEW_EVENT_LIMIT, &first)))
                for (uint32_t j = 0; j < k; ++j) if (first + j == before) { CHECK(out[j].kind == C3_PREVIEW_EVENT_STATUS && out[j].effect == 0x3A && out[j].target == 2 && out[j].cutin); found = 1; }
            CHECK(found);
        }
        check_notice_presented(&b, before, aura);
        /* Encounter 0x11: special 0xD9, the tutorial, POUND's attack sequence, and 0x772B without balls. */
        for (unsigned balls = 0; balls < 2; ++balls) {
            CHECK(c3_general_battle_begin(&b, &pack, 0, party, 33, error));
            b.encounter = 0x11; strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI"); b.balls[4] = (uint16_t)balls;
            before = b.event_count;
            c3_general_battle_appear_checks(&b);
            static const char *const pages[] = {
                "RUI: WES!\nIt's that one! That POK\xc3\xa9MON!",
                "RUI: Those jerks captured me because\nI saw that POK\xc3\xa9MON.",
                "RUI: I can see a black aura coming from it.\nThey must have done something to that", "POK\xc3\xa9MON!",
                "",
                "RUI: WES, be careful!\nThis POK\xc3\xa9MON isn't afraid to attack", "people!",
                "RUI: I know, WES!\nI hate to say this, but there's no other", "choice.",
                "RUI: Get that POK\xc3\xa9MON back from the\nbad guys!", "Please, you have to!",
                "RUI: You can...\nActually, only you can do it!",
                "RUI: ...Huh? Whaaaaat?!\nWhy don't you have any POK\xc3\xa9 BALLS", "with you?"};
            unsigned count = balls ? 12 : 14;
            CHECK(b.event_count == before + count);
            for (unsigned k = 0; k < count; ++k) CHECK(!strcmp(b.events[before + k].text, pages[k]) && b.events[before + k].actor == 2);
            CHECK(b.events[before].special == 0xD9 && b.events[before].cutin == 1);
            /* every page of the partner's waits for A; POUND's attack sequence does not */
            for (unsigned k = 0; k < count; ++k) CHECK(b.events[before + k].hold == (k != 4));
            CHECK(b.events[before + 4].waza_effect == 1 && b.events[before + 5].special == 0xD9);
            view_begin(&b); /* every event maps (the attack sequence is a move event without a target) */
        }
        t0[3] = 0; t2[3] = 1;
    }
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-general: Shadow appearance (0x7717, 0x770A, encounter 0x11 tutorial, kind flag, once) ok");
}

/* Encounter 525's shape: a CPU trainer on side 0 (pack v3 side-0 section).
 * A v2 pack refuses it (player side is not the hero); a v3 pack fights it
 * with the trainer AI on both sides and writes nothing back. */
static uint8_t *make_side0_pack(int version3, size_t *size)
{
    size_t n; uint8_t *base = make_pack(2, 5, &n);
    size_t extra = version3 ? 4 + 580 + 4 + 4 + 4 + 4 + 2 : 0; /* side-0 row, then empty AI, nature and filter sections */
    uint8_t *b = calloc(1, n + extra);
    memcpy(b, base, n); free(base);
    uint8_t *r = b + g32(b + 124);           /* encounter 0 */
    p16(r + 8 + 24, 780); p32(r + 8 + 28, 0); /* slot 0: CPU trainer 780 */
    if (version3) {
        uint8_t *s = b + n;
        p32(s, 1); s += 4;
        p16(s, 0); s[2] = 1;
        uint8_t *t = s + 68;                  /* one level-60 HERO, TACKLE */
        t[4] = 60; p16(t + 10, 1); p32(t + 0x34, 1);
        strcpy((char *)s + 548, "EAGUN"); strcpy((char *)s + 564, "ELDER");
        p32(b + 4, 3);
    }
    p32(b + 8, (uint32_t)(n + extra));
    c3_save_codec_sha1(b + 72, b + 128, n + extra - 128);
    *size = n + extra;
    return b;
}
static void side0_cpu(void)
{
    size_t n; uint8_t *bytes = make_side0_pack(0, &n);
    C3BattlePack pack = {0}; C3BattleEncounterInfo info;
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    CHECK(c3_battle_pack_info(&pack, 0, &info) && info.reason == 1 && !info.cpu_side0);
    c3_battle_pack_free(&pack); free(bytes);

    bytes = make_side0_pack(1, &n);
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error) && pack.side0_count == 1);
    CHECK(c3_battle_pack_info(&pack, 0, &info) && info.reason == 0 && info.cpu_side0 && info.side0_party_count == 1);
    CHECK(!strcmp(info.side0_trainer, "EAGUN") && !strcmp(info.side0_title, "ELDER"));
    {   /* a damaged side-0 row is refused */
        C3BattlePack bad = {0}; uint8_t *copy = malloc(n); CHECK(copy); memcpy(copy, bytes, n);
        copy[n - 18 - 580 + 2] = 7; c3_save_codec_sha1(copy + 72, copy + 128, n - 128);
        CHECK(!c3_battle_pack_decode(&bad, copy, n, error)); free(copy);
    }
    uint8_t party[6][0x138], copy[6][0x138];
    make_party(party, 2, 3, 1, 0);           /* the hero's own party is not used */
    static C3GeneralBattle b, again;
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 9, error));
    CHECK(b.party_count[0] == 1 && b.party[0][0].species == 1 && b.party[0][0].level == 60);
    CHECK(strstr(b.events[0].text, "EAGUN") && strstr(b.events[0].text, "are battling"));
    again = b;
    /* The hero's commands are ignored: Struggle at an empty slot would be invalid. */
    unsigned turns = 0;
    while (b.outcome == C3_GB_RUNNING) {
        C3GeneralCommand junk[2] = {{1, 5, 9}, {1, 5, 9}};
        CHECK(c3_general_battle_turn(&b, &pack, junk, error)); ++turns;
    }
    CHECK(b.outcome == C3_GB_WON && turns < 20);
    CHECK(strstr(b.events[b.event_count - 1].text, "EAGUN won"));
    CHECK(b.info.get_money && !prize_line(&b)); /* no hero on side 0: no prize line */
    unsigned t2 = 0; CHECK(play(&again, &pack, &t2) == C3_GB_WON && t2 == turns);
    C3GeneralRewards rw; memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(rw.result_code == 2 && !rw.written && !rw.wipeout && !rw.prize && !rw.exp[0]);
    CHECK(!memcmp(copy, party, sizeof copy));
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-general: side-0 CPU trainer (encounter 525 shape) ok");
}

/* Writeback invariants for one finished story battle against the
 * untouched party: nothing changes without backSaveDataFlag; otherwise
 * species and identity stay, HP/PP/levels/effort stay legal, experience
 * never drops and matches the level, and the result follows the outcome. */
static unsigned checked_writebacks, checked_premiums;
static void check_writeback(const C3BattlePack *pack, const C3GeneralBattle *b, const C3BattleEncounterInfo *info,
                            uint8_t before[6][0x138], uint8_t after[6][0x138], const C3GeneralRewards *rw)
{
    CHECK(rw->result_code == (b->outcome == C3_GB_WON ? 2u : 3u));
    {   /* The prize line: a won hero battle with getOkane (not okanePool),
         * and the amount recomputed as fn_8021E754 does: mult 2 with two
         * Pokemon a side, the opponent trainer kind's base, the highest
         * opponent level, and OkaneBai 2 only with an Amulet Coin holder. */
        unsigned best = 0, coin = 0;
        for (unsigned i = 0; i < b->party_count[1]; ++i) if (b->party[1][i].level > best) best = b->party[1][i].level;
        for (unsigned i = 0; i < b->party_count[0]; ++i) coin |= b->party[0][i].held_item == 189;
        CHECK(b->okane_bai == 1 || (b->okane_bai == 2 && coin));
        unsigned long expected = (info->active_per_side > 1 ? 2ul : 1ul) * info->prize_base * best * b->okane_bai;
        if (b->outcome == C3_GB_WON && info->get_money && !info->money_pool && info->back_save && !info->cpu_side0)
            CHECK(rw->prize == expected);
        /* okanePool: mult 4 into the Premium pool, whatever backSave says. */
        if (b->outcome == C3_GB_WON && info->get_money && info->money_pool && !info->cpu_side0) {
            CHECK(rw->premium == 4ul * info->prize_base * best * b->okane_bai && !rw->prize); ++checked_premiums;
        } else CHECK(!rw->premium);
        const char *line = prize_line(b);
        if (b->outcome != C3_GB_WON || !info->get_money || info->money_pool || info->cpu_side0) CHECK(!line);
        else if (b->event_count < C3_GB_EVENT_LIMIT) {
            CHECK(line && strchr(line, '$'));
            unsigned long amount = 0;
            for (const char *c = strchr(line, '$') + 1; (*c >= '0' && *c <= '9') || *c == ','; ++c)
                if (*c != ',') amount = amount * 10u + (unsigned long)(*c - '0');
            CHECK(!rw->written || amount == rw->prize);
        }
    }
    if (info->cpu_side0) { /* the hero only watched: no whiteout, nothing written */
        CHECK(!rw->wipeout && !rw->written && !rw->prize && !memcmp(before, after, 6 * 0x138)); return; }
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

/* Encounter 17 (Phenac mayor's house, MIROR B.PEON TRUDLY): DUSKULL and
 * SPINARAK lead, the Shadow MAKUHITA (Shadow ID 1) waits in reserve. It must
 * come out when one falls, and one Poke Ball snags it (fn_80213A78 treats
 * encounter 0x11 as a sure catch). A version-1 pack has no Shadow table, so
 * nothing can be snagged with it: reported, not failed. */
static void encounter17(const C3BattlePack *pack, uint8_t party[6][0x138])
{
    C3BattleEncounterInfo info; C3GeneralMon m; C3ShadowInfo sh;
    if (!c3_battle_pack_info(pack, 17, &info) || strcmp(info.trainer, "TRUDLY")) { puts("battle-general: encounter 17 is not TRUDLY's; skipped"); return; }
    CHECK(info.party_count == 3 && info.active_per_side == 2 && info.shadow_count == 1);
    CHECK(c3_general_mon_from_trainer(pack, 17, 2, &m) && m.shadow == 1 && !strcmp(m.name, "MAKUHITA") && m.level == 30);
    int table = c3_battle_pack_shadow(pack, m.shadow, &sh);
    if (table) CHECK(sh.species == m.species && sh.snag_flag == 0x84e);
    unsigned won = 0, tutorials = 0;
    for (uint32_t seed = 0; seed < 32; ++seed) {
        static C3GeneralBattle b;
        CHECK(c3_general_battle_begin(&b, pack, 17, party, 0x1700u + seed, error));
        CHECK(b.party_count[1] == 3 && b.active[1][0] == 0 && b.active[1][1] == 1 && b.party[1][2].shadow == 1);
        b.snag = 1; b.balls[4] = 5; strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI");
        uint32_t opening = b.event_count; c3_general_battle_appear_checks(&b); CHECK(b.event_count == opening);
        int out = 0;
        while (b.outcome == C3_GB_RUNNING) {
            C3GeneralCommand c[2] = {{0}}; int thrown = 0;
            for (unsigned a = 0; a < b.active_count; ++a) {
                if (b.active[0][a] == 255 || !b.party[0][b.active[0][a]].hp) continue;
                for (unsigned t = 2; t < 4 && !thrown; ++t)
                    if (c3_general_battle_can_snag(&b, pack, a, 4, t)) { c[a].kind = 2; c[a].slot = 4; c[a].target = (uint8_t)t; thrown = 1; }
                if (c[a].kind != 2) CHECK(c3_general_battle_choose(&b, pack, a, &c[a]));
            }
            CHECK(c3_general_battle_turn(&b, pack, c, error));
            for (uint32_t i = 0; i < b.event_count; ++i)
                if (b.events[i].kind == C3_GB_EVENT_SWITCH && b.events[i].actor >= 2 && b.events[i].actor < 4 && b.events[i].mon[b.events[i].actor] == 2) {
                    CHECK(strstr(b.events[i].text, "TRUDLY sent out MAKUHITA!")); out = 1;
                    /* fn_8022E6F0 for encounter 0x11: Rui's snag tutorial with the aura (0xD9). */
                    if (info.partner_aura) {
                        CHECK(i + 1 < b.event_count && b.events[i + 1].special == 0xD9 && b.events[i + 1].cutin);
                        CHECK(!strcmp(b.events[i + 1].text, "RUI: WES!\nIt's that one! That POK\xc3\xa9MON!"));
                        tutorials++;
                        /* the tutorial plays before the next command: nothing but messages follow in this turn */
                        for (uint32_t j = i + 1; j < b.event_count; ++j) CHECK(b.events[j].kind == C3_GB_EVENT_MESSAGE);
                    }
                }
        }
        if (b.outcome != C3_GB_WON) continue; /* the save's party lost before Makuhita was needed */
        ++won; CHECK(out);
        C3GeneralRewards rw; uint8_t copy[6][0x138]; memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, pack, copy, &rw));
        if (table) CHECK(rw.caught_count == 1 && rw.caught[0] == 2 && rw.balls_used[4] == 1 && b.caught[2] == 4);
        else CHECK(rw.caught_count == 0 && rw.balls_used[4] == 0);
    }
    CHECK(won && (!info.partner_aura || tutorials >= won));
    printf("battle-general: encounter 17: MAKUHITA (Shadow 1) came out from reserve in all %u won battles; %s; partner tutorial %u times (kind flag %u)\n", won,
           table ? "one Poke Ball snagged it each time" : "WARNING: version-1 pack without the Shadow table, it cannot be snagged", tutorials, info.partner_aura);
    {   /* Encounter 33 (MYSTERY TROOP BLUNO): the Shadow CROCONAW leads, so the
         * notice (0x7717, aura 0x3A) comes at the start, at position 2. */
        static C3GeneralBattle b;
        if (c3_battle_pack_info(pack, 33, &info) && !strcmp(info.trainer, "BLUNO")) {
            CHECK(c3_general_battle_begin(&b, pack, 33, party, 0x3300u, error) && b.party[1][0].shadow);
            strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI");
            uint32_t before = b.event_count; c3_general_battle_appear_checks(&b);
            if (info.partner_aura) CHECK(b.event_count == before + 1 && b.events[before].special == 0x3A && b.events[before].actor == 2 &&
                                         !strcmp(b.events[before].text, "RUI: Oh! WES!\nThere's a POK\xc3\xa9MON with a black aura!"));
            else CHECK(b.event_count == before);
            printf("battle-general: encounter 33: CROCONAW lead noticed at the start: %s (kind flag %u)\n", b.event_count > before ? "yes" : "no", info.partner_aura);
        }
    }
    {   /* Encounter 101 (CIPHER PEON NORE): the Shadow YANMA leads; its notice
         * closes the opening log and the presentation holds it until A. */
        static C3GeneralBattle b;
        if (c3_battle_pack_info(pack, 101, &info) && !strcmp(info.trainer, "NORE")) {
            CHECK(c3_general_battle_begin(&b, pack, 101, party, 0x10100u, error) && b.party[1][0].shadow && !strcmp(b.party[1][0].name, "YANMA"));
            strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI");
            uint32_t before = b.event_count; c3_general_battle_appear_checks(&b);
            CHECK(info.partner_aura == 1 && b.event_count == before + 1);
            check_notice_presented(&b, before, "RUI: Oh! WES!\nThere's a POK\xc3\xa9MON with a black aura!");
            puts("battle-general: encounter 101: YANMA lead noticed at the start, held on screen until A");
        } else puts("battle-general: encounter 101 is not NORE's; skipped");
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
    unsigned admitted = 0, won = 0, lost = 0, draw = 0, refused = 0, reserve_battles = 0, reserve_out = 0;
    unsigned long total_turns = 0;
    for (uint32_t e = 0; e < pack.encounters; ++e) {
        C3BattleEncounterInfo info;
        CHECK(c3_battle_pack_info(&pack, e, &info));
        if (info.reason) { ++refused; continue; }
        ++admitted;
        CHECK(c3_general_battle_begin(&b, &pack, e, party, 0x1000u + e, error));
        strcpy(b.side_name[0], "WES"); strcpy(b.partner_name, "RUI"); c3_general_battle_appear_checks(&b);
        unsigned turns = 0;
        foes_seen = 0; foe_order_len = 0;
        C3GeneralOutcome o = play(&b, &pack, &turns);
        /* A won battle saw every opponent Pokemon come out. */
        unsigned foe_out = 0;
        for (unsigned i = 0; i < b.party_count[1]; ++i) foe_out += foes_seen >> i & 1u;
        if (o == C3_GB_WON && !info.cpu_side0) CHECK(foe_out == b.party_count[1]);
        if (b.party_count[1] > b.active_count) { ++reserve_battles; reserve_out += foe_out == b.party_count[1]; }
        total_turns += turns;
        uint8_t copy[6][0x138]; C3GeneralRewards rw;
        memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
        check_writeback(&pack, &b, &info, party, copy, &rw);
        unsigned levels = 0, learned = 0, skipped = 0; unsigned long exp = 0;
        for (unsigned i = 0; i < 6; ++i) { levels += rw.levels_gained[i]; learned += rw.learned_count[i]; skipped += rw.skipped_count[i]; exp += rw.exp[i]; }
        printf("battle encounter=%lu trainer=%s/%s kind=%u party=%u active=%u foe_out=%u order=%s turns=%u outcome=%s result=%lu prize=%lu premium=%lu written=%d exp=%lu levels=%u learned=%u skipped=%u wipeout=%d\n",
               (unsigned long)e, info.title, info.trainer, info.kind, info.party_count, info.active_per_side, foe_out, foe_order, turns,
               o == C3_GB_WON ? "won" : o == C3_GB_LOST ? "lost" : "draw",
               (unsigned long)rw.result_code, (unsigned long)rw.prize, (unsigned long)rw.premium, rw.written, exp, levels, learned, skipped, rw.wipeout);
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
    printf("battle-general: %u battles with Pokemon in reserve, %u saw every opponent Pokemon\n", reserve_battles, reserve_out);
    encounter17(&pack, party);
    printf("battle-general: private sweep %u admitted (%u refused): %u won, %u lost, %u draw, %lu turns, %u writebacks checked (%u Premium pool shares)\n",
           admitted, refused, won, lost, draw, total_turns, checked_writebacks, checked_premiums);
    c3_game_state_free(&game); c3_battle_pack_free(&pack);
}

int main(int argc, char **argv)
{
    synthetic();
    reserve_replacement();
    shadow_appearance();
    side0_cpu();
    if (argc >= 4) private_sweep(argv[1], argv[2], argv[3]);
    else puts("battle-general: private sweep skipped (needs <battles.c3x> <newgame dir> <save.gci>)");
    return 0;
}
