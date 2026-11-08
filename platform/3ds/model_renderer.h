#ifndef COLOSSEUM3DS_MODEL_RENDERER_H
#define COLOSSEUM3DS_MODEL_RENDERER_H
#include "colosseum3ds/model.h"

#define C3_MODEL_RENDER_VERTEX_CAPACITY 8192u
#define C3_MODEL_RENDER_CHUNK_VERTICES 63u

/* Triangle boundaries are retained across frontend-sized draw calls. */
static inline uint32_t c3_model_renderer_chunk_size(uint32_t remaining)
{
    return remaining > C3_MODEL_RENDER_CHUNK_VERTICES ? C3_MODEL_RENDER_CHUNK_VERTICES : remaining;
}

static inline int c3_model_renderer_reserve(uint32_t used, uint32_t count)
{
    return count && count % 3u == 0 && used <= C3_MODEL_RENDER_VERTEX_CAPACITY &&
           count <= C3_MODEL_RENDER_VERTEX_CAPACITY - used;
}

typedef struct {
    uint32_t draw_calls, vertices, triangles;
    float gpu_ms;
    uint64_t draw_prepare_us;
    size_t gpu_owned_bytes, peak_upload_bytes, tracked_peak_bytes;
} C3ModelRenderStats;

int c3_model_renderer_init(const C3Model *model, char *error, size_t capacity);
int c3_model_renderer_draw(const C3Model *model, C3ModelRenderStats *stats,
                           char *error, size_t capacity);
void c3_model_renderer_shutdown(void);
#endif
