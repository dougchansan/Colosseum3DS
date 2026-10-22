#ifndef COLOSSEUM3DS_TITLE_RENDERER_H
#define COLOSSEUM3DS_TITLE_RENDERER_H

#include "colosseum3ds/title.h"

typedef struct {
    uint32_t draw_calls, vertices;
    uint64_t draw_prepare_us; /* CPU work after FrameBegin; excludes animation. */
    float gpu_previous_ms;   /* Last completed GPU sample, not this frame. */
    size_t gpu_buffer_bytes; /* Owned vertex/texture storage only. */
} C3TitleRenderStats;

/* Requires gfxInitDefault and sole ownership of citro3d. The title must outlive
 * this renderer. Geometry and textures are bounded by title.h's loader limits.
 * Embedded shader storage remains alive until after DVLB_Free. */
int c3_title_renderer_init(const C3Title *title, char *error, size_t capacity);

/* Call c3_title_animate first. Owns FrameBegin/FrameEnd and the top render target;
 * do not wrap in a second frame or call another citro3d renderer concurrently.
 * fade=0 is black, fade=1 is the authored scene. Bottom console is flushed. */
int c3_title_renderer_draw(const C3Title *title, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
void c3_title_renderer_shutdown(void);

#endif
