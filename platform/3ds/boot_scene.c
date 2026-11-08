#include "boot_scene.h"
#include "field_scene.h"
#include "game_menu.h"
#include "bottom_screen.h"
#include "movie_player.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "title_renderer.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/movie.h"
#include "colosseum3ds/title.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif

#define BOOT_ROOT "sdmc:/3ds/Colosseum3DS/boot/"
#define SAVE_PATH "sdmc:/3ds/Colosseum3DS/saves/pokemon_colosseum.gci"
#define GAME_DATA BOOT_ROOT "newgame.c3g"

typedef enum {
    TITLE_INTRO, TITLE_SKIP_OUT, TITLE_SKIP_IN, TITLE_WAIT_START,
    TITLE_ACCEPTED, TITLE_MENU
} TitlePhase;

bool c3_boot_scene_available(void)
{
#ifdef C3_BOOT_ONLY
    return true;
#else
    struct stat entry;
    return stat(BOOT_ROOT, &entry) == 0 && S_ISDIR(entry.st_mode);
#endif
}

static int file_exists(const char *path)
{
    struct stat entry;
    return stat(path, &entry) == 0 && S_ISREG(entry.st_mode);
}

static const char *phase_name(TitlePhase phase)
{
    switch (phase) {
    case TITLE_INTRO: return "title_intro";
    case TITLE_SKIP_OUT: return "title_skip_out";
    case TITLE_SKIP_IN: return "title_skip_in";
    case TITLE_WAIT_START: return "title_wait_start";
    case TITLE_ACCEPTED: return "title_start_accepted";
    case TITLE_MENU: return "main_menu";
    }
    return "invalid";
}

static void load_card(C3GameState *state, C3GameMenuSave *view,
                      C3GameMenuOptions *options, FILE *trace)
{
    C3GameSummary summary;
    char error[C3_GAME_ERROR_SIZE] = {0};
    memset(view, 0, sizeof(*view));
    view->present = file_exists(SAVE_PATH) || file_exists(SAVE_PATH ".bak");
    if (!view->present) {
        snprintf(view->reason, sizeof(view->reason), "No saved game on the SD card.");
        if (trace) { fprintf(trace, "game_load status=absent\n"); fflush(trace); }
        return;
    }
    const char *prepared = file_exists(GAME_DATA) ? GAME_DATA : NULL;
    if (!c3_game_state_load(state, SAVE_PATH, prepared, error) ||
        !c3_game_state_summary(state, &summary)) {
        snprintf(view->reason, sizeof(view->reason), "%.127s", error);
        if (trace) { fprintf(trace, "game_load status=invalid reason=%s\n", error); fflush(trace); }
        return;
    }
    view->valid = 1;
    view->can_continue = c3_field_scene_supports(summary.floor) && prepared != NULL;
    snprintf(view->name, sizeof(view->name), "%s", summary.name);
    snprintf(view->location, sizeof(view->location), "%s", summary.floor == 1 ? "OUTSKIRT STAND" : summary.floor == 2 ? "OUTSKIRT STAND - INSIDE" : "Unported area");
    view->money = summary.money;
    view->play_seconds = summary.play_seconds;
    if (!prepared)
        snprintf(view->reason, sizeof(view->reason), "Prepare boot/newgame.c3g before continuing.");
    else if (!view->can_continue)
        snprintf(view->reason, sizeof(view->reason), "Saved floor %lu is not available in this build.", (unsigned long)summary.floor);
    else if (summary.used_backup)
        snprintf(view->reason, sizeof(view->reason), "Recovered the verified backup save.");
    options->sound_mono = summary.sound_mono;
    options->rumble_disabled = summary.rumble_disabled;
    if (trace) {
        fprintf(trace, "game_load status=ok name=%s floor=%lu previous=%lu spawn=%u count=%lu backup=%u supported=%d\n",
                summary.name, (unsigned long)summary.floor, (unsigned long)summary.previous_floor,
                summary.spawn, (unsigned long)summary.saved_count, summary.used_backup, view->can_continue);
        fflush(trace);
    }
}

/* Return 0 with a prepared in-memory game, 1 on application exit, -1 on
 * rendering/asset failure. Native menus borrow the live title background. */
static int run_title(FILE *trace, C3GameState *state,
                      int *new_game, int start_at_menu, C3GameMenuOptions *session_options,
                      int *options_edited, char *error, size_t capacity)
{
    C3Title *title = calloc(1, sizeof(*title));
    C3GameMenu menu = {0};
    if(trace)fprintf(trace,"bottom_ui original_assets=%d format=C3UI fallback=libctru\n",c3_bottom_screen_has_original_assets());
    C3GameMenuOptions options = *session_options;
    C3N3dsInput input = {0};
    C3TitleRenderStats stats = {0};
    TitlePhase phase = start_at_menu ? TITLE_ACCEPTED : TITLE_INTRO;
    TitlePhase previous_phase = phase;
    C3GameMenuPage previous_menu_page = C3_MENU_MAIN;
    unsigned clip = start_at_menu ? 2u : 0u, frames = 0;
    float clip_seconds = 0.0f, phase_seconds = start_at_menu ? 1.0f : 0.0f;
    uint64_t previous, next_status = 0;
    int menu_open = 0, result = -1;
    if (!title) { snprintf(error, capacity, "Title allocation failed."); return -1; }
    if (!c3_title_load(title, BOOT_ROOT "title.c3t", error, capacity)) goto done;
    if (title->clip_count != 3 || !(title->fps > 0.0f)) {
        snprintf(error, capacity, "The title pack must contain the three original clips.");
        goto done;
    }
    if (!c3_title_animate(title, clip, 0.0f) || c3_title_renderer_init(title, error, capacity)) goto done;
    if (trace) {
        fprintf(trace, "title_begin vertices=%lu batches=%lu textures=%lu bones=%lu fps=%.3f\n",
                (unsigned long)title->vertex_count, (unsigned long)title->batch_count,
                (unsigned long)title->texture_count, (unsigned long)title->bone_count, (double)title->fps);
        fflush(trace);
    }
    previous = c3_n3ds_active_time_us();
    result = 1;
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_active_time_us();
        float delta = (float)(now - previous) / 1000000.0f;
        previous = now;
        clip_seconds += delta;
        phase_seconds += delta;
        float fade = 1.0f;
        if (phase == TITLE_INTRO && (input.pressed & (C3_PAD_A | C3_PAD_B | C3_PAD_START))) {
            phase = TITLE_SKIP_OUT;
            phase_seconds = 0.0f;
        }
        if (phase == TITLE_INTRO) {
            if (clip == 0 && clip_seconds < 0.5f) fade = clip_seconds * 2.0f;
            float duration = (float)(title->clips[clip].frame_count - 1u) / title->fps;
            if (clip_seconds >= duration) {
                clip_seconds -= duration;
                ++clip;
                phase_seconds = 0.0f;
                if (clip == 2) phase = TITLE_WAIT_START;
            }
        } else if (phase == TITLE_SKIP_OUT) {
            fade = 1.0f - phase_seconds * 2.0f;
            if (phase_seconds >= 0.5f) {
                clip = 2; clip_seconds = phase_seconds = 0.0f;
                phase = TITLE_SKIP_IN; fade = 0.0f;
            }
        } else if (phase == TITLE_SKIP_IN) {
            fade = phase_seconds * 2.0f;
            if (phase_seconds >= 0.5f) {
                phase = TITLE_WAIT_START; phase_seconds = 0.0f; fade = 1.0f;
            }
        } else if (phase == TITLE_WAIT_START && (input.pressed & (C3_PAD_A | C3_PAD_START))) {
            phase = TITLE_ACCEPTED; phase_seconds = 0.0f;
        } else if (phase == TITLE_ACCEPTED && phase_seconds >= 1.0f) {
            C3GameMenuSave view;
            C3GameMenuOptions saved_options = {0};
            load_card(state, &view, &saved_options, trace);
            if (!*options_edited) options = *session_options = saved_options;
            /* END applies options to this session. Returning through the
             * title may reload the saved game but must not undo that choice.
             * The explicit in-game Save persists it in the original body. */
            if (state->impl) c3_game_state_set_options(state, (uint8_t)options.sound_mono,
                                                      (uint8_t)options.rumble_disabled);
            c3_movie_player_set_mono(options.sound_mono);
            if (menu_open) c3_game_menu_close(&menu);
            c3_game_menu_init(&menu, &view, &options);
            menu_open = 1;
            char text_error[C3_GAME_ERROR_SIZE] = {0};
            if (!c3_game_menu_load_text(&menu, BOOT_ROOT "menu.c3u", text_error, sizeof(text_error)) && trace)
                fprintf(trace, "menu_text fallback=%s\n", text_error);
            phase = TITLE_MENU; phase_seconds = 0.0f;
            previous_menu_page = C3_MENU_MAIN;
        } else if (phase == TITLE_MENU) {
            C3GameMenuEvent event = c3_game_menu_update(&menu, &input, delta);
            if (event.kind == C3_MENU_EVENT_RETURN_TITLE) {
                phase = TITLE_WAIT_START; phase_seconds = 0.0f;
            } else if (event.kind == C3_MENU_EVENT_OPTIONS_CHANGED) {
                options = event.options;
                *session_options = options;
                *options_edited = 1;
                c3_movie_player_set_mono(options.sound_mono);
                if (state->impl) c3_game_state_set_options(state, (uint8_t)options.sound_mono,
                                                          (uint8_t)options.rumble_disabled);
                if (trace) {
                    fprintf(trace, "game_options sound_mono=%u rumble_disabled=%u persist=next_game_save\n",
                            options.sound_mono, options.rumble_disabled);
                    fflush(trace);
                }
            } else if (event.kind == C3_MENU_EVENT_NEW_GAME) {
                C3Movie opening = {0};
                uint32_t new_game_seed = (uint32_t)c3_n3ds_time_us();
                char game_error[C3_GAME_ERROR_SIZE] = {0};
                if (c3_movie_open(&opening, BOOT_ROOT "openingdemo.c3v", game_error, sizeof(game_error))) {
                    c3_game_menu_set_error(&menu, game_error);
                } else {
                    c3_movie_close(&opening);
                    if (!c3_field_scene_available()) {
                        c3_game_menu_set_error(&menu, "Prepare the OUTSKIRT STAND assets before New Game.");
                    } else if (c3_game_state_new(state, event.name, new_game_seed, GAME_DATA, game_error)) {
                        c3_game_state_set_options(state, (uint8_t)options.sound_mono, (uint8_t)options.rumble_disabled);
                        *new_game = 1; result = 0;
                        if (trace) {
                            C3GameSummary summary;
                            c3_game_state_summary(state, &summary);
                            fprintf(trace, "game_new name=%s seed=%08lx trainer_id=%08lx money=%lu party=%u replace_authorized=%d disk_write=0\n",
                                    summary.name, (unsigned long)new_game_seed, (unsigned long)summary.trainer_id, (unsigned long)summary.money,
                                    summary.party_count, event.replace_authorized);
                            fflush(trace);
                        }
                        break;
                    } else c3_game_menu_set_error(&menu, game_error);
                }
            } else if (event.kind == C3_MENU_EVENT_CONTINUE) {
                if (!c3_field_scene_available()) {
                    c3_game_menu_set_error(&menu, "Prepare the OUTSKIRT STAND assets before continuing.");
                } else { *new_game = 0; result = 0; break; }
            }
            if (menu.page != previous_menu_page) {
                if (trace) {
                    fprintf(trace, "menu_page name=%s input=%04x\n", c3_game_menu_page_name(menu.page), input.pressed);
                    fflush(trace);
                }
                previous_menu_page = menu.page;
            }
        }
        if (phase != previous_phase) {
            if (trace) {
                fprintf(trace, "title_state name=%s clip=%u frame=%u input=%04x\n",
                        phase_name(phase), clip, frames, input.pressed);
                fflush(trace);
            }
            previous_phase = phase; next_status = 0;
        }
        if (!c3_title_animate(title, clip, clip_seconds)) {
            snprintf(error, capacity, "Title animation sample failed."); result = -1; break;
        }
        if (phase == TITLE_MENU) c3_game_menu_draw(&menu);
        else if (now >= next_status) {
            c3_bottom_screen_title(phase == TITLE_WAIT_START,phase == TITLE_ACCEPTED);
            next_status = now + UINT64_C(250000);
        }
        int draw_result = c3_title_renderer_draw(title, fade, &stats, error, capacity);
        if (draw_result == C3_RENDER_BUSY) {
            C3RenderRecoveryResult recovery = c3_render_recover(trace, "title", frames, C3_RECOVER_WAIT);
            if (recovery == C3_RECOVERY_EXIT) { result = 1; break; }
            if (recovery == C3_RECOVERY_RESUME_PAUSED) {
                previous = c3_n3ds_active_time_us();
                next_status = 0;
                menu.dirty = 1;
            }
            continue;
        }
        if (draw_result) { result = -1; break; }
        ++frames;
    }
    if (trace) {
        fprintf(trace, "title_end result=%d frames=%u state=%s clip=%u\n", result, frames, phase_name(phase), clip);
        fflush(trace);
    }
done:
    if (c3_render_recover(trace, "title", frames,
                          result == 1 ? C3_RECOVER_EXIT : C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT)
        result = 1;
    if (menu_open) c3_game_menu_close(&menu);
    c3_title_close(title);
    free(title);
    return result;
}

int c3_boot_scene_run(int core_status)
{
    char error[C3_GAME_ERROR_SIZE] = {0};
    C3GameState state = {0};
    C3GameMenuOptions session_options = {0};
    int options_edited = 0;
    c3_movie_player_set_mono(0);
    FILE *trace = fopen("sdmc:/3ds/Colosseum3DS/boot-trace.log", "w");
    if (trace) { fprintf(trace, "boot_build %s core=%d\n", C3_BUILD_ID, core_status); fflush(trace); }
    c3_n3ds_log_memory(trace, "boot_start");
    int result = c3_movie_player_run(BOOT_ROOT "tpc.c3v", "tpc", trace, error, sizeof(error));
    if (!result) result = c3_movie_player_run(BOOT_ROOT "gs_logo.c3v", "gs_logo", trace, error, sizeof(error));
    int from_game = 0;
    while (!result) {
        int new_game = 0;
        result = run_title(trace, &state, &new_game, from_game,
                           &session_options, &options_edited, error, sizeof(error));
        if (result) break;
        if (new_game) {
            uint64_t before = c3_n3ds_active_time_us();
            if (trace) { fprintf(trace, "game_link floor=928 name=openingdemo\n"); fflush(trace); }
            result = c3_movie_player_run(BOOT_ROOT "openingdemo.c3v", "openingdemo", trace, error, sizeof(error));
            c3_game_state_add_play_time(&state, (double)(c3_n3ds_active_time_us() - before) / 1000000.0);
            if (result) break;
            /* movie.c fn_80035E04 sets this flag before floorLink(1,0).
             * Field entry owns its remaining scripts and resume policy. */
            if (!c3_game_state_set_flag(&state, 0x08D0, 1)) {
                snprintf(error, sizeof(error), "The opening story flag could not be set."); result = -1; break;
            }
            if (trace) { fprintf(trace, "opening_complete flag=08d0 floor=1 spawn=0 fade_script=05960008\n"); fflush(trace); }
        }
        result = c3_field_scene_run(&state, new_game, SAVE_PATH, trace, error, sizeof(error));
        C3GameSummary current_options;
        if(c3_game_state_summary(&state,&current_options)){
            if(session_options.sound_mono!=current_options.sound_mono||session_options.rumble_disabled!=current_options.rumble_disabled)options_edited=1;
            session_options.sound_mono=current_options.sound_mono;
            session_options.rumble_disabled=current_options.rumble_disabled;
            c3_movie_player_set_mono(session_options.sound_mono);
        }
        from_game = 1;
    }
    c3_game_state_free(&state);
    if (trace) { fprintf(trace, "boot_end result=%d error=%s\n", result, error); fclose(trace); }
    if (result < 0) {
        c3_bottom_screen_notice("Unable to continue",error,"START: exit");
        c3_n3ds_wait_for_exit();
    }
    return result < 0 || core_status != 0 ? 1 : 0;
}
