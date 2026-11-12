#ifndef C3_STAGE_CACHE_H
#define C3_STAGE_CACHE_H
#include "colosseum3ds/title.h"
#include <stdbool.h>
#include <string.h>
/* Per-batch record of what a model's range in the persistent vertex arena was
 * last staged from. A batch is staged again only when one of its inputs
 * differs: the sampled vertices (C3Title.batch_revision), the lights of a lit
 * batch, the view of a specular or normal-reflection batch, the material
 * fields baked into its vertices, or the television V flip.
 *
 * Tracking applies only to models whose sampled vertices change solely
 * through c3_title_animate: C3Title.retained, or the caller's
 * c3_stage_renderer_set_static promise. Every other model is staged in full
 * each frame, exactly as before. Shader-side state (modulation, TEV colours,
 * alpha test, blending) never reaches the staged vertices. */
typedef struct {
    uint32_t revision, lights, view;
    float alpha, origin[3], uv[12];
    uint32_t state; /* bit 0 valid, bit 1 television V flip; no padding */
} C3StageBatchKey;

typedef struct {
    bool enabled; /* c3_stage_renderer_set_static */
    bool stale;   /* keys predate an untracked frame or an explicit reset */
    uint32_t light_serial, view_serial, light_count;
    C3TitleLight lights[C3_TITLE_MAX_LIGHTS];
    unsigned char view[64];
    C3StageBatchKey batches[C3_TITLE_MAX_BATCHES];
} C3StageVertexCache;

/* Forget everything staged for this model; the opt-in flag is kept. */
static inline void c3_stage_cache_reset(C3StageVertexCache *c)
{
    c->stale = true;
}

/* Call once per model per frame, before its batches. Returns whether the
 * model is tracked, and advances the light and view serials when the model's
 * lights or its 64-byte view matrix changed since the previous tracked frame. */
static inline bool c3_stage_cache_frame(C3StageVertexCache *c, const C3Title *title, const void *view)
{
    if (!c->enabled && !title->retained) { c->stale = true; return false; }
    if (c->stale) {
        for (unsigned i = 0; i < C3_TITLE_MAX_BATCHES; ++i) c->batches[i].state = 0;
        c->stale = false;
    }
    uint32_t count = title->light_count <= C3_TITLE_MAX_LIGHTS ? title->light_count : C3_TITLE_MAX_LIGHTS;
    if (c->light_count != count || memcmp(c->lights, title->lights, count * sizeof *c->lights)) {
        memcpy(c->lights, title->lights, count * sizeof *c->lights);
        c->light_count = count;
        ++c->light_serial;
    }
    if (memcmp(c->view, view, sizeof c->view)) {
        memcpy(c->view, view, sizeof c->view);
        ++c->view_serial;
    }
    return true;
}

/* For a visible batch of a tracked model. Returns true when the arena range
 * already holds this batch for the current inputs. Otherwise records the new
 * inputs and returns false: the caller must stage the batch now. */
static inline bool c3_stage_cache_batch(C3StageVertexCache *c, const C3Title *title, uint32_t batch, bool flip)
{
    const C3TitleBatch *b = &title->batches[batch];
    const C3TitleMaterialState *m = &title->pose.materials[batch];
    C3StageBatchKey key;
    memset(&key, 0, sizeof key);
    key.state = 1u | (flip ? 2u : 0u);
    key.revision = title->batch_revision[batch];
    if (b->flags & C3_TITLE_LIT) key.lights = c->light_serial;
    else {
        /* The unlit raster alpha is material alpha unless the source
         * selects the vertex channel alone (HSD render-mode bits 13-14). */
        unsigned color_source = b->render_mode & 3u, alpha_source = (b->render_mode >> 13) & 3u;
        if (!alpha_source) alpha_source = color_source ? color_source : 1u;
        if (alpha_source != 2u) key.alpha = m->alpha;
    }
    if (b->flags & C3_TITLE_SPECULAR) {
        key.view = c->view_serial;
        memcpy(key.origin, m->specular_origin, sizeof key.origin);
    }
    if ((b->flags & C3_TITLE_REFLECTION_BLEND) && !(b->flags & C3_TITLE_SECOND_UV_BLEND)) {
        key.view = c->view_serial;
        memcpy(key.uv, m->uv_scale, 2 * sizeof(float)); memcpy(key.uv + 2, m->uv_offset, 2 * sizeof(float));
        memcpy(key.uv + 4, m->uv2_scale, 2 * sizeof(float)); memcpy(key.uv + 6, m->uv2_offset, 2 * sizeof(float));
        memcpy(key.uv + 8, m->uv_cross, 2 * sizeof(float)); memcpy(key.uv + 10, m->uv2_cross, 2 * sizeof(float));
    }
    if (!memcmp(&key, &c->batches[batch], sizeof key)) return true;
    memcpy(&c->batches[batch], &key, sizeof key);
    return false;
}
#endif
