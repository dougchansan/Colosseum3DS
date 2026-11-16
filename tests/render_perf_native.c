/* Old-versus-new equivalence and host micro-benchmark for the two per-vertex
 * loops: c3_title_animate (src/title.c) and the renderer's batch staging
 * (the block of platform/3ds/title_renderer.c that test_render_perf.py slices
 * into render_perf_staging.inc).
 *
 * The reference_* routines are the per-vertex code as it was before the
 * renderer performance work, kept verbatim apart from writing to caller
 * arrays. normalize() and attenuation() did not change and are shared.
 * Host timings show relative change only; they are not ARM11 numbers. */
#define _POSIX_C_SOURCE 199309L
#include "colosseum3ds/title.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;
#include "render_perf_staging.inc"
#include "stage_cache.h"

/* ---- reference: animation as it was -------------------------------------- */
static uint32_t ref_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float ref_f32(const uint8_t *p)
{
    uint32_t bits = ref_u32(p); float value; memcpy(&value, &bits, sizeof value); return value;
}
static float ref_interpolate(const uint8_t *a, const uint8_t *b, uint32_t index, float fraction)
{
    float first=ref_f32(a+index*4u),last=ref_f32(b+index*4u);
    return first+(last-first)*fraction;
}
static int ref_normal_matrix(const float *m, float *n)
{
    double scale=0;
    static const unsigned basis[9]={0,1,2,4,5,6,8,9,10};
    for(unsigned k=0;k<9;++k){
        double value=fabs((double)m[basis[k]]);
        if(!isfinite(value))return 0;
        if(value>scale)scale=value;
    }
    if(scale==0)return 0;
    double a=m[0]/scale,b=m[1]/scale,c=m[2]/scale;
    double d=m[4]/scale,e=m[5]/scale,f=m[6]/scale;
    double g=m[8]/scale,h=m[9]/scale,i=m[10]/scale;
    double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!isfinite(determinant)||fabs(determinant)<1e-12)return 0;
    double inverse=1.0/(determinant*scale);
    n[0]=(float)((e*i-f*h)*inverse);
    n[1]=(float)((f*g-d*i)*inverse);
    n[2]=(float)((d*h-e*g)*inverse);
    n[3]=(float)((c*h-b*i)*inverse);
    n[4]=(float)((a*i-c*g)*inverse);
    n[5]=(float)((b*g-a*h)*inverse);
    n[6]=(float)((b*f-c*e)*inverse);
    n[7]=(float)((c*d-a*f)*inverse);
    n[8]=(float)((a*e-b*d)*inverse);
    for(unsigned k=0;k<9;++k)if(!isfinite(n[k]))return 0;
    return 1;
}

static C3TitlePose ref_pose;
static void reference_animate(const C3Title *title, uint32_t clip_index, float seconds, C3TitleDrawVertex *draw)
{
    static float normals[C3_TITLE_MAX_BONES][9];
    const C3TitleClip *clip=title->clips+clip_index;
    const uint8_t *first,*last;
    uint32_t index,next,bone,batch,i,j;
    double frame;
    float fraction;
    C3TitlePose *pose=&ref_pose;
    frame=seconds>0 ? (double)seconds*(double)title->fps : 0.0;
    if (clip->loop && clip->frame_count>1u) frame=fmod(frame,(double)(clip->frame_count-1u));
    else if (frame>(double)(clip->frame_count-1u)) frame=(double)(clip->frame_count-1u);
    index=(uint32_t)frame;
    next=index+1u<clip->frame_count ? index+1u : index;
    fraction=(float)(frame-(double)index);
    first=title->file_data+title->frame_offset+(clip->first_frame+index)*title->frame_bytes;
    last=title->file_data+title->frame_offset+(clip->first_frame+next)*title->frame_bytes;
    memset(pose,0,sizeof *pose);
    for (bone=0; bone<title->bone_count; ++bone,first+=48u,last+=48u) {
        for (i=0;i<12;++i) pose->matrices[bone][i]=ref_interpolate(first,last,i,fraction);
        if (!ref_normal_matrix(pose->matrices[bone],normals[bone])) {
            /* Admissible only for a bone whose batches are hidden. */
            memset(normals[bone],0,sizeof normals[bone]);
            normals[bone][0]=normals[bone][4]=normals[bone][8]=1.0f;
        }
    }
    for (batch=0;batch<title->batch_count;++batch,first+=title->material_frame_bytes,last+=title->material_frame_bytes) {
        C3TitleMaterialState *state=pose->materials+batch;
        for (i=0;i<2;++i) {
            state->uv_scale[i]=ref_interpolate(first,last,i,fraction);
            state->uv_offset[i]=ref_interpolate(first,last,i+2u,fraction);
            state->uv2_scale[i]=title->format_version>=2u ? ref_interpolate(first,last,i+12u,fraction):1.0f;
            state->uv2_offset[i]=title->format_version>=2u ? ref_interpolate(first,last,i+14u,fraction):0.0f;
            state->uv_cross[i]=title->format_version>=3u ? ref_interpolate(first,last,i+16u,fraction):0.0f;
            state->uv2_cross[i]=title->format_version>=3u ? ref_interpolate(first,last,i+18u,fraction):0.0f;
        }
        for (i=0;i<3;++i) state->specular_origin[i]=title->format_version>=3u ? ref_interpolate(first,last,i+20u,fraction):0.0f;
        state->alpha=ref_interpolate(first,last,4u,fraction); state->visible=ref_f32(first+20u);
        for (i=0;i<3;++i) { state->tev0[i]=ref_f32(first+24u+i*4u); state->konst[i]=ref_f32(first+36u+i*4u); }
    }
    for (i=0;i<3;++i) { pose->camera.eye[i]=ref_interpolate(first,last,i,fraction); pose->camera.target[i]=ref_interpolate(first,last,i+3u,fraction); }
    pose->camera.roll=ref_interpolate(first,last,6u,fraction);
    pose->camera.fov_y_degrees=ref_interpolate(first,last,7u,fraction);
    pose->camera.near_plane=ref_interpolate(first,last,8u,fraction);
    pose->camera.far_plane=ref_interpolate(first,last,9u,fraction);
    pose->camera.aspect=ref_interpolate(first,last,10u,fraction);
    for (batch=0;batch<title->batch_count;++batch) {
        const C3TitleBatch *b=title->batches+batch;
        const C3TitleMaterialState *state=pose->materials+batch;
        for (i=b->first;i<b->first+b->count;++i) {
            const C3TitleVertex *source=title->vertices+i;
            C3TitleDrawVertex *target=draw+i;
            const float *m=pose->matrices[source->bone],*n=normals[source->bone];
            double length;
            for (j=0;j<3;++j) {
                target->pos[j]=m[j*4u]*source->pos[0]+m[j*4u+1u]*source->pos[1]+m[j*4u+2u]*source->pos[2]+m[j*4u+3u];
                target->normal[j]=n[j*3u]*source->normal[0]+n[j*3u+1u]*source->normal[1]+n[j*3u+2u]*source->normal[2];
            }
            length=sqrt((double)target->normal[0]*target->normal[0]+(double)target->normal[1]*target->normal[1]+(double)target->normal[2]*target->normal[2]);
            if (length>0.0) for (j=0;j<3;++j) target->normal[j]=(float)(target->normal[j]/length);
            target->uv[0]=source->uv[0]*state->uv_scale[0]+source->uv[1]*state->uv_cross[0]+state->uv_offset[0];
            target->uv[1]=source->uv[0]*state->uv_cross[1]+source->uv[1]*state->uv_scale[1]+state->uv_offset[1];
            target->uv2[0]=source->uv2[0]*state->uv2_scale[0]+source->uv2[1]*state->uv2_cross[0]+state->uv2_offset[0];
            target->uv2[1]=source->uv2[0]*state->uv2_cross[1]+source->uv2[1]*state->uv2_scale[1]+state->uv2_offset[1];
            memcpy(target->rgba,source->rgba,4);
        }
    }
}

/* The battle stage's second pass over every sampled vertex, as it was. */
static void reference_place(C3TitleDrawVertex *draw, uint32_t count, const float t[5])
{
    float co=cosf(t[3]),si=sinf(t[3]);
    for(uint32_t v=0;v<count;++v){C3TitleDrawVertex *d=&draw[v];float x=d->pos[0]*t[4],z=d->pos[2]*t[4],nx=d->normal[0],nz=d->normal[2];
        d->pos[0]=co*x+si*z+t[0];d->pos[1]=d->pos[1]*t[4]+t[1];d->pos[2]=-si*x+co*z+t[2];d->normal[0]=co*nx+si*nz;d->normal[2]=-si*nx+co*nz;
        for(unsigned axis=0;axis<3;++axis)assert(isfinite(d->pos[axis])&&isfinite(d->normal[axis]));}
}

/* ---- reference: staging as it was ---------------------------------------- */
static float ref_clamp01(float value)
{
    return fmaxf(0.0f, fminf(1.0f, value));
}
static float ref_spotlight_attenuation(const C3TitleLight *light, const float to_light[3])
{
    if (!light->spot_function || light->spot_cutoff <= 0 || light->spot_cutoff > 90) return 1;
    float direction[3];
    for (unsigned i=0;i<3;++i) direction[i]=light->interest[i]-light->pos[i];
    normalize(direction);
    float cosine=fmaxf(0, -(direction[0]*to_light[0]+direction[1]*to_light[1]+direction[2]*to_light[2]));
    float cutoff=cosf(light->spot_cutoff * (3.14159265358979323846f / 180.0f));
    if (cutoff >= 1) return cosine >= 1 ? 1 : 0;
    return fmaxf(0, (cosine*cosine-cutoff*cosine)/(1-cutoff));
}
static void ref_raster_color(const C3Title *title, const C3TitleBatch *batch,
                             const C3TitleMaterialState *material,
                             const C3TitleDrawVertex *source, float rgba[4])
{
    unsigned color_source = batch->render_mode & 3u;
    if (!color_source) color_source = 1;
    unsigned alpha_source = (batch->render_mode >> 13) & 3u;
    if (!alpha_source) alpha_source = color_source;
    float vertex_alpha = source->rgba[3] / 255.0f;
    if (!(batch->flags & C3_TITLE_LIT)) {
        for (unsigned c = 0; c < 3; ++c) {
            float vertex_color = source->rgba[c] / 255.0f;
            rgba[c] = color_source == 1 ? batch->diffuse[c] : color_source == 2 ? vertex_color :
                      ref_clamp01(vertex_color + batch->diffuse[c]);
        }
        rgba[3] = alpha_source == 1 ? material->alpha : alpha_source == 2 ? vertex_alpha : material->alpha * vertex_alpha;
        return;
    }
    float normal[3] = {source->normal[0], source->normal[1], source->normal[2]};
    normalize(normal);
    float light_color[3] = {0, 0, 0}, light_alpha = 0.0f;
    bool ambient_seen = false;
    for (uint32_t i = 0; i < title->light_count; ++i) {
        const C3TitleLight *light = &title->lights[i];
        if (light->flags & 32u) continue;
        unsigned type = light->flags & 3u;
        if (type == 0) {
            if (ambient_seen) continue;
            ambient_seen = true;
            if (light->flags & 4u) for (unsigned c = 0; c < 3; ++c) light_color[c] += batch->ambient[c] * light->rgba[c];
            if (light->flags & 16u) light_alpha += light->rgba[3];
            continue;
        }
        float direction[3];
        for (unsigned c = 0; c < 3; ++c)
            direction[c] = type == 1 ? light->pos[c] : light->pos[c] - source->pos[c];
        float distance = normalize(direction);
        float intensity = fmaxf(0.0f, normal[0]*direction[0] + normal[1]*direction[1] + normal[2]*direction[2]);
        if (type >= 2) intensity *= attenuation(light, distance);
        if (type == 3) intensity *= ref_spotlight_attenuation(light, direction);
        if (light->flags & 4u) for (unsigned c = 0; c < 3; ++c) light_color[c] += intensity * light->rgba[c];
        if (light->flags & 16u) light_alpha += intensity * light->rgba[3];
    }
    for (unsigned c = 0; c < 3; ++c)
        rgba[c] = ref_clamp01(light_color[c]) * (color_source & 2u ? source->rgba[c] / 255.0f : 1.0f);
    rgba[3] = (1.0f - ref_clamp01(light_alpha)) * (alpha_source & 2u ? vertex_alpha : 1.0f);
}
static void reference_stage(const C3Title *title, const C3TitleBatch *batch, const C3TitleMaterialState *material,
                            const C3TitleDrawVertex *draw, TitleGPUVertex *destination, bool flip_v)
{
    for (uint32_t j = batch->first; j < batch->first + batch->count; ++j) {
        const C3TitleDrawVertex *source = &draw[j];
        memcpy(destination[j].pos, source->pos, sizeof(destination[j].pos));
        memcpy(destination[j].uv, source->uv, sizeof(destination[j].uv));
        memcpy(destination[j].uv2, source->uv2, sizeof(destination[j].uv2));
        if (flip_v) destination[j].uv[1] = 1.0f-source->uv[1];
        ref_raster_color(title, batch, material, source, destination[j].rgba);
        memset(destination[j].specular, 0, sizeof(destination[j].specular));
    }
}

/* ---- synthetic C3T4 pack -------------------------------------------------- */
typedef struct {
    uint32_t bones, static_bones, batches, static_batches, vertices_per_batch, frames, lights;
    int rigid, unit_normals;
} Spec;

static uint32_t random_state = 0x13572468u;
static float unit_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return (float)(random_state >> 8) / 16777216.0f;
}
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8u));
}
static void putf(uint8_t *p, float value)
{
    uint32_t bits; memcpy(&bits, &value, 4); put32(p, bits);
}

static uint8_t *build(const Spec *spec, size_t *bytes)
{
    const uint32_t vertices = spec->batches * spec->vertices_per_batch;
    const uint32_t frame_bytes = (spec->bones + 1u) * 48u + spec->batches * 96u;
    const uint32_t vertex_offset = 128u, batch_offset = vertex_offset + vertices * 48u;
    const uint32_t texture_offset = batch_offset + spec->batches * 112u, clip_offset = texture_offset + 24u;
    const uint32_t light_offset = clip_offset + 16u, frame_offset = light_offset + spec->lights * 80u;
    const uint32_t pixel_offset = frame_offset + spec->frames * frame_bytes;
    const uint32_t counts[7] = {spec->bones, vertices, spec->batches, 1, 1, spec->frames, spec->lights};
    const uint32_t offsets[7] = {vertex_offset, batch_offset, texture_offset, clip_offset, light_offset, frame_offset, pixel_offset};
    *bytes = pixel_offset + 256u;
    uint8_t *p = calloc(1, *bytes);
    assert(p && spec->static_bones && spec->static_bones <= spec->bones && spec->lights <= 6u);
    memcpy(p, "C3T4", 4); put32(p + 4, 4); put32(p + 8, (uint32_t)*bytes);
    for (unsigned i = 0; i < 7; ++i) { put32(p + 12 + i * 4u, counts[i]); put32(p + 44 + i * 4u, offsets[i]); }
    putf(p + 40, 30); put32(p + 72, frame_bytes); put32(p + 76, 256);
    for (uint32_t i = 0; i < vertices; ++i) {
        uint8_t *v = p + vertex_offset + i * 48u;
        uint32_t batch = i / spec->vertices_per_batch;
        double n[3], length = 0;
        for (unsigned k = 0; k < 3; ++k) putf(v + k * 4u, unit_random() * 4.0f - 2.0f);
        do {
            length = 0;
            for (unsigned k = 0; k < 3; ++k) { n[k] = (double)unit_random() * 2.0 - 1.0; length += n[k] * n[k]; }
        } while (length < 0.01);
        length = sqrt(length) / (spec->unit_normals ? 1.0 : 0.9);
        for (unsigned k = 0; k < 3; ++k) putf(v + 12 + k * 4u, (float)(n[k] / length));
        putf(v + 24, unit_random()); putf(v + 28, unit_random());
        for (unsigned k = 0; k < 4; ++k) v[32 + k] = (uint8_t)(unit_random() * 255.99f);
        put32(v + 36, batch < spec->static_batches ? i % spec->static_bones : i % spec->bones);
        putf(v + 40, unit_random()); putf(v + 44, unit_random());
    }
    for (uint32_t i = 0; i < spec->batches; ++i) {
        uint8_t *b = p + batch_offset + i * 112u;
        static const uint32_t flags[4] = {C3_TITLE_LIT, 0, C3_TITLE_LIT, 0};
        static const uint32_t modes[4] = {0, 2u | (2u << 13), 3u, 3u};
        static const float colors[9] = {.8f, .7f, .6f, .5f, .4f, .3f, 1, 1, 1};
        put32(b, i * spec->vertices_per_batch); put32(b + 4, spec->vertices_per_batch);
        put32(b + 12, flags[i % 4u] | C3_TITLE_CULL_BACK); put32(b + 16, 4); put32(b + 24, modes[i % 4u]);
        for (unsigned k = 0; k < 9; ++k) putf(b + 32 + k * 4u, colors[k]);
        putf(b + 68, 50); putf(b + 72, .9f); putf(b + 76, 1);
        put32(b + 88, 7); put32(b + 92, 7); put32(b + 100, 3);
    }
    put32(p + texture_offset, 8); put32(p + texture_offset + 4, 8); put32(p + texture_offset + 8, pixel_offset);
    put32(p + texture_offset + 12, 256); put32(p + texture_offset + 20, 1);
    put32(p + clip_offset + 4, spec->frames); put32(p + clip_offset + 8, 1);
    for (uint32_t i = 0; i < spec->lights; ++i) {
        uint8_t *l = p + light_offset + i * 80u;
        static const uint32_t flags[6] = {20, 5, 21, 6, 7, 37};
        static const float rgba[6][4] = {{.3f,.3f,.35f,.1f},{.8f,.7f,.6f,1},{.2f,.3f,.4f,.2f},{.9f,.5f,.4f,1},{.6f,.9f,.7f,1},{1,1,1,1}};
        static const float pos[6][3] = {{0,0,0},{1,2,3},{-2,1,.5f},{3,4,5},{0,8,0},{1,1,1}};
        put32(l, flags[i]);
        for (unsigned k = 0; k < 4; ++k) putf(l + 8 + k * 4u, rgba[i][k]);
        for (unsigned k = 0; k < 3; ++k) putf(l + 24 + k * 4u, pos[i][k]);
        putf(l + 48, i == 3 ? .5f : .25f); putf(l + 52, 10); putf(l + 56, i == 3 ? 2.0f : i == 4 ? 3.0f : 0.0f);
        if (i == 4) { put32(l + 64, 3); putf(l + 68, 60); }
    }
    for (uint32_t f = 0; f < spec->frames; ++f) {
        uint8_t *frame = p + frame_offset + f * frame_bytes;
        for (uint32_t b = 0; b < spec->bones; ++b) {
            float moving = b >= spec->static_bones ? 1.0f : 0.0f;
            float angle = .37f * (float)b + .11f * (float)f * moving, c = cosf(angle), s = sinf(angle);
            float sx = spec->rigid ? 1.0f : 1.0f, sy = spec->rigid ? 1.0f : 1.3f, sz = spec->rigid ? 1.0f : .7f;
            /* Rotation about an axis that depends on the bone, then scale. */
            float m[12] = {c * sx, -s * sy, 0, (float)b,
                           s * sx, c * sy, 0, .01f * (float)f * moving,
                           0, 0, sz, -.5f * (float)b};
            if (b & 1u) { float swapped[12] = {m[10], 0, 0, m[3], 0, m[0], m[1], m[7], 0, m[4], m[5], m[11]}; memcpy(m, swapped, sizeof m); }
            for (unsigned k = 0; k < 12; ++k) putf(frame + b * 48u + k * 4u, m[k]);
        }
        for (uint32_t b = 0; b < spec->batches; ++b) {
            uint8_t *m = frame + spec->bones * 48u + b * 96u;
            putf(m, 1); putf(m + 4, 1);
            /* The second batch scrolls its UVs although its bones are static. */
            putf(m + 8, b == 1u ? .01f * (float)f : 0.0f);
            putf(m + 16, .9f); putf(m + 20, 1);
            for (unsigned k = 0; k < 3; ++k) putf(m + 36 + k * 4u, 1);
            putf(m + 48, 1); putf(m + 52, 1);
            putf(m + 80, (float)b); putf(m + 84, 1); putf(m + 88, 2);
        }
        uint8_t *camera = frame + spec->bones * 48u + spec->batches * 96u;
        putf(camera + 4, 5); putf(camera + 8, 10); putf(camera + 28, 45);
        putf(camera + 32, .1f); putf(camera + 36, 100); putf(camera + 40, 4.0f / 3.0f);
    }
    memset(p + pixel_offset, 255, 256);
    return p;
}

static void open_pack(C3Title *title, const Spec *spec)
{
    size_t bytes; char error[128];
    uint8_t *data = build(spec, &bytes);
    memset(title, 0, sizeof *title);
    if (!c3_title_parse(title, data, bytes, error, sizeof error)) { fprintf(stderr, "synthetic pack: %s\n", error); exit(1); }
    free(data);
}

/* ---- comparisons ----------------------------------------------------------- */
static float worst_normal, worst_position, worst_color;
static unsigned worst_byte;

static void same_vertices(const C3Title *title, const C3TitleDrawVertex *expected, uint32_t batch,
                          bool exact_positions, float position_tolerance, float normal_tolerance)
{
    const C3TitleBatch *b = &title->batches[batch];
    for (uint32_t i = b->first; i < b->first + b->count; ++i) {
        const C3TitleDrawVertex *got = &title->draw_vertices[i], *want = &expected[i];
        if (exact_positions) assert(!memcmp(got->pos, want->pos, sizeof got->pos));
        for (unsigned k = 0; k < 3; ++k) {
            float position = fabsf(got->pos[k] - want->pos[k]) / (1.0f + fabsf(want->pos[k]));
            float normal = fabsf(got->normal[k] - want->normal[k]);
            if (position > worst_position) worst_position = position;
            if (normal > worst_normal) worst_normal = normal;
            assert(position <= position_tolerance && normal <= normal_tolerance);
        }
        assert(!memcmp(got->uv, want->uv, sizeof got->uv) && !memcmp(got->uv2, want->uv2, sizeof got->uv2));
        assert(!memcmp(got->rgba, want->rgba, 4));
    }
}

static void same_staging(const C3Title *title, const C3TitleDrawVertex *draw, bool flip_v, bool exact)
{
    TitleGPUVertex *old = malloc(title->vertex_count * sizeof *old), *new = malloc(title->vertex_count * sizeof *new);
    LightSetup lights;
    assert(old && new);
    memset(old, 0x5a, title->vertex_count * sizeof *old); memset(new, 0xa5, title->vertex_count * sizeof *new);
    light_setup(title, &lights);
    for (uint32_t batch = 0; batch < title->batch_count; ++batch) {
        const C3TitleBatch *b = &title->batches[batch];
        const C3TitleMaterialState *material = &title->pose.materials[batch];
        reference_stage(title, b, material, draw, old, flip_v);
        stage_batch(&lights, b, material, draw + b->first, new + b->first, b->count, flip_v);
        for (uint32_t i = b->first; i < b->first + b->count; ++i) {
            assert(!memcmp(old[i].pos, new[i].pos, sizeof old[i].pos) && !memcmp(old[i].uv, new[i].uv, sizeof old[i].uv));
            assert(!memcmp(old[i].uv2, new[i].uv2, sizeof old[i].uv2));
            assert(!memcmp(old[i].specular, new[i].specular, sizeof old[i].specular));
            if (exact || !(b->flags & C3_TITLE_LIT)) assert(!memcmp(old[i].rgba, new[i].rgba, sizeof old[i].rgba));
            for (unsigned c = 0; c < 4; ++c) {
                float difference = fabsf(old[i].rgba[c] - new[i].rgba[c]);
                int a = (int)(old[i].rgba[c] * 255.0f + 0.5f), z = (int)(new[i].rgba[c] * 255.0f + 0.5f);
                unsigned steps = (unsigned)abs(a - z);
                if (difference > worst_color) worst_color = difference;
                if (steps > worst_byte) worst_byte = steps;
                /* Far inside one 8-bit step (1/255 = 3.9e-3). */
                assert(difference <= 1e-4f && steps <= 1u);
            }
        }
    }
    free(old); free(new);
}

/* The renderer's per-model staging step: only batches whose tracked inputs
 * changed are written into the persistent arena. Returns vertices staged. */
static uint32_t stage_tracked(const C3Title *title, C3StageVertexCache *cache, const void *view, TitleGPUVertex *arena)
{
    LightSetup lights;
    uint32_t staged = 0;
    bool tracked = c3_stage_cache_frame(cache, title, view);
    light_setup(title, &lights);
    for (uint32_t batch = 0; batch < title->batch_count; ++batch) {
        const C3TitleBatch *b = &title->batches[batch];
        if (title->pose.materials[batch].visible < 0.5f) continue;
        if (tracked && c3_stage_cache_batch(cache, title, batch, false)) continue;
        stage_batch(&lights, b, &title->pose.materials[batch], title->draw_vertices + b->first, arena + b->first, b->count, false);
        staged += b->count;
    }
    return staged;
}

/* Every visible range of the persistent arena equals a full restage. */
static void arena_is_current(const C3Title *title, const TitleGPUVertex *arena, TitleGPUVertex *fresh)
{
    LightSetup lights;
    light_setup(title, &lights);
    for (uint32_t batch = 0; batch < title->batch_count; ++batch) {
        const C3TitleBatch *b = &title->batches[batch];
        if (title->pose.materials[batch].visible < 0.5f) continue;
        stage_batch(&lights, b, &title->pose.materials[batch], title->draw_vertices + b->first, fresh + b->first, b->count, false);
        assert(!memcmp(arena + b->first, fresh + b->first, b->count * sizeof *arena));
    }
}

static void equivalence(void)
{
    static const float times[] = {0.0f, .0123f, .25f, .5f, .7777f, 1.0f, 1.31f};
    static const float placement[5] = {3.5f, -1.25f, 7.0f, 2.1f, .75f};
    for (int variant = 0; variant < 4; ++variant) {
        Spec spec = {12, 3, 8, 3, 300, 40, 6, variant < 2, (variant & 1) == 0};
        C3Title title;
        C3TitlePerf perf;
        open_pack(&title, &spec);
        C3TitleDrawVertex *expected = malloc(title.vertex_count * sizeof *expected);
        assert(expected);
        /* The skip is taken only for rigid bones over unit normals. */
        bool skip = spec.rigid && spec.unit_normals;
        for (uint32_t batch = 0; batch < title.batch_count; ++batch) assert(title.batch_unit_normals[batch] == spec.unit_normals);
        /* Legacy contract: every vertex, positions and UVs bit-identical. */
        for (unsigned i = 0; i < sizeof times / sizeof times[0]; ++i) {
            c3_title_perf_take(NULL);
            assert(c3_title_animate(&title, 0, times[i]));
            c3_title_perf_take(&perf);
            assert(perf.calls == 1 && perf.vertices_animated == title.vertex_count && !perf.vertices_retained);
            reference_animate(&title, 0, times[i], expected);
            assert(!memcmp(ref_pose.matrices, title.pose.matrices, sizeof ref_pose.matrices));
            assert(!memcmp(&ref_pose.camera, &title.pose.camera, sizeof ref_pose.camera));
            for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                same_vertices(&title, expected, batch, true, 0.0f, skip ? 4e-5f : 1e-6f);
                assert(!memcmp(&ref_pose.materials[batch], &title.pose.materials[batch], sizeof ref_pose.materials[batch]));
            }
            same_staging(&title, title.draw_vertices, (i & 1u) != 0, false);
            /* With normals that force the renormalisation both stagings agree bit for bit. */
            C3TitleDrawVertex *scaled = malloc(title.vertex_count * sizeof *scaled);
            assert(scaled);
            memcpy(scaled, title.draw_vertices, title.vertex_count * sizeof *scaled);
            for (uint32_t v = 0; v < title.vertex_count; ++v) for (unsigned k = 0; k < 3; ++k) scaled[v].normal[k] *= 1.5f;
            same_staging(&title, scaled, false, true);
            free(scaled);
        }
        /* Placement folded into the bones against the old second pass. */
        c3_title_place_yaw(&title, placement, cosf(placement[3]), sinf(placement[3]), placement[4], 0);
        for (unsigned i = 0; i < sizeof times / sizeof times[0]; ++i) {
            assert(c3_title_animate(&title, 0, times[i]));
            reference_animate(&title, 0, times[i], expected);
            reference_place(expected, title.vertex_count, placement);
            for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                same_vertices(&title, expected, batch, false, 2e-5f, skip ? 4e-5f : 2e-6f);
                /* Battle placement never moved the specular origins. */
                assert(!memcmp(&ref_pose.materials[batch], &title.pose.materials[batch], sizeof ref_pose.materials[batch]));
            }
            same_staging(&title, title.draw_vertices, false, false);
        }
        /* Retained: only changed, visible batches are rewritten, and what is
         * visible always equals a full reference sample. */
        title.retained = 1;
        uint32_t revisions[C3_TITLE_MAX_BATCHES];
        C3StageVertexCache *cache = calloc(1, sizeof *cache);
        TitleGPUVertex *arena = malloc(title.vertex_count * sizeof *arena), *fresh = malloc(title.vertex_count * sizeof *fresh);
        unsigned char view[64] = {0};
        assert(cache && arena && fresh);
        memset(arena, 0xee, title.vertex_count * sizeof *arena);
        for (unsigned i = 0; i < 3 * (sizeof times / sizeof times[0]); ++i) {
            float seconds = times[i % 7u] + (i >= 7u ? .05f : 0.0f);
            if (i == 9u) title.hidden = 1;
            if (i == 12u) title.hidden = 0;
            if (i == 15u) title.placement.enabled = 0;
            if (i == 17u) c3_title_place_yaw(&title, placement, 1.0f, 0.0f, 1.0f, 1);
            memcpy(revisions, title.batch_revision, sizeof revisions);
            c3_title_perf_take(NULL);
            assert(c3_title_animate(&title, 0, seconds));
            c3_title_perf_take(&perf);
            assert(perf.vertices_animated + perf.vertices_retained == title.vertex_count);
            reference_animate(&title, 0, seconds, expected);
            if (title.placement.enabled) {
                float t[5] = {placement[0], placement[1], placement[2], i >= 17u ? 0.0f : placement[3], i >= 17u ? 1.0f : placement[4]};
                reference_place(expected, title.vertex_count, t);
            }
            for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                if (title.hidden) { assert(title.pose.materials[batch].visible == 0 && title.batch_revision[batch] == revisions[batch]); continue; }
                assert(title.pose.materials[batch].visible == 1 && title.batch_current[batch]);
                same_vertices(&title, expected, batch, !title.placement.enabled, 2e-5f, skip ? 4e-5f : 2e-6f);
            }
            /* The persistent arena, staged through the renderer's cache keys,
             * always equals a full restage of what is visible. */
            if (i == 6u) title.lights[1].rgba[0] *= .5f;
            if (i == 13u) view[5] = 1;
            uint32_t staged = stage_tracked(&title, cache, view, arena);
            assert(staged == (title.hidden ? 0u : perf.vertices_animated) ||
                   (i == 6u && staged > perf.vertices_animated) || i == 12u);
            arena_is_current(&title, arena, fresh);
            assert(!stage_tracked(&title, cache, view, arena));
            if (i == 4u) {
                /* Lights alone restage the lit batches (every other one) and
                 * nothing else; a view change restages none of these. */
                title.lights[2].rgba[1] = .9f;
                assert(stage_tracked(&title, cache, view, arena) == title.vertex_count / 2u);
                view[1] = 7;
                assert(!stage_tracked(&title, cache, view, arena));
                arena_is_current(&title, arena, fresh);
                c3_stage_cache_reset(cache);
                assert(stage_tracked(&title, cache, view, arena) == title.vertex_count);
            }
            if (i == 8u) {
                /* Same clip time again: nothing may be rewritten. */
                memcpy(revisions, title.batch_revision, sizeof revisions);
                assert(c3_title_animate(&title, 0, seconds));
                c3_title_perf_take(&perf);
                assert(!perf.vertices_animated && perf.vertices_retained == title.vertex_count);
                assert(!memcmp(revisions, title.batch_revision, sizeof revisions));
            }
            if (i == 3u || i == 4u) {
                /* Static-bone batches stay; the UV-scroll batch (1) and every
                 * batch on moving bones are rewritten. */
                for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                    bool rewritten = title.batch_revision[batch] != revisions[batch];
                    assert(rewritten == (batch == 1u || batch >= spec.static_batches));
                }
                assert(perf.vertices_retained == 2u * spec.vertices_per_batch);
            }
            if (i == 17u) for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                /* origin=1 moves the specular origin with the actor. */
                assert(fabsf(title.pose.materials[batch].specular_origin[0] - ((float)batch + placement[0])) < 1e-5f);
            }
        }
        free(cache); free(arena); free(fresh);
        /* A failing placement leaves pose and vertices untouched. */
        C3TitlePose pose = title.pose;
        C3TitleDrawVertex first = title.draw_vertices[0];
        title.placement.matrix[3] = INFINITY;
        assert(!c3_title_animate(&title, 0, .3f));
        assert(!memcmp(&pose, &title.pose, sizeof pose) && !memcmp(&first, &title.draw_vertices[0], sizeof first));
        free(expected);
        c3_title_close(&title);
    }
    printf("render perf equivalence passed: normal %.3g, placed position %.3g (relative), colour %.3g (%u/255 steps)\n",
           (double)worst_normal, (double)worst_position, (double)worst_color, worst_byte);
}

/* ---- micro-benchmark -------------------------------------------------------- */
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static volatile float sink;
#define BENCH(label, vertices, body) do { \
    double best = 1e30; \
    for (unsigned round = 0; round < 7; ++round) { \
        double started = now_ns(); \
        for (unsigned pass = 0; pass < passes; ++pass) { body; } \
        double elapsed = (now_ns() - started) / ((double)passes * (double)(vertices)); \
        if (elapsed < best) best = elapsed; \
    } \
    printf("%-44s %8.2f ns/vertex\n", label, best); \
} while (0)

static void benchmark(void)
{
    const unsigned passes = 40;
    /* Roughly the battle's dynamic set: 20,160 vertices on 28 bones. */
    for (int rigid = 1; rigid >= 0; --rigid) {
        Spec spec = {28, 2, 8, 2, 2520, 60, 3, rigid, 1};
        C3Title title;
        open_pack(&title, &spec);
        uint32_t count = title.vertex_count;
        C3TitleDrawVertex *draw = malloc(count * sizeof *draw);
        TitleGPUVertex *gpu = malloc(count * sizeof *gpu);
        LightSetup lights;
        static const float placement[5] = {3.5f, -1.25f, 7.0f, 2.1f, .75f};
        float clock = 0;
        assert(draw && gpu);
        printf("%s bones, unit source normals, %lu vertices, %lu lights\n", rigid ? "rigid" : "scaled",
               (unsigned long)count, (unsigned long)title.light_count);
        BENCH("animate: old", count, (clock += .013f, reference_animate(&title, 0, clock, draw)));
        BENCH("animate: new (legacy contract)", count, (clock += .013f, c3_title_animate(&title, 0, clock)));
        BENCH("animate+place: old two passes", count,
              (clock += .013f, reference_animate(&title, 0, clock, draw), reference_place(draw, count, placement)));
        c3_title_place_yaw(&title, placement, cosf(placement[3]), sinf(placement[3]), placement[4], 0);
        BENCH("animate+place: new folded", count, (clock += .013f, c3_title_animate(&title, 0, clock)));
        title.placement.enabled = 0; title.retained = 1;
        BENCH("animate: new retained, clock advancing", count, (clock += .013f, c3_title_animate(&title, 0, clock)));
        BENCH("animate: new retained, clock held", count, c3_title_animate(&title, 0, clock));
        title.retained = 0;
        c3_title_animate(&title, 0, .4f);
        light_setup(&title, &lights);
        BENCH("stage: old", count, {
            for (uint32_t b = 0; b < title.batch_count; ++b)
                reference_stage(&title, &title.batches[b], &title.pose.materials[b], title.draw_vertices, gpu, false);
            sink = gpu[count - 1].rgba[0]; });
        BENCH("stage: new", count, {
            light_setup(&title, &lights);
            for (uint32_t b = 0; b < title.batch_count; ++b)
                stage_batch(&lights, &title.batches[b], &title.pose.materials[b],
                            title.draw_vertices + title.batches[b].first, gpu + title.batches[b].first,
                            title.batches[b].count, false);
            sink = gpu[count - 1].rgba[0]; });
        uint32_t lit = 0;
        for (uint32_t b = 0; b < title.batch_count; ++b) if (title.batches[b].flags & C3_TITLE_LIT) lit += title.batches[b].count;
        BENCH("stage lit batches only: old", lit, {
            for (uint32_t b = 0; b < title.batch_count; ++b) if (title.batches[b].flags & C3_TITLE_LIT)
                reference_stage(&title, &title.batches[b], &title.pose.materials[b], title.draw_vertices, gpu, false);
            sink = gpu[0].rgba[0]; });
        BENCH("stage lit batches only: new", lit, {
            for (uint32_t b = 0; b < title.batch_count; ++b) if (title.batches[b].flags & C3_TITLE_LIT)
                stage_batch(&lights, &title.batches[b], &title.pose.materials[b],
                            title.draw_vertices + title.batches[b].first, gpu + title.batches[b].first,
                            title.batches[b].count, false);
            sink = gpu[0].rgba[0]; });
        free(draw); free(gpu);
        c3_title_close(&title);
    }
    puts("host timing: relative change only, not ARM11 time");
}

/* ---- prepared packs --------------------------------------------------------
 * `packs FILE...`: the same comparisons on real C3T packs, and how many
 * vertices a retained model rewrites per frame over four seconds at 30 fps. */
static int packs(int count, char **paths)
{
    unsigned long total = 0, total_animated = 0, total_peak = 0;
    const unsigned frames = 120;
    printf("%-28s %7s %7s %10s %10s %10s\n", "pack", "verts", "bones", "avg/frame", "peak/frame", "unit-skip");
    for (int index = 0; index < count; ++index) {
        C3Title title;
        C3TitlePerf perf;
        char error[128];
        memset(&title, 0, sizeof title);
        if (!c3_title_load(&title, paths[index], error, sizeof error)) { fprintf(stderr, "%s: %s\n", paths[index], error); return 1; }
        C3TitleDrawVertex *expected = malloc(title.vertex_count * sizeof *expected);
        assert(expected);
        uint32_t unit = 0;
        for (uint32_t batch = 0; batch < title.batch_count; ++batch)
            if (title.batch_unit_normals[batch]) unit += title.batches[batch].count;
        for (uint32_t clip = 0; clip < title.clip_count; ++clip) for (unsigned step = 0; step < 6; ++step) {
            float seconds = (float)step * .37f;
            assert(c3_title_animate(&title, clip, seconds));
            reference_animate(&title, clip, seconds, expected);
            assert(!memcmp(ref_pose.matrices, title.pose.matrices, sizeof ref_pose.matrices));
            for (uint32_t batch = 0; batch < title.batch_count; ++batch) {
                assert(!memcmp(&ref_pose.materials[batch], &title.pose.materials[batch], sizeof ref_pose.materials[batch]));
                /* A hidden batch may sit on a collapsed bone; its normals are unused. */
                if (title.pose.materials[batch].visible >= 0.5f) same_vertices(&title, expected, batch, true, 0.0f, 4e-5f);
            }
            same_staging(&title, title.draw_vertices, false, false);
        }
        title.retained = 1;
        unsigned long animated = 0, peak = 0;
        assert(c3_title_animate(&title, 0, 0.0f));
        for (unsigned frame = 1; frame <= frames; ++frame) {
            float seconds = (float)frame / 30.0f;
            c3_title_perf_take(NULL);
            assert(c3_title_animate(&title, 0, seconds));
            c3_title_perf_take(&perf);
            animated += perf.vertices_animated;
            if (perf.vertices_animated > peak) peak = perf.vertices_animated;
            reference_animate(&title, 0, seconds, expected);
            for (uint32_t batch = 0; batch < title.batch_count; ++batch)
                if (title.pose.materials[batch].visible >= 0.5f) {
                    assert(title.batch_current[batch]);
                    same_vertices(&title, expected, batch, true, 0.0f, 4e-5f);
                }
        }
        const char *name = strrchr(paths[index], '/');
        printf("%-28s %7lu %7lu %10.1f %10lu %10lu\n", name ? name + 1 : paths[index], (unsigned long)title.vertex_count,
               (unsigned long)title.bone_count, (double)animated / frames, peak, (unsigned long)unit);
        total += title.vertex_count; total_animated += animated; total_peak += peak;
        free(expected);
        c3_title_close(&title);
    }
    printf("%-28s %7lu %7s %10.1f %10lu\n", "all packs", total, "", (double)total_animated / frames, total_peak);
    printf("old-versus-new on these packs: normal %.3g, colour %.3g (%u/255 steps)\n",
           (double)worst_normal, (double)worst_color, worst_byte);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "packs")) return packs(argc - 2, argv + 2);
    equivalence();
    if (argc > 1 && !strcmp(argv[1], "bench")) benchmark();
    return 0;
}
