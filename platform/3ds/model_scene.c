#include "n3ds_platform.h"
#include "model_renderer.h"
#include "model_scene.h"
#include "colosseum3ds/core.h"
#include "colosseum3ds/model.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "development"
#endif
#define BENCH_FRAMES 600u
#define MODEL_PATH "sdmc:/3ds/Colosseum3DS/data/eevee.c3m"
typedef struct { uint32_t frame, skin, prepare, gpu; } Sample;
static Sample samples[BENCH_FRAMES];
static uint32_t scratch[BENCH_FRAMES], median[4], tail[4];
static unsigned sample_count;
static char benchmark_status[80];

bool c3_model_scene_available(void)
{
    FILE *file = fopen(MODEL_PATH, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static int compare_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void finish_benchmark(const C3Model *model, const C3ModelRenderStats *stats,
                             uint64_t load_us, uint64_t run_id)
{
    char path[160];
    FILE *log;
    for (unsigned field = 0; field < 4; ++field) {
        for (unsigned i = 0; i < BENCH_FRAMES; ++i) {
            const Sample *s = &samples[i];
            scratch[i] = field == 0 ? s->frame : field == 1 ? s->skin : field == 2 ? s->prepare : s->gpu;
        }
        qsort(scratch, BENCH_FRAMES, sizeof(*scratch), compare_u32);
        median[field] = (scratch[BENCH_FRAMES / 2 - 1] + scratch[BENCH_FRAMES / 2]) / 2;
        tail[field] = scratch[(BENCH_FRAMES * 95 + 99) / 100 - 1];
    }
    snprintf(path, sizeof(path), "sdmc:/3ds/Colosseum3DS/data/model-bench-%016llx.csv", (unsigned long long)run_id);
    log = fopen(path, "wx");
    if (!log) { snprintf(benchmark_status, sizeof(benchmark_status), "CSV write failed; screen stats valid"); return; }
    fprintf(log, "# build=%s; samples=%u; warmup=60; units=us; camera=stationary; preview_loop=true\n", C3_BUILD_ID, BENCH_FRAMES);
    fprintf(log, "# model_bytes=%lu; model_resident=%lu; load_peak=%lu; gpu_tracked=%lu; upload_peak=%lu; load_us=%llu\n",
            (unsigned long)model->file_bytes, (unsigned long)model->resident_bytes,
            (unsigned long)model->peak_load_bytes, (unsigned long)stats->gpu_owned_bytes,
            (unsigned long)stats->peak_upload_bytes, (unsigned long long)load_us);
    fprintf(log, "# draws=%lu; vertices=%lu; triangles=%lu; streaming=externally_recorded; total_process_memory=unmeasured\n",
            (unsigned long)stats->draw_calls, (unsigned long)stats->vertices, (unsigned long)stats->triangles);
    fprintf(log, "# tracked_peak=%lu; tracking=model_load_and_gpu_arenas_textures_staging; driver_framebuffer_shader_static_bss_excluded=true\n",
            (unsigned long)stats->tracked_peak_bytes);
    fprintf(log, "frame_us,skin_us,draw_prepare_us,previous_gpu_us\n");
    for (unsigned i = 0; i < BENCH_FRAMES; ++i)
        fprintf(log, "%lu,%lu,%lu,%lu\n", (unsigned long)samples[i].frame,
                (unsigned long)samples[i].skin, (unsigned long)samples[i].prepare, (unsigned long)samples[i].gpu);
    snprintf(benchmark_status, sizeof(benchmark_status), "CSV saved (%u frames)", BENCH_FRAMES);
    if (fclose(log) != 0) snprintf(benchmark_status, sizeof(benchmark_status), "CSV close failed; screen stats valid");
}

int c3_model_scene_run(int core_status, int data_result, const char *initial_data_status)
{
    C3Model model = {0};
    C3ModelRenderStats stats = {0};
    C3N3dsInput input = {0};
    char error[160], data_status[192];
    uint64_t run_id = c3_n3ds_time_us(), previous_time, next_status = 0, load_us;
    unsigned frames = 0;
    float animation_time = 0;
    bool paused = false;
    int result = core_status == 0 ? 0 : 1;
    snprintf(data_status, sizeof(data_status), "%s", initial_data_status);
    sample_count = 0;
    if (c3_model_load(&model, MODEL_PATH, error, sizeof(error)) != 0 ||
        c3_model_renderer_init(&model, error, sizeof(error)) != 0) {
        printf("Model scene failed: %s\nSTART: exit\n", error);
        c3_model_close(&model); c3_n3ds_wait_for_exit(); return 1;
    }
    load_us = c3_n3ds_time_us() - run_id;
    previous_time = c3_n3ds_time_us();
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_time_us(), skin_start;
        uint32_t frame_us = (uint32_t)(now - previous_time), skin_us;
        float delta = (float)frame_us / 1000000.0f;
        previous_time = now;
        if (delta > 0.1f) delta = 0.1f;
        if (input.pressed & C3_PAD_A) paused = !paused;
        if (input.pressed & C3_PAD_X) {
            data_result = c3_data_probe(c3_n3ds_data_root(), data_status, sizeof(data_status));
            next_status = 0;
        }
        if (!paused) animation_time += delta;
        skin_start = c3_n3ds_time_us();
        c3_model_animate(&model, animation_time);
        skin_us = (uint32_t)(c3_n3ds_time_us() - skin_start);
        if (c3_model_renderer_draw(&model, &stats, error, sizeof(error)) != 0) {
            printf("\nModel draw failed: %s\n", error); result = 1; break;
        }
        if ((paused || frame_us >= 100000) && sample_count < BENCH_FRAMES) sample_count = 0;
        if (!paused && frames >= 60 && sample_count < BENCH_FRAMES && delta < 0.1f) {
            samples[sample_count++] = (Sample){frame_us, skin_us,
                (uint32_t)stats.draw_prepare_us, (uint32_t)(stats.gpu_ms * 1000.0f)};
            if (sample_count == BENCH_FRAMES) finish_benchmark(&model, &stats, load_us, run_id);
        }
        frames++;
        if (now >= next_status) {
            consoleClear();
            printf("Colosseum3DS %s\nNative bring-up | not playable\n", C3_BUILD_ID);
            printf("Core %s (%d) | archive %s\n", core_status == 0 ? "PASS" : "FAIL", core_status,
                   data_result == 0 ? "PASS" : "NOT READY");
            printf("Eevee | GekkoCTR -> Citro3D\n");
            printf("Idle %.2fs %s | fixed camera\n", animation_time, paused ? "PAUSED" : "PLAY");
            printf("Unlit 1-texture; no full HSD TEV\n");
            printf("Bones %lu | triangles %lu\n", (unsigned long)model.bone_count, (unsigned long)stats.triangles);
            printf("Textures %lu | draws %lu\n", (unsigned long)model.texture_count, (unsigned long)stats.draw_calls);
            printf("Model %lu KiB; GPU tracked %lu KiB\n", (unsigned long)(model.resident_bytes/1024), (unsigned long)(stats.gpu_owned_bytes/1024));
            printf("Load peak %lu; upload %lu KiB\n", (unsigned long)(model.peak_load_bytes/1024), (unsigned long)(stats.peak_upload_bytes/1024));
            printf("Tracked peak %lu KiB (not total)\n", (unsigned long)(stats.tracked_peak_bytes/1024));
            printf("Load %.2f ms | skin %.3f ms\n", (double)load_us/1000.0, (double)skin_us/1000.0);
            printf("Bench %u/%u | frame %u\n", sample_count, BENCH_FRAMES, frames);
            if (sample_count == BENCH_FRAMES) {
                printf("Median/p95 ms: frame %.2f/%.2f\n", median[0]/1000.0, tail[0]/1000.0);
                printf("Skin %.3f/%.3f | prep %.3f/%.3f\n", median[1]/1000.0, tail[1]/1000.0, median[2]/1000.0, tail[2]/1000.0);
                printf("GPU prev %.3f/%.3f\n%s\n", median[3]/1000.0, tail[3]/1000.0, benchmark_status);
            }
            printf("PAD %04X | A: pause skeleton\nX: archive probe\nSTART + SELECT: exit\n", input.held);
            next_status = now + UINT64_C(250000);
        }
    }
    c3_model_renderer_shutdown(); c3_model_close(&model);
    return result;
}
