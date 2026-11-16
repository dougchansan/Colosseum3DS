/* GPU skinning uniforms against the CPU skin and lighting paths.
 *
 * Synthetic checks cover every packing rule. With C3_SKIN_PACKS set to a
 * directory of prepared .c3t packs (e.g. the battle actors), every supported
 * batch of every pack is also sampled and compared vertex by vertex: the CPU
 * path (c3_title_animate's skin plus the renderer's stage_batch colour rules,
 * transcribed below) against c3_skin_eval fed from pose.skin. */
#include "colosseum3ds/skin_uniforms.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int near(float a, float b, float tolerance) { return fabsf(a - b) <= tolerance; }

static void synthetic(void)
{
    static C3Title t;
    memset(&t, 0, sizeof t);
    /* Light reduction: ambient, a second ambient (ignored), infinite, point. */
    t.light_count = 5;
    t.lights[0] = (C3TitleLight){.flags = 0u | 4u | 16u, .rgba = {0.2f, 0.4f, 0.6f, 0.3f}};
    t.lights[1] = (C3TitleLight){.flags = 0u | 4u, .rgba = {1, 1, 1, 1}};
    t.lights[2] = (C3TitleLight){.flags = 1u | 4u, .rgba = {0.5f, 0.25f, 0.125f, 0.9f}, .pos = {0, 2, 0}};
    t.lights[3] = (C3TitleLight){.flags = 2u | 4u | 16u, .rgba = {0.1f, 0.2f, 0.3f, 0.4f}, .pos = {0, 0, 5},
                                 .reference_brightness = 0.5f, .reference_distance = 2.0f, .distance_function = 2};
    t.lights[4] = (C3TitleLight){.flags = 1u | 4u | 32u, .rgba = {1, 1, 1, 1}}; /* hidden */
    C3SkinLights lights;
    assert(c3_skin_lights(&t, &lights) && lights.count == 2);
    assert(near(lights.ambient[0], 0.2f, 0) && near(lights.ambient[3], 0.3f, 0));
    assert(lights.vector[0][1] == 1.0f && lights.vector[0][3] == 0.0f && lights.color[0][3] == 0.0f);
    assert(lights.vector[1][2] == 5.0f && lights.vector[1][3] == 1.0f && near(lights.color[1][3], 0.4f, 0));
    /* function 2, brightness 0.5 (factor 1), reference 2: 1 + 0.5(d/2 + d^2/4). */
    assert(near(lights.attenuation[1][0], 0.25f, 1e-7f) && near(lights.attenuation[1][1], 0.125f, 1e-7f));
    t.lights[3].distance_function = 1;
    assert(c3_skin_lights(&t, &lights) && near(lights.attenuation[1][0], 0.5f, 1e-7f) && lights.attenuation[1][1] == 0);
    t.lights[3].distance_function = 3;
    assert(c3_skin_lights(&t, &lights) && lights.attenuation[1][0] == 0 && near(lights.attenuation[1][1], 0.25f, 1e-7f));
    t.lights[3].distance_function = 0;
    assert(c3_skin_lights(&t, &lights) && lights.attenuation[1][0] == 0 && lights.attenuation[1][1] == 0);
    C3Title spot = t; spot.lights[2].flags = 3u | 4u;
    assert(!c3_skin_lights(&spot, &lights));
    C3Title many = t; many.light_count = 8;
    for (unsigned i = 4; i < 8; ++i) many.lights[i] = (C3TitleLight){.flags = 1u | 4u, .rgba = {1, 1, 1, 1}, .pos = {1, 0, 0}};
    assert(!c3_skin_lights(&many, &lights)); /* 6 directional/point lights */
    assert(c3_skin_lights(&t, &lights));

    /* Slots and palette rows. */
    uint64_t set = (UINT64_C(1) << 3) | (UINT64_C(1) << 7) | (UINT64_C(1) << 40);
    assert(c3_skin_slot(set, 3) == 0 && c3_skin_slot(set, 7) == 1 && c3_skin_slot(set, 40) == 2);
    t.bone_count = 41;
    for (unsigned k = 0; k < 12; ++k) t.pose.skin[7][k] = (float)(k + 1);
    for (unsigned k = 0; k < 9; ++k) t.pose.skin_normal[7][k] = (float)(k + 20);
    static float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4];
    assert(c3_skin_palette(&t, set, rows) == 3);
    assert(rows[6][0] == 1 && rows[6][3] == 4 && rows[8][3] == 12 && rows[9][0] == 20 && rows[11][2] == 28 && rows[11][3] == 0);

    /* Supported batches: specular, normal-generated UVs and >10 bones fall back. */
    static C3TitleBatch batches[4];
    t.batches = batches; t.batch_count = 4;
    batches[0].flags = C3_TITLE_LIT; t.batch_bones[0] = set;
    batches[1].flags = C3_TITLE_LIT | C3_TITLE_SPECULAR; t.batch_bones[1] = 1;
    batches[2].flags = C3_TITLE_REFLECTION_BLEND; t.batch_bones[2] = 1;
    batches[3].flags = 0; t.batch_bones[3] = (UINT64_C(1) << 11) - 1u;
    assert(c3_skin_batch_supported(&t, 0) && !c3_skin_batch_supported(&t, 1) &&
           !c3_skin_batch_supported(&t, 2) && !c3_skin_batch_supported(&t, 3) && !c3_skin_batch_supported(&t, 4));
    batches[2].flags |= C3_TITLE_SECOND_UV_BLEND; assert(c3_skin_batch_supported(&t, 2));

    /* One vertex through the shader mirror: identity bone, normal +Y. */
    memset(rows, 0, sizeof rows);
    rows[0][0] = rows[1][1] = rows[2][2] = 1; rows[0][3] = 1; /* translate x by 1 */
    rows[3][0] = rows[4][1] = rows[5][2] = 1;
    C3TitleVertex v = {.pos = {0, 0, 0}, .normal = {0, 2, 0}, .uv = {0.25f, 0.5f}, .rgba = {255, 128, 0, 51}, .uv2 = {1, 2}};
    C3TitleMaterialState m = {.uv_scale = {2, 3}, .uv_offset = {0.5f, -1}, .uv_cross = {0.25f, 0.125f},
                              .uv2_scale = {1, 1}, .alpha = 0.75f};
    C3TitleBatch lit = {.flags = C3_TITLE_LIT, .render_mode = 2u | (2u << 13), .ambient = {1, 0.5f, 0.25f}};
    C3SkinBatchUniforms u; C3SkinOutput out;
    c3_skin_batch_uniforms(&lit, &m, &lights, &u);
    c3_skin_eval((const float (*)[4])rows, &lights, &u, &v, 0, &out);
    assert(out.pos[0] == 1 && out.pos[1] == 0 && out.pos[2] == 0);
    assert(near(out.uv[0], 0.25f*2 + 0.5f*0.25f + 0.5f, 1e-6f) && near(out.uv[1], 0.25f*0.125f + 0.5f*3 - 1, 1e-6f));
    /* Infinite light straight overhead (intensity 1); point light at (0,0,5)
     * from world (1,0,0) is perpendicular to +Y (intensity 0). */
    float red = 0.2f*1 + 0.5f, green = 0.4f*0.5f + 0.25f, blue = 0.6f*0.25f + 0.125f;
    assert(near(out.rgba[0], red, 1e-6f) && near(out.rgba[1], green * 128.0f/255.0f, 1e-6f) && near(out.rgba[2], blue * 0.0f, 1e-6f));
    assert(near(out.rgba[3], (1.0f - 0.3f) * 51.0f/255.0f, 1e-6f));
    /* Unlit sources: 1 diffuse, 2 vertex, 3 clamp(vertex + diffuse). */
    C3TitleBatch unlit = {.render_mode = 1u, .diffuse = {0.5f, 0.5f, 0.5f}};
    c3_skin_batch_uniforms(&unlit, &m, &lights, &u); c3_skin_eval((const float (*)[4])rows, &lights, &u, &v, 0, &out);
    assert(out.rgba[0] == 0.5f && out.rgba[2] == 0.5f && out.rgba[3] == 0.75f);
    unlit.render_mode = 2u;
    c3_skin_batch_uniforms(&unlit, &m, &lights, &u); c3_skin_eval((const float (*)[4])rows, &lights, &u, &v, 0, &out);
    assert(out.rgba[0] == 1.0f && near(out.rgba[1], 128.0f/255.0f, 1e-7f) && out.rgba[2] == 0 && near(out.rgba[3], 0.2f, 1e-7f));
    unlit.render_mode = 3u | (3u << 13);
    c3_skin_batch_uniforms(&unlit, &m, &lights, &u); c3_skin_eval((const float (*)[4])rows, &lights, &u, &v, 0, &out);
    /* 128/255 + 0.5 saturates to 1. */
    assert(out.rgba[0] == 1.0f && out.rgba[1] == 1.0f && out.rgba[2] == 0.5f && near(out.rgba[3], 0.75f*0.2f, 1e-7f));
}

/* --- CPU reference: stage_batch's colour (platform/3ds/title_renderer.c). --- */
static float unit_interval(float v) { return !(v < 1.0f) ? 1.0f : v > 0.0f ? v : 0.0f; }
static float normalize3(float v[3])
{
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    return l;
}
static void cpu_color(const C3Title *t, const C3TitleBatch *b, const C3TitleMaterialState *m,
                      const C3TitleDrawVertex *s, float rgba[4])
{
    unsigned cs = b->render_mode & 3u; if (!cs) cs = 1;
    unsigned as = (b->render_mode >> 13) & 3u; if (!as) as = cs;
    float vtx[4]; for (unsigned c = 0; c < 4; ++c) vtx[c] = (float)s->rgba[c] / 255.0f;
    if (!(b->flags & C3_TITLE_LIT)) {
        for (unsigned c = 0; c < 3; ++c) rgba[c] = cs == 1 ? b->diffuse[c] : cs == 2 ? vtx[c] : unit_interval(vtx[c] + b->diffuse[c]);
        rgba[3] = as == 1 ? m->alpha : as == 2 ? vtx[3] : m->alpha * vtx[3];
        return;
    }
    float n[3] = {s->normal[0], s->normal[1], s->normal[2]}, sum[4] = {0, 0, 0, 0};
    float sq = n[0]*n[0] + n[1]*n[1] + n[2]*n[2];
    if (fabsf(sq - 1.0f) > 1e-4f) normalize3(n);
    int ambient_seen = 0;
    for (uint32_t i = 0; i < t->light_count; ++i) {
        const C3TitleLight *l = &t->lights[i];
        if (l->flags & 32u) continue;
        unsigned type = l->flags & 3u;
        if (type == 0) { if (ambient_seen) continue; ambient_seen = 1; }
        if (!(l->flags & 20u)) continue;
        float col[4]; for (unsigned c = 0; c < 3; ++c) col[c] = l->flags & 4u ? l->rgba[c] : 0; col[3] = l->flags & 16u ? l->rgba[3] : 0;
        if (type == 0) { for (unsigned c = 0; c < 3; ++c) sum[c] += b->ambient[c] * col[c]; sum[3] += col[3]; continue; }
        float intensity;
        if (type == 1) { float d[3] = {l->pos[0], l->pos[1], l->pos[2]}; normalize3(d);
            intensity = n[0]*d[0] + n[1]*d[1] + n[2]*d[2]; intensity = intensity > 0 ? intensity : 0; }
        else {
            float d[3] = {l->pos[0]-s->pos[0], l->pos[1]-s->pos[1], l->pos[2]-s->pos[2]}, dist = normalize3(d);
            intensity = n[0]*d[0] + n[1]*d[1] + n[2]*d[2]; intensity = intensity > 0 ? intensity : 0;
            float ref = l->reference_distance, br = l->reference_brightness; int f = (int)l->distance_function;
            if (!(ref <= 0 || br <= 0 || br >= 1 || f < 1 || f > 3)) {
                float r = dist / ref, k = (1 - br) / br;
                intensity /= f == 1 ? 1 + k*r : f == 2 ? 1 + 0.5f*k*(r + r*r) : 1 + k*r*r;
            }
        }
        for (unsigned c = 0; c < 4; ++c) sum[c] += intensity * col[c];
    }
    for (unsigned c = 0; c < 3; ++c) rgba[c] = unit_interval(sum[c]) * (cs & 2u ? vtx[c] : 1.0f);
    rgba[3] = (1.0f - unit_interval(sum[3])) * (as & 2u ? vtx[3] : 1.0f);
}

static unsigned compared, worst_bucket[4];
static float worst_pos, worst_color, worst_uv;

static void parity(const char *path)
{
    static C3Title t; char error[160];
    memset(&t, 0, sizeof t);
    if (!c3_title_load(&t, path, error, sizeof error)) { printf("skip %s: %s\n", path, error); return; }
    C3SkinLights lights;
    if (!c3_skin_lights(&t, &lights)) { printf("skip %s: lights unsupported\n", path); c3_title_close(&t); return; }
    uint64_t supported = 0;
    for (uint32_t i = 0; i < t.batch_count; ++i) if (c3_skin_batch_supported(&t, i)) supported |= UINT64_C(1) << i;
    /* Place the model as a battle actor would (rotation, scale, offset). */
    float position[3] = {3.5f, -0.25f, 7.0f};
    c3_title_place_yaw(&t, position, cosf(0.7f), sinf(0.7f), 1.3f, 0);
    float seconds[] = {0.0f, 0.37f, 1.9f};
    for (unsigned clip = 0; clip < t.clip_count && clip < 3; ++clip) for (unsigned k = 0; k < 3; ++k) {
        t.retained = 0; t.gpu_batches = 0;
        assert(c3_title_animate(&t, clip, seconds[k]));
        static C3TitleDrawVertex cpu[C3_TITLE_MAX_VERTICES];
        memcpy(cpu, t.draw_vertices, t.vertex_count * sizeof *cpu);
        t.retained = 1; t.gpu_batches = supported;
        assert(c3_title_animate(&t, clip, seconds[k]));
        for (uint32_t i = 0; i < t.batch_count; ++i) {
            if (!((supported >> i) & 1u) || t.pose.materials[i].visible < 0.5f) continue;
            const C3TitleBatch *b = &t.batches[i];
            static float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4];
            c3_skin_palette(&t, t.batch_bones[i], rows);
            C3SkinBatchUniforms u; c3_skin_batch_uniforms(b, &t.pose.materials[i], &lights, &u);
            for (uint32_t j = b->first; j < b->first + b->count; ++j) {
                C3SkinOutput out; float ref[4];
                c3_skin_eval((const float (*)[4])rows, &lights, &u, &t.vertices[j], c3_skin_slot(t.batch_bones[i], t.vertices[j].bone), &out);
                cpu_color(&t, b, &t.pose.materials[i], &cpu[j], ref);
                for (unsigned c = 0; c < 3; ++c) {
                    float scale = fmaxf(1.0f, fabsf(cpu[j].pos[c]));
                    float e = fabsf(out.pos[c] - cpu[j].pos[c]) / scale; if (e > worst_pos) worst_pos = e;
                }
                for (unsigned c = 0; c < 4; ++c) {
                    float e = fabsf(out.rgba[c] - ref[c]) * 255.0f; if (e > worst_color) worst_color = e;
                    worst_bucket[e < 0.01f ? 0 : e < 0.1f ? 1 : e < 1.0f ? 2 : 3]++;
                }
                for (unsigned c = 0; c < 2; ++c) {
                    float e = fmaxf(fabsf(out.uv[c] - cpu[j].uv[c]), fabsf(out.uv2[c] - cpu[j].uv2[c]));
                    if (e > worst_uv) worst_uv = e;
                }
                ++compared;
            }
        }
    }
    c3_title_close(&t);
}

int main(void)
{
    synthetic();
    const char *dir = getenv("C3_SKIN_PACKS");
    if (!dir) { printf("skin uniforms: synthetic checks passed; C3_SKIN_PACKS unset, no pack parity\n"); return 0; }
    DIR *d = opendir(dir); assert(d);
    struct dirent *e; char path[1024];
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".c3t")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        parity(path);
    }
    closedir(d);
    printf("skin parity: %u vertices; worst position %.2e (relative), colour %.4f/255, uv %.2e; colour error buckets <0.01:%u <0.1:%u <1:%u >=1:%u\n",
           compared, worst_pos, worst_color, worst_uv, worst_bucket[0], worst_bucket[1], worst_bucket[2], worst_bucket[3]);
    assert(compared > 0 && worst_pos < 1e-5f && worst_color < 0.5f && worst_uv < 1e-5f);
    return 0;
}
