#include "colosseum3ds/link_battle.h"
#include <stdio.h>
#include <string.h>

/* Lockstep link battles and trades (docs/link-play.md). The original's
 * rules are cited at each check; everything else is the port's network
 * glue, which the original never needed. */

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int failf(char *e, size_t n, const char *m) { if (e && n) snprintf(e, n, "%s", m); return 0; }

/* min level, max level, level sum, min count, dup species, dup items */
const C3LinkRule c3_link_rule_presets[3] = {
    {1, 100, 600, 1, 1, 1},
    {1, 50, 300, 6, 0, 0},
    {1, 100, 600, 6, 0, 0},
};

unsigned c3_link_team_size(C3LinkMode mode, C3LinkBattleType type, unsigned rule)
{
    /* fn_80077DB8: versus with rule 0 brings all six; else single 3,
     * double 4, multi 2. Battle Now (mode 3) never takes the 6 branch. */
    if (mode == C3_LINK_MODE_COLOSSEUM && rule == 0) return 6;
    return type == C3_LINK_SINGLE ? 3u : type == C3_LINK_DOUBLE ? 4u : 2u;
}

unsigned c3_link_team_count(C3LinkMode mode, C3LinkBattleType type, unsigned rule)
{
    unsigned n = c3_link_team_size(mode, type, rule);
    return type == C3_LINK_MULTI && n > 3 ? 3u : n;
}

unsigned c3_link_team_minimum(C3LinkMode mode, C3LinkBattleType type, unsigned rule)
{
    unsigned n = c3_link_team_count(mode, type, rule);
    if (c3_link_team_size(mode, type, rule) == 6) { unsigned m = c3_link_rule_presets[rule % 3].min_count; return m && m < n ? m : n; }
    return n;
}

#define ITEM_BANNED 0xAFu /* fn_80076398: always refused */
static int is_shadow(const uint8_t *r) { return be16(r + 0xd8) && (int32_t)be32(r + 0xdc) >= 0; }
static int is_egg(const uint8_t *r) { return r[0xcb] != 0; }

static int mon_legal(const C3BattlePack *pack, const C3LinkRule *rule, const uint8_t *r, char *e, size_t n)
{
    unsigned species = be16(r), level = r[0x60], ev_total = 0, moves = 0;
    if (!species) return failf(e, n, "An empty party slot cannot battle.");
    if (is_egg(r)) return failf(e, n, "An Egg cannot battle.");
    if (is_shadow(r)) return failf(e, n, "Shadow Pokemon cannot enter Battle Mode.");
    if (be16(r + 0x88) == ITEM_BANNED) return failf(e, n, "A Pokemon holds an item that is not allowed.");
    if (level < rule->min_level || level > rule->max_level) {
        if (e && n) snprintf(e, n, "Pokemon must be level %u to %u.", rule->min_level, rule->max_level);
        return 0;
    }
    for (unsigned j = 0; j < 6; ++j) { unsigned ev = be16(r + 0x98 + 2 * j); if (ev > 255) return failf(e, n, "A Pokemon's effort values are not legal."); ev_total += ev; }
    if (ev_total > 510) return failf(e, n, "A Pokemon's effort values are not legal.");
    for (unsigned j = 0; j < 4; ++j) { unsigned m = be16(r + 0x78 + 4 * j); if (m && pack && m >= pack->moves) return failf(e, n, "A Pokemon knows a move that is not valid."); moves += m != 0; }
    if (!moves) return failf(e, n, "A Pokemon knows no moves.");
    if (pack) { C3GeneralMon mon; if (!c3_general_mon_from_record(pack, r, 0, &mon)) return failf(e, n, "A Pokemon record is damaged."); }
    return 1;
}

int c3_link_rules_check(const C3BattlePack *pack, const C3LinkRule *rule, const uint8_t *records, unsigned count,
                        unsigned minimum, unsigned maximum, char *e, size_t n)
{
    unsigned sum = 0;
    if (!rule || !records) return failf(e, n, "No team.");
    if (count < minimum || count > maximum) {
        if (e && n) { if (minimum == maximum) snprintf(e, n, "Choose %u Pokemon.", maximum); else snprintf(e, n, "Choose %u to %u Pokemon.", minimum, maximum); }
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *r = records + (size_t)i * C3_GB_POKEMON_SIZE;
        if (!mon_legal(pack, rule, r, e, n)) return 0;
        sum += r[0x60];
        for (unsigned k = 0; k < i; ++k) {
            const uint8_t *q = records + (size_t)k * C3_GB_POKEMON_SIZE;
            if (!rule->duplicate_species && be16(q) == be16(r)) return failf(e, n, "Two Pokemon of the same species cannot enter.");
            if (!rule->duplicate_items && be16(r + 0x88) && be16(q + 0x88) == be16(r + 0x88)) return failf(e, n, "Two Pokemon cannot hold the same item.");
        }
    }
    if (sum > rule->level_sum) { if (e && n) snprintf(e, n, "The levels add up to more than %u.", rule->level_sum); return 0; }
    return 1;
}

unsigned c3_link_pick_team(const C3BattlePack *pack, const C3LinkRule *rule, const uint8_t party[6][C3_GB_POKEMON_SIZE],
                           unsigned wanted, uint8_t *out)
{
    unsigned count = 0, sum = 0;
    for (unsigned i = 0; i < 6 && count < wanted; ++i) {
        const uint8_t *r = party[i];
        if (!mon_legal(pack, rule, r, NULL, 0) || sum + r[0x60] > rule->level_sum) continue;
        int clash = 0;
        for (unsigned k = 0; k < count; ++k) {
            const uint8_t *q = out + (size_t)k * C3_GB_POKEMON_SIZE;
            if (!rule->duplicate_species && be16(q) == be16(r)) clash = 1;
            if (!rule->duplicate_items && be16(r + 0x88) && be16(q + 0x88) == be16(r + 0x88)) clash = 1;
        }
        if (clash) continue;
        memcpy(out + (size_t)count * C3_GB_POKEMON_SIZE, r, C3_GB_POKEMON_SIZE);
        sum += r[0x60]; ++count;
    }
    return count;
}

uint32_t c3_link_pack_hash(const C3BattlePack *pack)
{ return pack && pack->bytes ? c3_link_hash(C3_LINK_HASH_INIT, pack->bytes, pack->size) : 0; }

uint32_t c3_link_default_stage(const C3BattlePack *pack)
{
    C3BattleEncounterInfo info; uint32_t first = UINT32_MAX;
    for (uint32_t i = 0; pack && i < pack->encounters; ++i) {
        if (!c3_battle_pack_info(pack, i, &info) || info.reason) continue;
        if (first == UINT32_MAX) first = i;
        if (info.floor_data_id == 26) return i; /* a story Colosseum floor */
    }
    return first == UINT32_MAX ? 0 : first;
}

/* ---- roles ----------------------------------------------------------------- */

static const uint8_t player_orders[3][4] = {{0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}}; /* menuCBPlayerOrders */

static void assign_positions(C3LinkBattle *lb)
{
    memset(lb->side_of, 0, sizeof lb->side_of); memset(lb->slot_of, 0xFF, sizeof lb->slot_of);
    if (lb->type == C3_LINK_MULTI) {
        const uint8_t *o = player_orders[lb->order % 3];
        for (unsigned k = 0; k < 4; ++k) { lb->side_of[o[k]] = (uint8_t)(k / 2); lb->slot_of[o[k]] = (uint8_t)(k % 2); }
    } else {
        lb->side_of[0] = 0; lb->side_of[1] = 1;
    }
}

unsigned c3_link_battle_viewer(const C3LinkBattle *lb) { return lb->side_of[lb->session->self_player % C3_LINK_MAX_PLAYERS]; }

static unsigned positions_of(const C3LinkBattle *lb, unsigned player)
{
    unsigned side = lb->side_of[player];
    if (lb->type == C3_LINK_MULTI) return 1u << (side * 2 + lb->slot_of[player]);
    return lb->type == C3_LINK_DOUBLE ? 3u << (side * 2) : 1u << (side * 2);
}
unsigned c3_link_battle_local_positions(const C3LinkBattle *lb) { return positions_of(lb, lb->session->self_player); }

static int fail_battle(C3LinkBattle *lb, const char *m)
{
    if (lb->phase != C3_LB_FAILED) { snprintf(lb->error, sizeof lb->error, "%s", m); lb->phase = C3_LB_FAILED; }
    return 0;
}

/* ---- setup ----------------------------------------------------------------- */

static void send_setup(C3LinkBattle *lb)
{
    uint8_t m[32 + 4 * C3_LINK_MAX_PLAYERS];
    m[0] = lb->mode; m[1] = lb->type; m[2] = lb->rule; m[3] = lb->order; m[4] = lb->players;
    put32(m + 5, lb->seed); put32(m + 9, lb->stage); put32(m + 13, lb->pack_hash);
    for (unsigned p = 0; p < C3_LINK_MAX_PLAYERS; ++p) put32(m + 17 + 4 * p, lb->preset[p]);
    c3_link_send(lb->session, C3_LINK_BROADCAST, C3_LINK_MSG_SETUP, m, 17 + 4 * C3_LINK_MAX_PLAYERS);
}

int c3_link_battle_host(C3LinkBattle *lb, C3LinkSession *s, const C3BattlePack *pack, C3LinkMode mode,
                        C3LinkBattleType type, unsigned rule, unsigned order, uint32_t stage, uint32_t seed)
{
    memset(lb, 0, sizeof *lb);
    lb->session = s; lb->pack = pack; lb->mode = (uint8_t)mode; lb->type = (uint8_t)type;
    lb->rule = (uint8_t)(rule % 3); lb->order = (uint8_t)(order % 3); lb->players = s->player_count;
    lb->seed = seed; lb->stage = stage; lb->pack_hash = c3_link_pack_hash(pack);
    if (!s->is_host) return fail_battle(lb, "Only the host chooses the battle.");
    if (type == C3_LINK_MULTI ? lb->players != 4 : lb->players != 2)
        return fail_battle(lb, type == C3_LINK_MULTI ? "A multi battle needs four players." : "Single and double battles need two players.");
    if (mode == C3_LINK_MODE_BATTLE_NOW) {
        /* fn_80069C0C case 3: every player gets a different preset trainer
         * party. The draws use their own generator seeded from the battle
         * seed, sent in the setup, so no console draws them itself. */
        C3BattleRng rng; c3_battle_rng_init(&rng, seed ^ 0x8006AABCu);
        unsigned need = c3_link_team_count(mode, type, lb->rule);
        uint32_t candidates[1024]; unsigned count = 0;
        C3BattleEncounterInfo info;
        for (uint32_t i = 0; i < pack->encounters && count < 1024; ++i)
            if (c3_battle_pack_info(pack, i, &info) && !info.reason && info.party_count >= need) candidates[count++] = i;
        if (count < lb->players) return fail_battle(lb, "The battle pack has too few trainer teams.");
        for (unsigned p = 0; p < lb->players; ++p) {
            for (;;) {
                uint32_t pick; if (!c3_battle_rng_bounded32(&rng, count, &pick)) return fail_battle(lb, "Random-number failure.");
                C3BattleEncounterInfo a; c3_battle_pack_info(pack, candidates[pick], &a);
                int clash = 0;
                for (unsigned q = 0; q < p; ++q) { C3BattleEncounterInfo b; c3_battle_pack_info(pack, lb->preset[q], &b);
                    if (b.trainer_id == a.trainer_id && !strcmp(b.trainer, a.trainer)) clash = 1; }
                if (!clash) { lb->preset[p] = candidates[pick]; break; }
            }
        }
    }
    assign_positions(lb);
    memcpy(lb->names, s->names, sizeof lb->names);
    send_setup(lb);
    lb->phase = C3_LB_TEAMS;
    return 1;
}

int c3_link_battle_join(C3LinkBattle *lb, C3LinkSession *s, const C3BattlePack *pack)
{
    memset(lb, 0, sizeof *lb);
    lb->session = s; lb->pack = pack; lb->phase = C3_LB_SETUP; lb->pack_hash = c3_link_pack_hash(pack);
    return 1;
}

static void broadcast_team(C3LinkBattle *lb, unsigned player)
{
    uint8_t m[2 + C3_GB_PARTY * C3_GB_POKEMON_SIZE];
    m[0] = (uint8_t)player; m[1] = lb->team_count[player];
    memcpy(m + 2, lb->team[player], (size_t)lb->team_count[player] * C3_GB_POKEMON_SIZE);
    c3_link_send(lb->session, C3_LINK_BROADCAST, C3_LINK_MSG_TEAM, m, 2 + (size_t)lb->team_count[player] * C3_GB_POKEMON_SIZE);
}

int c3_link_battle_offer_team(C3LinkBattle *lb, const uint8_t *records, unsigned count)
{
    unsigned self = lb->session->self_player;
    if (self >= C3_LINK_MAX_PLAYERS) return fail_battle(lb, "Not in a link session yet.");
    if (count == 0 || count > C3_GB_PARTY) return fail_battle(lb, "Invalid team.");
    memcpy(lb->team[self], records, (size_t)count * C3_GB_POKEMON_SIZE);
    lb->team_count[self] = (uint8_t)count; lb->team_in[self] = 1;
    if (lb->phase == C3_LB_TEAMS) broadcast_team(lb, self);
    return 1;
}

static int build_battle(C3LinkBattle *lb)
{
    static C3GeneralMon mons[2][C3_GB_PARTY];
    uint8_t counts[2] = {0, 0}, owner[2][C3_GB_PARTY];
    char names[2][C3_GB_NAME];
    C3BattleEncounterInfo stage;
    char e[C3_GB_ERROR_SIZE];
    memset(mons, 0, sizeof mons); memset(owner, 0, sizeof owner); memset(names, 0, sizeof names);
    if (!c3_battle_pack_info(lb->pack, lb->stage, &stage)) return fail_battle(lb, "The battle stage is missing from the pack.");
    unsigned need = c3_link_team_count((C3LinkMode)lb->mode, (C3LinkBattleType)lb->type, lb->rule);
    /* Slot 0's player before slot 1's within a side, so owner ranges are tidy. */
    for (unsigned slot = 0; slot < 2; ++slot)
        for (unsigned p = 0; p < lb->players; ++p) {
            if (lb->type == C3_LINK_MULTI ? lb->slot_of[p] != slot : slot != 0) continue;
            unsigned s = lb->side_of[p];
            size_t used = strlen(names[s]);
            snprintf(names[s] + used, C3_GB_NAME - used, "%s%s", used ? "&" : "", lb->names[p]);
            if (lb->mode == C3_LINK_MODE_BATTLE_NOW) {
                for (unsigned i = 0; i < need; ++i) {
                    C3GeneralMon *m = &mons[s][counts[s]];
                    if (!c3_general_mon_from_trainer(lb->pack, lb->preset[p], i, m)) return fail_battle(lb, "Damaged preset team.");
                    m->held_item = 0; /* fn_80069C0C clears every held item */
                    owner[s][counts[s]++] = lb->type == C3_LINK_MULTI ? (uint8_t)slot : 0;
                }
            } else {
                for (unsigned i = 0; i < lb->team_count[p]; ++i) {
                    C3GeneralMon *m = &mons[s][counts[s]];
                    if (counts[s] >= C3_GB_PARTY || !c3_general_mon_from_record(lb->pack, lb->team[p][i], i, m))
                        return fail_battle(lb, "A player's team record is damaged.");
                    owner[s][counts[s]++] = lb->type == C3_LINK_MULTI ? (uint8_t)slot : 0;
                }
            }
        }
    if (!c3_general_battle_begin_link(&lb->battle, &stage, lb->type == C3_LINK_SINGLE ? 1u : 2u, (const C3GeneralMon (*)[C3_GB_PARTY])mons,
                                      counts, lb->type == C3_LINK_MULTI ? (const uint8_t (*)[C3_GB_PARTY])owner : NULL,
                                      (const char (*)[C3_GB_NAME])names, lb->seed, c3_link_battle_viewer(lb), e))
        return fail_battle(lb, e);
    lb->last_hash = c3_general_battle_hash(&lb->battle);
    lb->new_turn = 1; /* the opening send-outs */
    lb->phase = C3_LB_COMMANDS;
    return 1;
}

static int teams_complete(const C3LinkBattle *lb)
{
    if (lb->mode == C3_LINK_MODE_BATTLE_NOW) return 1;
    for (unsigned p = 0; p < lb->players; ++p) if (!lb->team_in[p]) return 0;
    return 1;
}

/* ---- turns ----------------------------------------------------------------- */

static void apply_turn(C3LinkBattle *lb, const C3GeneralCommand cmd[4])
{
    char e[C3_GB_ERROR_SIZE];
    if (!c3_general_battle_turn_link(&lb->battle, lb->pack, cmd, e)) { fail_battle(lb, e); return; }
    lb->last_hash = c3_general_battle_hash(&lb->battle);
    ++lb->turns_applied; lb->new_turn = 1;
    memset(lb->have, 0, sizeof lb->have);
    uint8_t m[8]; put32(m, lb->battle.turn); put32(m + 4, lb->last_hash);
    if (lb->session->is_host) {
        lb->hash_turn = lb->battle.turn; memset(lb->hash_have, 0, sizeof lb->hash_have);
        lb->hashes[0] = lb->last_hash; lb->hash_have[0] = 1;
    } else c3_link_send(lb->session, 0, C3_LINK_MSG_HASH, m, sizeof m);
    lb->phase = lb->battle.outcome == C3_GB_RUNNING ? C3_LB_COMMANDS : C3_LB_FINISHED;
}

static int alive_at(const C3GeneralBattle *b, unsigned pos)
{
    unsigned s = pos / 2, a = pos % 2;
    if (a >= b->active_count || b->active[s][a] == 255) return 0;
    return b->party[s][b->active[s][a]].hp != 0;
}

/* Host: once every occupied position has a command, send and run the turn. */
static void host_try_turn(C3LinkBattle *lb)
{
    if (lb->phase != C3_LB_WAITING && lb->phase != C3_LB_COMMANDS) return;
    for (unsigned pos = 0; pos < 4; ++pos) if (alive_at(&lb->battle, pos) && !lb->have[pos]) return;
    C3GeneralCommand cmd[4];
    uint8_t m[4 + 12];
    for (unsigned pos = 0; pos < 4; ++pos) {
        cmd[pos] = lb->have[pos] ? lb->gathered[pos] : (C3GeneralCommand){0, 0, 0};
        if (alive_at(&lb->battle, pos) && !c3_general_battle_command_valid(&lb->battle, lb->pack, pos, cmd[pos])) {
            fail_battle(lb, "A player sent a command that is not legal."); return;
        }
        m[4 + pos * 3] = cmd[pos].kind; m[5 + pos * 3] = cmd[pos].slot; m[6 + pos * 3] = cmd[pos].target;
    }
    put32(m, lb->battle.turn + 1);
    c3_link_send(lb->session, C3_LINK_BROADCAST, C3_LINK_MSG_TURN, m, sizeof m);
    apply_turn(lb, cmd);
}

int c3_link_battle_submit(C3LinkBattle *lb, const C3GeneralCommand commands[4])
{
    if (lb->phase != C3_LB_COMMANDS) return 0;
    unsigned mine = c3_link_battle_local_positions(lb), n = 0;
    uint8_t m[5 + 4 * 4];
    put32(m, lb->battle.turn + 1);
    for (unsigned pos = 0; pos < 4; ++pos) if ((mine >> pos & 1u) && alive_at(&lb->battle, pos)) {
        m[5 + n * 4] = (uint8_t)pos; m[6 + n * 4] = commands[pos].kind; m[7 + n * 4] = commands[pos].slot; m[8 + n * 4] = commands[pos].target;
        ++n;
        if (lb->session->is_host) { lb->gathered[pos] = commands[pos]; lb->have[pos] = 1; }
    }
    m[4] = (uint8_t)n;
    lb->phase = C3_LB_WAITING;
    if (!n) return 1; /* every Pokemon of this player has fainted: nothing to say */
    if (lb->session->is_host) host_try_turn(lb);
    else if (!c3_link_send(lb->session, 0, C3_LINK_MSG_COMMAND, m, 5 + n * 4u)) { lb->phase = C3_LB_COMMANDS; return 0; }
    return 1;
}

int c3_link_battle_update(C3LinkBattle *lb)
{
    C3LinkSession *s = lb->session;
    C3LinkMessage msg;
    char text[C3_GB_ERROR_SIZE];
    if (lb->phase == C3_LB_FAILED) return 0;
    if (s->state == C3_LINK_FAILED) { snprintf(text, sizeof text, "Link lost: %s.", c3_link_reason_text(s->reason)); return fail_battle(lb, text); }
    while (c3_link_poll(s, &msg)) {
        switch (msg.type) {
        case C3_LINK_MSG_LEFT:
            if (lb->phase != C3_LB_FINISHED) {
                snprintf(text, sizeof text, "%s left the battle.", msg.from < C3_LINK_MAX_PLAYERS && lb->names[msg.from][0] ? lb->names[msg.from] : "A player");
                return fail_battle(lb, text);
            }
            break;
        case C3_LINK_MSG_SETUP:
            if (s->is_host || msg.size < 17 + 4 * C3_LINK_MAX_PLAYERS || lb->phase != C3_LB_SETUP) break;
            lb->mode = msg.data[0]; lb->type = msg.data[1]; lb->rule = msg.data[2]; lb->order = msg.data[3]; lb->players = msg.data[4];
            lb->seed = be32(msg.data + 5); lb->stage = be32(msg.data + 9);
            for (unsigned p = 0; p < C3_LINK_MAX_PLAYERS; ++p) lb->preset[p] = be32(msg.data + 17 + 4 * p);
            assign_positions(lb);
            memcpy(lb->names, s->names, sizeof lb->names);
            if (be32(msg.data + 13) != lb->pack_hash) {
                const char *why = "Your prepared battle pack differs from the host's.";
                c3_link_send(s, C3_LINK_BROADCAST, C3_LINK_MSG_REFUSE, why, strlen(why) + 1);
                return fail_battle(lb, why);
            }
            lb->phase = C3_LB_TEAMS;
            if (lb->team_in[s->self_player]) broadcast_team(lb, s->self_player);
            break;
        case C3_LINK_MSG_REFUSE:
            snprintf(text, sizeof text, "%s: %.*s", msg.from < C3_LINK_MAX_PLAYERS ? lb->names[msg.from] : "?", (int)(msg.size ? msg.size - 1 : 0), (const char *)msg.data);
            return fail_battle(lb, text);
        case C3_LINK_MSG_TEAM:
            if (msg.size < 2 || msg.data[0] != msg.from || msg.from >= C3_LINK_MAX_PLAYERS || msg.data[1] > C3_GB_PARTY ||
                msg.size != 2u + msg.data[1] * C3_GB_POKEMON_SIZE) return fail_battle(lb, "A damaged team arrived.");
            memcpy(lb->team[msg.from], msg.data + 2, (size_t)msg.data[1] * C3_GB_POKEMON_SIZE);
            lb->team_count[msg.from] = msg.data[1]; lb->team_in[msg.from] = 1;
            break;
        case C3_LINK_MSG_COMMAND:
            if (!s->is_host || msg.size < 5 || msg.from >= C3_LINK_MAX_PLAYERS) break;
            if (be32(msg.data) != lb->battle.turn + 1) {
                if (!msg.data[4]) break; /* an empty command overtaken by the turn it waited for */
                return fail_battle(lb, "A command for the wrong turn arrived.");
            }
            for (unsigned k = 0; k < msg.data[4] && 5u + k * 4u + 4u <= msg.size; ++k) {
                unsigned pos = msg.data[5 + k * 4];
                if (pos >= 4 || !(positions_of(lb, msg.from) >> pos & 1u)) return fail_battle(lb, "A player commanded a position that is not theirs.");
                lb->gathered[pos] = (C3GeneralCommand){msg.data[6 + k * 4], msg.data[7 + k * 4], msg.data[8 + k * 4]};
                lb->have[pos] = 1;
            }
            host_try_turn(lb);
            break;
        case C3_LINK_MSG_TURN:
            if (s->is_host || msg.size < 16) break;
            if (be32(msg.data) != lb->battle.turn + 1) return fail_battle(lb, "Turns arrived out of order.");
            {
                C3GeneralCommand cmd[4];
                for (unsigned pos = 0; pos < 4; ++pos) cmd[pos] = (C3GeneralCommand){msg.data[4 + pos * 3], msg.data[5 + pos * 3], msg.data[6 + pos * 3]};
                apply_turn(lb, cmd);
            }
            /* One turn per update, so the scene can play each turn's events
             * before the next overwrites them; later messages stay queued. */
            return lb->phase != C3_LB_FAILED;
        case C3_LINK_MSG_HASH:
            if (!s->is_host || msg.size < 8 || msg.from >= C3_LINK_MAX_PLAYERS) break;
            if (be32(msg.data) == lb->hash_turn) {
                lb->hashes[msg.from] = be32(msg.data + 4); lb->hash_have[msg.from] = 1;
                if (lb->hashes[msg.from] != lb->hashes[0]) {
                    uint8_t m[4]; put32(m, lb->hash_turn);
                    c3_link_send(s, C3_LINK_BROADCAST, C3_LINK_MSG_DESYNC, m, 4);
                    snprintf(text, sizeof text, "Desync at turn %lu: %s's battle differs.", (unsigned long)lb->hash_turn, lb->names[msg.from]);
                    return fail_battle(lb, text);
                }
            }
            break;
        case C3_LINK_MSG_DESYNC:
            snprintf(text, sizeof text, "Desync at turn %lu: the battles differ.", (unsigned long)(msg.size >= 4 ? be32(msg.data) : 0));
            return fail_battle(lb, text);
        default: break;
        }
        if (lb->phase == C3_LB_FAILED) return 0;
    }
    if (lb->phase == C3_LB_TEAMS && teams_complete(lb)) return build_battle(lb);
    return lb->phase != C3_LB_FAILED;
}

