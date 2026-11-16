#include "colosseum3ds/link_battle.h"
#include "colosseum3ds/game_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for lockstep link battles (src/link_battle.c) over
 * the loopback transport with packet loss, duplication and reordering.
 *
 * Part 1 (synthetic pack, no original data): Colosseum battles between two
 * consoles (single, double) and four consoles (multi), where every console
 * must log the same state hash after every turn and reach the same outcome;
 * the rule checks; a forced desync and a dropped player are reported.
 *
 * Part 2 (PRIVATE, optional): with <battles.c3x> <newgame.c3g> <save A>
 * <save B> it plays Battle Now (2P single, 2P double, 4P multi) with preset
 * teams from the real pack, a Colosseum battle with both saves' parties
 * under the first preset rule both parties satisfy. Saves are only read. */

static char error[256];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
#include "battle_pack_fixture.h"

typedef struct {
    C3LinkLoopback net;
    C3LinkLoopbackEnd end[5];
    C3LinkTransport t[5];
    C3LinkSession s[5];
    C3LinkBattle lb[5];
    C3BattleRng ai[5];
    uint32_t now;
    uint32_t hashes[5][C3_GB_MAX_TURNS + 1];
    unsigned nh[5];
    unsigned n;
} Net;

static Net *net_new(unsigned players, uint32_t seed, unsigned loss)
{
    Net *w = calloc(1, sizeof *w);
    w->n = players; w->now = 5000;
    c3_link_loopback_init(&w->net, seed);
    w->net.drop_percent = loss; w->net.duplicate_percent = loss / 3; w->net.reorder_percent = loss;
    for (uint8_t i = 1; i <= players; ++i) c3_link_loopback_transport(&w->end[i], &w->net, i, &w->t[i]);
    static const char *names[] = {"", "WES", "RUI", "MIROR", "EVICE"};
    CHECK(c3_link_host(&w->s[1], &w->t[1], names[1], "test-build", w->now));
    for (unsigned i = 2; i <= players; ++i) CHECK(c3_link_join(&w->s[i], &w->t[i], names[i], "test-build", w->now));
    for (unsigned k = 0; k < 2000 && w->s[1].player_count < players; ++k) {
        w->now += 10; for (unsigned i = 1; i <= players; ++i) c3_link_update(&w->s[i], w->now);
    }
    CHECK(w->s[1].player_count == players);
    c3_link_lock(&w->s[1]);
    for (unsigned k = 0; k < 2000; ++k) {
        unsigned ready = 0;
        for (unsigned i = 2; i <= players; ++i) ready += w->s[i].state == C3_LINK_LOBBY && w->s[i].locked && w->s[i].player_count == players;
        if (ready == players - 1) break;
        w->now += 10; for (unsigned i = 1; i <= players; ++i) c3_link_update(&w->s[i], w->now);
    }
    for (unsigned i = 2; i <= players; ++i) {
        snprintf(error, sizeof error, "node %u state %d reason %d locked %d self %u count %u", i, w->s[i].state, w->s[i].reason, w->s[i].locked, w->s[i].self_player, w->s[i].player_count);
        CHECK(w->s[i].locked && w->s[i].self_player < players);
    }
    for (unsigned i = 1; i <= players; ++i) c3_battle_rng_init(&w->ai[i], 0xA1u + i * 7919u);
    return w;
}
static void pump(Net *w) { w->now += 10; for (unsigned i = 1; i <= w->n; ++i) c3_link_update(&w->s[i], w->now); }

/* Drive every console until all finish (or one fails). Returns phases. */
static void play(Net *w, int tamper_turn, int drop_player_turn)
{
    for (unsigned round = 0; round < 400000; ++round) {
        pump(w);
        unsigned done = 0;
        for (unsigned i = 1; i <= w->n; ++i) {
            C3LinkBattle *lb = &w->lb[i];
            if (!w->net.alive[i]) { ++done; continue; }
            c3_link_battle_update(lb);
            if (lb->new_turn) {
                lb->new_turn = 0;
                w->hashes[i][w->nh[i]++] = lb->last_hash;
                if (tamper_turn >= 0 && i == 2 && lb->battle.turn == (uint32_t)tamper_turn) {
                    /* A console whose battle silently diverged: its next hash differs. */
                    lb->battle.party[0][0].hp = lb->battle.party[0][0].hp > 1 ? 1 : 2;
                    tamper_turn = -1;
                }
                if (drop_player_turn >= 0 && lb->battle.turn == (uint32_t)drop_player_turn && i == 1) {
                    w->net.alive[w->n] = 0; drop_player_turn = -1;
                }
            }
            if (lb->phase == C3_LB_COMMANDS) {
                C3GeneralCommand cmd[4]; memset(cmd, 0, sizeof cmd);
                unsigned mine = c3_link_battle_local_positions(lb);
                for (unsigned pos = 0; pos < 4; ++pos)
                    if (mine >> pos & 1u) c3_general_battle_choose_with(&lb->battle, lb->pack, pos, &w->ai[i], &cmd[pos]);
                c3_link_battle_submit(lb, cmd);
            }
            if (lb->phase == C3_LB_FINISHED || lb->phase == C3_LB_FAILED) ++done;
        }
        if (done == w->n) return;
    }
    for (unsigned i = 1; i <= w->n; ++i)
        fprintf(stderr, "console %u phase %d turn %lu error '%s' have %d%d%d%d state %d\n", i, w->lb[i].phase, (unsigned long)w->lb[i].battle.turn,
                w->lb[i].error, w->lb[i].have[0], w->lb[i].have[1], w->lb[i].have[2], w->lb[i].have[3], w->s[i].state);
    CHECK(!"battle did not finish");
}

static void check_lockstep(Net *w, const char *label)
{
    for (unsigned i = 1; i <= w->n; ++i) {
        snprintf(error, sizeof error, "%s", w->lb[i].error);
        CHECK(w->lb[i].phase == C3_LB_FINISHED);
        CHECK(w->nh[i] == w->nh[1]);
        CHECK(!memcmp(w->hashes[i], w->hashes[1], w->nh[1] * sizeof w->hashes[1][0]));
        CHECK(w->lb[i].battle.outcome == w->lb[1].battle.outcome);
        CHECK(c3_general_battle_hash(&w->lb[i].battle) == c3_general_battle_hash(&w->lb[1].battle));
    }
    /* Each console tells the result from its own side. */
    const C3GeneralBattle *b = &w->lb[1].battle;
    printf("%s: %u consoles, %lu turns, outcome %d, final hash %08lX, %u datagrams dropped\n", label, w->n,
           (unsigned long)b->turn, (int)b->outcome, (unsigned long)w->hashes[1][w->nh[1] - 1], (unsigned)w->net.dropped);
}

static void colosseum_synthetic(C3BattlePack *pack, C3LinkBattleType type, unsigned players, unsigned loss)
{
    Net *w = net_new(players, 11u + type, loss);
    uint8_t party[6][0x138];
    uint32_t stage = c3_link_default_stage(pack);
    CHECK(c3_link_battle_host(&w->lb[1], &w->s[1], pack, C3_LINK_MODE_COLOSSEUM, type, 0, 1, stage, 0xC0105EEDu));
    for (unsigned i = 2; i <= players; ++i) CHECK(c3_link_battle_join(&w->lb[i], &w->s[i], pack));
    unsigned need = c3_link_team_count(C3_LINK_MODE_COLOSSEUM, type, 0);
    for (unsigned i = 1; i <= players; ++i) {
        make_party(party, i % 2 ? 1 : 2, 30 + 5 * i, 6, 0);
        uint8_t team[6 * 0x138];
        unsigned got = c3_link_pick_team(pack, &c3_link_rule_presets[0], (const uint8_t (*)[0x138])party, need, team);
        CHECK(got == need);
        CHECK(c3_link_rules_check(pack, &c3_link_rule_presets[0], team, got, need, need, error, sizeof error));
        CHECK(c3_link_battle_offer_team(&w->lb[i], team, got));
    }
    play(w, -1, -1);
    char label[64]; snprintf(label, sizeof label, "colosseum %s", type == C3_LINK_SINGLE ? "single" : type == C3_LINK_DOUBLE ? "double" : "multi");
    check_lockstep(w, label);
    /* Viewers: player 0 sees side 0; in multi the order {0,2,1,3} puts player 2 with player 0. */
    if (type == C3_LINK_MULTI) {
        CHECK(c3_link_battle_viewer(&w->lb[1]) == 0 && c3_link_battle_viewer(&w->lb[3]) == 0);
        CHECK(c3_link_battle_viewer(&w->lb[2]) == 1 && c3_link_battle_viewer(&w->lb[4]) == 1);
        CHECK(c3_link_battle_local_positions(&w->lb[3]) == 2u && c3_link_battle_local_positions(&w->lb[4]) == 8u);
        CHECK(w->lb[1].battle.owner_split && w->lb[1].battle.party_count[0] == 6);
    }
    /* The mirrored copy swaps the sides and the outcome back. */
    static C3GeneralBattle m, mm;
    c3_general_battle_mirror(&w->lb[2].battle, &m);
    c3_general_battle_mirror(&m, &mm);
    CHECK(c3_general_battle_hash(&mm) == c3_general_battle_hash(&w->lb[2].battle));
    CHECK(m.party[0][0].species == w->lb[2].battle.party[1][0].species);
    free(w);
}

static void failures(C3BattlePack *pack)
{
    uint8_t party[6][0x138], team[6 * 0x138];
    /* Desync: console 2's state is changed behind the engine's back. */
    Net *w = net_new(2, 99, 0);
    CHECK(c3_link_battle_host(&w->lb[1], &w->s[1], pack, C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, 0, 0, 0, 7));
    CHECK(c3_link_battle_join(&w->lb[2], &w->s[2], pack));
    for (unsigned i = 1; i <= 2; ++i) { make_party(party, 1, 60, 6, 0); CHECK(c3_link_pick_team(pack, &c3_link_rule_presets[0], (const uint8_t (*)[0x138])party, 6, team) == 6); c3_link_battle_offer_team(&w->lb[i], team, 6); }
    play(w, 1, -1);
    CHECK(w->lb[1].phase == C3_LB_FAILED && strstr(w->lb[1].error, "Desync"));
    CHECK(w->lb[2].phase == C3_LB_FAILED && strstr(w->lb[2].error, "Desync"));
    printf("link battle desync: host reports \"%s\"\n", w->lb[1].error);
    free(w);
    /* A player drops mid-battle. */
    w = net_new(2, 98, 0);
    CHECK(c3_link_battle_host(&w->lb[1], &w->s[1], pack, C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, 0, 0, 0, 8));
    CHECK(c3_link_battle_join(&w->lb[2], &w->s[2], pack));
    for (unsigned i = 1; i <= 2; ++i) { make_party(party, 1, 60, 6, 0); c3_link_pick_team(pack, &c3_link_rule_presets[0], (const uint8_t (*)[0x138])party, 6, team); c3_link_battle_offer_team(&w->lb[i], team, 6); }
    play(w, -1, 1);
    CHECK(w->lb[1].phase == C3_LB_FAILED && strstr(w->lb[1].error, "left"));
    printf("link battle drop: host reports \"%s\"\n", w->lb[1].error);
    free(w);
    /* Wrong player counts are refused before anything is sent. */
    w = net_new(2, 97, 0);
    CHECK(!c3_link_battle_host(&w->lb[1], &w->s[1], pack, C3_LINK_MODE_COLOSSEUM, C3_LINK_MULTI, 0, 0, 0, 9));
    free(w);
}

static void rules(C3BattlePack *pack)
{
    uint8_t party[6][0x138], team[6 * 0x138];
    const C3LinkRule *lv50 = &c3_link_rule_presets[1];
    CHECK(c3_link_team_size(C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, 0) == 6);
    CHECK(c3_link_team_size(C3_LINK_MODE_COLOSSEUM, C3_LINK_DOUBLE, 1) == 4);
    CHECK(c3_link_team_size(C3_LINK_MODE_BATTLE_NOW, C3_LINK_SINGLE, 0) == 3);
    CHECK(c3_link_team_size(C3_LINK_MODE_COLOSSEUM, C3_LINK_MULTI, 2) == 2);
    CHECK(c3_link_team_count(C3_LINK_MODE_COLOSSEUM, C3_LINK_MULTI, 0) == 3);
    CHECK(c3_link_team_minimum(C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, 0) == 1);
    CHECK(c3_link_team_minimum(C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, 1) == 3);
    make_party(party, 1, 50, 6, 0);
    memcpy(team, party, 3 * 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 3, 3, 3, error, sizeof error) && strstr(error, "same species"));
    CHECK(c3_link_rules_check(pack, &c3_link_rule_presets[0], team, 3, 3, 3, error, sizeof error));
    CHECK(!c3_link_rules_check(pack, &c3_link_rule_presets[0], team, 2, 3, 3, error, sizeof error));
    make_party(party, 1, 51, 1, 0); memcpy(team, party, 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 1, 1, 1, error, sizeof error) && strstr(error, "level"));
    make_party(party, 1, 40, 1, 1); memcpy(team, party, 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 1, 1, 1, error, sizeof error) && strstr(error, "Shadow"));
    make_party(party, 1, 40, 1, 0); party[0][0xcb] = 1; memcpy(team, party, 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 1, 1, 1, error, sizeof error) && strstr(error, "Egg"));
    make_party(party, 1, 40, 1, 0); p16(party[0] + 0x88, 0xAF); memcpy(team, party, 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 1, 1, 1, error, sizeof error) && strstr(error, "item"));
    make_party(party, 1, 40, 1, 0); p16(party[0] + 0x98, 256); memcpy(team, party, 0x138);
    CHECK(!c3_link_rules_check(pack, lv50, team, 1, 1, 1, error, sizeof error) && strstr(error, "effort"));
    /* The picker skips what the rule refuses: one HERO, then FOE, at Lv50. */
    make_party(party, 1, 50, 6, 0); party[1][0x01] = 2;
    CHECK(c3_link_pick_team(pack, lv50, (const uint8_t (*)[0x138])party, 4, team) == 2);
    puts("link rules: team sizes, levels, duplicates, Shadow, Egg, item 0xAF, effort values");
}

static void battle_now(C3BattlePack *pack, C3LinkBattleType type, unsigned players, const char *label)
{
    Net *w = net_new(players, 21u + type, 15);
    CHECK(c3_link_battle_host(&w->lb[1], &w->s[1], pack, C3_LINK_MODE_BATTLE_NOW, type, 0, 0, c3_link_default_stage(pack), 0xBA77u + type));
    for (unsigned i = 2; i <= players; ++i) CHECK(c3_link_battle_join(&w->lb[i], &w->s[i], pack));
    for (unsigned p = 0; p < players; ++p) for (unsigned q = 0; q < p; ++q) CHECK(w->lb[1].preset[p] != w->lb[1].preset[q]);
    play(w, -1, -1);
    check_lockstep(w, label);
    for (unsigned s = 0; s < 2; ++s) for (unsigned i = 0; i < w->lb[1].battle.party_count[s]; ++i) CHECK(w->lb[1].battle.party[s][i].held_item == 0);
    printf("  presets: encounters %lu %lu %lu %lu\n", (unsigned long)w->lb[1].preset[0], (unsigned long)w->lb[1].preset[1],
           (unsigned long)w->lb[1].preset[2], (unsigned long)w->lb[1].preset[3]);
    free(w);
}

static void private_part(const char *pack_path, const char *newgame, const char *save_a, const char *save_b)
{
    C3BattlePack pack = {0}; C3GameState ga = {0}, gb = {0}; char gerror[C3_GAME_ERROR_SIZE] = {0};
    CHECK(c3_battle_pack_load(&pack, pack_path, error));
    battle_now(&pack, C3_LINK_SINGLE, 2, "private battle now single");
    battle_now(&pack, C3_LINK_DOUBLE, 2, "private battle now double");
    battle_now(&pack, C3_LINK_MULTI, 4, "private battle now multi");
    CHECK(c3_game_state_load(&ga, save_a, newgame, gerror));
    CHECK(c3_game_state_load(&gb, save_b, newgame, gerror));
    uint8_t pa[6][0x138], pb[6][0x138];
    for (unsigned i = 0; i < 6; ++i) { CHECK(c3_game_state_pokemon(&ga, i, pa[i])); CHECK(c3_game_state_pokemon(&gb, i, pb[i])); }
    /* Colosseum with each save's party; the rule is the first preset both teams satisfy. */
    unsigned rule = 1, need = 0, least = 0, got_a = 0, got_b = 0;
    uint8_t team_a[6 * 0x138], team_b[6 * 0x138];
    for (rule = 1; rule < 4; ++rule) {
        unsigned r = rule % 3;
        need = c3_link_team_count(C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, r);
        least = c3_link_team_minimum(C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, r);
        got_a = c3_link_pick_team(&pack, &c3_link_rule_presets[r], (const uint8_t (*)[0x138])pa, need, team_a);
        got_b = c3_link_pick_team(&pack, &c3_link_rule_presets[r], (const uint8_t (*)[0x138])pb, need, team_b);
        if (got_a >= least && got_b >= least) break;
    }
    if (rule < 4) {
        rule %= 3;
        Net *w = net_new(2, 77, 10);
        CHECK(c3_link_battle_host(&w->lb[1], &w->s[1], &pack, C3_LINK_MODE_COLOSSEUM, C3_LINK_SINGLE, rule, 0, c3_link_default_stage(&pack), 0x5A7Eu));
        CHECK(c3_link_battle_join(&w->lb[2], &w->s[2], &pack));
        CHECK(c3_link_rules_check(&pack, &c3_link_rule_presets[rule], team_a, got_a, least, need, error, sizeof error));
        CHECK(c3_link_rules_check(&pack, &c3_link_rule_presets[rule], team_b, got_b, least, need, error, sizeof error));
        c3_link_battle_offer_team(&w->lb[1], team_a, got_a); c3_link_battle_offer_team(&w->lb[2], team_b, got_b);
        printf("  teams: %u and %u Pokemon\n", got_a, got_b);
        play(w, -1, -1);
        char label[64]; snprintf(label, sizeof label, "private colosseum single rule %u (saves)", rule);
        check_lockstep(w, label);
        free(w);
    } else puts("private colosseum: the saves' parties fit no preset rule (skipped)");
    c3_game_state_free(&ga); c3_game_state_free(&gb); c3_battle_pack_free(&pack);
}

int main(int argc, char **argv)
{
    size_t size; uint8_t *bytes = make_pack(2, 30, &size);
    C3BattlePack pack;
    CHECK(c3_battle_pack_decode(&pack, bytes, size, error));
    rules(&pack);
    colosseum_synthetic(&pack, C3_LINK_SINGLE, 2, 20);
    colosseum_synthetic(&pack, C3_LINK_DOUBLE, 2, 20);
    colosseum_synthetic(&pack, C3_LINK_MULTI, 4, 20);
    failures(&pack);
    if (argc >= 5) private_part(argv[1], argv[2], argv[3], argv[4]);
    else puts("link battle: private part skipped (needs <battles.c3x> <newgame.c3g> <save A> <save B>)");
    c3_battle_pack_free(&pack); free(bytes);
    puts("link battle: all checks passed");
    return 0;
}
