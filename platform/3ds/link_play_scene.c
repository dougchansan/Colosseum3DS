/* Battle Now, Colosseum Battle and trading over local wireless
 * (docs/link-play.md): the battle runner shared by the title menu's lobby
 * (link_lobby.c) and the unattended -DC3_LINK_PROBE build. The battle plays
 * on the 3D stage through c3_battle_general_3d_run_link; every agreed
 * turn's state hash goes to the trace. Nothing here writes a save. */
#include "link_play.h"
#ifdef C3_LINK_PROBE
#include "link_probe.h"
#endif
#include "battle_general_3d.h"
#include "battle_general_scene.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "colosseum3ds/link_battle.h"
#include "colosseum3ds/game_state.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "sdmc:/3ds/Colosseum3DS/"
#define SAVE ROOT "saves/pokemon_colosseum.gci"
#define NEWGAME ROOT "boot/newgame.c3g"

int c3_link_play_console;

void c3_link_play_log(FILE *log, const char *fmt, ...)
{
    va_list ap;
    if (log) { fprintf(log, "[%lu] ", (unsigned long)osGetTime()); va_start(ap, fmt); vfprintf(log, fmt, ap); va_end(ap); fputc('\n', log); fflush(log); }
    if (c3_link_play_console) { va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }
}

int c3_link_play_pump(C3LinkSession *s)
{
    C3N3dsInput input;
    if (!c3_n3ds_poll(&input) || !aptMainLoop()) return 0;
    c3_link_update(s, (uint32_t)osGetTime());
    gspWaitForVBlank();
    return 1;
}

typedef struct {
    C3LinkBattle *lb;
    FILE *log;
    unsigned viewer;
} LinkCtx;

static void log_turn(LinkCtx *c)
{
    const C3GeneralBattle *b = &c->lb->battle;
    c3_link_play_log(c->log, "link_turn turn=%lu hash=%08lX outcome=%d draws=%llu hp=%u,%u,%u,%u",
        (unsigned long)b->turn, (unsigned long)c->lb->last_hash, (int)b->outcome, (unsigned long long)b->rng.draws,
        b->active[0][0] != 255 ? b->party[0][b->active[0][0]].hp : 0, b->active_count > 1 && b->active[0][1] != 255 ? b->party[0][b->active[0][1]].hp : 0,
        b->active[1][0] != 255 ? b->party[1][b->active[1][0]].hp : 0, b->active_count > 1 && b->active[1][1] != 255 ? b->party[1][b->active[1][1]].hp : 0);
}

static void make_view(LinkCtx *c, C3GeneralBattle *view)
{
    if (c->viewer) c3_general_battle_mirror(&c->lb->battle, view); else *view = c->lb->battle;
}

static int hook_pump(void *ctx, char *error, size_t cap)
{
    LinkCtx *c = ctx;
    c3_link_update(c->lb->session, (uint32_t)osGetTime());
    if (!c->lb->new_turn) c3_link_battle_update(c->lb);
    if (c->lb->phase == C3_LB_FAILED) { snprintf(error, cap, "%s", c->lb->error); c3_link_play_log(c->log, "link_battle_failed error=%s", c->lb->error); return 0; }
    return 1;
}

static int hook_submit(void *ctx, const C3GeneralCommand view_cmd[2])
{
    LinkCtx *c = ctx;
    C3GeneralCommand cmd[4]; memset(cmd, 0, sizeof cmd);
    for (unsigned a = 0; a < 2; ++a) {
        C3GeneralCommand v = view_cmd[a];
        if (c->viewer && v.kind == 0 && v.target < 4) v.target ^= 2u;
        cmd[c->viewer * 2 + a] = v;
    }
    int ok = c3_link_battle_submit(c->lb, cmd);
    c3_link_play_log(c->log, "link_submit turn=%lu ok=%d cmd=%u/%u/%u,%u/%u/%u", (unsigned long)c->lb->battle.turn + 1, ok,
        view_cmd[0].kind, view_cmd[0].slot, view_cmd[0].target, view_cmd[1].kind, view_cmd[1].slot, view_cmd[1].target);
    return ok;
}

static int hook_turn_ready(void *ctx, C3GeneralBattle *view)
{
    LinkCtx *c = ctx;
    if (!c->lb->new_turn) return 0;
    c->lb->new_turn = 0;
    log_turn(c);
    make_view(c, view);
    return 1;
}

int c3_link_play_battle(C3LinkSession *s, C3LinkMode mode, C3LinkBattleType type, unsigned rule, uint32_t seed,
                        const uint8_t party[6][C3_GB_POKEMON_SIZE], int autoplay, FILE *log, char *message, size_t capacity)
{
    static C3BattlePack pack;
    static C3LinkBattle lb;
    static C3GeneralBattle view;
    char error[192] = "";
    if (message && capacity) message[0] = 0;
    if (!c3_battle_pack_load(&pack, C3_BATTLE_PACK_PATH, error)) { c3_link_play_log(log, "link_pack_failed error=%s", error); return 1; }
    c3_link_play_log(log, "link_pack hash=%08lX encounters=%lu", (unsigned long)c3_link_pack_hash(&pack), (unsigned long)pack.encounters);
    if (s->is_host) {
        if (!seed) seed = (uint32_t)osGetTime() * 2654435761u;
        if (!c3_link_battle_host(&lb, s, &pack, mode, type, rule % 3, 0, c3_link_default_stage(&pack), seed)) {
            c3_link_play_log(log, "link_battle_setup_failed error=%s", lb.error);
            if (message) snprintf(message, capacity, "%s", lb.error);
            return 1; }
    } else c3_link_battle_join(&lb, s, &pack);
    if (mode == C3_LINK_MODE_COLOSSEUM) {
        uint8_t team[6 * C3_GB_POKEMON_SIZE];
        if (!party) return 1;
        /* The client learns the rule from the setup: wait for it first. */
        while (!s->is_host && lb.phase == C3_LB_SETUP) {
            if (!c3_link_play_pump(s)) return 1;
            if (!c3_link_battle_update(&lb)) { c3_link_play_log(log, "link_battle_failed error=%s", lb.error); return 1; }
        }
        unsigned most = c3_link_team_count(mode, (C3LinkBattleType)lb.type, lb.rule), least = c3_link_team_minimum(mode, (C3LinkBattleType)lb.type, lb.rule);
        unsigned got = c3_link_pick_team(&pack, &c3_link_rule_presets[lb.rule], (const uint8_t (*)[C3_GB_POKEMON_SIZE])party, most, team);
        if (!c3_link_rules_check(&pack, &c3_link_rule_presets[lb.rule], team, got, least, most, error, sizeof error)) {
            c3_link_play_log(log, "link_team_refused rule=%u error=%s", lb.rule, error);
            c3_link_send(s, C3_LINK_BROADCAST, C3_LINK_MSG_REFUSE, error, strlen(error) + 1);
            if (message) snprintf(message, capacity, "Your team: %s", error);
            for (unsigned k = 0; k < 120; ++k) if (!c3_link_play_pump(s)) break; /* let the refusal go out */
            return 1;
        }
        c3_link_play_log(log, "link_team rule=%u count=%u", lb.rule, got);
        c3_link_battle_offer_team(&lb, team, got);
    }
    while (lb.phase == C3_LB_SETUP || lb.phase == C3_LB_TEAMS) {
        if (!c3_link_play_pump(s)) return 1;
        if (!c3_link_battle_update(&lb)) { c3_link_play_log(log, "link_battle_failed error=%s", lb.error); if (message) snprintf(message, capacity, "%s", lb.error); return 1; }
    }
    LinkCtx ctx = {&lb, log, c3_link_battle_viewer(&lb)};
    c3_link_play_log(log, "link_battle_start mode=%s type=%u rule=%u players=%u seed=%08lX stage=%lu viewer=%u positions=%u presets=%lu,%lu,%lu,%lu",
        mode == C3_LINK_MODE_BATTLE_NOW ? "battle-now" : "colosseum", lb.type, lb.rule, lb.players, (unsigned long)lb.seed,
        (unsigned long)lb.stage, ctx.viewer, c3_link_battle_local_positions(&lb),
        (unsigned long)lb.preset[0], (unsigned long)lb.preset[1], (unsigned long)lb.preset[2], (unsigned long)lb.preset[3]);
    lb.new_turn = 0;
    log_turn(&ctx);
    make_view(&ctx, &view);
    C3BattleLinkHooks hooks = {0};
    hooks.ctx = &ctx; hooks.autoplay = autoplay; hooks.pump = hook_pump; hooks.submit = hook_submit; hooks.turn_ready = hook_turn_ready;
    hooks.control_mask = (c3_link_battle_local_positions(&lb) >> (ctx.viewer * 2)) & 3u;
    char prefix[32]; snprintf(prefix, sizeof prefix, "link-%s-p%u", mode == C3_LINK_MODE_BATTLE_NOW ? "bn" : "colo", s->self_player);
    hooks.shot_prefix = prefix;
    int result;
    if (c3_battle_general_3d_ready(&view, log, error, sizeof error)) {
        c3_link_play_log(log, "link_scene 3d");
        result = c3_battle_general_3d_run_link(&pack, &view, &hooks, log);
    } else {
        /* No prepared stage for these Pokemon: play the same lockstep turns headless. */
        c3_link_play_log(log, "link_scene headless reason=%s", error);
        C3BattleRng rng; c3_battle_rng_init(&rng, 0x77u + s->self_player);
        result = 0;
        while (lb.phase != C3_LB_FINISHED && lb.phase != C3_LB_FAILED) {
            if (!hook_pump(&ctx, error, sizeof error)) break;
            if (!c3_link_play_pump(s)) { result = 1; break; }
            if (lb.new_turn) { C3GeneralBattle *tmp = &view; hook_turn_ready(&ctx, tmp); }
            if (lb.phase == C3_LB_COMMANDS) {
                C3GeneralCommand c[2]; memset(c, 0, sizeof c);
                for (unsigned a = 0; a < view.active_count; ++a) if ((hooks.control_mask >> a & 1u) && view.active[0][a] != 255 && view.party[0][view.active[0][a]].hp)
                    c3_general_battle_choose_with(&view, &pack, a, &rng, &c[a]);
                hook_submit(&ctx, c);
            }
        }
    }
    if (lb.new_turn) { lb.new_turn = 0; log_turn(&ctx); }
    c3_link_play_log(log, "link_battle_end phase=%d outcome=%d turns=%lu final_hash=%08lX scene_result=%d error=%s save_writes=0",
        lb.phase, (int)lb.battle.outcome, (unsigned long)lb.battle.turn, (unsigned long)lb.last_hash, result, lb.error);
#ifdef C3_LINK_PROBE
    c3_link_probe_shot(log, "end");
#endif
    c3_battle_pack_free(&pack);
    if (message && lb.phase == C3_LB_FAILED) snprintf(message, capacity, "%s", lb.error);
    return lb.phase == C3_LB_FINISHED ? 0 : 1;
}

#ifdef C3_LINK_PROBE
int c3_link_probe_load_party(uint8_t party[6][C3_GB_POKEMON_SIZE], FILE *log)
{
    static C3GameState state;
    char error[C3_GAME_ERROR_SIZE] = "";
    memset(party, 0, 6 * C3_GB_POKEMON_SIZE);
    if (!c3_game_state_load(&state, SAVE, NEWGAME, error)) { c3_link_play_log(log, "link_save_failed error=%s", error); return 0; }
    for (unsigned i = 0; i < 6; ++i) c3_game_state_pokemon(&state, i, party[i]);
    c3_game_state_free(&state);
    char line[128] = ""; size_t n = 0;
    for (unsigned i = 0; i < 6; ++i) if (party[i][0] | party[i][1])
        n += (size_t)snprintf(line + n, sizeof line - n, "%u/L%u ", (unsigned)(party[i][0] << 8 | party[i][1]), party[i][0x60]);
    c3_link_play_log(log, "link_party save=%s party=%s writes=0", SAVE, line);
    return 1;
}

__attribute__((weak)) int c3_link_probe_trade(C3LinkSession *s, FILE *log)
{ (void)s; c3_link_play_log(log, "link_trade_unavailable"); return 1; }

int c3_link_probe_game(C3LinkSession *s, const char *mode, FILE *log)
{
    C3LinkProbeConfig *cfg = &c3_link_probe_config;
    C3LinkBattleType type = s->player_count == 4 ? C3_LINK_MULTI : !strcmp(cfg->type, "double") ? C3_LINK_DOUBLE : C3_LINK_SINGLE;
    c3_link_play_console = 1;
    if (!strcmp(mode, "battle-now")) return c3_link_play_battle(s, C3_LINK_MODE_BATTLE_NOW, type, cfg->rule, cfg->seed, NULL, 1, log, NULL, 0);
    if (!strcmp(mode, "colosseum")) {
        uint8_t party[6][C3_GB_POKEMON_SIZE];
        if (!c3_link_probe_load_party(party, log)) return 1;
        return c3_link_play_battle(s, C3_LINK_MODE_COLOSSEUM, type, cfg->rule, cfg->seed, (const uint8_t (*)[C3_GB_POKEMON_SIZE])party, 1, log, NULL, 0);
    }
    if (!strcmp(mode, "trade")) return c3_link_probe_trade(s, log);
    c3_link_play_log(log, "link_probe_mode_unknown mode=%s", mode);
    return 1;
}
#endif
