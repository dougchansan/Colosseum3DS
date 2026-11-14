#include "battle_general_3d.h"
#include "battle_general_scene.h"
#include "battle_general_stage.h"
#include "battle_audio_player.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "colosseum3ds/battle3d_index.h"
#include "colosseum3ds/battle_general_view.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define EFFECTS_ROOT "sdmc:/3ds/Colosseum3DS/boot/battle"
#define HERO_TRAINER 1u /* Wes: trainer row 1, as in the encounter-10 scene */

static void opening_cast(const C3GeneralBattle *b, C3BattleGeneralCast *c)
{
    memset(c, 0, sizeof *c);
    c->floor = b->info.battle_floor;
    c->active_per_side = b->active_count;
    c->trainer[0] = HERO_TRAINER;
    c->trainer[1] = b->info.trainer_id;
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
    s->sound_cue = audio ? c3_battle_audio_player_cue : silent;
    if (trace) { fprintf(trace, "general_battle_stage_open us=%llu\n", (unsigned long long)(c3_n3ds_time_us() - start));
        c3_n3ds_log_memory(trace, "general_battle_stage"); fflush(trace); }
    return 1;
}

int c3_battle_general_3d_run(const C3BattlePack *pack, C3GeneralBattle *b, FILE *trace)
{
    C3Battle3dIndex index; C3BattleGeneralCast cast; C3GeneralView view; C3GeneralUi ui; C3N3dsInput input = {0};
    char error[192] = "";
    static C3BattlePreviewEvent batch[C3_PREVIEW_EVENT_LIMIT];
    C3BattleGeneralStage *stage = calloc(1, sizeof *stage);
    int result = -1, audio = 0, ended = 0;
    unsigned frames = 0;
    memset(&view, 0, sizeof view); memset(&ui, 0, sizeof ui);
    ui.page = C3_GUI_EVENTS;
    if (!stage) return -1;
    if (!c3_battle3d_index_load(&index, C3_BATTLE3D_ROOT "/index.c3n", error, sizeof error)) { free(stage); return -1; }
    opening_cast(b, &cast);
    if (trace) { fprintf(trace, "general_battle_begin encounter=%lu trainer=%s/%s active=%u hero_party=%u foe_party=%u scene=3d floor=%u\n",
        (unsigned long)b->encounter, b->info.title, b->info.trainer, b->active_count, b->party_count[0], b->party_count[1], cast.floor); fflush(trace); }
    if (c3_battle_audio_player_open(EFFECTS_ROOT, error, sizeof error)) {
        audio = 1; c3_render_recovery_set_pause_callback(c3_battle_audio_player_pause);
    } else {
        if (trace) { fprintf(trace, "battle_audio_unavailable error=%s playing_silently=1\n", error); fflush(trace); }
        error[0] = 0;
    }
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
                    if (close_stage(stage, trace, frames)) { result = 1; goto done; }
                    if (!open_stage(stage, &index, &next, pack, hp, audio, trace, error, sizeof error)) goto done;
                    previous = c3_n3ds_active_time_us();
                }
                if (!c3_battle_general_stage_submit(stage, batch, n)) { snprintf(error, sizeof error, "Invalid battle event batch."); goto done; }
                count = n; dirty = 1;
            } else if (b->outcome != C3_GB_RUNNING) { ui.page = C3_GUI_RESULT; dirty = 1; }
            else { c3_general_ui_commands(&ui, b); dirty = 1; }
        }
        uint16_t pressed = input.pressed;
        /* Hold A or B to hurry the messages along. */
        stage->message_ms = (input.held & (C3_PAD_A | C3_PAD_B)) && ui.page == C3_GUI_EVENTS ? 300 : 1100;
        if (ui.page == C3_GUI_RESULT) {
            if (pressed & C3_PAD_A) { ended = 1; result = 0; break; }
        } else if (ui.page != C3_GUI_EVENTS && pressed) {
            dirty = 1;
            if (c3_general_ui_press(&ui, pack, b, pressed, trace)) { c3_general_view_restart(&view, b); count = 0; }
        }
        if (!c3_battle_general_stage_step(stage, ms, error, sizeof error)) goto done;
        uint32_t cursor = stage->presentation.cursor < count ? stage->presentation.cursor : count ? count - 1 : 0;
        uint32_t now_shown = count ? first + cursor : UINT32_MAX;
        if (now_shown != shown || ui.page != drawn_page) { shown = now_shown; dirty = 1; }
        if (dirty) {
            c3_general_ui_draw(pack, b, &ui, shown < b->event_count ? &b->events[shown] : NULL);
            drawn_page = ui.page; dirty = 0;
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
        } else ++frames;
    }
    if (!ended) result = 1;
done:
    if (error[0] && trace) { fprintf(trace, "general_battle_3d_failed error=%s turn=%lu\n", error, (unsigned long)b->turn); fflush(trace); }
    if (close_stage(stage, trace, frames)) result = 1;
    c3_render_recovery_set_pause_callback(NULL);
    if (audio) c3_battle_audio_player_close();
    c3_battle3d_index_free(&index);
    free(stage);
    if (input.exit_requested || !aptMainLoop()) result = 1;
    if (trace) { fprintf(trace, "general_battle_end encounter=%lu outcome=%d turns=%lu scene=3d result=%d frames=%u\n",
        (unsigned long)b->encounter, (int)b->outcome, (unsigned long)b->turn, result, frames); fflush(trace); }
    return result;
}
