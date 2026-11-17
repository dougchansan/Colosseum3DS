#include "battle_general_3d.h"
#include "battle_general_scene.h"
#include "battle_general_stage.h"
#include "move_forget_scene.h"
#include "battle_audio_player.h"
#include "sfx_player.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "colosseum3ds/battle3d_index.h"
#include "colosseum3ds/battle_general_view.h"
#include "colosseum3ds/game_state.h"
#include "trace_writer.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define EFFECTS_ROOT "sdmc:/3ds/Colosseum3DS/boot/battle"
#define HERO_TRAINER 1u /* Wes: trainer row 1, as in the encounter-10 scene */

static void opening_cast(const C3GeneralBattle *b, C3BattleGeneralCast *c)
{
    memset(c, 0, sizeof *c);
    c->floor = b->info.floor_data_id; /* battle-floor row; battle_floor is its field id */
    c->active_per_side = b->active_count;
    c->trainer[0] = HERO_TRAINER;
    c->trainer[1] = b->info.trainer_id;
    c->intro_wzx = (uint16_t)(b->info.intro_wzx <= 0xFFFF ? b->info.intro_wzx : 0);
    c->ball_mask = 1u << 4;
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned k = 0; k < b->party_count[side]; ++k) { unsigned ball = b->party[side][k].ball; if (ball >= 1 && ball <= 12) c->ball_mask |= (uint16_t)(1u << ball); }
    /* The species each position shows first: the log's first send-out there
     * (a turn log may switch a position), else the active Pokemon. */
    for (unsigned pos = 0; pos < 4; ++pos) {
        unsigned side = pos / 2, slot = pos % 2;
        uint8_t m = slot < b->active_count ? b->active[side][slot] : 255;
        c->species[pos] = m != 255 ? b->party[side][m].species : 0;
    }
    for (uint32_t i = 0; i < b->event_count; ++i) {
        unsigned pos; uint16_t species;
        if (c3_general_view_send_out(b, i, &pos, &species)) {
            uint32_t j;
            for (j = 0; j < i; ++j) { unsigned p2; if (c3_general_view_send_out(b, j, &p2, NULL) && p2 == pos) break; }
            if (j == i) c->species[pos] = species;
        }
    }
}

int c3_battle_general_3d_ready(const C3GeneralBattle *b, FILE *trace, char *why, size_t cap)
{
    C3Battle3dIndex index; C3BattleGeneralCast cast; char error[160] = "";
    if (why && cap) why[0] = 0;
    if (!c3_battle3d_index_load(&index, C3_BATTLE3D_ROOT "/index.c3n", error, sizeof error)) {
        if (why) snprintf(why, cap, "%s", error);
        return 0;
    }
    opening_cast(b, &cast);
    const char *missing = c3_battle_general_stage_missing(&index, &cast);
    c3_battle3d_index_free(&index);
    if (missing && why) snprintf(why, cap, "no prepared 3D %s (floor %u species %u,%u,%u,%u trainer %u)", missing,
                                 cast.floor, cast.species[0], cast.species[1], cast.species[2], cast.species[3], cast.trainer[1]);
    (void)trace;
    return !missing;
}

static int sfx_open_ok, cue_audio;
/* Every game sound request from the full pack (sfx.c3s); the encounter-10
 * cue pack only for ids that pack lacks. */
static int battle_cue(uint32_t cue, uint32_t sound, char *e, size_t n)
{
    if (sfx_open_ok && c3_sfx_has(sound)) { c3_sfx_play(sound, 127); return 1; }
    if (cue_audio) return c3_battle_audio_player_cue(cue, sound, e, n);
    return 1;
}
static int battle_cue_pan(uint32_t sound, float pan)
{
    if (!sfx_open_ok || !c3_sfx_has(sound)) return 0;
    c3_sfx_play_pan(sound, 127, pan);
    return 1;
}
static int autoplay; /* unattended probe: the hero uses the opponent policy */
static unsigned hyper_shot_at; /* probe: frame to capture a Hyper Mode entry at */
static char story_prefix[48];
void c3_battle_general_3d_set_autoplay(int on, const char *prefix)
{
    autoplay = on ? 1 : 0;
    snprintf(story_prefix, sizeof story_prefix, "%s", on && prefix ? prefix : "");
}
/* Probe hero: the best ball (Master, Ultra, Great, then the rest) at a
 * Shadow Pokemon it may snag, else the opponent policy. */
static void probe_hero_command(C3GeneralBattle *b, const C3BattlePack *pack, unsigned a, C3GeneralCommand *c)
{
    static const uint8_t order[12] = {1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 5, 7};
    for (unsigned k = 0; k < 12; ++k)
        for (unsigned t = 2; t < 2u + b->active_count; ++t)
            if (c3_general_battle_can_snag(b, pack, a, order[k], t)) { c->kind = 2; c->slot = order[k]; c->target = (uint8_t)t; return; }
    c3_general_battle_choose(b, pack, a, c);
}
int c3_battle_general_autoplaying(void) { return autoplay != 0; }
void c3_battle_general_probe_commands(C3GeneralBattle *b, const C3BattlePack *pack, C3GeneralCommand c[2])
{
    memset(c, 0, 2 * sizeof *c);
    for (unsigned a = 0; a < b->active_count; ++a)
        if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp) probe_hero_command(b, pack, a, &c[a]);
}

static int silent(uint32_t cue, uint32_t sound, char *e, size_t n) { (void)cue; (void)sound; (void)e; (void)n; return 1; }

/* Drain the GPU, then free the stage (it may hold in-flight GPU buffers). */
static int close_stage(C3BattleGeneralStage *s, FILE *trace, unsigned frames)
{
    int exit_requested = 0;
    if (!s->initialized) return 0;
    if (c3_render_recover(trace, "general_battle", frames, C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT) exit_requested = 1;
    while (!c3_battle_general_stage_close(s))
        if (c3_render_recover(trace, "general_battle_teardown", frames, C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT) exit_requested = 1;
    return exit_requested;
}

static int open_stage(C3BattleGeneralStage *s, const C3Battle3dIndex *index, const C3BattleGeneralCast *cast,
                      const C3BattlePack *pack, const uint16_t hp[4], int audio, FILE *trace, char *error, size_t cap)
{
    uint64_t start = c3_n3ds_time_us();
    c3_bottom_screen_notice("BATTLE", "Loading the battle stage...", "");
    if (!c3_battle_general_stage_load(s, index, cast, pack, C3_BATTLE3D_ROOT, EFFECTS_ROOT, hp, trace, error, cap)) return 0;
    s->sound_cue = audio || sfx_open_ok ? battle_cue : silent;
    s->sound_cue_pan = battle_cue_pan; s->sound_owner = -1;
    if (trace) { fprintf(trace, "general_battle_stage_open us=%llu\n", (unsigned long long)(c3_n3ds_time_us() - start));
        c3_n3ds_log_memory(trace, "general_battle_stage"); fflush(trace); }
    return 1;
}

static int run(const C3BattlePack *pack, C3GeneralBattle *b, C3BattleLinkHooks *hooks, FILE *trace);
/* Story battles: the writeback that follows asks the forget-a-move prompt
 * (colosseum3ds/move_forget.h); it stays bound for purification too. */
int c3_battle_general_3d_run(const C3BattlePack *pack, C3GeneralBattle *b, FILE *trace)
{ c3_move_forget_scene_bind(trace); return run(pack, b, NULL, trace); }
int c3_battle_general_3d_run_link(const C3BattlePack *pack, C3GeneralBattle *view, C3BattleLinkHooks *hooks, FILE *trace)
{
    int saved = autoplay;
    autoplay = hooks && hooks->autoplay;
    int r = run(pack, view, hooks, trace);
    autoplay = saved;
    return r;
}

/* Link: send this console's commands and wait on C3_GUI_WAIT. */
static void link_submit(C3BattleLinkHooks *hooks, C3GeneralUi *ui, const C3GeneralCommand c[2])
{
    hooks->submit(hooks->ctx, c);
    ui->page = C3_GUI_WAIT;
}

static int run(const C3BattlePack *pack, C3GeneralBattle *b, C3BattleLinkHooks *hooks, FILE *trace)
{
    C3Battle3dIndex index; C3BattleGeneralCast cast; C3GeneralView view; C3GeneralUi ui; C3N3dsInput input = {0};
    char error[192] = "";
    static C3BattlePreviewEvent batch[C3_PREVIEW_EVENT_LIMIT];
    C3BattleGeneralStage *stage = calloc(1, sizeof *stage);
    int result = -1, audio = 0, ended = 0;
    unsigned frames = 0;
    memset(&view, 0, sizeof view); memset(&ui, 0, sizeof ui);
    ui.page = C3_GUI_EVENTS;
    if (hooks) { ui.control_mask = hooks->control_mask; ui.defer = 1; }
    C3BattleRng local_rng; c3_battle_rng_init(&local_rng, 0x1u + (uint32_t)c3_n3ds_time_us());
    if (!stage) return -1;
    if (!c3_battle3d_index_load(&index, C3_BATTLE3D_ROOT "/index.c3n", error, sizeof error)) { free(stage); return -1; }
    opening_cast(b, &cast);
    if (trace) { fprintf(trace, "general_battle_begin encounter=%lu trainer=%s/%s active=%u hero_party=%u foe_party=%u scene=3d floor=%u\n",
        (unsigned long)b->encounter, b->info.title, b->info.trainer, b->active_count, b->party_count[0], b->party_count[1], cast.floor); fflush(trace); }
    sfx_open_ok = c3_sfx_open(C3_BATTLE3D_ROOT "/sfx.c3s", error, sizeof error);
    if (!sfx_open_ok && trace) { fprintf(trace, "battle_sfx_unavailable error=%s\n", error); fflush(trace); }
    error[0] = 0;
    if (c3_battle_audio_player_open(EFFECTS_ROOT, error, sizeof error)) {
        audio = 1; cue_audio = 1; c3_render_recovery_set_pause_callback(c3_battle_audio_player_pause);
    } else {
        if (trace) { fprintf(trace, "battle_audio_unavailable error=%s playing_silently=1\n", error); fflush(trace); }
        error[0] = 0;
    }
    C3BattleMusic music; c3_battle_music_begin(&music, &b->info, trace);
    c3_general_view_restart(&view, b);
    uint16_t hp[4]; memcpy(hp, view.hp, sizeof hp);
    if (!open_stage(stage, &index, &cast, pack, hp, audio, trace, error, sizeof error)) goto done;
    uint32_t first = 0, count = 0, shown = UINT32_MAX;
    C3GeneralUiPage drawn_page = C3_GUI_RESULT;
    int dirty = 1;
    uint64_t previous = c3_n3ds_active_time_us();
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_active_time_us();
        uint64_t delta = (now - previous) / 1000u; previous += delta * 1000u;
        uint32_t ms = delta > 1000 ? 1000 : (uint32_t)delta;
        if (hooks) {
            if (!hooks->pump(hooks->ctx, error, sizeof error)) { ended = 1; result = 0; goto done; }
            if (ui.page == C3_GUI_WAIT && hooks->turn_ready(hooks->ctx, b)) {
                ui.page = C3_GUI_EVENTS; c3_general_view_restart(&view, b); count = 0; dirty = 1;
            }
        }
        /* Feed the next part of the log once the stage has shown the last. */
        if (ui.page == C3_GUI_EVENTS && c3_battle_general_stage_idle(stage)) {
            uint32_t n = c3_general_view_batch(&view, b, batch, C3_PREVIEW_EVENT_LIMIT, &first);
            if (n) {
                unsigned pos; uint16_t species;
                if (c3_general_view_send_out(b, first, &pos, &species) && species != stage->cast.species[pos]) {
                    /* A different Pokemon comes in: rebuild the stage with it
                     * (hidden until its send-out plays). */
                    C3BattleGeneralCast next = stage->cast; next.species[pos] = species;
                    memcpy(hp, view.hp, sizeof hp); hp[pos] = 0;
                    for (unsigned p = 0; p < 4; ++p) if (!stage->presentation.visible[p]) hp[p] = 0;
                    if (trace) { fprintf(trace, "general_battle_swap position=%u species=%u->%u\n", pos, stage->cast.species[pos], species); fflush(trace); }
                    uint64_t swap_start = c3_n3ds_time_us();
                    if (c3_render_recover(trace, "general_battle_swap", frames, C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT) { result = 1; goto done; }
                    if (c3_battle_general_stage_swap(stage, &index, pos, species, C3_BATTLE3D_ROOT, error, sizeof error)) {
                        if (trace) { fprintf(trace, "general_battle_swap_fast us=%llu\n", (unsigned long long)(c3_n3ds_time_us() - swap_start)); fflush(trace); }
                    } else {
                        /* Shared pack or no room: rebuild the whole stage. */
                        if (trace) { fprintf(trace, "general_battle_swap_reload reason=%s\n", error); fflush(trace); }
                        error[0] = 0;
                        if (close_stage(stage, trace, frames)) { result = 1; goto done; }
                        if (!open_stage(stage, &index, &next, pack, hp, audio, trace, error, sizeof error)) goto done;
                    }
                    previous = c3_n3ds_active_time_us();
                }
                int moves = 0;
                for (uint32_t k = 0; k < n; ++k) moves |= batch[k].kind == C3_PREVIEW_EVENT_MOVE || (batch[k].kind == C3_PREVIEW_EVENT_STATUS && batch[k].effect);
                if (moves && stage->wzx) {
                    /* This turn's move effects: load them while the GPU is idle. */
                    uint64_t prepare_start = c3_n3ds_time_us();
                    if (c3_render_recover(trace, "general_battle_effects", frames, C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT) { result = 1; goto done; }
                    if (!c3_battle_general_stage_prepare(stage, batch, n, C3_BATTLE3D_ROOT, error, sizeof error)) goto done;
                    if (trace) { fprintf(trace, "general_battle_effects_ready us=%llu\n", (unsigned long long)(c3_n3ds_time_us() - prepare_start)); fflush(trace); }
                    previous = c3_n3ds_active_time_us();
                }
                if (!c3_battle_general_stage_submit(stage, batch, n)) { snprintf(error, sizeof error, "Invalid battle event batch."); goto done; }
                count = n; dirty = 1;
            } else if (b->outcome != C3_GB_RUNNING) { ui.page = C3_GUI_RESULT; dirty = 1; }
            else {
                int any = c3_general_ui_commands(&ui, b); dirty = 1;
                if (hooks && !any) { C3GeneralCommand none[2]; memset(none, 0, sizeof none); link_submit(hooks, &ui, none); }
            }
        }
        uint16_t pressed = input.pressed;
        /* Hold A or B to hurry the messages along. */
        stage->message_ms = (input.held & (C3_PAD_A | C3_PAD_B)) && ui.page == C3_GUI_EVENTS ? 300 : 1100;
        if (autoplay && hooks && ui.page != C3_GUI_EVENTS && ui.page != C3_GUI_RESULT && ui.page != C3_GUI_WAIT) {
            C3GeneralCommand c[2]; memset(c, 0, sizeof c);
            for (unsigned a = 0; a < b->active_count; ++a)
                if ((hooks->control_mask >> a & 1u) && b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp)
                    c3_general_battle_choose_with(b, pack, a, &local_rng, &c[a]);
            link_submit(hooks, &ui, c); dirty = 1;
        } else if ((autoplay || b->info.cpu_side0) && !hooks && ui.page != C3_GUI_EVENTS && ui.page != C3_GUI_RESULT) {
            /* A CPU trainer on side 0 (encounter 525): the engine's trainer AI
             * picks both sides; the hero watches. */
            C3GeneralCommand c[2]; memset(c, 0, sizeof c);
            for (unsigned a = 0; a < b->active_count; ++a)
                if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp) probe_hero_command(b, pack, a, &c[a]);
            if (!c3_general_battle_turn(b, pack, c, error)) goto done;
            if (trace) { fprintf(trace, "general_battle_turn turn=%lu events=%lu outcome=%d autoplay=1\n",
                (unsigned long)b->turn, (unsigned long)b->event_count, (int)b->outcome); fflush(trace); }
            for (uint32_t k = 0; k < b->event_count; ++k) if (b->events[k].hyper) {
                if (trace) { fprintf(trace, "general_battle_hyper turn=%lu event=%lu text=\"%s\"\n", (unsigned long)b->turn, (unsigned long)k, b->events[k].text); fflush(trace); }
            }
            ui.page = C3_GUI_EVENTS; c3_general_view_restart(&view, b); count = 0; dirty = 1;
        }
        if (ui.page == C3_GUI_RESULT) {
            if ((pressed & C3_PAD_A) || (autoplay && ++autoplay > 120)) { ended = 1; result = 0; break; }
        } else if (ui.page != C3_GUI_EVENTS && ui.page != C3_GUI_WAIT && pressed) {
            dirty = 1;
            int pr = c3_general_ui_press(&ui, pack, b, pressed, trace);
            if (pr == 2 && hooks) link_submit(hooks, &ui, ui.commands);
            else if (pr) { c3_general_view_restart(&view, b); count = 0; }
        }
        c3_battle_general_stage_command_phase(stage, ui.page != C3_GUI_EVENTS && ui.page != C3_GUI_WAIT);
        if (!c3_battle_general_stage_step(stage, ms, error, sizeof error)) goto done;
        uint32_t cursor = stage->presentation.cursor < count ? stage->presentation.cursor : count ? count - 1 : 0;
        uint32_t now_shown = count ? first + cursor : UINT32_MAX;
        if (now_shown != shown || ui.page != drawn_page) { shown = now_shown; dirty = 1; }
        if (dirty) {
            c3_general_ui_draw(pack, b, &ui, shown < b->event_count ? &b->events[shown] : NULL);
            drawn_page = ui.page; dirty = 0;
            /* probe: capture the Hyper Mode entry while its message is up */
            if (autoplay && !hyper_shot_at && shown < b->event_count && b->events[shown].hyper) hyper_shot_at = frames + 20u;
        }
        C3StageBattleHud hud = {0};
        if (c3_stage_renderer_set_battle_hud(&hud, error, sizeof error) != C3_RENDER_OK) goto done;
        C3TitleRenderStats stats;
        int drawn = c3_battle_general_stage_draw(stage, &stats, error, sizeof error);
        if (drawn == C3_RENDER_ERROR) goto done;
        if (drawn == C3_RENDER_BUSY) {
            C3RenderRecoveryResult r = c3_render_recover(trace, "general_battle", frames, C3_RECOVER_WAIT);
            if (r == C3_RECOVERY_EXIT) { result = 1; goto done; }
            if (r == C3_RECOVERY_RESUME_PAUSED) previous = c3_n3ds_active_time_us();
            error[0] = 0;
        } else {
            ++frames;
            if (autoplay && (frames == 240 || frames == 900 || frames == 1800 || frames == 3000 || frames == hyper_shot_at)) {
                char shot[96];
                const char *prefix = frames == hyper_shot_at ? "hyper" : hooks && hooks->shot_prefix ? hooks->shot_prefix : story_prefix[0] ? story_prefix : "general3d";
                snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/%s_%u_top.bmp", prefix, frames);
                int top = c3_n3ds_capture_top(shot) == 0;
                snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/%s_%u_bottom.bmp", prefix, frames);
                int bottom = c3_n3ds_capture_bottom(shot) == 0;
                if (trace) { fprintf(trace, "general3d_shot frame=%u top=%d bottom=%d\n", frames, top, bottom); fflush(trace); }
            }
        }
    }
    if (!ended) result = 1;
done:
    if (error[0] && trace) { fprintf(trace, "general_battle_3d_failed error=%s turn=%lu\n", error, (unsigned long)b->turn); fflush(trace); }
    if (close_stage(stage, trace, frames)) result = 1;
    c3_render_recovery_set_pause_callback(NULL);
    if (audio) { c3_battle_audio_player_close(); cue_audio = 0; }
    if (sfx_open_ok) { c3_sfx_stop_all(); c3_sfx_close(); sfx_open_ok = 0; }
    c3_battle_music_end(&music);
    c3_battle3d_index_free(&index);
    free(stage);
    if (input.exit_requested || !aptMainLoop()) result = 1;
    if (trace) { fprintf(trace, "general_battle_end encounter=%lu outcome=%d turns=%lu scene=3d result=%d frames=%u\n",
        (unsigned long)b->encounter, (int)b->outcome, (unsigned long)b->turn, result, frames); fflush(trace); }
    return result;
}

#ifdef C3_GENERAL_3D_PROBE
/* Compile-time diagnostic (-DC3_GENERAL_3D_PROBE=<encounter>): a fresh
 * game's party fights that encounter on its 3D stage with both sides on
 * the opponent policy; writes general3d-probe.log and never a save. */
int c3_battle_general_3d_probe_run(uint32_t encounter)
{
    C3GameState state; C3BattlePack pack; char error[192] = ""; int result = 1;
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE];
    memset(&state, 0, sizeof state); memset(&pack, 0, sizeof pack);
    FILE *trace = fopen("sdmc:/3ds/Colosseum3DS/general3d-probe.log", "w"); /* synchronous: survives a hang */
    if (trace) { fprintf(trace, "general3d_probe_start encounter=%lu\n", (unsigned long)encounter); fflush(trace); }
    C3GeneralBattle *b = calloc(1, sizeof *b);
    int ok = b && c3_game_state_new(&state, "WES", 0x12345678u, "sdmc:/3ds/Colosseum3DS/boot/newgame.c3g", error) &&
             c3_battle_pack_load(&pack, C3_BATTLE_PACK_PATH, error);
    for (unsigned i = 0; ok && i < C3_GB_PARTY; ++i) ok = c3_game_state_pokemon(&state, i, party[i]);
#ifdef C3_BATTLE_RULES_PROBE
    /* The lead becomes the hero's Shadow Pokemon (Shadow ID 1, full gauge)
     * knowing only Shadow Rush, so Hyper Mode can show up unattended. */
    C3ShadowInfo shadow_row;
    if (ok && c3_battle_pack_shadow(&pack, 1, &shadow_row)) {
        uint8_t *r = party[0];
        r[0xd8] = 0; r[0xd9] = 1;
        /* 70% of the gauge (dark level 2) and nature 0, so Shadow Rush
         * enters Hyper Mode by the nature row's level-2 chance. */
        uint32_t dp = (uint32_t)shadow_row.gauge * 70u;
        uint32_t pid = (uint32_t)r[4] << 24 | (uint32_t)r[5] << 16 | (uint32_t)r[6] << 8 | r[7];
        pid -= pid % 25u; r[4] = (uint8_t)(pid >> 24); r[5] = (uint8_t)(pid >> 16); r[6] = (uint8_t)(pid >> 8); r[7] = (uint8_t)pid;
        r[0xdc] = (uint8_t)(dp >> 24); r[0xdd] = (uint8_t)(dp >> 16); r[0xde] = (uint8_t)(dp >> 8); r[0xdf] = (uint8_t)dp;
        for (unsigned k = 0; k < 4; ++k) { r[0x78 + 4 * k] = 0x01; r[0x79 + 4 * k] = 0x64; r[0x7a + 4 * k] = 5; r[0x7b + 4 * k] = 0; }
        if (trace) { fprintf(trace, "battle_rules_probe shadow_lead species=%u gauge=%lu nature=%lu\n", (unsigned)(r[0] << 8 | r[1]), (unsigned long)dp,
                             (unsigned long)(((uint32_t)r[4] << 24 | (uint32_t)r[5] << 16 | (uint32_t)r[6] << 8 | r[7]) % 25u)); fflush(trace); }
    }
#endif
    if (ok) ok = c3_general_battle_begin(b, &pack, encounter, party, 0x5EED0000u ^ encounter, error);
    if (ok && !c3_battle_general_3d_ready(b, trace, error, sizeof error)) ok = 0;
    if (trace) { fprintf(trace, "general3d_probe encounter=%lu ready=%d error=%s save_writes=0\n", (unsigned long)encounter, ok, error); fflush(trace); }
    if (ok) { autoplay = 1; result = c3_battle_general_3d_run(&pack, b, trace) == 0 ? 0 : 1; }
#ifdef C3_BATTLE_RULES_PROBE
    /* The forget-a-move prompt: purification experience into the second
     * party member (four moves) up to its next level-up move, answered by a
     * script (yes, forget the first move), each page captured. */
    if (ok && (party[1][0] | party[1][1])) {
        uint8_t r[C3_GB_POKEMON_SIZE], scratch[C3_GB_POKEMON_SIZE]; C3ExpRecovery o; uint32_t add = 0;
        memcpy(r, party[1], sizeof r);
        static const uint16_t fill[4] = {1, 10, 33, 45};
        for (unsigned k = 0; k < 4; ++k) if (!(r[0x78 + 4 * k] | r[0x79 + 4 * k])) c3_battle_pack_set_move(&pack, r, k, fill[k]);
        c3_general_set_forget_prompt(NULL, NULL);
        for (uint32_t step = 0; step < 400 && !add; ++step) {
            memcpy(scratch, r, sizeof scratch);
            if (!c3_battle_pack_recover_exp(&pack, scratch, (step + 1u) * 250u, &o)) break;
            if (o.skipped_count) add = (step + 1u) * 250u;
        }
        if (trace) { fprintf(trace, "battle_rules_probe forget_setup species=%u level=%u add=%lu\n", (unsigned)(r[0] << 8 | r[1]), (unsigned)r[0x60], (unsigned long)add); fflush(trace); }
        if (add) {
            static const int answers[] = {1, 0};
            c3_move_forget_scene_bind(trace);
            c3_move_forget_scene_set_script(answers, 2, 2500);
            int learned = c3_battle_pack_recover_exp(&pack, r, add, &o);
            c3_move_forget_scene_set_script(NULL, 0, 0);
            if (trace) { fprintf(trace, "battle_rules_probe forget_done ok=%d level=%u learned=%u skipped=%u forgot0=%u move0=%u\n", learned, (unsigned)r[0x60],
                                 o.learned_count, o.skipped_count, o.learned_count ? o.forgot[o.learned_count - 1] : 0u, (unsigned)(r[0x78] << 8 | r[0x79])); fflush(trace); }
        }
    }
#endif
    if (trace) { fprintf(trace, "general3d_probe_end result=%d outcome=%d turns=%lu\n", result, b ? (int)b->outcome : 0, b ? (unsigned long)b->turn : 0ul); fclose(trace); }
    if (!ok) { consoleClear(); printf("GENERAL 3D PROBE\n\n%s\n\nSTART + SELECT: exit\n", error); c3_n3ds_wait_for_exit(); }
    free(b); c3_battle_pack_free(&pack); c3_game_state_free(&state);
    return result;
}
#endif
