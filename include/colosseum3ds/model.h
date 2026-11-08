#ifndef COLOSSEUM3DS_MODEL_H
#define COLOSSEUM3DS_MODEL_H
#include <stddef.h>
#include <stdint.h>

typedef struct { float pos[3], uv[2]; uint32_t bone; uint8_t rgba[4]; } C3ModelVertex;
typedef struct { float x, y, z, u, v; uint8_t rgba[4]; } C3ModelDrawVertex;
typedef struct { uint32_t first, count, texture, flags; } C3ModelBatch;
typedef struct {
    uint32_t width, height, wrap_s, wrap_t;
    const uint8_t *pixels;
    size_t size;
} C3ModelTexture;
typedef struct {
    uint8_t *file_data;
    size_t file_bytes;
    C3ModelVertex *vertices;
    C3ModelDrawVertex *draw_vertices;
    C3ModelBatch *batches;
    C3ModelTexture *textures;
    float *matrices;
    uint32_t bone_count, vertex_count, batch_count, texture_count, frame_count;
    float fps, bounds[6];
    size_t resident_bytes, peak_load_bytes;
} C3Model;

/* Initialize model to zero before first use. Failure leaves it empty, except
 * parse input aliasing its owned file storage is rejected without changing it.
 * Pack limits: positions/bounds abs <= 10000, UV abs <= 1e6, and matrix
 * elements abs <= 1e6, keeping CPU skinning results finite. */
int c3_model_load(C3Model *model, const char *path, char *error, size_t capacity);
int c3_model_parse(C3Model *model, const void *data, size_t bytes, char *error, size_t capacity);
void c3_model_close(C3Model *model);
void c3_model_animate(C3Model *model, float seconds);
#endif
