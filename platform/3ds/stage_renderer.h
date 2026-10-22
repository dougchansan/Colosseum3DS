#ifndef COLOSSEUM3DS_STAGE_RENDERER_H
#define COLOSSEUM3DS_STAGE_RENDERER_H

#include "title_renderer.h"

#define C3_STAGE_MAX_MODELS 8u
#define C3_STAGE_MAX_VERTICES 131072u
#define C3_STAGE_MAX_TEXTURES 128u

/* Implemented by the shared renderer in title_renderer.c. Packs retain their
 * independent animation clocks and bounded C3Title storage. All packs must
 * outlive this renderer, and init/draw must use the same ordered identities.
 * The renderer owns one GPU frame and disjoint arena ranges for every pack.
 * It cannot coexist with an initialized title renderer. */
int c3_stage_renderer_init(const C3Title *const *models, size_t count,
                          char *error, size_t capacity);
int c3_stage_renderer_draw(const C3Title *const *models, size_t count,
                          const C3TitleCamera *camera, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
void c3_stage_renderer_shutdown(void);

#endif
