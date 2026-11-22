#include "boot_scene.h"
#ifdef C3_AGENT_HARNESS
#include "agent_link.h"
#endif
#include "boot_index.h"
#include "link_play.h"
#include "trace_writer.h"
#include "field_scene.h"
#include "ending_scene.h"
#include "dna_key_scene.h"
#include "realgam_stage_probe.h"
#include "dev_floor_scene.h"
#include "game_menu.h"
#include "bottom_screen.h"
#include "movie_player.h"
#include "music_player.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "title_renderer.h"
#include "colosseum3ds/floor_profile.h"
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

/* Original .sdata starts at 3; never persisted to a save or SD config. */
static C3TitleCharacterState title_characters = {.page = 3};

static void title_characters_enter(FILE *trace)
{
    c3_title_characters_enter(&title_characters);
    if (trace) { fprintf(trace,"title_characters_enter page=%u\n",title_characters.page); fflush(trace); }
}

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
    snprintf(view->location, sizeof(view->location), "%s", summary.floor == 2 ? "OUTSKIRT STAND - INSIDE" : c3_floor_profile(summary.floor) ? c3_floor_profile(summary.floor)->label : "Unported area");
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
                      int *new_game, int *dev_floor, int *link_mode, int start_at_menu, C3GameMenuOptions *session_options,
                      int *options_edited, char *error, size_t capacity)
{
    C3Title *title = calloc(1, sizeof(*title));
    C3GameMenu menu = {0};
    C3TitleCharacters characters = {0};
    title_characters_enter(trace);
#ifdef C3_TITLE_CHARACTERS_PROBE
    unsigned probe_visits = 0;
    int probe_captured = 0, probe_early = 0, probe_menu = 0;
#endif
    if(trace){
        const C3UiAssets *ui=c3_bottom_screen_assets();const uint8_t *ball;unsigned bw,bh;
        fprintf(trace,"bottom_ui original_assets=%d format=C3UI fallback=libctru sprites=%u loading_ball=%d\n",
                c3_bottom_screen_has_original_assets(),ui?ui->sprites:0u,c3_ui_sprite_pixels(ui,C3_UI_BALL,&ball,&bw,&bh));
    }
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
#ifndef C3_DEV_FLOOR
    (void)dev_floor;
#endif
    if (!title) { snprintf(error, capacity, "Title allocation failed."); return -1; }
    if (!c3_title_load(title, BOOT_ROOT "title.c3t", error, capacity)) goto done;
    if (title->clip_count != 3 || !(title->fps > 0.0f)) {
        snprintf(error, capacity, "The title pack must contain the three original clips.");
        goto done;
    }
    if (!c3_title_animate(title, clip, 0.0f) || c3_title_renderer_init(title, error, capacity)) goto done;
    int have_characters = c3_title_characters_load(&characters, BOOT_ROOT "title_characters.c3tc") &&
                          c3_title_renderer_set_characters(&characters);
    if (trace) { fprintf(trace,"title_characters_pack loaded=%d bytes=%lu\n",have_characters,(unsigned long)characters.size); fflush(trace); }
    if (trace) {
        fprintf(trace, "title_begin vertices=%lu batches=%lu textures=%lu bones=%lu fps=%.3f\n",
                (unsigned long)title->vertex_count, (unsigned long)title->batch_count,
                (unsigned long)title->texture_count, (unsigned long)title->bone_count, (double)title->fps);
        fflush(trace);
    }
    /* List boot/ once while the title waits for input (boot_index.h):
     * after the title packs, so it does not delay them. */
    c3_boot_index_start();
    /* gs_title_8001EF78.c fn_8001FD48: fn_80165A20(0x46A, 0, 0xFF). The
     * original runs the attract demo when the song ends (fn_801666BC == 0)
     * and restarts it on return; without the demo, restart it directly. */
    c3_music_play(C3_MUSIC_SLOT_406, 0x46A, 0xFF, 0);
    uint64_t music_check = c3_n3ds_active_time_us() + 1000000u;
    previous = c3_n3ds_active_time_us();
    result = 1;
    while (c3_n3ds_poll(&input)) {
#ifdef C3_AGENT_HARNESS
        c3_agent_link_scene(C3_AGENT_MODE_TITLE);
#endif
        uint64_t now = c3_n3ds_active_time_us();
        float delta = (float)(now - previous) / 1000000.0f;
        previous = now;
        if (now >= music_check) {
            music_check = now + 1000000u;
            if (!c3_music_status(0x46A)) c3_music_play(C3_MUSIC_SLOT_406, 0x46A, 0xFF, 0);
        }
        clip_seconds += delta;
        phase_seconds += delta;
#ifdef C3_TITLE_CHARACTERS_PROBE
        /* Inject app-level edges only into this unattended diagnostic build. */
        if (phase == TITLE_WAIT_START && probe_captured) input.pressed |= C3_PAD_START;
        if (phase == TITLE_MENU && phase_seconds >= 1.0f) input.pressed |= C3_PAD_B;
#endif
        float fade = 1.0f;
        if (phase == TITLE_INTRO && ((input.pressed & (C3_PAD_A | C3_PAD_B | C3_PAD_START)) || input.touch_pressed)) {
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
#ifdef C3_DEV_FLOOR
        } else if (phase == TITLE_WAIT_START && (input.pressed & C3_PAD_Y)) {
            /* Developer build only: leave the title renderer, touch no state. */
            *dev_floor = 1; result = 0;
            if (trace) { fprintf(trace, "dev_floor_request input=%04x\n", input.pressed); fflush(trace); }
            break;
#endif
        } else if (phase == TITLE_WAIT_START && ((input.pressed & (C3_PAD_A | C3_PAD_START)) || input.touch_pressed)) {
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
                title_characters_enter(trace);
#ifdef C3_TITLE_CHARACTERS_PROBE
                ++probe_visits; probe_captured = probe_early = probe_menu = 0;
                if (probe_visits == 4) { result = 1; break; }
#endif
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
            } else if (event.kind == C3_MENU_EVENT_COLOSSEUM_BATTLE || event.kind == C3_MENU_EVENT_BATTLE_NOW) {
                *link_mode = event.kind == C3_MENU_EVENT_COLOSSEUM_BATTLE ? C3_LINK_MODE_COLOSSEUM : C3_LINK_MODE_BATTLE_NOW;
                if (trace) { fprintf(trace, "link_play mode=%d\n", *link_mode); fflush(trace); }
                result = 0; break;
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
#ifdef C3_DEV_FLOOR
            if (phase == TITLE_WAIT_START)
                c3_bottom_screen_notice("DEVELOPER BUILD",
                    "A/START: normal game.\n\nY: DEVELOPER FLOOR LOADER (Phenac City).\n"
                    "Not story progress. Nothing is saved.", "C3_DEV_FLOOR=1");
            else
#endif
            c3_bottom_screen_title(phase == TITLE_WAIT_START,phase == TITLE_ACCEPTED);
            next_status = now + UINT64_C(250000);
        }
        bool show_characters = phase == TITLE_WAIT_START || phase == TITLE_ACCEPTED;
        if (show_characters) c3_title_characters_tick(&title_characters, delta);
        c3_title_renderer_characters(&title_characters, show_characters);
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
#ifdef C3_TITLE_CHARACTERS_PROBE
        if ((phase == TITLE_WAIT_START && ((!probe_early && phase_seconds >= 0.5f) ||
                                           (!probe_captured && phase_seconds >= 6.0f))) ||
            (phase == TITLE_MENU && !probe_menu && phase_seconds >= 0.5f)) {
            char shot[128];
            const char *kind = phase == TITLE_MENU ? "menu" : phase_seconds >= 6.0f ? "settled" : "slide";
            snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/title-page-%u-%s.bmp",title_characters.page,kind);
            int captured = c3_n3ds_capture_top(shot) == 0;
            if (trace) { fprintf(trace,"title_probe page=%u kind=%s captured=%d offsets=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f path=%s\n",
                title_characters.page,kind,captured,(double)title_characters.offsets[0],(double)title_characters.offsets[1],
                (double)title_characters.offsets[2],(double)title_characters.offsets[3],(double)title_characters.offsets[4],
                (double)title_characters.offsets[5],shot); fflush(trace); }
            if (phase == TITLE_MENU) probe_menu = 1;
            else if (phase_seconds >= 6.0f) probe_captured = 1;
            else probe_early = 1;
        }
#endif
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
    c3_title_characters_close(&characters);
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
    FILE *trace = c3_trace_open("sdmc:/3ds/Colosseum3DS/boot-trace.log");
    if (trace) { fprintf(trace, "boot_build %s core=%d\n", C3_BUILD_ID, core_status); fflush(trace); }
    c3_n3ds_log_memory(trace, "boot_start");
#ifdef C3_REALGAM_STAGE_PROBE
    {
        int stage_result=c3_realgam_stage_probe(trace,error,sizeof error);
        if(trace){fprintf(trace,"realgam_stage_probe_exit result=%d error=%s\n",stage_result,error);fclose(trace);}
        return stage_result?1:0;
    }
#endif
#if defined(C3_DNA_KEY_PROBE) && !defined(C3_ROUNDTRIP_PROBE)
    /* Standalone screen check. With the round-trip probe the puzzle runs
     * from the field instead (native CC), with the same scripted inputs. */
    {
        const uint16_t expected[3]={0x99,0x9c,0x9f};
        int dna_result=c3_dna_key_scene_run(expected,trace,error,sizeof error);
        if(trace){fprintf(trace,"dna_probe_exit result=%d expected=0099,009c,009f error=%s\n",dna_result,error);fclose(trace);}
        return dna_result==0?0:1;
    }
#endif
#ifdef C3_FINAL_EVENT_PROBE
    /* Final-floor diagnostic, explicitly warped from an uncleared COPY.
     * Never qualifies travel through the preceding chapters. */
    {
        int played=-1;uint32_t clear=1;
        const char *copy="sdmc:/3ds/Colosseum3DS/final-probe/copy.gci";
        c3_music_open("sdmc:/3ds/Colosseum3DS",error,sizeof error);
        if(c3_game_state_load(&state,copy,GAME_DATA,error)&&
           c3_game_state_get_flag(&state,0x476,&clear)&&!clear&&
           c3_game_state_set_floor(&state,0x76,0x75,0)){
            if(trace){fprintf(trace,"final_event_probe fixture=warped_uncleared_copy clear=0 battles=skip_as_win\n");fflush(trace);}
            played=c3_field_scene_run(&state,0,copy,trace,error,sizeof error);
        }
        if(trace){fprintf(trace,"final_event_end result=%d error=%s\n",played,error);fclose(trace);}
        c3_music_close();c3_game_state_free(&state);return played?1:0;
    }
#endif
#if defined(C3_CLEAR_CONTINUE_PROBE) || defined(C3_RETURN_FADE_PROBE)
    {
        const char *copy="sdmc:/3ds/Colosseum3DS/ending-probe/copy.gci";
        C3GameSummary checkpoint;uint32_t clear=0,saved_clear=0;int continued=-1;
        if(c3_game_state_load(&state,copy,GAME_DATA,error)&&
           c3_game_state_summary(&state,&checkpoint)&&
           c3_game_state_get_flag(&state,0x476,&clear)&&
           c3_game_state_get_flag(&state,0x478,&saved_clear)&&
           checkpoint.floor==2&&checkpoint.previous_floor==1&&checkpoint.spawn==1&&clear==1&&saved_clear==1){
            if(trace){fprintf(trace,"clear_continue_load fresh_process=1 floor=%03lx previous=%03lx spawn=%u clear=%lu saved_clear=%lu saved_count=%lu\n",(unsigned long)checkpoint.floor,(unsigned long)checkpoint.previous_floor,checkpoint.spawn,(unsigned long)clear,(unsigned long)saved_clear,(unsigned long)checkpoint.saved_count);fflush(trace);}
#ifdef C3_RETURN_FADE_PROBE
            /* Isolated return-override check, not a fresh Continue claim. */
            if(!c3_game_state_set_floor(&state,2,0x39f,1)){c3_game_state_free(&state);return 1;}
            if(trace){fprintf(trace,"return_fade_probe fixture=staffroll_destination_tuple save_writes=0\n");fflush(trace);}
#endif
            continued=c3_field_scene_run(&state,0,copy,trace,error,sizeof error);
        }
        if(trace){fprintf(trace,"clear_continue_end result=%d error=%s\n",continued,error);fclose(trace);}
        c3_game_state_free(&state);return continued==0?0:1;
    }
#endif
#ifdef C3_ENDING_PROBE
    /* Explicit guard fixture: a COPY of post-game state, not proof of playing
     * the final events. Never grants victory flags or touches imported saves. */
    {
        int probe_result=-1; uint32_t clear=0; C3GameSummary checkpoint;
        const char *copy="sdmc:/3ds/Colosseum3DS/ending-probe/copy.gci";
        c3_music_open("sdmc:/3ds/Colosseum3DS",error,sizeof error);
        if(c3_game_state_load(&state,copy,GAME_DATA,error)&&
           c3_game_state_get_flag(&state,0x476,&clear)&&clear==1&&
           c3_game_state_set_floor(&state,0x39f,0x76,0))
            probe_result=c3_ending_scene_run(&state,copy,trace,error,sizeof error);
        if(trace){fprintf(trace,"ending_probe fixture=postgame_guard_only result=%d clear=%lu error=%s\n",probe_result,(unsigned long)clear,error);fflush(trace);}
        c3_game_state_free(&state);
        /* Read the written copy through the fresh Continue decoder. */
        if(probe_result==2&&c3_game_state_load(&state,copy,GAME_DATA,error)&&c3_game_state_summary(&state,&checkpoint)&&trace){
            fprintf(trace,"ending_probe_reload floor=%03lx previous=%03lx spawn=%u saved_count=%lu\n",(unsigned long)checkpoint.floor,(unsigned long)checkpoint.previous_floor,checkpoint.spawn,(unsigned long)checkpoint.saved_count);fflush(trace);
        }
        c3_music_close();c3_game_state_free(&state);if(trace)fclose(trace);
        return probe_result==2?0:1;
    }
#endif
#ifdef C3_CAMPAIGN
    /* Campaign build: play the story unattended from the campaign's own
     * save (resumed when present) or from a new game, never the player's
     * save path; progress is saved there at each newly reached floor. */
    {
        const char *campaign_save = "sdmc:/3ds/Colosseum3DS/campaign/save.gci";
        const char *prepared = file_exists(GAME_DATA) ? GAME_DATA : NULL;
        int new_game = !file_exists(campaign_save);
        int campaign_result = -1;
        mkdir("sdmc:/3ds/Colosseum3DS/campaign", 0777);
        if (new_game ? (prepared && c3_game_state_new(&state, "WES", 0x12345678u, prepared, error) &&
                        c3_game_state_set_flag(&state, 0x08D0, 1))
                     : c3_game_state_load(&state, campaign_save, prepared, error))
            campaign_result = c3_field_scene_run(&state, new_game, campaign_save, trace, error, sizeof(error));
        if (trace) { fprintf(trace, "campaign_exit result=%d new_game=%d error=%s\n", campaign_result, new_game, error); fclose(trace); }
        c3_game_state_free(&state);
        return campaign_result < 0 ? 1 : 0;
    }
#endif
#ifdef C3_ROUNDTRIP_PROBE
    /* Memory probe: the campaign's save (read only), then area/world-map
     * round trips with no input. */
    {
        int probe_result = -1;
        /* As the title does: list boot/ (or start from the saved index). */
        c3_boot_index_start();
        /* probe-save.gci (a copy of a console save) when present. */
        const char *probe_save = file_exists("sdmc:/3ds/Colosseum3DS/probe-save.gci")
            ? "sdmc:/3ds/Colosseum3DS/probe-save.gci" : "sdmc:/3ds/Colosseum3DS/campaign/save.gci";
        if (!c3_game_state_load(&state, probe_save,
                                file_exists(GAME_DATA) ? GAME_DATA : NULL, error))
            fprintf(trace ? trace : stderr, "roundtrip_save status=invalid reason=%s\n", error);
        else probe_result = c3_field_scene_roundtrip_probe(&state, trace, error, sizeof(error));
        if (trace) { fprintf(trace, "roundtrip_exit result=%d\n", probe_result); fclose(trace); }
        c3_game_state_free(&state);
        return probe_result < 0 ? 1 : 0;
    }
#endif
#ifdef C3_WARP_TOUR
    /* Tour build: the tour's own copy of a save (never the player's save
     * path), no movies or title, every floor in boot/tour.txt, then exit. */
    {
        int tour_result = -1;
        if (!c3_game_state_load(&state, "sdmc:/3ds/Colosseum3DS/tour/save.gci",
                                file_exists(GAME_DATA) ? GAME_DATA : NULL, error))
            fprintf(trace ? trace : stderr, "tour_save status=invalid reason=%s\n", error);
        else tour_result = c3_field_scene_tour(&state, trace, error, sizeof(error));
        if (trace) { fprintf(trace, "tour_exit result=%d error=%s\n", tour_result, error); fclose(trace); }
        c3_game_state_free(&state);
        return tour_result < 0 ? 1 : 0;
    }
#endif
    {
        char music_error[128] = {0};
        if (!c3_music_open("sdmc:/3ds/Colosseum3DS", music_error, sizeof music_error) && trace)
            fprintf(trace, "music_open status=unavailable reason=%s\n", music_error);
    }
#ifdef C3_TITLE_CHARACTERS_PROBE
    int result = 0;
#else
    int result = c3_movie_player_run(BOOT_ROOT "tpc.c3v", "tpc", trace, error, sizeof(error));
    /* movie.c fn_800364C8: the converted logo movie has no audio track; the
     * original plays its jingle as sound 0x4D1 (volume 0x7F) at movie start. */
    if (!result) c3_music_play(C3_MUSIC_SLOT_406, 0x4D1, 0x7F, 0);
    if (!result) result = c3_movie_player_run(BOOT_ROOT "gs_logo.c3v", "gs_logo", trace, error, sizeof(error));
#endif
    c3_music_trace(trace, "gs_logo_end");
    int from_game = 0;
    while (!result) {
        int new_game = 0, dev_floor = 0, link_mode = 0;
        result = run_title(trace, &state, &new_game, &dev_floor, &link_mode, from_game,
                           &session_options, &options_edited, error, sizeof(error));
        if (result) break;
        c3_music_stop_slot(C3_MUSIC_SLOT_406, 500);   /* the floor scripts start area music */
        if (link_mode) {
            /* Battle Mode over local wireless; returns to the title menu. */
            result = c3_link_lobby_run(link_mode, &state, SAVE_PATH, trace);
            from_game = 1;
            continue;
        }
        if (dev_floor) {
            /* No game state crosses this call; return to the title menu. */
            result = c3_dev_floor_scene_run(trace, error, sizeof(error));
            from_game = 1;
            continue;
        }
        if (new_game) {
            uint64_t before = c3_n3ds_active_time_us();
            c3_music_stop_slot(C3_MUSIC_SLOT_406, 0);   /* the opening movie carries its own audio */
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
            c3_music_set_mono(session_options.sound_mono);
        }
        from_game = 1;
    }
    c3_music_close();
    c3_game_state_free(&state);
    if (trace) { fprintf(trace, "boot_end result=%d error=%s\n", result, error); fclose(trace); }
    if (result < 0) {
        c3_bottom_screen_notice("Unable to continue",error,"START: exit");
        c3_n3ds_wait_for_exit();
    }
    return result < 0 || core_status != 0 ? 1 : 0;
}
