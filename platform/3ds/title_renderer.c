#include "title_renderer.h"

#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "title_shader_shbin.h"

typedef struct { float pos[3], uv[2], rgba[4]; } TitleGPUVertex;

static const C3Title *loaded_title;
static bool c3d_started, program_started;
static C3D_RenderTarget *target;
static DVLB_s *shader;
static shaderProgram_s program;
static int mvp_location;
static TitleGPUVertex *vertices;
static C3D_Tex textures[C3_TITLE_MAX_TEXTURES];
static uint32_t texture_count;
static size_t gpu_buffer_bytes;

static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return -1;
}

static float clamp01(float value)
{
    return fmaxf(0.0f, fminf(1.0f, value));
}

static uint32_t color4(float r, float g, float b, float a)
{
    return (uint32_t)(clamp01(r) * 255.0f + 0.5f) |
          ((uint32_t)(clamp01(g) * 255.0f + 0.5f) << 8) |
          ((uint32_t)(clamp01(b) * 255.0f + 0.5f) << 16) |
          ((uint32_t)(clamp01(a) * 255.0f + 0.5f) << 24);
}

static float normalize(float v[3])
{
    float length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length > 1e-12f) for (unsigned i = 0; i < 3; ++i) v[i] /= length;
    else { v[0] = v[1] = 0.0f; v[2] = 1.0f; }
    return length;
}

static unsigned morton8(unsigned x, unsigned y)
{
    return (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) |
           ((y & 2u) << 2) | ((x & 4u) << 2) | ((y & 4u) << 3);
}

static GPU_TEXTURE_WRAP_PARAM wrap_mode(uint32_t mode)
{
    return mode == 2 ? GPU_MIRRORED_REPEAT : mode == 1 ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
}

static bool upload_texture(C3D_Tex *gpu, const C3TitleTexture *source)
{
    if (!C3D_TexInit(gpu, source->width, source->height, GPU_RGBA8)) return false;
    /* PICA RGBA8 is byte-ordered ABGR, with 8x8 Morton tiles. Keep top-origin
     * GX UVs by reversing the stored pixel rows. The colored-corner
     * native probe verifies this pairing, including the screen rotation. */
    for (uint32_t y = 0; y < source->height; ++y) {
        for (uint32_t x = 0; x < source->width; ++x) {
            size_t tile = ((size_t)(y >> 3) * (source->width >> 3) + (x >> 3)) * 64;
            uint8_t *out = (uint8_t *)gpu->data + (tile + morton8(x, y)) * 4;
            const uint8_t *in = source->pixels + ((size_t)(source->height - 1 - y) * source->width + x) * 4;
            out[0] = in[3]; out[1] = in[2]; out[2] = in[1]; out[3] = in[0];
        }
    }
    C3D_TexSetFilter(gpu, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(gpu, wrap_mode(source->wrap_s), wrap_mode(source->wrap_t));
    C3D_TexFlush(gpu);
    return true;
}

static bool valid_batch(const C3Title *title, const C3TitleBatch *batch)
{
    if (!batch->count || batch->count % 3 || batch->first > title->vertex_count ||
        batch->count > title->vertex_count - batch->first || batch->texture >= title->texture_count ||
        (batch->color_mode != 0 && batch->color_mode != 3 && batch->color_mode != 4) ||
        (batch->alpha_mode != 0 && batch->alpha_mode != 2 && batch->alpha_mode != 3) ||
        (batch->flags & C3_TITLE_SPECULAR) || batch->depth_compare > 7 ||
        batch->alpha_compare0 != 7 || batch->alpha_compare1 != 7 || batch->alpha_op != 0)
        return false;
    return true;
}

int c3_title_renderer_init(const C3Title *title, char *error, size_t capacity)
{
    if (c3d_started) return fail(error, capacity, "Title renderer is already initialized.");
    if (!title || !title->draw_vertices || !title->batches || !title->textures ||
        !title->vertex_count || title->vertex_count > C3_TITLE_MAX_VERTICES ||
        !title->batch_count || title->batch_count > C3_TITLE_MAX_BATCHES ||
        !title->texture_count || title->texture_count > C3_TITLE_MAX_TEXTURES ||
        title->light_count > C3_TITLE_MAX_LIGHTS)
        return fail(error, capacity, "Title renderer needs a valid bounded title asset.");
    uint32_t next_vertex = 0;
    for (uint32_t i = 0; i < title->batch_count; ++i) {
        if (!valid_batch(title, &title->batches[i]) || title->batches[i].first != next_vertex)
            return fail(error, capacity, "Unsupported title material or invalid geometry batch.");
        next_vertex += title->batches[i].count;
    }
    if (next_vertex != title->vertex_count)
        return fail(error, capacity, "Title batches must partition the expanded vertex arena.");
    for (uint32_t i = 0; i < title->light_count; ++i)
        if ((title->lights[i].flags & 3u) == 3u || title->lights[i].attenuation_flags)
            return fail(error, capacity, "Spot/raw-attenuation lights are outside the title subset.");
    for (uint32_t i = 0; i < title->texture_count; ++i) {
        const C3TitleTexture *t = &title->textures[i];
        if (!t->pixels || t->width < 8 || t->height < 8 || t->width > 1024 || t->height > 1024 ||
            (t->width & (t->width - 1)) || (t->height & (t->height - 1)) ||
            t->size != (size_t)t->width * t->height * 4u || t->wrap_s > 2 || t->wrap_t > 2)
            return fail(error, capacity, "Title textures must be power-of-two RGBA8, 8..1024 pixels.");
    }
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return fail(error, capacity, "Title citro3d initialization failed.");
    c3d_started = true;
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!target) { fail(error, capacity, "Title top-screen allocation failed."); goto cleanup; }
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    shader = DVLB_ParseFile((u32 *)title_shader_shbin, title_shader_shbin_size);
    if (!shader || !shader->numDVLE) { fail(error, capacity, "Title vertex shader parse failed."); goto cleanup; }
    if (R_FAILED(shaderProgramInit(&program))) { fail(error, capacity, "Title shader initialization failed."); goto cleanup; }
    program_started = true;
    if (R_FAILED(shaderProgramSetVsh(&program, &shader->DVLE[0]))) { fail(error, capacity, "Title vertex shader setup failed."); goto cleanup; }
    mvp_location = shaderInstanceGetUniformLocation(program.vertexShader, "mvp");
    if (mvp_location < 0) { fail(error, capacity, "Title shader matrix uniform is missing."); goto cleanup; }
    vertices = linearAlloc((size_t)title->vertex_count * sizeof(*vertices));
    if (!vertices) { fail(error, capacity, "Title vertex arena allocation failed."); goto cleanup; }
    gpu_buffer_bytes = (size_t)title->vertex_count * sizeof(*vertices);
    for (uint32_t i = 0; i < title->texture_count; ++i) {
        if (!upload_texture(&textures[i], &title->textures[i])) { fail(error, capacity, "Title texture allocation failed."); goto cleanup; }
        texture_count++;
        gpu_buffer_bytes += textures[i].size;
    }
    loaded_title = title;
    if (error && capacity) error[0] = '\0';
    return 0;
cleanup:
    c3_title_renderer_shutdown();
    return -1;
}

static float attenuation(const C3TitleLight *light, float distance)
{
    float reference = light->reference_distance, brightness = light->reference_brightness;
    int function = (int)light->distance_function;
    if (reference <= 0.0f || brightness <= 0.0f || brightness >= 1.0f || function < 1 || function > 3)
        return 1.0f;
    float ratio = distance / reference, factor = (1.0f - brightness) / brightness;
    float denom = function == 1 ? 1.0f + factor * ratio :
                  function == 2 ? 1.0f + 0.5f * factor * (ratio + ratio * ratio) :
                                  1.0f + factor * ratio * ratio;
    return 1.0f / denom;
}

/* HSD state.c uses a white material raster channel, ambient material * ambient
 * light, and clamped Lambert diffuse. Its MObj texture stages multiply this
 * channel afterward. There are no specular-enabled materials in this title. */
static void raster_color(const C3Title *title, const C3TitleBatch *batch,
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
                      clamp01(vertex_color + batch->diffuse[c]);
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
            if (ambient_seen) continue; /* HSD uses one active ambient light. */
            ambient_seen = true;
            if (light->flags & 4u) for (unsigned c = 0; c < 3; ++c) light_color[c] += batch->ambient[c] * light->rgba[c];
            if (light->flags & 16u) light_alpha += light->rgba[3];
            continue;
        }
        float direction[3];
        /* HSD infinite diffuse lights place the source position at 2^20 times
         * its value. Interest controls specular/spot direction, not this path. */
        for (unsigned c = 0; c < 3; ++c)
            direction[c] = type == 1 ? light->pos[c] : light->pos[c] - source->pos[c];
        float distance = normalize(direction);
        float intensity = fmaxf(0.0f, normal[0]*direction[0] + normal[1]*direction[1] + normal[2]*direction[2]);
        if (type == 2) intensity *= attenuation(light, distance);
        if (light->flags & 4u) for (unsigned c = 0; c < 3; ++c) light_color[c] += intensity * light->rgba[c];
        if (light->flags & 16u) light_alpha += intensity * light->rgba[3];
    }
    for (unsigned c = 0; c < 3; ++c)
        rgba[c] = clamp01(light_color[c]) * (color_source & 2u ? source->rgba[c] / 255.0f : 1.0f);
    /* MObjMakeTExp multiplies texture/material alpha by (1 - raster alpha).
     * The optional vertex-alpha channel is independent of light alpha. */
    rgba[3] = (1.0f - clamp01(light_alpha)) * (alpha_source & 2u ? vertex_alpha : 1.0f);
}

static void combiner(const C3TitleBatch *batch, const C3TitleMaterialState *material, float fade)
{
    for (int i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnv *tex = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tex, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(tex, C3D_Both, GPU_REPLACE);
    if (batch->flags & C3_TITLE_TEV_LERP) {
        /* Seed the first endpoint as the preceding color, then interpolate
         * in stage one. Reading PREVIOUS_BUFFER in stage zero lost that
         * endpoint in the native emulator probe; this pairing was verified
         * with nonzero expected values at both gradient endpoints. */
        C3D_TexEnvColor(tex, color4(material->tev0[0], material->tev0[1], material->tev0[2], 1));
        C3D_TexEnvSrc(tex, C3D_RGB, GPU_CONSTANT, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnv *gradient = C3D_GetTexEnv(1);
        C3D_TexEnvColor(gradient, color4(material->konst[0], material->konst[1], material->konst[2], 1));
        C3D_TexEnvSrc(gradient, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_TEXTURE0);
        C3D_TexEnvFunc(gradient, C3D_RGB, GPU_INTERPOLATE);
    }
    C3D_TexEnv *map = C3D_GetTexEnv(2);
    bool lit = (batch->flags & C3_TITLE_LIT) != 0;
    float blend = clamp01(batch->texture_blend);
    GPU_TEVSRC base = lit ? GPU_CONSTANT : GPU_PRIMARY_COLOR;
    if (lit) {
        unsigned color_source = batch->render_mode & 3u;
        unsigned alpha_source = (batch->render_mode >> 13) & 3u;
        if (!color_source) color_source = 1;
        if (!alpha_source) alpha_source = color_source;
        float input_color[3], input_alpha = alpha_source == 2 ? 1.0f : material->alpha;
        for (unsigned c = 0; c < 3; ++c) input_color[c] = color_source == 2 ? 1.0f : batch->diffuse[c];
        C3D_TexEnvColor(map, color4(
            batch->color_mode == 3 ? blend : input_color[0],
            batch->color_mode == 3 ? blend : input_color[1],
            batch->color_mode == 3 ? blend : input_color[2],
            batch->alpha_mode == 2 ? blend : input_alpha));
        C3D_TexEnv *add = C3D_GetTexEnv(3);
        C3D_TexEnvColor(add, color4(input_color[0]*(1-blend), input_color[1]*(1-blend), input_color[2]*(1-blend), input_alpha*(1-blend)));
        if (batch->color_mode == 3) {
            C3D_TexEnvSrc(add, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(add, C3D_RGB, GPU_ADD);
        }
        if (batch->alpha_mode == 2) {
            C3D_TexEnvSrc(add, C3D_Alpha, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(add, C3D_Alpha, GPU_ADD);
        }
        C3D_TexEnv *light = C3D_GetTexEnv(4);
        C3D_TexEnvSrc(light, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(light, C3D_Both, GPU_MODULATE);
    } else C3D_TexEnvColor(map, color4(blend, blend, blend, blend));
    C3D_TexEnvSrc(map, C3D_RGB, batch->color_mode == 0 ? base : GPU_PREVIOUS, base, GPU_CONSTANT);
    C3D_TexEnvFunc(map, C3D_RGB, batch->color_mode == 0 ? GPU_REPLACE :
        batch->color_mode == 3 && !lit ? GPU_INTERPOLATE : GPU_MODULATE);
    C3D_TexEnvSrc(map, C3D_Alpha, batch->alpha_mode == 0 ? base : GPU_PREVIOUS, base, GPU_CONSTANT);
    C3D_TexEnvFunc(map, C3D_Alpha, batch->alpha_mode == 0 ? GPU_REPLACE :
        batch->alpha_mode == 2 && !lit ? GPU_INTERPOLATE : GPU_MODULATE);
    C3D_TexEnv *fade_stage = C3D_GetTexEnv(5);
    C3D_TexEnvColor(fade_stage, color4(fade, fade, fade, 1.0f));
    C3D_TexEnvSrc(fade_stage, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(fade_stage, C3D_RGB, GPU_MODULATE);
}

static bool camera_matrices(const C3TitleCamera *camera, C3D_Mtx *mvp)
{
    float fields[sizeof(*camera) / sizeof(float)];
    memcpy(fields, camera, sizeof(fields));
    for (unsigned i = 0; i < sizeof(*camera) / sizeof(float); ++i) if (!isfinite(fields[i])) return false;
    if (camera->fov_y_degrees <= 0.0f || camera->fov_y_degrees >= 179.0f || camera->near_plane <= 0.0f ||
        camera->far_plane <= camera->near_plane || camera->aspect < 0.1f || camera->aspect > 10.0f) return false;
    float direction[3], up[3];
    for (unsigned i = 0; i < 3; ++i) direction[i] = camera->target[i] - camera->eye[i];
    if (normalize(direction) <= 1e-12f) return false;
    /* Match HSD cobj.c roll2upvec, including its near-vertical fallback and
     * negative roll convention, in unpermuted source Y-up coordinates. */
    if (1.0f - fabsf(direction[1]) < 0.0001f) {
        up[0] = sqrtf(direction[1]*direction[1] + direction[2]*direction[2]);
        up[1] = direction[1] * (-direction[0] / up[0]);
        up[2] = direction[2] * (-direction[0] / up[0]);
    } else {
        up[1] = sqrtf(direction[0]*direction[0] + direction[2]*direction[2]);
        up[0] = direction[0] * (-direction[1] / up[1]);
        up[2] = direction[2] * (-direction[1] / up[1]);
    }
    float cosine = cosf(-camera->roll), sine = sinf(-camera->roll);
    float rotated[3] = {
        up[0]*cosine + (direction[1]*up[2] - direction[2]*up[1])*sine,
        up[1]*cosine + (direction[2]*up[0] - direction[0]*up[2])*sine,
        up[2]*cosine + (direction[0]*up[1] - direction[1]*up[0])*sine};
    normalize(rotated);
    C3D_Mtx view, projection;
    Mtx_LookAt(&view, FVec3_New(camera->eye[0], camera->eye[1], camera->eye[2]),
        FVec3_New(camera->target[0], camera->target[1], camera->target[2]),
        FVec3_New(rotated[0], rotated[1], rotated[2]), false);
    Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(camera->fov_y_degrees), camera->aspect,
                 camera->near_plane, camera->far_plane, false);
    Mtx_Multiply(mvp, &projection, &view);
    return true;
}

int c3_title_renderer_draw(const C3Title *title, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity)
{
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!loaded_title || title != loaded_title) return fail(error, capacity, "Title renderer does not own this asset.");
    if (!isfinite(fade)) return fail(error, capacity, "Invalid title fade value.");
    C3D_Mtx mvp;
    if (!camera_matrices(&title->pose.camera, &mvp)) return fail(error, capacity, "Invalid sampled title camera.");
    for (uint32_t i = 0; i < title->batch_count; ++i)
        if (!valid_batch(title, &title->batches[i])) return fail(error, capacity, "Invalid title batch before submission.");
    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return fail(error, capacity, "Title frame begin failed.");
    /* FrameBegin drains the previous GPU queue before this single bounded
     * arena is overwritten. No CPU buffer is reused within an open frame. */
    uint64_t start = svcGetSystemTick();
    if (stats) stats->gpu_previous_ms = C3D_GetDrawingTime();
    for (uint32_t i = 0; i < title->batch_count; ++i) {
        const C3TitleBatch *batch = &title->batches[i];
        for (uint32_t j = batch->first; j < batch->first + batch->count; ++j) {
            const C3TitleDrawVertex *source = &title->draw_vertices[j];
            memcpy(vertices[j].pos, source->pos, sizeof(vertices[j].pos));
            memcpy(vertices[j].uv, source->uv, sizeof(vertices[j].uv));
            raster_color(title, batch, &title->pose.materials[i], source, vertices[j].rgba);
        }
    }
    GSPGPU_FlushDataCache(vertices, (size_t)title->vertex_count * sizeof(*vertices));
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x000000FF, 0);
    C3D_FrameDrawOn(target);
    /* Preserve the authored camera aspect rather than stretching its image.
     * GPU coordinates are rotated: viewport height is display width. */
    float aspect = title->pose.camera.aspect;
    unsigned display_width = 400, display_height = 240;
    if (aspect < 400.0f/240.0f) display_width = (unsigned)(240.0f * aspect + 0.5f);
    else display_height = (unsigned)(400.0f / aspect + 0.5f);
    C3D_SetViewport((240-display_height)/2, (400-display_width)/2, display_height, display_width);
    C3D_BindProgram(&program);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, &mvp);
    C3D_AttrInfo *attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 4);
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, vertices, sizeof(*vertices), 3, 0x210);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    static const GPU_TESTFUNC depth_function[8] = {
        GPU_NEVER, GPU_GREATER, GPU_EQUAL, GPU_GEQUAL,
        GPU_LESS, GPU_NOTEQUAL, GPU_LEQUAL, GPU_ALWAYS};
    for (uint32_t i = 0; i < title->batch_count; ++i) {
        const C3TitleBatch *batch = &title->batches[i];
        const C3TitleMaterialState *material = &title->pose.materials[i];
        if (material->visible < 0.5f ||
            ((batch->flags & C3_TITLE_CULL_BACK) && (batch->flags & C3_TITLE_CULL_FRONT))) continue;
        C3D_CullFace(batch->flags & C3_TITLE_CULL_BACK ? GPU_CULL_BACK_CCW :
                     batch->flags & C3_TITLE_CULL_FRONT ? GPU_CULL_FRONT_CCW : GPU_CULL_NONE);
        C3D_DepthTest(true, depth_function[batch->depth_compare],
            batch->flags & C3_TITLE_NO_DEPTH_WRITE ? GPU_WRITE_COLOR : GPU_WRITE_ALL);
        if (batch->flags & (C3_TITLE_TRANSLUCENT | C3_TITLE_ADDITIVE))
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA,
                batch->flags & C3_TITLE_ADDITIVE ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA,
                GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
        else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        combiner(batch, material, clamp01(fade));
        C3D_TexBind(0, &textures[batch->texture]);
        C3D_DrawArrays(GPU_TRIANGLES, batch->first, batch->count);
        if (stats) { stats->draw_calls++; stats->vertices += batch->count; }
    }
    if (stats) {
        stats->draw_prepare_us = (svcGetSystemTick() - start) * 1000000ull / SYSCLOCK_ARM11;
        stats->gpu_buffer_bytes = gpu_buffer_bytes;
    }
    gfxFlushBuffers();
    C3D_FrameEnd(0);
    if (error && capacity) error[0] = '\0';
    return 0;
}

void c3_title_renderer_shutdown(void)
{
    loaded_title = NULL;
    /* Target deletion drains the linked GPU queue. Do not use FrameSync here:
     * HOME cancellation can shut down while VBlank callbacks are suspended. */
    if (target) { C3D_RenderTargetDelete(target); target = NULL; }
    for (uint32_t i = 0; i < texture_count; ++i) C3D_TexDelete(&textures[i]);
    texture_count = 0;
    if (vertices) { linearFree(vertices); vertices = NULL; }
    if (program_started) { shaderProgramFree(&program); program_started = false; }
    if (shader) { DVLB_Free(shader); shader = NULL; }
    if (c3d_started) { C3D_Fini(); c3d_started = false; }
    gpu_buffer_bytes = 0;
}
