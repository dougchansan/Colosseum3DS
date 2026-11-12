#ifndef COLOSSEUM3DS_SKIN_UNIFORMS_H
#define COLOSSEUM3DS_SKIN_UNIFORMS_H

/* GPU skinning: the uniforms the skin vertex shader
 * (platform/3ds/title_skin_shader.pica) reads, packed from a sampled title,
 * and a C mirror of that shader for host tests. Host-compilable.
 *
 * A GPU-skinned batch replaces two CPU passes: c3_title_animate's per-vertex
 * skin (position, normal, UV transforms) and the renderer's per-vertex
 * lighting. Its vertices stay in bind space in a static stream; per draw the
 * shader receives at most C3_SKIN_MAX_BONES placed bone matrices with their
 * normal matrices, the batch's UV rows and material, and the model's lights.
 *
 * Supported lights: one ambient (folded into the batch base colour) and up to
 * C3_SKIN_MAX_LIGHTS infinite or point lights with distance attenuation.
 * Spotlights, specular and normal-generated (reflection) UVs stay on the CPU
 * path. Every vec4 is (x, y, z, w). */

#include "colosseum3ds/title.h"
#include <stdint.h>

#define C3_SKIN_MAX_BONES 10u
#define C3_SKIN_MAX_LIGHTS 4u
#define C3_SKIN_ROWS_PER_BONE 6u /* 3 position rows, 3 normal rows */

typedef struct {
    /* xyz: unit direction to an infinite light, or a point light's position;
     * w: 0 infinite, 1 point. */
    float vector[C3_SKIN_MAX_LIGHTS][4];
    float color[C3_SKIN_MAX_LIGHTS][4];       /* diffuse rgb, alpha channel */
    /* x, y: 1 / (1 + x*d + y*d*d); zero for no attenuation. */
    float attenuation[C3_SKIN_MAX_LIGHTS][4];
    float ambient[4];                        /* the ambient light's colour and alpha */
    unsigned count;
} C3SkinLights;

typedef struct {
    float uv_rows[4][4];  /* u', v' (texcoord0), u2', v2' (texcoord1): dot((u, v, 0, 1)) */
    float base[4];        /* rgb: lit ambient term or unlit diffuse; a: lit ambient alpha */
    float mode[4];        /* x: add vertex rgb, y: multiply vertex rgb, z: multiply vertex alpha, w: light weight */
    float alpha_mode[4];  /* alpha = (x + y * clamp(light alpha)) * vertex alpha factor */
} C3SkinBatchUniforms;

/* Batches the GPU path can draw: no specular, no normal-generated UVs and at
 * most C3_SKIN_MAX_BONES bones. Lights are checked by c3_skin_lights. */
int c3_skin_batch_supported(const C3Title *title, uint32_t batch);
/* Palette slot of bone within a batch's bone set (ascending bone order). */
unsigned c3_skin_slot(uint64_t batch_bones, uint32_t bone);
/* Returns 0 when the model's active lights exceed the shader (spotlights,
 * more than C3_SKIN_MAX_LIGHTS, or two ambient lights are handled as the CPU
 * path does: only the first ambient counts). */
int c3_skin_lights(const C3Title *title, C3SkinLights *out);
void c3_skin_batch_uniforms(const C3TitleBatch *batch, const C3TitleMaterialState *material,
                            const C3SkinLights *lights, C3SkinBatchUniforms *out);
/* rows[slot*6 + 0..2] position rows, [slot*6 + 3..5] normal rows (w = 0),
 * from pose.skin / pose.skin_normal. Returns the number of slots written. */
unsigned c3_skin_palette(const C3Title *title, uint64_t batch_bones,
                         float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4]);

/* C mirror of the shader for one vertex: world position, rgba colour and
 * both texture coordinates. */
typedef struct { float pos[3], rgba[4], uv[2], uv2[2]; } C3SkinOutput;
void c3_skin_eval(const float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4],
                  const C3SkinLights *lights, const C3SkinBatchUniforms *batch,
                  const C3TitleVertex *vertex, unsigned slot, C3SkinOutput *out);

#endif
