#define C3_FIXTURE_MOVES 360
#include "colosseum3ds/battle_general.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shadow Pokemon in battle (src/battle_general.c, "Hyper Mode"): entry on
 * Shadow Rush (fn_8022EDEC), misbehaviour (fn_8022A504), the Shadow Rush
 * critical rule, the end-of-turn reminder, Call (fn_80212D6C), heart gauge
 * drops (pokemonAddDpFormPokemonDpFilterId filters 0 and 2) and experience
 * banking (fn_802232F4), on a synthetic version-3 pack whose nature rows
 * force each outcome. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

enum { SHADOW_RUSH = 0x164, NATURES = 25 };
/* Natures: 0 enters at every level and obeys; 1 uses another move; 2 ignores
 * the order; 3 hits itself; 4 never enters; 5 Call always fails. Call works
 * for 0..4. */
static uint8_t *make_hyper_pack(size_t *size)
{
    size_t n; uint8_t *base = make_pack(2, 5, &n);
    size_t extra = 4 + 4 + 4 + 4 + NATURES * 40 + 4 + 6 + 2;
    uint8_t *b = calloc(1, n + extra);
    memcpy(b, base, n); free(base);
    uint8_t *m = b + 128 + SHADOW_RUSH * 56;   /* Shadow Rush: typeless 9, 90 power */
    m[1] = 5; m[2] = 9; m[3] = 0; m[4] = 100; p16(m + 22, 90);
    uint8_t *s = b + n;
    p32(s, 0); s += 4; p32(s, 0); s += 4; p32(s, 0); s += 4;   /* no side-0, AI rows or codes */
    p32(s, NATURES); s += 4;
    for (unsigned k = 0; k < NATURES; ++k) {
        uint8_t *row = s + k * 40;
        for (unsigned l = 0; l < 7; ++l) { row[0x18 + l] = k == 4 ? 0 : 100; row[0x1F + l] = k == 5 ? 0 : 100; }
        if (k == 1) row[0x0B] = 100;
        if (k == 2) row[0x0C] = 100;
        if (k == 3) row[0x0E] = 100;
    }
    s += NATURES * 40;
    p32(s, 6); s += 4;
    s[0] = 0xff; s[1] = 0xff; s[2] = 0xfd; s[3] = 0xfd; s[4] = 0xff; s[5] = 0xff;   /* battle -1, call -3 */
    p32(b + 4, 3); p32(b + 8, (uint32_t)(n + extra));
    c3_save_codec_sha1(b + 72, b + 128, n + extra - 128);
    *size = n + extra;
    return b;
}

static C3BattlePack pack;
static uint8_t party[6][0x138];
/* One level-50 Shadow HERO (Shadow ID 1, full gauge 30 -> 3000) knowing
 * TACKLE and the Fire hit, against two level-5 foes. As in the original its
 * slot 0 shows Shadow Rush and the Fire hit (slot 1) shows "????" until the
 * gauge is under 80% (include/colosseum3ds/shadow_moves.h): OPEN (2200,
 * 2100 = 70% after the send-out drop) opens it. */
enum { OPEN = 2200 };
static void hero(unsigned nature, int32_t dp)
{
    make_party(party, 1, 50, 1, 1);
    p32(party[0] + 4, nature); p32(party[0] + 0xdc, (uint32_t)dp);
}
static int has(const C3GeneralBattle *b, const char *text)
{ for (uint32_t i = 0; i < b->event_count; ++i) if (strstr(b->events[i].text, text)) return 1; return 0; }
static unsigned moves_by(const C3GeneralBattle *b, unsigned actor)
{ unsigned n = 0; for (uint32_t i = 0; i < b->event_count; ++i) n += b->events[i].kind == C3_GB_EVENT_MOVE && b->events[i].actor == actor; return n; }
static void turn(C3GeneralBattle *b, unsigned kind, unsigned slot)
{
    C3GeneralCommand c[2] = {{(uint8_t)kind, (uint8_t)slot, kind == 3 ? 0 : 2}, {0}};
    CHECK(c3_general_battle_turn(b, &pack, c, error));
}

int main(void)
{
    size_t n; uint8_t *bytes = make_hyper_pack(&n);
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error) && pack.nature_count == NATURES && pack.dp_filter_count == 6);
    static C3GeneralBattle b;

    /* Dark levels from the gauge. */
    {   C3GeneralMon m; memset(&m, 0, sizeof m); m.shadow = 1; m.dp_max = 3000;
        const int32_t dp[] = {3000, 2400, 2399, 1800, 1200, 600, 1, 0, -100};
        const unsigned lv[] = {0, 1, 2, 2, 3, 4, 5, 6, 6};
        for (unsigned i = 0; i < 9; ++i) { m.dp = dp[i]; CHECK(c3_general_dark_level(&m) == lv[i]); }
        m.shadow = 0; CHECK(c3_general_dark_level(&m) == 7);
    }
    /* Sent out: filter 0 (-1 DP) once. */
    hero(0, 3000);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 1, error));
    CHECK(b.party[0][0].dp_max == 3000 && b.party[0][0].dp == 2900 && b.party[0][0].gauge_dropped);
    for (unsigned i = 0; i < b.party_count[1]; ++i) b.party[1][i].hp = b.party[1][i].max_hp = 999;  /* nobody faints */
    CHECK(b.party[0][0].moves[0] == SHADOW_RUSH && b.party[0][0].moves[1] == 0x165 && b.party[0][0].own_moves[0] == 1);
    /* The Fire hit never enters; Shadow Rush does (nature 0: 100 at every level). */
    hero(0, OPEN);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 1, error));
    for (unsigned i = 0; i < b.party_count[1]; ++i) b.party[1][i].hp = b.party[1][i].max_hp = 999;
    CHECK(b.party[0][0].dp == 2100 && b.party[0][0].moves[1] == 3);
    turn(&b, 0, 1);
    CHECK(!b.party[0][0].hyper && !has(&b, "Hyper Mode"));
    turn(&b, 0, 0);
    CHECK(b.party[0][0].hyper && has(&b, "went into Hyper Mode!"));
    { int flagged = 0; for (uint32_t i = 0; i < b.event_count; ++i) flagged |= b.events[i].hyper; CHECK(flagged); }
    CHECK(has(&b, "is in Hyper Mode!"));      /* end-of-turn reminder */
    /* Nature 4 never enters; a foe's Shadow Pokemon never does. */
    hero(4, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 2, error));
    for (unsigned t = 0; t < 3 && b.outcome == C3_GB_RUNNING; ++t) turn(&b, 0, 0);
    CHECK(!b.party[0][0].hyper);
    for (unsigned i = 0; i < b.party_count[1]; ++i) CHECK(!b.party[1][i].hyper);
    /* Without the fight kind's Hyper Mode flag nothing happens. */
    {   C3BattlePack k = pack; k.bytes = malloc(pack.size); CHECK(k.bytes); memcpy(k.bytes, pack.bytes, pack.size);
        k.bytes[k.entries_at + 110] = 0;
        hero(0, 3000); CHECK(c3_general_battle_begin(&b, &k, 0, party, 3, error));
        CHECK(b.party[0][0].dp == 3000);
        C3GeneralCommand c[2] = {{0, 0, 2}, {0}}; CHECK(c3_general_battle_turn(&b, &k, c, error));
        CHECK(!b.party[0][0].hyper);
        free(k.bytes);
    }
    /* Misbehaviour in Hyper Mode with a move other than Shadow Rush. */
    hero(1, OPEN); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 4, error));
    b.party[0][0].hyper = 1; turn(&b, 0, 1);
    CHECK(has(&b, "acted on its own") && moves_by(&b, 0) == 1);
    hero(2, OPEN); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 5, error));
    b.party[0][0].hyper = 1; turn(&b, 0, 1);
    CHECK(has(&b, "ignoring your orders") && moves_by(&b, 0) == 0);
    hero(3, OPEN); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 6, error));
    b.party[0][0].hyper = 1; { unsigned before = b.party[0][0].hp; turn(&b, 0, 1);
        CHECK(has(&b, "hit itself") && moves_by(&b, 0) == 0 && b.party[0][0].hp < before); }
    /* Shadow Rush in Hyper Mode obeys, and is a critical hit about 90% of the time. */
    {   unsigned crits = 0, uses = 0;
        for (uint32_t seed = 0; seed < 100; ++seed) {
            hero(0, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
            b.party[0][0].hyper = 1; b.party[0][0].stats[5] = 999;
            turn(&b, 0, 0);
            uses += moves_by(&b, 0); crits += has(&b, "critical hit");
        }
        printf("battle-hyper: Shadow Rush in Hyper Mode: %u critical hits in %u uses\n", crits, uses);
        CHECK(uses >= 95 && crits >= 75);
    }
    /* Each action in Hyper Mode: 1 in 256 snaps out on its own. */
    {   unsigned cleared = 0;
        for (uint32_t seed = 0; seed < 1500; ++seed) {
            hero(0, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, seed, error));
            b.party[0][0].hyper = 1; b.party[0][0].stats[5] = 999; turn(&b, 0, 0);
            cleared += !b.party[0][0].hyper && has(&b, "came to its senses");
        }
        printf("battle-hyper: %u of 1500 actions snapped out by themselves\n", cleared);
        CHECK(cleared >= 1 && cleared <= 20);
    }
    /* Call: success snaps out and drops the gauge (filter 2, -3 DP); nature 5 fails. */
    hero(0, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 7, error));
    b.party[0][0].hyper = 1; b.party[0][0].stats[5] = 999;
    { C3GeneralCommand cc = {3, 0, 0}; CHECK(c3_general_battle_command_valid(&b, &pack, 0, cc)); }
    { C3GeneralCommand cc = {3, 0, 2}; CHECK(c3_general_battle_command_valid(&b, &pack, 0, cc)); } /* target ignored: the caller's own */
    turn(&b, 3, 0);
    CHECK(!b.party[0][0].hyper && has(&b, "came to its senses!") && b.party[0][0].dp == 2900 - 300 && moves_by(&b, 0) == 0);
    hero(5, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 8, error));
    b.party[0][0].hyper = 1; turn(&b, 3, 0);
    CHECK(b.party[0][0].hyper && has(&b, "didn't respond"));
    /* Out of Hyper Mode Call wakes a sleeper. */
    hero(0, 3000); CHECK(c3_general_battle_begin(&b, &pack, 0, party, 9, error));
    b.party[0][0].status = C3_GB_SLEEP; b.party[0][0].status_turns = 5; b.party[0][0].stats[5] = 999;
    turn(&b, 3, 0);
    CHECK(b.party[0][0].status == C3_GB_NONE && has(&b, "woke up"));
    /* Experience: a full gauge (level 0) banks none; under 60% (level 3) banks it; the gauge is written back. */
    for (int half = 0; half < 2; ++half) {
        hero(4, half ? 1400 : 3000);
        CHECK(c3_general_battle_begin(&b, &pack, 0, party, 10, error));
        while (b.outcome == C3_GB_RUNNING) turn(&b, 0, 0);
        CHECK(b.outcome == C3_GB_WON);
        uint8_t copy[6][0x138]; memcpy(copy, party, sizeof copy); C3GeneralRewards rw;
        CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
        CHECK(half ? g32(copy[0] + 0xe0) > 0 : g32(copy[0] + 0xe0) == 0);
        CHECK((int32_t)g32(copy[0] + 0xdc) == (half ? 1300 : 2900));
    }
    c3_battle_pack_free(&pack); free(bytes);
    puts("battle-hyper: entry, misbehaviour, critical rule, reminder, Call, gauge drops and banking ok");
    return 0;
}
