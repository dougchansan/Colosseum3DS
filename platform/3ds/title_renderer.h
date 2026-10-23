#ifndef COLOSSEUM3DS_TITLE_RENDERER_H
#define COLOSSEUM3DS_TITLE_RENDERER_H

#include "colosseum3ds/title.h"
#include <stdbool.h>

enum { C3_RENDER_ERROR = -1, C3_RENDER_OK = 0, C3_RENDER_BUSY = 1 };

typedef struct {
    uint32_t draw_calls, vertices; /* Submitted totals, including specular/fade passes. */
    uint32_t vertices_prepared, cached_models; /* CPU staging work; draws still submit cached models. */
    uint32_t particles, particle_vertices;
    uint64_t draw_prepare_us; /* CPU work after FrameBegin; excludes animation. */
    float gpu_previous_ms;   /* Last completed GPU sample, not this frame. */
    size_t gpu_buffer_bytes; /* Owned vertices/textures and optional TV depth; excludes main output. */
} C3TitleRenderStats;

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
 * fade=0 is black, fade=1 is the authored scene. Bottom console is flushed.
 * Returns C3_RENDER_BUSY without GPU writes when the previous frame is active;
 * the caller retains ownership and must keep polling application input. */
int c3_title_renderer_draw(const C3Title *title, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
void c3_title_renderer_shutdown(void);

#endif
