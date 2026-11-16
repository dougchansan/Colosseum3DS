#include "debug_overlay.h"
#include "stage_renderer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int enabled = -1; /* -1: not decided yet */
static uint64_t last_us;
static float fps, frame_ms; /* smoothed */
static unsigned shown;      /* frames since the overlay last changed state */

static void decide(void)
{
    if (enabled >= 0) return;
    FILE *flag = fopen("sdmc:/3ds/Colosseum3DS/debug-overlay", "rb");
    enabled = flag != NULL;
    if (flag) fclose(flag);
}

void c3_debug_overlay_input(C3N3dsInput *input)
{
    decide();
    if (!input) return;
    if ((input->pressed & C3_PAD_START) && (input->held & C3_PAD_L) && (input->held & C3_PAD_R)) {
        enabled = !enabled;
        input->pressed &= (uint16_t)~C3_PAD_START;
        shown = 0;
        if (!enabled) {
            /* Clear the HUD of the running scene (ignored without one). */
            C3StageBattleHud off = {0};
            char error[64];
            (void)c3_stage_renderer_set_battle_hud(&off, error, sizeof error);
        }
    }
}

int c3_debug_overlay_enabled(void)
{
    decide();
    return enabled > 0;
}

/* Cut at the HUD's 48 columns. */
static void line(char out[49], const char *text)
{
    size_t n = text ? strlen(text) : 0;
    if (n > 48) n = 48;
    if (n) memcpy(out, text, n);
    out[n] = 0;
}

void c3_debug_overlay_frame(const char *scene, const C3TitleCamera *camera, float authored_fov,
                            const C3TitleRenderStats *stats, const char *extra1, const char *extra2,
                            const char *extra3)
{
    uint64_t now = c3_n3ds_time_us();
    if (last_us && now > last_us) {
        float ms = (float)(now - last_us) / 1000.0f;
        if (ms < 1000.0f) {
            frame_ms = frame_ms > 0 ? frame_ms * 0.9f + ms * 0.1f : ms;
            fps = frame_ms > 0 ? 1000.0f / frame_ms : 0;
        }
    }
    last_us = now;
    if (!c3_debug_overlay_enabled()) return;
    C3StageBattleHud hud;
    memset(&hud, 0, sizeof hud);
    hud.enabled = 1; hud.layout = 1;
    char text[96];
    snprintf(text, sizeof text, "%-8.8s %5.1f fps %5.1f ms", scene ? scene : "", (double)fps, (double)frame_ms);
    line(hud.lines[0], text);
    if (stats) {
        snprintf(text, sizeof text, "cpu %4.1f gpu %4.1f wait %4.1f ms",
                 (double)stats->draw_prepare_us / 1000.0, (double)stats->gpu_previous_ms,
                 (double)stats->wait_us / 1000.0);
        line(hud.lines[1], text);
        snprintf(text, sizeof text, "draws %lu verts %lu prep %lu", (unsigned long)stats->draw_calls,
                 (unsigned long)stats->vertices, (unsigned long)stats->vertices_prepared);
        line(hud.lines[2], text);
    }
    if (camera) {
        float dx = camera->target[0] - camera->eye[0], dy = camera->target[1] - camera->eye[1],
              dz = camera->target[2] - camera->eye[2];
        snprintf(text, sizeof text, "eye %.1f %.1f %.1f", (double)camera->eye[0], (double)camera->eye[1],
                 (double)camera->eye[2]);
        line(hud.lines[3], text);
        snprintf(text, sizeof text, "tgt %.1f %.1f %.1f d %.1f", (double)camera->target[0],
                 (double)camera->target[1], (double)camera->target[2], (double)sqrtf(dx * dx + dy * dy + dz * dz));
        line(hud.lines[4], text);
        if (authored_fov > 0 && camera->fov_y_degrees > 0)
            snprintf(text, sizeof text, "fov %.1f zoom %.2fx roll %.1f", (double)camera->fov_y_degrees,
                     (double)(authored_fov / camera->fov_y_degrees), (double)camera->roll);
        else
            snprintf(text, sizeof text, "fov %.1f roll %.1f n %.2f", (double)camera->fov_y_degrees,
                     (double)camera->roll, (double)camera->near_plane);
        line(hud.lines[5], text);
    }
    /* Scene lines fill the rows that are still free. */
    const char *extra[3] = {extra1, extra2, extra3};
    unsigned row = camera ? 6 : stats ? 3 : 1;
    for (unsigned i = 0; i < 3 && row < 8; ++i) if (extra[i]) line(hud.lines[row++], extra[i]);
    char error[64];
    (void)c3_stage_renderer_set_battle_hud(&hud, error, sizeof error); /* retried next frame when busy */
    ++shown;
}
