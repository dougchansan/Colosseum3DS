#include "boot_scene.h"
#include "movie_player.h"
#include "n3ds_platform.h"
#include "title_renderer.h"
#include "colosseum3ds/save.h"
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

typedef enum {
    TITLE_INTRO, TITLE_SKIP_OUT, TITLE_SKIP_IN, TITLE_WAIT_START,
    TITLE_ACCEPTED, TITLE_GAME_HANDOFF
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

static const char *phase_name(TitlePhase phase)
{
    switch (phase) {
    case TITLE_INTRO: return "title_intro";
    case TITLE_SKIP_OUT: return "title_skip_out";
    case TITLE_SKIP_IN: return "title_skip_in";
    case TITLE_WAIT_START: return "title_wait_start";
    case TITLE_ACCEPTED: return "title_start_accepted";
    case TITLE_GAME_HANDOFF: return "original_menu_boundary";
    }
    return "invalid";
}

static void inspect_save(char *status, size_t capacity, FILE *trace)
{
    struct stat entry;
    size_t length = 0;
    C3SaveInfo info = {0};
    int used_backup = 0;
    if (stat(SAVE_PATH, &entry) != 0 && stat(SAVE_PATH ".bak", &entry) != 0) {
        snprintf(status, capacity, "No imported Colosseum GCI.");
        if (trace) fprintf(trace, "save_load status=absent path=%s\n", SAVE_PATH);
        return;
    }
    unsigned char *image = malloc(C3_SAVE_GCI_SIZE);
    unsigned char *body = malloc(C3_SAVE_BODY_SIZE);
    if (!image || !body) {
        snprintf(status, capacity, "Save inspection allocation failed.");
        free(image); free(body);
        return;
    }
    C3SaveResult result = c3_save_load_file(SAVE_PATH, image, C3_SAVE_GCI_SIZE,
                                          &length, &info, &used_backup);
    if (result == C3_SAVE_OK)
        result = c3_save_decode(image, length, body, C3_SAVE_BODY_SIZE, &info);
    if (result == C3_SAVE_OK)
        snprintf(status, capacity, "Valid %s: slot %u count %lu%s%s", info.has_gci_header ? "GCI" : "raw save",
                 info.selected_slot, (unsigned long)info.selected_count,
                 used_backup ? " (backup)" : "", info.recovered_older_slot ? " (older slot)" : "");
    else
        snprintf(status, capacity, "Save: %s", c3_save_result_string(result));
    if (trace) {
        fprintf(trace, "save_load status=%d bytes=%lu slot=%u count=%lu backup=%d older=%u\n",
                result, (unsigned long)length, info.selected_slot,
                (unsigned long)info.selected_count, used_backup, info.recovered_older_slot);
        fflush(trace);
    }
    /* Decoding proves compatibility only. The original game-state importer
     * must consume this body before it can become a playable Continue. */
    free(body);
    free(image);
}

static int run_title(FILE *trace, int core_status, char *error, size_t capacity)
{
    C3Title title = {0};
    C3N3dsInput input = {0};
    C3TitleRenderStats stats = {0};
    TitlePhase phase = TITLE_INTRO, previous_phase = TITLE_INTRO;
    unsigned clip = 0, frames = 0;
    float clip_seconds = 0.0f, phase_seconds = 0.0f;
    uint64_t previous, next_status = 0;
    char save_status[128] = "Card check runs after START.";
    int result = -1;
    if (!c3_title_load(&title, BOOT_ROOT "title.c3t", error, capacity)) return -1;
    if (title.clip_count != 3 || !(title.fps > 0.0f)) {
        snprintf(error, capacity, "The title pack must contain the three original clips.");
        goto done;
    }
    if (!c3_title_animate(&title, 0, 0.0f) || c3_title_renderer_init(&title, error, capacity)) goto done;
    if (trace) {
        fprintf(trace, "title_begin vertices=%lu batches=%lu textures=%lu bones=%lu fps=%.3f\n",
                (unsigned long)title.vertex_count, (unsigned long)title.batch_count,
                (unsigned long)title.texture_count, (unsigned long)title.bone_count, (double)title.fps);
        fflush(trace);
    }
    previous = c3_n3ds_active_time_us();
    result = 0;
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_active_time_us();
        float delta = (float)(now - previous) / 1000000.0f;
        previous = now;
        clip_seconds += delta;
        phase_seconds += delta;
        float fade = 1.0f;
        /* These are the original title's rising-edge masks. Movie playback
         * deliberately does not share this skip behavior. */
        if (phase == TITLE_INTRO && (input.pressed & (C3_PAD_A | C3_PAD_B | C3_PAD_START))) {
            phase = TITLE_SKIP_OUT;
            phase_seconds = 0.0f;
        }
        if (phase == TITLE_INTRO) {
            if (clip == 0 && clip_seconds < 0.5f) fade = clip_seconds * 2.0f;
            float duration = (float)(title.clips[clip].frame_count - 1u) / title.fps;
            if (clip_seconds >= duration) {
                clip_seconds -= duration;
                ++clip;
                phase_seconds = 0.0f;
                if (clip == 2) phase = TITLE_WAIT_START;
            }
        } else if (phase == TITLE_SKIP_OUT) {
            fade = 1.0f - phase_seconds * 2.0f;
            if (phase_seconds >= 0.5f) {
                clip = 2;
                clip_seconds = 0.0f;
                phase_seconds = 0.0f;
                phase = TITLE_SKIP_IN;
                fade = 0.0f;
            }
        } else if (phase == TITLE_SKIP_IN) {
            fade = phase_seconds * 2.0f;
            if (phase_seconds >= 0.5f) {
                phase = TITLE_WAIT_START;
                phase_seconds = 0.0f;
                fade = 1.0f;
            }
        } else if (phase == TITLE_WAIT_START && (input.pressed & (C3_PAD_A | C3_PAD_START))) {
            phase = TITLE_ACCEPTED;
            phase_seconds = 0.0f;
        } else if (phase == TITLE_ACCEPTED && phase_seconds >= 1.0f) {
            inspect_save(save_status, sizeof(save_status), trace);
            phase = TITLE_GAME_HANDOFF;
            phase_seconds = 0.0f;
        }
        if (phase != previous_phase) {
            if (trace) {
                fprintf(trace, "title_state name=%s clip=%u frame=%u input=%04x\n",
                        phase_name(phase), clip, frames, (unsigned)input.pressed);
                fflush(trace);
            }
            previous_phase = phase;
            next_status = 0;
        }
        if (!c3_title_animate(&title, clip, clip_seconds)) {
            snprintf(error, capacity, "Title animation sample failed.");
            result = -1;
            break;
        }
        if (now >= next_status) {
            consoleClear();
            printf("Colosseum3DS | native title\nBuild: %.12s | Core: %s\n\n", C3_BUILD_ID,
                   core_status == 0 ? "PASS" : "FAIL");
            printf("Live HSD title | clip %u sample %.1f\n", clip, (double)title.active_frame);
            printf("%lu vertices | %lu textures\n", (unsigned long)title.vertex_count,
                   (unsigned long)title.texture_count);
            printf("PAD %04X  L %3u R %3u\n", (unsigned)input.held,
                   (unsigned)input.trigger_l, (unsigned)input.trigger_r);
            printf("Stick %4d %4d | C %4d %4d\n\n", input.stick_x, input.stick_y,
                   input.cstick_x, input.cstick_y);
            if (phase == TITLE_WAIT_START) printf("PRESS START\n\n");
            else if (phase == TITLE_ACCEPTED) printf("START accepted. Checking card file...\n\n");
            else if (phase == TITLE_GAME_HANDOFF)
                printf("Reached original menu boundary 0x3A1.\nGame menu/state bridge is not ported.\n\n");
            else printf("Title entrance | A/B/START: advance\n\n");
            printf("%.120s\n", save_status);
            printf("Native save codec/storage available.\nPlayable new-game saves: not connected.\n\n");
            printf("START + SELECT: exit\n");
            next_status = now + UINT64_C(250000);
        }
        if (c3_title_renderer_draw(&title, fade, &stats, error, capacity)) {
            result = -1;
            break;
        }
        ++frames;
    }
    if (trace) {
        fprintf(trace, "title_end result=%d frames=%u state=%s clip=%u\n", result, frames, phase_name(phase), clip);
        fflush(trace);
    }
done:
    c3_title_renderer_shutdown();
    c3_title_close(&title);
    return result;
}

int c3_boot_scene_run(int core_status)
{
    char error[192] = {0};
    FILE *trace = fopen("sdmc:/3ds/Colosseum3DS/boot-trace.log", "w");
    if (trace) {
        fprintf(trace, "boot_build %s core=%d\n", C3_BUILD_ID, core_status);
        fflush(trace);
    }
    int result = c3_movie_player_run(BOOT_ROOT "tpc.c3v", "tpc", trace, error, sizeof(error));
    if (!result) result = c3_movie_player_run(BOOT_ROOT "gs_logo.c3v", "gs_logo", trace, error, sizeof(error));
    if (!result) result = run_title(trace, core_status, error, sizeof(error));
    if (trace) {
        fprintf(trace, "boot_end result=%d error=%s\n", result, error);
        fclose(trace);
    }
    if (result < 0) {
        consoleClear();
        printf("Colosseum3DS | boot stopped\n\n%s\n\n", error);
        printf("Prepare the original boot assets:\nboot/tpc.c3v\nboot/gs_logo.c3v\nboot/title.c3t\n\n");
        printf("See docs/native-boot.md.\nSTART: exit\n");
        c3_n3ds_wait_for_exit();
    }
    return result < 0 || core_status != 0 ? 1 : 0;
}
