#ifndef COLOSSEUM3DS_TITLE_RENDERER_H
#define COLOSSEUM3DS_TITLE_RENDERER_H

#include "colosseum3ds/title.h"
#include <stdbool.h>

/* Shared driver allocation, separate from scene-owned vertex/texture bytes. */
#define C3_RENDER_COMMAND_BUFFER_BYTES (1024u * 1024u)
#define C3_RENDER_COMMAND_CAPACITY_WORDS (C3_RENDER_COMMAND_BUFFER_BYTES / 4u)

enum { C3_RENDER_ERROR = -1, C3_RENDER_OK = 0, C3_RENDER_BUSY = 1 };

typedef struct {
    uint32_t draw_calls, vertices; /* Submitted totals, including specular/fade passes. */
    uint32_t vertices_prepared, cached_models; /* CPU staging work; draws still submit cached models. */
    uint32_t particles, particle_vertices;
    uint32_t command_words, particle_command_words, command_capacity_words;
    uint64_t draw_prepare_us; /* CPU work after FrameBegin; excludes animation. */
    float gpu_previous_ms;   /* Last completed GPU sample, not this frame. */
    size_t gpu_buffer_bytes; /* Owned vertices/textures and optional TV depth; excludes main output. */
    /* This frame's split of draw_prepare_us. vertices_static are visible
     * vertices of tracked batches left untouched in the vertex arena. */
    uint32_t cached_batches, vertices_static, upload_bytes;
    uint32_t prepare_us, upload_us, submit_us;
    /* Time this draw spent idle: holding the VBlank cadence and waiting for
     * the GPU to release the next frame. Not CPU work. */
    uint32_t wait_us;
} C3TitleRenderStats;

/* Interval totals since the previous take, over submitted frames. Maxima are
 * per frame. Animation counters come from c3_title_animate (title.h) and so
 * include samples made for frames that were then reported BUSY. */
typedef struct {
    uint32_t frames;
    uint32_t animate_calls, vertices_animated, vertices_retained;
    uint64_t animate_us, animate_max_us;
    uint64_t place_us;                    /* scene-reported second passes over sampled vertices */
    uint64_t prepare_us, prepare_max_us;  /* CPU lighting/staging, excluding cache flushes */
    uint32_t vertices_prepared, vertices_static, cached_models, cached_batches;
    uint64_t upload_us, upload_bytes;     /* GSPGPU_FlushDataCache: models, HUD, particles, Confusion */
    uint64_t submit_us, submit_max_us;    /* command building */
    uint64_t end_us;                      /* C3D_FrameEnd */
    uint64_t pace_us;                     /* idle, holding the VBlank cadence */
    uint64_t gpu_wait_us;                 /* waiting for a busy GPU to release the next frame */
    uint32_t gpu_busy;                    /* frames whose acquisition found the GPU busy */
    float gpu_max_ms;                     /* largest completed GPU drawing time */
} C3RenderPerf;
void c3_stage_renderer_perf_take(C3RenderPerf *perf);
/* Scene-side placement time (microseconds) that is not inside c3_title_animate. */
void c3_stage_renderer_perf_place(uint64_t microseconds);
/* Takes the interval and writes " perf_frames=... perf_gpu_max_ms=..." (leading
 * space, no newline) for appending to a once-per-second trace line. */
void c3_stage_renderer_perf_format(char *text, size_t capacity);

/* Passive completion evidence. libctru processes its queue interrupt before
 * invoking these counters; snapshots never wait on GPU services. Counters
 * cover this shared renderer's context lifetime, including scene changes. */
typedef struct {
    uint32_t submissions, memory_fills, command_lists, display_transfers;
    uint32_t last_command_words, last_draw_calls, last_vertices;
    uint32_t scene_models, scene_textures;
    size_t gpu_buffer_bytes;
} C3TitleRenderSnapshot;
void c3_stage_renderer_snapshot(C3TitleRenderSnapshot *snapshot);

/* Requires gfxInitDefault and sole ownership of citro3d. The title must outlive
 * this renderer. Geometry and textures are bounded by title.h's loader limits.
 * Embedded shader storage remains alive until after DVLB_Free. The shared
 * shader/Citro3D context persists across scene bundles until application fini. */
int c3_title_renderer_init(const C3Title *title, char *error, size_t capacity);

/* Call c3_title_animate first. Owns FrameBegin/FrameEnd and the top render target;
 * do not wrap in a second frame or call another citro3d renderer concurrently.
 * fade=0 is black, fade=1 is the authored scene. The bottom screen is not
 * flushed here; its owner flushes when it redraws.
 * Returns C3_RENDER_BUSY without GPU writes when the previous frame is active;
 * the caller retains ownership and must keep polling application input.
 * A successful draw paces presentation to C3_RENDER_VBLANKS_PER_FRAME and then
 * tries to acquire the next GPU frame before returning, so the caller's next
 * simulation step runs with the frame already owned (see frame_pacing.inc). */
int c3_title_renderer_draw(const C3Title *title, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
void c3_title_renderer_shutdown(void);

#endif
