#include "colosseum3ds/skin_uniforms.h"

#include <math.h>
#include <string.h>

int c3_skin_batch_supported(const C3Title *title, uint32_t batch)
{
    if (!title || batch >= title->batch_count) return 0;
    uint32_t flags = title->batches[batch].flags;
    if (flags & C3_TITLE_SPECULAR) return 0;
    /* Normal-generated texture2 needs the eye-space normal per vertex. */
    if ((flags & C3_TITLE_REFLECTION_BLEND) && !(flags & C3_TITLE_SECOND_UV_BLEND)) return 0;
    uint64_t bones = title->batch_bones[batch];
    unsigned count = 0;
    for (; bones; bones &= bones - 1u) ++count;
    return count >= 1u && count <= C3_SKIN_MAX_BONES;
}

unsigned c3_skin_slot(uint64_t batch_bones, uint32_t bone)
{
    uint64_t below = bone >= 64u ? batch_bones : batch_bones & ((UINT64_C(1) << bone) - 1u);
    unsigned count = 0;
    for (; below; below &= below - 1u) ++count;
    return count;
}

/* Mirrors the renderer's light_setup and attenuation(): the first active
 * ambient light, then infinite lights (direction normalised once) and point
 * lights in source order. Channels a light does not drive are zero. */
int c3_skin_lights(const C3Title *title, C3SkinLights *out)
{
    if (!title || !out) return 0;
    memset(out, 0, sizeof *out);
    int ambient_seen = 0;
    for (uint32_t i = 0; i < title->light_count && i < C3_TITLE_MAX_LIGHTS; ++i) {
        const C3TitleLight *light = &title->lights[i];
        if (light->flags & 32u) continue;
        unsigned type = light->flags & 3u;
        if (type == 0) {
            if (ambient_seen) continue;
            ambient_seen = 1;
        }
        if (!(light->flags & 20u)) continue;
        float color[4];
        for (unsigned c = 0; c < 3; ++c) color[c] = light->flags & 4u ? light->rgba[c] : 0.0f;
        color[3] = light->flags & 16u ? light->rgba[3] : 0.0f;
        if (type == 0) { memcpy(out->ambient, color, sizeof color); continue; }
        if (type == 3 || out->count >= C3_SKIN_MAX_LIGHTS) return 0;
        unsigned slot = out->count++;
        memcpy(out->color[slot], color, sizeof color);
        memcpy(out->vector[slot], light->pos, 3 * sizeof(float));
        if (type == 1) {
            float *v = out->vector[slot];
            float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
            if (length > 0.0f) { v[0] /= length; v[1] /= length; v[2] /= length; }
            continue; /* infinite: w = 0, no attenuation */
        }
        out->vector[slot][3] = 1.0f;
        float reference = light->reference_distance, brightness = light->reference_brightness;
        int function = (int)light->distance_function;
        if (reference <= 0.0f || brightness <= 0.0f || brightness >= 1.0f || function < 1 || function > 3) continue;
        float factor = (1.0f - brightness) / brightness;
        if (function == 1) out->attenuation[slot][0] = factor / reference;
        else if (function == 2) {
            out->attenuation[slot][0] = 0.5f * factor / reference;
            out->attenuation[slot][1] = 0.5f * factor / (reference * reference);
        } else out->attenuation[slot][1] = factor / (reference * reference);
    }
    return 1;
}

/* Mirrors stage_batch's colour/alpha source rules (HSD render_mode bits). */
void c3_skin_batch_uniforms(const C3TitleBatch *batch, const C3TitleMaterialState *m,
                            const C3SkinLights *lights, C3SkinBatchUniforms *out)
{
    memset(out, 0, sizeof *out);
    out->uv_rows[0][0] = m->uv_scale[0]; out->uv_rows[0][1] = m->uv_cross[0]; out->uv_rows[0][3] = m->uv_offset[0];
    out->uv_rows[1][0] = m->uv_cross[1]; out->uv_rows[1][1] = m->uv_scale[1]; out->uv_rows[1][3] = m->uv_offset[1];
    out->uv_rows[2][0] = m->uv2_scale[0]; out->uv_rows[2][1] = m->uv2_cross[0]; out->uv_rows[2][3] = m->uv2_offset[0];
    out->uv_rows[3][0] = m->uv2_cross[1]; out->uv_rows[3][1] = m->uv2_scale[1]; out->uv_rows[3][3] = m->uv2_offset[1];
    unsigned color_source = batch->render_mode & 3u;
    if (!color_source) color_source = 1;
    unsigned alpha_source = (batch->render_mode >> 13) & 3u;
    if (!alpha_source) alpha_source = color_source;
    if (!(batch->flags & C3_TITLE_LIT)) {
        /* 1: diffuse, 2: vertex, 3: clamp(vertex + diffuse). */
        for (unsigned c = 0; c < 3; ++c) out->base[c] = color_source == 2 ? 0.0f : batch->diffuse[c];
        out->mode[0] = color_source == 1 ? 0.0f : 1.0f;
        /* 1: material alpha, 2: vertex alpha, 3: their product. */
        out->alpha_mode[0] = alpha_source == 2 ? 1.0f : m->alpha;
        out->mode[2] = alpha_source & 2u ? 1.0f : 0.0f;
        return;
    }
    for (unsigned c = 0; c < 3; ++c) out->base[c] = batch->ambient[c] * lights->ambient[c];
    out->base[3] = lights->ambient[3];
    out->mode[1] = color_source & 2u ? 1.0f : 0.0f;
    out->mode[2] = alpha_source & 2u ? 1.0f : 0.0f;
    out->mode[3] = 1.0f;
    /* MObjMakeTExp: material alpha times (1 - raster alpha). */
    out->alpha_mode[0] = 1.0f; out->alpha_mode[1] = -1.0f;
}

unsigned c3_skin_palette(const C3Title *title, uint64_t batch_bones,
                         float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4])
{
    unsigned slot = 0;
    for (uint32_t bone = 0; bone < title->bone_count && bone < 64u; ++bone) {
        if (!((batch_bones >> bone) & 1u)) continue;
        if (slot >= C3_SKIN_MAX_BONES) break;
        const float *m = title->pose.skin[bone], *n = title->pose.skin_normal[bone];
        float (*r)[4] = rows + slot * C3_SKIN_ROWS_PER_BONE;
        for (unsigned row = 0; row < 3; ++row) {
            memcpy(r[row], m + row * 4u, 4 * sizeof(float));
            r[3 + row][0] = n[row * 3u]; r[3 + row][1] = n[row * 3u + 1u]; r[3 + row][2] = n[row * 3u + 2u];
            r[3 + row][3] = 0.0f;
        }
        ++slot;
    }
    return slot;
}

static float clamp01(float v) { return v > 1.0f ? 1.0f : v > 0.0f ? v : 0.0f; }

void c3_skin_eval(const float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4],
                  const C3SkinLights *lights, const C3SkinBatchUniforms *b,
                  const C3TitleVertex *v, unsigned slot, C3SkinOutput *out)
{
    const float (*r)[4] = rows + slot * C3_SKIN_ROWS_PER_BONE;
    float world[3], normal[3];
    for (unsigned i = 0; i < 3; ++i) {
        world[i] = r[i][0]*v->pos[0] + r[i][1]*v->pos[1] + r[i][2]*v->pos[2] + r[i][3];
        normal[i] = r[3+i][0]*v->normal[0] + r[3+i][1]*v->normal[1] + r[3+i][2]*v->normal[2];
    }
    float squared = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];
    float inverse = 1.0f / sqrtf(squared > 1e-30f ? squared : 1e-30f);
    for (unsigned i = 0; i < 3; ++i) normal[i] *= inverse;
    float sum[4] = {0, 0, 0, 0};
    for (unsigned l = 0; l < C3_SKIN_MAX_LIGHTS; ++l) {
        const float *lv = lights->vector[l];
        float d[3] = {lv[0] - world[0]*lv[3], lv[1] - world[1]*lv[3], lv[2] - world[2]*lv[3]};
        float d2 = d[0]*d[0] + d[1]*d[1] + d[2]*d[2];
        if (d2 < 1e-30f) d2 = 1e-30f;
        float inv = 1.0f / sqrtf(d2), distance = d2 * inv;
        float intensity = (normal[0]*d[0] + normal[1]*d[1] + normal[2]*d[2]) * inv;
        if (!(intensity > 0.0f)) intensity = 0.0f;
        intensity /= 1.0f + lights->attenuation[l][0]*distance + lights->attenuation[l][1]*d2;
        for (unsigned c = 0; c < 4; ++c) sum[c] += lights->color[l][c] * intensity;
    }
    float vertex[4];
    for (unsigned c = 0; c < 4; ++c) vertex[c] = (float)v->rgba[c] * (1.0f / 255.0f);
    float lit[4];
    for (unsigned c = 0; c < 4; ++c) lit[c] = b->base[c] + sum[c] * b->mode[3];
    for (unsigned c = 0; c < 3; ++c) lit[c] += vertex[c] * b->mode[0];
    for (unsigned c = 0; c < 4; ++c) lit[c] = clamp01(lit[c]);
    for (unsigned c = 0; c < 3; ++c) out->rgba[c] = lit[c] * (1.0f + (vertex[c] - 1.0f) * b->mode[1]);
    out->rgba[3] = (b->alpha_mode[0] + b->alpha_mode[1] * lit[3]) * (1.0f + (vertex[3] - 1.0f) * b->mode[2]);
    memcpy(out->pos, world, sizeof world);
    for (unsigned i = 0; i < 2; ++i) {
        out->uv[i] = b->uv_rows[i][0]*v->uv[0] + b->uv_rows[i][1]*v->uv[1] + b->uv_rows[i][3];
        out->uv2[i] = b->uv_rows[2+i][0]*v->uv2[0] + b->uv_rows[2+i][1]*v->uv2[1] + b->uv_rows[2+i][3];
    }
}
