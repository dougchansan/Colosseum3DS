#include "model_renderer.h"
#include "n3ds_platform.h"
#include "gekkoctr/gx_scene.h"
#include "gekkoctr/platform/ctr_backends.h"
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static gctr_gx_context_t gx;
static gctr_gx_scene_t scene;
static gctr_gx_projection_t projection;
static uint32_t texture_ids[16];
static size_t gpu_bytes, upload_peak, tracked_peak;
static bool romfs_started, backend_started, ready;
static const C3Model *loaded_model;

static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return -1;
}

/* Fit the entire animation's bounds in a fixed three-quarter camera. */
static void fit_camera(const C3Model *model)
{
    float center[3], half[3];
    const float yaw_cos = -0.866025404f, yaw_sin = -0.5f;
    for (unsigned i = 0; i < 3; ++i) {
        half[i] = (model->bounds[i + 3] - model->bounds[i]) * 0.5f;
        center[i] = (model->bounds[i + 3] + model->bounds[i]) * 0.5f;
    }
    float x_span = 2.0f * (fabsf(yaw_cos) * half[0] + fabsf(yaw_sin) * half[2]);
    float y_span = 2.0f * half[1];
    float z_span = 2.0f * (fabsf(yaw_sin) * half[0] + fabsf(yaw_cos) * half[2]);
    if (x_span < 0.001f) x_span = 0.001f;
    if (y_span < 0.001f) y_span = 0.001f;
    if (z_span < 0.001f) z_span = 0.001f;
    memset(&projection, 0, sizeof(projection));
    const float aspect = 400.0f / 240.0f;
    const float sy = fminf(1.8f / y_span, 1.8f * aspect / x_span);
    const float sx = sy / aspect, sz = 1.0f / z_span;
    projection.m[0] = sx * yaw_cos;
    projection.m[2] = sx * yaw_sin;
    projection.m[3] = -sx * (yaw_cos * center[0] + yaw_sin * center[2]);
    projection.m[5] = sy;
    projection.m[7] = -sy * center[1];
    projection.m[8] = -sz * yaw_sin;
    projection.m[10] = sz * yaw_cos;
    projection.m[11] = sz * (yaw_sin * center[0] - yaw_cos * center[2]);
    projection.m[15] = 1.0f;
}

void c3_model_renderer_shutdown(void)
{
    ready = false;
    loaded_model = NULL;
    if (backend_started) gctr_ctr_gx_shutdown();
    backend_started = false;
    if (romfs_started) romfsExit();
    romfs_started = false;
    gpu_bytes = upload_peak = tracked_peak = 0;
}

int c3_model_renderer_init(const C3Model *model, char *error, size_t capacity)
{
    if (backend_started || romfs_started)
        return fail(error, capacity, "Model renderer is already initialized.");
    if (!model || !model->texture_count || model->texture_count > 16 ||
        !model->draw_vertices || !model->batches || !model->textures)
        return fail(error, capacity, "Model renderer requires geometry and 1..16 textures.");
    if (R_FAILED(romfsInit())) return fail(error, capacity, "Model shader RomFS initialization failed.");
    romfs_started = true;
    /* Set before initialization so partial backend allocation is also cleaned. */
    backend_started = true;
    if (!gctr_ctr_gx_init_textured("romfs:/p5_material.shbin")) {
        fail(error, capacity, gctr_ctr_gx_error() ? gctr_ctr_gx_error() : "Model backend initialization failed.");
        goto cleanup;
    }
    if (!gctr_gx_init(&gx, gctr_ctr_gx_backend()) || !gctr_gx_scene_init(&scene, &gx)) {
        fail(error, capacity, "Model graphics frontend initialization failed.");
        goto cleanup;
    }
    /* Both donor arenas are allocated: xyz+rgba floats and xyz+rgba+uv floats.
     * Excludes command buffers, framebuffer and driver bookkeeping. */
    gpu_bytes = C3_MODEL_RENDER_VERTEX_CAPACITY * (7u + 9u) * sizeof(float);
    tracked_peak = model->peak_load_bytes;
    for (uint32_t i = 0; i < model->texture_count; ++i) {
        const C3ModelTexture *texture = &model->textures[i];
        gctr_gx_texture_desc_t desc = {
            texture->width, texture->height, GCTR_GX_TEXFMT_RGBA8,
            {GCTR_GX_FILTER_LINEAR, GCTR_GX_FILTER_LINEAR,
             texture->wrap_s ? GCTR_GX_WRAP_REPEAT : GCTR_GX_WRAP_CLAMP,
             texture->wrap_t ? GCTR_GX_WRAP_REPEAT : GCTR_GX_WRAP_CLAMP}};
        if (texture->width < 8 || texture->height < 8 || texture->width > 1024 ||
            texture->height > 1024 || (texture->width & (texture->width - 1)) ||
            (texture->height & (texture->height - 1)) ||
            texture->size != (size_t)texture->width * texture->height * 4u) {
            fail(error, capacity, "Model textures must be RGBA8 power-of-two sizes 8..1024.");
            goto cleanup;
        }
        if (!gctr_gx_scene_create_texture(&scene, &desc, texture->pixels,
                                         texture->size, &texture_ids[i])) {
            fail(error, capacity, gctr_ctr_gx_error() ? gctr_ctr_gx_error() : "Model texture upload failed.");
            goto cleanup;
        }
        /* At upload: prior arenas/textures + new GPU texture + linear staging.
         * Inputs are power-of-two, so the donor's extra padding buffer is unused. */
        size_t upload_live = model->resident_bytes + gpu_bytes + 2u * texture->size;
        if (upload_live > tracked_peak) tracked_peak = upload_live;
        gpu_bytes += texture->size;
        if (texture->size > upload_peak) upload_peak = texture->size;
    }
    fit_camera(model);
    loaded_model = model;
    ready = true;
    return 0;
cleanup:
    c3_model_renderer_shutdown();
    return -1;
}

static gctr_gx_material_t material_for(const C3ModelBatch *batch)
{
    gctr_gx_material_t material;
    memset(&material, 0, sizeof(material));
    material.stage_count = 1;
    material.texture[0] = texture_ids[batch->texture];
    material.stages[0].texture_unit = 0;
    material.stages[0].color_op = GCTR_GX_OP_MODULATE;
    material.stages[0].color_src[0] = GCTR_GX_SRC_TEXTURE0;
    material.stages[0].color_src[1] = GCTR_GX_SRC_RASTER;
    material.stages[0].alpha_op = GCTR_GX_OP_MODULATE;
    material.stages[0].alpha_src[0] = GCTR_GX_SRC_TEXTURE0;
    material.stages[0].alpha_src[1] = GCTR_GX_SRC_RASTER;
    material.blend = (batch->flags & 1u) ? GCTR_GX_BLEND_SRC_ALPHA : GCTR_GX_BLEND_NONE;
    material.depth_test = true;
    material.depth_write = !(batch->flags & 1u);
    material.cull = GCTR_GX_CULL_NONE;
    material.alpha_test.compare0 = (batch->flags & 2u) ? GCTR_GX_COMPARE_GEQUAL : GCTR_GX_COMPARE_ALWAYS;
    material.alpha_test.compare1 = GCTR_GX_COMPARE_ALWAYS;
    material.alpha_test.reference0 = 128;
    material.alpha_test.logic = GCTR_GX_ALPHA_LOGIC_AND;
    return material;
}

int c3_model_renderer_draw(const C3Model *model, C3ModelRenderStats *stats,
                           char *error, size_t capacity)
{
    uint32_t total = 0;
    uint64_t start;
    bool okay;
    gctr_gx_vertex_uv_t chunk[C3_MODEL_RENDER_CHUNK_VERTICES];
    const gctr_gx_viewport_t viewport = {0, 0, 400, 240, 0, 1};
    const gctr_gx_color_t clear = {24, 29, 38, 255};
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!ready || model != loaded_model) return fail(error, capacity, "Model renderer is not initialized for this asset.");
    /* Preflight the whole frame, before issuing any commands. */
    for (uint32_t i = 0; i < model->batch_count; ++i) {
        const C3ModelBatch *batch = &model->batches[i];
        if (!batch->count || batch->count % 3 || batch->first > model->vertex_count ||
            batch->count > model->vertex_count - batch->first || batch->texture >= model->texture_count ||
            !c3_model_renderer_reserve(total, batch->count))
            return fail(error, capacity, "Model batch is invalid or exceeds the 8192-vertex frame arena.");
        total += batch->count;
    }
    if (!gctr_gx_begin_frame(&gx)) return fail(error, capacity, "Model frame begin failed.");
    /* Exclude frame pacing/GPU wait from CPU draw-preparation time. */
    start = c3_n3ds_time_us();
    scene.cache.active_slot = GCTR_GX_UNIT_NONE;
    okay = gctr_gx_clear(&gx, clear) && gctr_gx_set_viewport(&gx, &viewport) &&
           gctr_gx_set_projection(&gx, &projection);
    for (uint32_t i = 0; okay && i < model->batch_count; ++i) {
        const C3ModelBatch *batch = &model->batches[i];
        gctr_gx_material_t material = material_for(batch);
        okay = gctr_gx_scene_set_material(&scene, &material);
        for (uint32_t offset = 0; okay && offset < batch->count;) {
            uint32_t count = c3_model_renderer_chunk_size(batch->count - offset);
            for (uint32_t j = 0; j < count; ++j) {
                const C3ModelDrawVertex *v = &model->draw_vertices[batch->first + offset + j];
                chunk[j] = (gctr_gx_vertex_uv_t){v->x, v->y, v->z, v->u, v->v,
                    {v->rgba[0], v->rgba[1], v->rgba[2], v->rgba[3]}};
            }
            okay = gctr_gx_draw_textured(&gx, GCTR_GX_PRIM_TRIANGLES, chunk, count);
            if (okay && stats) {
                stats->draw_calls++;
                stats->vertices += count;
                stats->triangles += count / 3;
            }
            offset += count;
        }
    }
    if (stats) stats->draw_prepare_us = c3_n3ds_time_us() - start;
    /* End even a partially refused frame so shutdown never sees an open frame. */
    gfxFlushBuffers();
    if (!gctr_gx_end_frame(&gx)) okay = false;
    if (stats) {
        stats->gpu_ms = C3D_GetDrawingTime();
        stats->gpu_owned_bytes = gpu_bytes;
        stats->peak_upload_bytes = upload_peak;
        stats->tracked_peak_bytes = tracked_peak;
    }
    return okay ? 0 : fail(error, capacity, "Model material or geometry submission failed.");
}
