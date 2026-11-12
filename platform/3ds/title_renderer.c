#include "title_renderer.h"
#include "stage_renderer.h"
#include "stage_cache.h"

#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "title_shader_shbin.h"

typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;

typedef struct {
    const C3Title *asset;
    uint32_t first_vertex, vertex_count, batch_count, texture_count;
    uint32_t textures[C3_TITLE_MAX_TEXTURES];
    uint32_t specular_lights;
} SceneBinding;

static SceneBinding scenes[C3_STAGE_MAX_MODELS];
static size_t scene_count;
static C3StageVertexCache vertex_cache[C3_STAGE_MAX_MODELS];
static uint8_t model_modulation[C3_STAGE_MAX_MODELS][4];

static bool c3d_started, program_started;
static C3D_RenderTarget *target;
static DVLB_s *shader;
static shaderProgram_s program;
static int mvp_location, color_pass_location;
static TitleGPUVertex *vertices;
static C3D_Tex textures[C3_STAGE_MAX_TEXTURES];
static const C3TitleTexture *texture_sources[C3_STAGE_MAX_TEXTURES];
static uint32_t texture_count, vertex_count;
static size_t gpu_buffer_bytes;
static bool scene_drawn;
/* A successful nonblocking FrameBegin that has not been ended yet. */
static bool frame_acquired;
static C3StageBattleHud battle_hud;
static uint32_t hud_texture = UINT32_MAX;
#define C3_HUD_MAX_VERTICES ((9u + 8u * 48u) * 6u)
static TitleGPUVertex *hud_vertices;
/* One original field-camera texture entry. Its model is a member of the
 * bounded scene arena, but is never submitted to the main room camera. */
typedef struct { bool configured, enabled; size_t model, screen_model; uint32_t texture; } TVBinding;
static TVBinding tv;
static C3D_Tex tv_texture;
static C3D_RenderTarget *tv_target;
static volatile uint32_t memory_fill_completions, command_list_completions, display_transfer_completions;
static C3TitleRenderSnapshot submission_snapshot;
static bool completion_hooks;

static void completion_count(void *counter)
{
    /* Each counter has one writer on libctru's GSP event thread. Aligned ARM
     * word loads/stores let the CPU recovery view read an advisory snapshot
     * without waiting for a GPU service or disturbing queue completion. */
    ++*(volatile uint32_t *)counter;
}

static void install_completion_counters(void)
{
    memory_fill_completions = command_list_completions = display_transfer_completions = 0;
    memset(&submission_snapshot, 0, sizeof(submission_snapshot));
    gspSetEventCallback(GSPGPU_EVENT_PSC0, completion_count, (void *)&memory_fill_completions, false);
    gspSetEventCallback(GSPGPU_EVENT_P3D, completion_count, (void *)&command_list_completions, false);
    gspSetEventCallback(GSPGPU_EVENT_PPF, completion_count, (void *)&display_transfer_completions, false);
    completion_hooks = true;
}

void c3_stage_renderer_snapshot(C3TitleRenderSnapshot *snapshot)
{
    if (!snapshot) return;
    *snapshot = submission_snapshot;
    snapshot->memory_fills = memory_fill_completions;
    snapshot->command_lists = command_list_completions;
    snapshot->display_transfers = display_transfer_completions;
    snapshot->scene_models = (uint32_t)scene_count;
    snapshot->scene_textures = texture_count + (tv.configured ? 1u : 0u);
    snapshot->gpu_buffer_bytes = gpu_buffer_bytes;
}

static void record_submission(C3TitleRenderStats *stats)
{
    ++submission_snapshot.submissions;
    submission_snapshot.last_command_words = stats ? (uint32_t)(C3D_GetCmdBufUsage() *
        (C3_RENDER_COMMAND_CAPACITY_WORDS)) : 0;
    if(stats)stats->command_words=submission_snapshot.last_command_words;
    submission_snapshot.last_draw_calls = stats ? stats->draw_calls : 0;
    submission_snapshot.last_vertices = stats ? stats->vertices : 0;
}
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

static void write_texture_pixels(C3D_Tex *gpu,const C3TitleTexture *source,const uint8_t route[3])
{
    /* PICA RGBA8 is byte-ordered ABGR, with 8x8 Morton tiles. Keep top-origin
     * GX UVs by reversing the stored pixel rows. The colored-corner
     * native probe verifies this pairing, including the screen rotation. */
    for (uint32_t y = 0; y < source->height; ++y) {
        for (uint32_t x = 0; x < source->width; ++x) {
            size_t tile = ((size_t)(y >> 3) * (source->width >> 3) + (x >> 3)) * 64;
            uint8_t *out = (uint8_t *)gpu->data + (tile + morton8(x, y)) * 4;
            const uint8_t *in = source->pixels + ((size_t)(source->height - 1 - y) * source->width + x) * 4;
            out[0] = in[3]; out[1] = in[route?route[2]:2]; out[2] = in[route?route[1]:1]; out[3] = in[route?route[0]:0];
        }
    }
    C3D_TexFlush(gpu);
}
static bool upload_texture(C3D_Tex *gpu, const C3TitleTexture *source)
{
    if (!C3D_TexInit(gpu, source->width, source->height, GPU_RGBA8)) return false;
    write_texture_pixels(gpu,source,NULL);
    C3D_TexSetFilter(gpu, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(gpu, wrap_mode(source->wrap_s), wrap_mode(source->wrap_t));
    return true;
}

int c3_stage_renderer_set_texture_routing(const uint8_t route[3],char *error,size_t capacity)
{
    if(!scene_count||scene_drawn||tv.configured||!route||route[0]>3||route[1]>3||route[2]>3)
        return fail(error,capacity,"Texture routing requires a fresh sole actor scene.");
    for(unsigned i=0;i<texture_count;++i)if(!texture_sources[i])
        return fail(error,capacity,"Texture routing requires only authored actor textures.");
    for(unsigned i=0;i<texture_count;++i)write_texture_pixels(&textures[i],texture_sources[i],route);
    return C3_RENDER_OK;
}

static bool valid_batch(const C3Title *title, const C3TitleBatch *batch)
{
    unsigned blend_mode = batch->flags & (C3_TITLE_ADDITIVE | C3_TITLE_SUBTRACT | C3_TITLE_ADDITIVE_ONE);
    bool alpha_second_always=batch->alpha_compare1==7 || (batch->alpha_compare1==3&&batch->alpha_ref1==255);
    bool alpha_supported=batch->alpha_op==0 &&
        ((batch->alpha_compare0==7&&alpha_second_always) ||
         (batch->alpha_compare0==4&&(alpha_second_always ||
          (batch->alpha_compare1==4&&batch->alpha_ref0==batch->alpha_ref1))));
    if (!batch->count || batch->count % 3 || batch->first > title->vertex_count ||
        batch->count > title->vertex_count - batch->first || batch->texture >= title->texture_count ||
        (batch->color_mode != 0 && batch->color_mode != 3 && batch->color_mode != 4) ||
        (batch->alpha_mode != 0 && batch->alpha_mode != 2 && batch->alpha_mode != 3) ||
        batch->depth_compare > 7 ||
        (blend_mode && (blend_mode & (blend_mode - 1u))) ||
        !alpha_supported ||
        batch->alpha_ref0 > 255u || batch->alpha_ref1 > 255u ||
        ((batch->flags & C3_TITLE_SPECULAR) &&
         (!(batch->flags & C3_TITLE_LIT) ||
          (batch->flags & (C3_TITLE_TEV_LERP | C3_TITLE_DUAL_MODULATE)) ||
          blend_mode ||
          (batch->color_mode != 0 && batch->color_mode != 3 && batch->color_mode != 4) ||
          (batch->alpha_mode != 0 && batch->alpha_mode != 2 && batch->alpha_mode != 3) ||
          !isfinite(batch->shininess) || batch->shininess <= 0 || batch->shininess > 512)) ||
        ((batch->flags & C3_TITLE_REFLECTION_BLEND) &&
         (batch->texture2 >= title->texture_count || batch->color_mode != 4 || batch->alpha_mode != 0 ||
          !(batch->flags & C3_TITLE_LIT) ||
          (batch->flags & (C3_TITLE_TEV_LERP | C3_TITLE_DUAL_MODULATE)) ||
          !isfinite(batch->texture2_blend) || batch->texture2_blend < 0 || batch->texture2_blend > 1)) ||
        ((batch->flags & C3_TITLE_DUAL_MODULATE) &&
         (batch->texture2 >= title->texture_count || batch->color_mode != 4 ||
          batch->alpha_mode != 0 || !(batch->flags & C3_TITLE_TEV_LERP) ||
          (batch->flags & C3_TITLE_LIT))))
        return false;
    return true;
}

/* HSD reserves a ninth logical slot for ambient, leaving eight physical GX
 * lights. Diffuse/alpha lights consume their first slot before combined
 * diffuse+specular lights receive a second slot. A specular-only light uses
 * its first slot. The exported light order is the original active order. */
static uint32_t specular_light_mask(const C3Title *title)
{
    uint32_t active = 0, mask = 0, used = 0;
    for (uint32_t i = 0; i < title->light_count && used < 8; ++i) {
        unsigned flags = title->lights[i].flags;
        if ((flags & 32u) || !(flags & 3u) || !(flags & 28u)) continue;
        active |= 1u << i;
        ++used;
        if ((flags & 8u) && !(flags & 20u)) mask |= 1u << i;
    }
    for (uint32_t i = 0; i < title->light_count && used < 8; ++i)
        if ((active & (1u << i)) && (title->lights[i].flags & 8u) && (title->lights[i].flags & 20u)) {
            mask |= 1u << i;
            ++used;
        }
    return mask;
}

static int validate_asset(const C3Title *title, char *error, size_t capacity)
{
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
        if (title->lights[i].attenuation_flags ||
            ((title->lights[i].flags & 3u) == 3u &&
             title->lights[i].spot_function != 0 && title->lights[i].spot_function != 3))
            return fail(error, capacity, "Unsupported raw attenuation or spotlight function.");
    for (uint32_t i = 0; i < title->texture_count; ++i) {
        const C3TitleTexture *t = &title->textures[i];
        if (!t->pixels || t->width < 8 || t->height < 8 || t->width > 1024 || t->height > 1024 ||
            (t->width & (t->width - 1)) || (t->height & (t->height - 1)) ||
            t->size != (size_t)t->width * t->height * 4u || t->wrap_s > 2 || t->wrap_t > 2)
            return fail(error, capacity, "Title textures must be power-of-two RGBA8, 8..1024 pixels.");
    }
    return 0;
}

int c3_stage_renderer_init(const C3Title *const *models, size_t count,
                          char *error, size_t capacity)
{
    if (target || scene_count) return fail(error, capacity, "The shared scene renderer is already initialized.");
    memset(&battle_hud, 0, sizeof battle_hud);
    memset(vertex_cache, 0, sizeof vertex_cache);
    memset(model_modulation, 0, sizeof model_modulation);
    hud_texture = UINT32_MAX;
    scene_drawn=false;
    if (!models || !count || count > C3_STAGE_MAX_MODELS)
        return fail(error, capacity, "Invalid bounded stage model count.");
    uint32_t total_vertices = 0;
    for (size_t i = 0; i < count; ++i) {
        if (validate_asset(models[i], error, capacity)) return -1;
        if (models[i]->vertex_count > C3_STAGE_MAX_VERTICES - total_vertices)
            return fail(error, capacity, "Combined stage vertex arena exceeds its bound.");
        total_vertices += models[i]->vertex_count;
    }
    /* Citro3D retains its current shaderProgram pointer across C3D_Fini/Init.
     * Recreating our static program at that same address could skip the next
     * bind's shader upload, or leave the context referring to freed storage.
     * Keep the context and program alive until application shutdown; scene
     * changes release only drained targets, textures and vertex arenas. */
    if (!c3d_started) {
        if (!C3D_Init(C3_RENDER_COMMAND_BUFFER_BYTES)) return fail(error, capacity, "Scene citro3d initialization failed.");
        c3d_started = true;
        shader = DVLB_ParseFile((u32 *)title_shader_shbin, title_shader_shbin_size);
        if (!shader || !shader->numDVLE) { fail(error, capacity, "Title vertex shader parse failed."); goto driver_error; }
        if (R_FAILED(shaderProgramInit(&program))) { fail(error, capacity, "Title shader initialization failed."); goto driver_error; }
        program_started = true;
        if (R_FAILED(shaderProgramSetVsh(&program, &shader->DVLE[0]))) { fail(error, capacity, "Title vertex shader setup failed."); goto driver_error; }
        mvp_location = shaderInstanceGetUniformLocation(program.vertexShader, "mvp");
        color_pass_location = shaderInstanceGetUniformLocation(program.vertexShader, "colorPass");
        if (mvp_location < 0 || color_pass_location < 0) { fail(error, capacity, "Title shader uniforms are missing."); goto driver_error; }
        install_completion_counters();
    }
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!target) { fail(error, capacity, "Title top-screen allocation failed."); goto cleanup; }
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    vertices = linearAlloc((size_t)total_vertices * sizeof(*vertices));
    if (!vertices) { fail(error, capacity, "Title vertex arena allocation failed."); goto cleanup; }
    vertex_count = total_vertices;
    gpu_buffer_bytes = (size_t)total_vertices * sizeof(*vertices);
    uint32_t first_vertex = 0;
    for (size_t model = 0; model < count; ++model) {
        const C3Title *title = models[model];
        SceneBinding *binding = &scenes[model];
        binding->asset = title;
        binding->first_vertex = first_vertex;
        binding->vertex_count = title->vertex_count;
        binding->batch_count = title->batch_count;
        binding->texture_count = title->texture_count;
        binding->specular_lights = specular_light_mask(title);
        first_vertex += title->vertex_count;
        for (uint32_t i = 0; i < title->texture_count; ++i) {
            const C3TitleTexture *source = &title->textures[i];
            uint32_t found;
            /* Split packs may reference the same original image. Reuse its
             * GPU copy only when bytes and sampler state are identical.
             * Models that share one owner through c3_title_instance present
             * the very same pixel storage, which needs no byte comparison. */
            for (found = 0; found < texture_count; ++found) {
                const C3TitleTexture *prior = texture_sources[found];
                if (prior->width == source->width && prior->height == source->height &&
                    prior->wrap_s == source->wrap_s && prior->wrap_t == source->wrap_t &&
                    prior->size == source->size &&
                    (prior->pixels == source->pixels || !memcmp(prior->pixels, source->pixels, source->size))) break;
            }
            if (found == texture_count) {
                if (texture_count >= C3_STAGE_MAX_TEXTURES ||
                    source->size > 32u * 1024u * 1024u - gpu_buffer_bytes) {
                    fail(error, capacity, "Combined scene GPU storage exceeds its bound."); goto cleanup;
                }
                if (!upload_texture(&textures[found], source)) { fail(error, capacity, "Scene texture allocation failed."); goto cleanup; }
                texture_sources[texture_count++] = source;
                gpu_buffer_bytes += textures[found].size;
            }
            binding->textures[i] = found;
        }
    }
    scene_count = count;
    if (error && capacity) error[0] = '\0';
    return 0;
cleanup:
    c3_title_renderer_shutdown();
    return -1;
driver_error:
    c3_stage_renderer_fini();
    return -1;
}

int c3_title_renderer_init(const C3Title *title, char *error, size_t capacity)
{
    return c3_stage_renderer_init(&title, 1, error, capacity);
}

int c3_stage_renderer_set_tv(size_t model, size_t screen_model, uint32_t texture,
                             bool enabled, char *error, size_t capacity)
{
    if (!scene_count || model >= scene_count || screen_model >= scene_count || model == screen_model ||
        texture >= scenes[screen_model].texture_count) {
        fail(error, capacity, "Invalid television model or screen texture binding."); return 0;
    }
    if (tv.configured) {
        if (tv.model != model || tv.screen_model != screen_model || tv.texture != texture) {
            fail(error, capacity, "Television binding cannot change during an owned scene."); return 0;
        }
        if(tv.enabled!=enabled)for(size_t i=0;i<scene_count;++i)c3_stage_cache_reset(&vertex_cache[i]);
        tv.enabled = enabled;
        if (error && capacity) error[0] = '\0';
        return 1;
    }
    const C3Title *screen = scenes[screen_model].asset;
    unsigned matches = 0;
    for (uint32_t i = 0; i < screen->batch_count; ++i) {
        const C3TitleBatch *batch = &screen->batches[i];
        if (batch->texture == texture) {
            if (batch->flags & (C3_TITLE_DUAL_MODULATE | C3_TITLE_REFLECTION_BLEND)) {
                fail(error, capacity, "Television screen must have one texture layer."); return 0;
            }
            ++matches;
        }
    }
    if (matches != 1 || texture_count >= C3_STAGE_MAX_TEXTURES ||
        gpu_buffer_bytes > 32u*1024u*1024u - 256u*256u*8u) {
        fail(error, capacity, "Television target or material exceeds its reviewed bound."); return 0;
    }
    /* Citro3D's texture-backed target requires VRAM and uses the texture's
     * native tiled layout. Keep color and depth until the queue is drained. */
    if (!C3D_TexInitVRAM(&tv_texture, 256, 256, GPU_RGBA8)) {
        fail(error, capacity, "Television texture allocation failed."); return 0;
    }
    C3D_TexSetFilter(&tv_texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&tv_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    tv_target = C3D_RenderTargetCreateFromTex(&tv_texture, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH24_STENCIL8);
    if (!tv_target) {
        C3D_TexDelete(&tv_texture); memset(&tv_texture, 0, sizeof(tv_texture));
        fail(error, capacity, "Television depth target allocation failed."); return 0;
    }
    tv = (TVBinding){true, enabled, model, screen_model, texture};
    gpu_buffer_bytes += 256u*256u*8u;
    if (error && capacity) error[0] = '\0';
    return 1;
}

/* Opt-in for a single-sample pack whose sampled vertices the caller never
 * rewrites. Its batches are then staged only when a tracked input changes
 * (stage_cache.h): lights, view-dependent shading and visibility are detected
 * here, so those need no opt-out. Disabling or re-enabling forgets what was
 * staged. The flag is reset for every scene. Animated packs opt in through
 * C3Title.retained instead. */
int c3_stage_renderer_set_static(size_t model, bool enabled)
{
    if(model>=scene_count)return 0;
    const C3Title *t=scenes[model].asset;
    if(enabled&&(t->clip_count!=1||t->clips[0].frame_count!=1))return 0;
    C3StageVertexCache *c=&vertex_cache[model];
    c->enabled=enabled;c3_stage_cache_reset(c);
    return 1;
}
int c3_stage_renderer_set_modulation(size_t model, const uint8_t rgb[3])
{
    if(model>=scene_count)return 0;
    model_modulation[model][3]=(uint8_t)(rgb!=NULL);
    if(rgb)memcpy(model_modulation[model],rgb,3);
    return 1;
}
int c3_stage_renderer_get_modulation(size_t model, uint8_t rgb[3], int *enabled)
{
    if(model>=scene_count||!rgb||!enabled)return 0;
    *enabled=model_modulation[model][3]!=0;
    if(*enabled)memcpy(rgb,model_modulation[model],3);
    else memset(rgb,128,3);
    return 1;
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

/* Inline forms of fmaxf(0, v) and fmaxf(0, fminf(1, v)), including their
 * NaN results; the library calls dominated the per-vertex cost on ARM11. */
static inline float positive_part(float value)
{
    return value > 0.0f ? value : 0.0f;
}

static inline float unit_interval(float value)
{
    return !(value < 1.0f) ? 1.0f : positive_part(value);
}

/* GXInitLightSpot COS2 cone, evaluated once per light instead of per vertex. */
typedef struct { float axis[3], cutoff; bool active; } SpotCone;

static SpotCone spot_cone(const C3TitleLight *light)
{
    SpotCone cone = {{0, 0, 1}, 1, false};
    if (!light->spot_function || light->spot_cutoff <= 0 || light->spot_cutoff > 90) return cone;
    cone.active = true;
    for (unsigned i=0;i<3;++i) cone.axis[i]=light->interest[i]-light->pos[i];
    normalize(cone.axis); /* HSD's degenerate light vector also falls back to +Z. */
    cone.cutoff=cosf(light->spot_cutoff * (3.14159265358979323846f / 180.0f));
    return cone;
}

static float spotlight_attenuation(const SpotCone *cone, const float to_light[3])
{
    if (!cone->active) return 1;
    float cosine=positive_part(-(cone->axis[0]*to_light[0]+cone->axis[1]*to_light[1]+cone->axis[2]*to_light[2]));
    float cutoff=cone->cutoff;
    /* GXInitLightSpot COS2: a=(0,-c/(1-c),1/(1-c)). The hardware
     * clamps the direction dot before evaluating the angular polynomial.
     * Distance attenuation is applied independently by stage_batch. */
    if (cutoff >= 1) return cosine >= 1 ? 1 : 0;
    return positive_part((cosine*cosine-cutoff*cosine)/(1-cutoff));
}

/* HSD state.c uses a white material raster channel, ambient material * ambient
 * light, and clamped Lambert diffuse. Its MObj texture stages multiply this
 * channel afterward. Specular is a separate channel added after texturing.
 *
 * The active lights of one model are reduced once to what the vertex loop
 * needs, in the original order so the float sums are unchanged: the single
 * ambient light HSD uses, infinite lights with their direction already
 * normalised (their source position is 2^20 times its value; interest controls
 * specular/spot direction, not this path), and point/spot lights. Colours of
 * channels a light does not drive are zero, which adds exactly nothing. */
typedef struct {
    float color[3], alpha;
    float vector[3]; /* infinite: unit direction; point/spot: position */
    const C3TitleLight *light;
    SpotCone cone;
    unsigned type; /* 0 ambient, 1 infinite, 2 point, 3 spot */
} StageLight;

typedef struct {
    StageLight lights[C3_TITLE_MAX_LIGHTS];
    unsigned count;
} LightSetup;

/* byte / 255.0f, the same quotient the per-vertex divide produced. */
static float byte_unit[256];

static void byte_unit_init(void)
{
    if (byte_unit[255] != 1.0f) for (unsigned i=0;i<256;++i) byte_unit[i]=(float)i/255.0f;
}

static void light_setup(const C3Title *title, LightSetup *setup)
{
    bool ambient_seen = false;
    setup->count = 0;
    for (uint32_t i = 0; i < title->light_count && i < C3_TITLE_MAX_LIGHTS; ++i) {
        const C3TitleLight *light = &title->lights[i];
        StageLight *out = &setup->lights[setup->count];
        if (light->flags & 32u) continue;
        unsigned type = light->flags & 3u;
        if (type == 0) {
            if (ambient_seen) continue; /* HSD uses one active ambient light. */
            ambient_seen = true;
        }
        if (!(light->flags & 20u)) continue;
        out->type = type;
        out->light = light;
        for (unsigned c = 0; c < 3; ++c) out->color[c] = light->flags & 4u ? light->rgba[c] : 0.0f;
        out->alpha = light->flags & 16u ? light->rgba[3] : 0.0f;
        memcpy(out->vector, light->pos, sizeof out->vector);
        if (type == 1) normalize(out->vector);
        out->cone = spot_cone(light);
        ++setup->count;
    }
}

/* Stage one batch: positions and UVs are copied, the raster colour is lit.
 * Specular and normal-generated UVs are added by the caller. Against the
 * previous per-vertex routine the unlit path is bit-identical. The lit path
 * is bit-identical too unless it skips renormalising a normal that is
 * already unit within 1e-4 in squared length, where the colour differs by a
 * relative 1e-4 at most: under one 8-bit step. */
static void stage_batch(const LightSetup *setup, const C3TitleBatch *batch,
                        const C3TitleMaterialState *material,
                        const C3TitleDrawVertex *restrict source,
                        TitleGPUVertex *restrict destination, uint32_t count, bool flip_v)
{
    unsigned color_source = batch->render_mode & 3u;
    if (!color_source) color_source = 1;
    unsigned alpha_source = (batch->render_mode >> 13) & 3u;
    if (!alpha_source) alpha_source = color_source;
    const C3TitleDrawVertex *end = source + count;
    byte_unit_init();
    if (!(batch->flags & C3_TITLE_LIT)) {
        const float diffuse[3] = {batch->diffuse[0], batch->diffuse[1], batch->diffuse[2]};
        const float alpha = material->alpha;
        for (; source < end; ++source, ++destination) {
            const float vertex_alpha = byte_unit[source->rgba[3]];
            memcpy(destination->pos, source->pos, sizeof destination->pos);
            destination->uv[0] = source->uv[0];
            destination->uv[1] = flip_v ? 1.0f - source->uv[1] : source->uv[1];
            memcpy(destination->uv2, source->uv2, sizeof destination->uv2);
            for (unsigned c = 0; c < 3; ++c) {
                const float vertex_color = byte_unit[source->rgba[c]];
                destination->rgba[c] = color_source == 1 ? diffuse[c] : color_source == 2 ? vertex_color :
                                       unit_interval(vertex_color + diffuse[c]);
            }
            destination->rgba[3] = alpha_source == 1 ? alpha : alpha_source == 2 ? vertex_alpha : alpha * vertex_alpha;
            destination->specular[0] = destination->specular[1] = destination->specular[2] = 0.0f;
        }
        return;
    }
    const bool vertex_color = (color_source & 2u) != 0, vertex_alpha = (alpha_source & 2u) != 0;
    const float ambient[3] = {batch->ambient[0], batch->ambient[1], batch->ambient[2]};
    for (; source < end; ++source, ++destination) {
        float normal[3] = {source->normal[0], source->normal[1], source->normal[2]};
        float squared = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];
        float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 0.0f;
        if (fabsf(squared - 1.0f) > 1e-4f) normalize(normal);
        for (unsigned i = 0; i < setup->count; ++i) {
            const StageLight *light = &setup->lights[i];
            float intensity;
            if (light->type == 0) {
                red += ambient[0] * light->color[0];
                green += ambient[1] * light->color[1];
                blue += ambient[2] * light->color[2];
                alpha += light->alpha;
                continue;
            }
            if (light->type == 1) {
                intensity = positive_part(normal[0]*light->vector[0] + normal[1]*light->vector[1] + normal[2]*light->vector[2]);
            } else {
                float direction[3] = {light->vector[0] - source->pos[0], light->vector[1] - source->pos[1],
                                      light->vector[2] - source->pos[2]};
                float distance = normalize(direction);
                intensity = positive_part(normal[0]*direction[0] + normal[1]*direction[1] + normal[2]*direction[2]);
                intensity *= attenuation(light->light, distance);
                if (light->type == 3) intensity *= spotlight_attenuation(&light->cone, direction);
            }
            red += intensity * light->color[0];
            green += intensity * light->color[1];
            blue += intensity * light->color[2];
            alpha += intensity * light->alpha;
        }
        memcpy(destination->pos, source->pos, sizeof destination->pos);
        destination->uv[0] = source->uv[0];
        destination->uv[1] = flip_v ? 1.0f - source->uv[1] : source->uv[1];
        memcpy(destination->uv2, source->uv2, sizeof destination->uv2);
        destination->rgba[0] = unit_interval(red) * (vertex_color ? byte_unit[source->rgba[0]] : 1.0f);
        destination->rgba[1] = unit_interval(green) * (vertex_color ? byte_unit[source->rgba[1]] : 1.0f);
        destination->rgba[2] = unit_interval(blue) * (vertex_color ? byte_unit[source->rgba[2]] : 1.0f);
        /* MObjMakeTExp multiplies texture/material alpha by (1 - raster alpha).
         * The optional vertex-alpha channel is independent of light alpha. */
        destination->rgba[3] = (1.0f - unit_interval(alpha)) * (vertex_alpha ? byte_unit[source->rgba[3]] : 1.0f);
        destination->specular[0] = destination->specular[1] = destination->specular[2] = 0.0f;
    }
}

typedef struct { float position[3], half_direction[3], color[3]; } SpecularLight;

static void view_normal(const C3D_Mtx *view, const float world[3], float normal[3])
{
    C3D_FVec transformed = Mtx_MultiplyFVec3(view, FVec3_New(world[0], world[1], world[2]));
    normal[0] = transformed.x; normal[1] = transformed.y; normal[2] = transformed.z;
    normalize(normal);
}

static void prepare_specular(const C3Title *title, const C3TitleMaterialState *material,
                             const C3D_Mtx *view, uint32_t mask, SpecularLight lights[C3_TITLE_MAX_LIGHTS])
{
    /* HSD_LObjSetupSpecularInit uses the owning joint's eye-space translation,
     * not each vertex, to construct a half vector. GXInitLightDir negates it. */
    C3D_FVec origin = Mtx_MultiplyFVecH(view, FVec3_New(material->specular_origin[0],
        material->specular_origin[1], material->specular_origin[2]));
    float camera_direction[3] = {origin.x, origin.y, origin.z};
    if (normalize(camera_direction) <= 1e-12f) camera_direction[2] = -1;
    for (uint32_t i = 0; i < title->light_count; ++i) {
        if (!(mask & (1u << i))) continue;
        const C3TitleLight *light = &title->lights[i];
        float scale = (light->flags & 3u) == 1u ? 1048576.0f : 1.0f;
        C3D_FVec position = Mtx_MultiplyFVecH(view, FVec3_New(light->pos[0]*scale,
            light->pos[1]*scale, light->pos[2]*scale));
        lights[i].position[0] = position.x; lights[i].position[1] = position.y; lights[i].position[2] = position.z;
        float direction[3];
        bool degenerate = false;
        if ((light->flags & 3u) == 1u) {
            for (unsigned c = 0; c < 3; ++c) direction[c] = light->interest[c] - light->pos[c];
            if (normalize(direction) <= 1e-12f) direction[2] = 1;
            view_normal(view, direction, lights[i].half_direction);
        } else {
            /* Original HSD point/spot specular uses light-to-joint direction,
             * and a fixed half vector when the joint lies on the light. The
             * subsequent shininess setup replaces distance coefficients;
             * the spotlight cone belongs only to the diffuse channel. */
            direction[0] = origin.x-position.x;
            direction[1] = origin.y-position.y;
            direction[2] = origin.z-position.z;
            degenerate = normalize(direction) <= 1e-12f;
            memcpy(lights[i].half_direction, direction, sizeof(direction));
        }
        if (!degenerate) {
            for (unsigned c = 0; c < 3; ++c) lights[i].half_direction[c] += camera_direction[c];
            degenerate = normalize(lights[i].half_direction) <= 1e-12f;
        }
        if (degenerate) {
            lights[i].half_direction[0] = lights[i].half_direction[1] = 0;
            lights[i].half_direction[2] = 1;
        }
        for (unsigned c = 0; c < 3; ++c) {
            lights[i].half_direction[c] = -lights[i].half_direction[c];
            lights[i].color[c] = light->rgba[c];
        }
    }
}

static void raster_specular(const C3TitleBatch *batch, const C3TitleDrawVertex *source,
                            const C3D_Mtx *view, uint32_t mask,
                            const SpecularLight lights[C3_TITLE_MAX_LIGHTS], float rgb[3])
{
    float normal[3];
    view_normal(view, source->normal, normal);
    C3D_FVec position = Mtx_MultiplyFVecH(view, FVec3_New(source->pos[0], source->pos[1], source->pos[2]));
    unsigned accumulated[3] = {0, 0, 0};
    for (unsigned i = 0; i < C3_TITLE_MAX_LIGHTS; ++i) {
        if (!(mask & (1u << i))) continue;
        const SpecularLight *light = &lights[i];
        if (normal[0]*(light->position[0]-position.x) + normal[1]*(light->position[1]-position.y) +
            normal[2]*(light->position[2]-position.z) < 0) continue;
        float cosine = clamp01(normal[0]*light->half_direction[0] + normal[1]*light->half_direction[1] +
                               normal[2]*light->half_direction[2]);
        float square = cosine*cosine;
        /* GXInitLightShininess is a rational attenuation curve, not pow(N.H,s).
         * Angular coefficients=(0,0,1), distance=(s/2,0,1-s/2), GX_DF_NONE. */
        float intensity = square / (batch->shininess*0.5f + (1-batch->shininess*0.5f)*square);
        for (unsigned c = 0; c < 3; ++c)
            accumulated[c] += (unsigned)(clamp01(intensity*light->color[c])*255.0f + 0.5f);
    }
    for (unsigned c = 0; c < 3; ++c)
        rgb[c] = (accumulated[c] > 255 ? 1.0f : accumulated[c]/255.0f) * batch->specular[c];
}

static void combiner(const C3TitleBatch *batch, const C3TitleMaterialState *material, float fade)
{
    for (int i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvBufUpdate(C3D_Both, 0);
    C3D_TexEnv *tex = C3D_GetTexEnv(0);
#include "pda_mask_combiner.inc"
    if(batch->flags&C3_TITLE_PAIRED_NORMAL_EXT){
        /* Original diffuse raster, then two normal-generated EXT maps:
         * blend T0 by source factor, add T1 RGB/alpha, saturate each stage. */
        C3D_TexEnvColor(tex,color4(batch->diffuse[0],batch->diffuse[1],batch->diffuse[2],material->alpha));
        C3D_TexEnvSrc(tex,C3D_Both,GPU_PRIMARY_COLOR,GPU_CONSTANT,GPU_CONSTANT);
        C3D_TexEnvFunc(tex,C3D_Both,GPU_MODULATE);
        C3D_TexEnv *blend=C3D_GetTexEnv(1);
        C3D_TexEnvColor(blend,color4(batch->texture2_blend,batch->texture2_blend,batch->texture2_blend,1));
        C3D_TexEnvSrc(blend,C3D_RGB,GPU_TEXTURE0,GPU_PREVIOUS,GPU_CONSTANT);
        C3D_TexEnvFunc(blend,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnv *add=C3D_GetTexEnv(2);
        C3D_TexEnvSrc(add,C3D_Both,GPU_PREVIOUS,GPU_TEXTURE1,GPU_CONSTANT);
        C3D_TexEnvFunc(add,C3D_Both,GPU_ADD);
        return;
    }
    if (batch->flags & C3_TITLE_DUAL_MODULATE) {
        /* Reviewed S1_out water material: both TEV0 endpoints are zero,
         * both konst colors match, and both layers modulate RGB while alpha
         * passes through. Keep both independent UV channels and multiply
         * exactly T0 * T1 * konst^2 * the source raster color. */
        C3D_TexEnvSrc(tex, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE1, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(tex, C3D_RGB, GPU_MODULATE);
        C3D_TexEnvSrc(tex, C3D_Alpha, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(tex, C3D_Alpha, GPU_REPLACE);
        C3D_TexEnv *scale = C3D_GetTexEnv(1);
        C3D_TexEnvColor(scale, color4(material->konst[0]*material->konst[0],
            material->konst[1]*material->konst[1], material->konst[2]*material->konst[2], 1));
        C3D_TexEnvSrc(scale, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(scale, C3D_RGB, GPU_MODULATE);
        C3D_TexEnv *raster = C3D_GetTexEnv(2);
        C3D_TexEnvSrc(raster, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(raster, C3D_RGB, GPU_MODULATE);
        C3D_TexEnv *fade_stage = C3D_GetTexEnv(5);
        C3D_TexEnvColor(fade_stage, color4(fade, fade, fade, 1));
        C3D_TexEnvSrc(fade_stage, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(fade_stage, C3D_RGB, GPU_MODULATE);
        return;
    }
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
        /* Reflection EXT on the actual bike uses color MODULATE / alpha PASS,
         * so stage three is free for lighting and stage four can apply EXT. */
        C3D_TexEnv *light = C3D_GetTexEnv(batch->flags & C3_TITLE_REFLECTION_BLEND ? 3 : 4);
        C3D_TexEnvSrc(light, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(light, C3D_Both, GPU_MODULATE);
    } else C3D_TexEnvColor(map, color4(blend, blend, blend, blend));
    C3D_TexEnvSrc(map, C3D_RGB, batch->color_mode == 0 ? base : GPU_PREVIOUS, base, GPU_CONSTANT);
    C3D_TexEnvFunc(map, C3D_RGB, batch->color_mode == 0 ? GPU_REPLACE :
        batch->color_mode == 3 && !lit ? GPU_INTERPOLATE : GPU_MODULATE);
    C3D_TexEnvSrc(map, C3D_Alpha, batch->alpha_mode == 0 ? base : GPU_PREVIOUS, base, GPU_CONSTANT);
    C3D_TexEnvFunc(map, C3D_Alpha, batch->alpha_mode == 0 ? GPU_REPLACE :
        batch->alpha_mode == 2 && !lit ? GPU_INTERPOLATE : GPU_MODULATE);
    if (batch->flags & C3_TITLE_REFLECTION_BLEND) {
        C3D_TexEnv *reflection = C3D_GetTexEnv(4);
        C3D_TexEnvColor(reflection, color4(batch->texture2_blend, batch->texture2_blend, batch->texture2_blend, 1));
        C3D_TexEnvSrc(reflection, C3D_RGB, GPU_TEXTURE1, GPU_PREVIOUS, GPU_CONSTANT);
        C3D_TexEnvFunc(reflection, C3D_RGB, GPU_INTERPOLATE);
    }
    C3D_TexEnv *fade_stage = C3D_GetTexEnv(5);
    C3D_TexEnvColor(fade_stage, color4(fade, fade, fade, 1.0f));
    C3D_TexEnvSrc(fade_stage, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(fade_stage, C3D_RGB, GPU_MODULATE);
}

static void apply_model_modulation(size_t model)
{
    if(!model_modulation[model][3])return;
    const uint8_t *c=model_modulation[model];
    /* _matGSmatObjMakeTExp: (0 + color * previous) * GX_CS_SCALE_2.
     * Floor-color entries do not change alpha. Reapply after the specular
     * combiner reset as well as the diffuse path. */
    C3D_TexEnv *env=C3D_GetTexEnv(5);
    C3D_TexEnvColor(env,(uint32_t)c[0]|(uint32_t)c[1]<<8|(uint32_t)c[2]<<16|0xff000000u);
    C3D_TexEnvSrc(env,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_MODULATE);
    C3D_TexEnvScale(env,C3D_RGB,GPU_TEVSCALE_2);
}

static bool camera_matrices(const C3TitleCamera *camera, C3D_Mtx *mvp, C3D_Mtx *view, bool tilted)
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
    C3D_Mtx projection;
    Mtx_LookAt(view, FVec3_New(camera->eye[0], camera->eye[1], camera->eye[2]),
        FVec3_New(camera->target[0], camera->target[1], camera->target[2]),
        FVec3_New(rotated[0], rotated[1], rotated[2]), false);
    if (tilted)
        Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(camera->fov_y_degrees), camera->aspect,
                     camera->near_plane, camera->far_plane, false);
    else
        Mtx_Persp(&projection, C3D_AngleFromDegrees(camera->fov_y_degrees), camera->aspect,
                 camera->near_plane, camera->far_plane, false);
    Mtx_Multiply(mvp, &projection, view);
    return true;
}

#include "frame_pacing.inc"
/* From here on every cache flush of a CPU-written linear range, including the
 * HUD, particle and Confusion ranges below, is timed and counted. */
#define GSPGPU_FlushDataCache(address, bytes) stage_flush((address), (bytes))

int c3_stage_renderer_set_battle_hud(const C3StageBattleHud *hud, char *error, size_t capacity)
{
    if (!hud || !scene_count) return fail(error, capacity, "HUD requires an owned stage.");
    for (unsigned i=0;i<8;++i)
        if (!memchr(hud->lines[i],0,sizeof hud->lines[i])) return fail(error,capacity,"HUD text exceeds bound.");
    if (hud->enabled && hud_texture==UINT32_MAX) {
        if (texture_count>=C3_STAGE_MAX_TEXTURES || !c3_stage_renderer_is_idle())
            return fail(error,capacity,"HUD initialization requires an idle texture slot.");
        if(!hud_vertices){
            hud_vertices=linearAlloc(C3_HUD_MAX_VERTICES*sizeof *hud_vertices);
            if(!hud_vertices)return fail(error,capacity,"HUD vertex allocation failed.");
            gpu_buffer_bytes+=C3_HUD_MAX_VERTICES*sizeof *hud_vertices;
        }
        const ConsoleFont *font=&consoleGetDefault()->font;
        if (!font->gfx || font->asciiOffset || font->numChars<128)
            return fail(error,capacity,"Unsupported development font.");
        uint8_t *pixels=calloc(128u*64u,4);
        if (!pixels) return fail(error,capacity,"HUD atlas allocation failed.");
        for (unsigned c=0;c<128;++c)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            size_t p=(((c/16)*8+y)*128+(c%16)*8+x)*4;
            pixels[p]=pixels[p+1]=pixels[p+2]=255;
            pixels[p+3]=(font->gfx[c*8+y]&(0x80u>>x))?255:0;
        }
        C3TitleTexture source={128,64,0,0,pixels,128u*64u*4u};
        bool ok=upload_texture(&textures[texture_count],&source);
        free(pixels);
        if (!ok) return fail(error,capacity,"HUD atlas upload failed.");
        C3D_TexSetFilter(&textures[texture_count],GPU_NEAREST,GPU_NEAREST);
        hud_texture=texture_count++;
        gpu_buffer_bytes+=textures[hud_texture].size;
    }
    battle_hud=*hud;
    return 0;
}

static void hud_quad(uint32_t *count,float x,float y,float w,float h,float u,float v,float uw,float vh,
                     float r,float g,float b,float a)
{
    static const unsigned corners[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    if(*count>C3_HUD_MAX_VERTICES-6u)return;
    for(unsigned i=0;i<6;++i){
        float cx=(float)corners[i][0],cy=(float)corners[i][1];
        TitleGPUVertex *out=&hud_vertices[(*count)++];memset(out,0,sizeof *out);
        out->pos[0]=x+w*cx;out->pos[1]=y+h*cy;
        out->uv[0]=u+uw*cx;out->uv[1]=v+vh*cy;
        out->rgba[0]=r;out->rgba[1]=g;out->rgba[2]=b;out->rgba[3]=a;
    }
}

static void draw_battle_hud(C3TitleRenderStats *stats)
{
    if(!battle_hud.enabled || hud_texture==UINT32_MAX || !hud_vertices)return;
    uint32_t count=0;
    C3D_Mtx ortho;Mtx_OrthoTilt(&ortho,0,400,240,0,-1,1,false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,mvp_location,&ortho);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,color_pass_location,1,0,0,0);
    C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    hud_quad(&count,0,166,400,74,0,0,0,0,.02f,.04f,.08f,.9f);
    for(unsigned i=0;i<4;++i){
        float x=(i&1u)?292:8,y=i<2?146:8;
        hud_quad(&count,x,y,100,5,0,0,0,0,.1f,.1f,.1f,.9f);
        float f=battle_hud.maximum_hp[i]?(float)battle_hud.hp[i]/battle_hud.maximum_hp[i]:0;
        hud_quad(&count,x,y,100*clamp01(f),5,0,0,0,0,.1f,.85f,.3f,1);
    }
    uint32_t background_count=count;
    C3D_TexBind(0,&textures[hud_texture]);
    for(unsigned row=0;row<8;++row)for(unsigned col=0;col<48&&battle_hud.lines[row][col];++col){
        unsigned c=(unsigned char)battle_hud.lines[row][col];if(c<32||c>=128)c='?';
        if(c==' ')continue;
        hud_quad(&count,8+col*8,170+row*8,8,8,(c%16)/16.f,(c/16)/8.f,1/16.f,1/8.f,1,1,1,1);
    }
    /* Own one stable linear arena until the scene's GPU drain. Bind it through
     * the same proven attribute layout as authored geometry, with two bounded
     * draws rather than hundreds of immediate-mode attribute submissions. */
    GSPGPU_FlushDataCache(hud_vertices,count*sizeof *hud_vertices);
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,hud_vertices,sizeof *hud_vertices,5,0x43210);
    C3D_SetViewport(0,0,240,400);
    C3D_DrawArrays(GPU_TRIANGLES,0,background_count);
    stats->draw_calls++;stats->vertices+=background_count;
    if(count>background_count){
        /* Getter dirties this stage again after DrawArrays consumed its state.
         * Mutating the cached pointer alone would leave primary-color REPLACE
         * active, turning every non-space glyph into an opaque white cell. */
        env=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        C3D_DrawArrays(GPU_TRIANGLES,background_count,count-background_count);
        stats->draw_calls++;stats->vertices+=count-background_count;
    }
}

#include "particle_renderer.inc"
#include "particle_line_renderer.inc"
#include "confusion_renderer.inc"

int c3_stage_renderer_draw(const C3Title *const *models, size_t count,
                          const C3TitleCamera *authored_camera, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity)
{
    /* Widescreen (Hor+): keep the authored vertical FOV and framing but project
     * at the native 400x240 top-screen aspect instead of letterboxing the GC
     * 4:3 image. Every pass (main, Confusion capture, particles) derives from
     * this one camera, so their screen mappings stay mutually consistent.
     * Build with -DC3_AUTHORED_ASPECT_LETTERBOX for authored-aspect parity. */
    const C3TitleCamera *camera = authored_camera;
#ifndef C3_AUTHORED_ASPECT_LETTERBOX
    C3TitleCamera display_camera;
    if (authored_camera) {
        display_camera = *authored_camera;
        display_camera.aspect = 400.0f / 240.0f;
        camera = &display_camera;
    }
#endif
    C3TitleRenderStats local_stats;
    if (!stats) stats = &local_stats;
    if (stats) memset(stats, 0, sizeof(*stats));
    stats->command_capacity_words=C3_RENDER_COMMAND_CAPACITY_WORDS;
    if (!models || !scene_count || count != scene_count)
        return fail(error, capacity, "Scene renderer does not own this model set.");
    if (!isfinite(fade) || !camera) return fail(error, capacity, "Invalid scene fade or camera.");
    C3D_Mtx mvp, view;
    if (!camera_matrices(camera, &mvp, &view, true)) return fail(error, capacity, "Invalid sampled scene camera.");
    for (size_t model = 0; model < count; ++model) {
        const C3Title *title = models[model];
        const SceneBinding *binding = &scenes[model];
        if (!title || title != binding->asset || title->vertex_count != binding->vertex_count ||
            title->batch_count != binding->batch_count || title->texture_count != binding->texture_count ||
            !title->draw_vertices || !title->batches || title->light_count > C3_TITLE_MAX_LIGHTS)
            return fail(error, capacity, "Scene geometry identity or size changed after initialization.");
        for (uint32_t i = 0; i < title->batch_count; ++i) {
            const C3TitleBatch *batch = &title->batches[i];
            if (!valid_batch(title, batch)) return fail(error, capacity, "Invalid scene batch before submission.");
            if (batch->flags & C3_TITLE_DUAL_MODULATE)
                for (unsigned component = 0; component < 3; ++component)
                    if (title->pose.materials[i].tev0[component] != 0.0f)
                        return fail(error, capacity, "Dual-texture material requires zero TEV0 endpoints.");
        }
    }
    C3D_Mtx capture_mvp, capture_view;
    if (!camera_matrices(camera,&capture_mvp,&capture_view,false))
        return fail(error,capacity,"Invalid Confusion capture camera.");
    C3D_Mtx tv_mvp, tv_view;
    if (tv.configured && tv.enabled &&
        !camera_matrices(&models[tv.model]->pose.camera, &tv_mvp, &tv_view, false))
        return fail(error, capacity, "Invalid sampled television camera.");
    /* The frame is normally already owned: the previous draw acquired it
     * before the caller simulated. Otherwise make the one nonblocking attempt. */
    if (!stage_frame_begin()) return C3_RENDER_BUSY;
    scene_drawn=true;
    uint64_t start = svcGetSystemTick();
    uint64_t prepare_start = stage_now_us();
    int confusion_active=confusion_prepare(&capture_mvp,&capture_view,error,capacity);
    if(confusion_active<0){stage_frame_abort();return C3_RENDER_ERROR;}
    if(confusion_active){
        for(size_t model=0;model<count;++model)
            for(uint32_t i=0;i<models[model]->batch_count;++i)
                if((models[model]->batches[i].render_mode&0x60000000u)==0x20000000u){
                    stage_frame_abort();
                    return fail(error,capacity,"Unsupported original HSD transparency class for capture.");
                }
    }
    /* FrameBegin drained the previous GPU queue before this single bounded
     * arena is written. Ranges of tracked batches whose inputs did not change
     * are left as they are: the arena persists across completed frames. */
    if (stats) stats->gpu_previous_ms = C3D_GetDrawingTime();
    for (size_t model = 0; model < count; ++model) {
        if (tv.configured && model == tv.model && !tv.enabled && !confusion_active) continue;
        const C3Title *title = models[model];
        const C3D_Mtx *model_view = tv.configured && tv.enabled && model == tv.model && !confusion_active ? &tv_view : &view;
        TitleGPUVertex *destination = vertices + scenes[model].first_vertex;
        _Static_assert(sizeof(C3D_Mtx)==64,"Prepared-view cache matrix extent");
        C3StageVertexCache *cache = &vertex_cache[model];
        bool tracked = c3_stage_cache_frame(cache, title, model_view);
        uint32_t staged_first = UINT32_MAX, staged_end = 0;
        LightSetup lights;
        bool lights_ready = false;
        lights.count = 0;
        for (uint32_t i = 0; i < title->batch_count; ++i) {
            const C3TitleBatch *batch = &title->batches[i];
            const C3TitleMaterialState *material = &title->pose.materials[i];
            if (material->visible < 0.5f) continue;
            bool flip_v = tv.configured && tv.enabled && model == tv.screen_model && batch->texture == tv.texture;
            if (tracked && c3_stage_cache_batch(cache, title, i, flip_v)) {
                if (stats) { ++stats->cached_batches; stats->vertices_static += batch->count; }
                continue;
            }
            if (batch->first < staged_first) staged_first = batch->first;
            if (batch->first + batch->count > staged_end) staged_end = batch->first + batch->count;
            if(stats)stats->vertices_prepared+=batch->count;
            if ((batch->flags & C3_TITLE_LIT) && !lights_ready) { light_setup(title, &lights); lights_ready = true; }
            /* Normal uploads reverse pixel rows to retain GX's top-origin
             * UVs. A texture-backed framebuffer has native bottom-origin
             * rows, so flip_v reverses V only on the screen sampling this
             * target. The colored-corner RTT probe verifies this convention. */
            stage_batch(&lights, batch, material, title->draw_vertices + batch->first,
                        destination + batch->first, batch->count, flip_v);
            if (batch->flags & C3_TITLE_SPECULAR) {
                SpecularLight specular[C3_TITLE_MAX_LIGHTS];
                prepare_specular(title, material, model_view, scenes[model].specular_lights, specular);
                for (uint32_t j = batch->first; j < batch->first + batch->count; ++j)
                    raster_specular(batch, &title->draw_vertices[j], model_view, scenes[model].specular_lights,
                                    specular, destination[j].specular);
            }
            if ((batch->flags & C3_TITLE_REFLECTION_BLEND) && !(batch->flags&C3_TITLE_SECOND_UV_BLEND)) {
                for (uint32_t j = batch->first; j < batch->first + batch->count; ++j) {
                    float normal[3];
                    view_normal(model_view, title->draw_vertices[j].normal, normal);
                    float u = normal[0]*0.5f + 0.5f, v = -normal[1]*0.5f + 0.5f;
                    destination[j].uv2[0] = u*material->uv2_scale[0] + v*material->uv2_cross[0] + material->uv2_offset[0];
                    destination[j].uv2[1] = u*material->uv2_cross[1] + v*material->uv2_scale[1] + material->uv2_offset[1];
                    if(batch->flags&C3_TITLE_PAIRED_NORMAL_EXT){
                        destination[j].uv[0]=u*material->uv_scale[0]+v*material->uv_cross[0]+material->uv_offset[0];
                        destination[j].uv[1]=u*material->uv_cross[1]+v*material->uv_scale[1]+material->uv_offset[1];
                    }
                }
            }
        }
        /* Only the rewritten span needs cache maintenance. */
        if (staged_end)
            GSPGPU_FlushDataCache(destination + staged_first, (size_t)(staged_end - staged_first) * sizeof(*vertices));
        else if (tracked && stats) ++stats->cached_models;
    }
    uint64_t submit_start = stage_now_us(), prepare_upload_us = frame_upload_us;
    unsigned tv_passes=tv.configured&&tv.enabled?1u:0u;
    unsigned target_passes = 1u+tv_passes+(confusion_active?1u:0u);
    for (unsigned target_pass = 0; target_pass < target_passes; ++target_pass) {
        bool capture=confusion_active&&target_pass==tv_passes;
        bool offscreen=tv_passes&&target_pass==0;
        C3D_RenderTarget *draw_target = capture ? confusion_target : offscreen ? tv_target : target;
        /* PICA reversed depth clears to zero (GX ZFFFFFF/far); source capture
         * inherits transparent black instead of the main opaque clear. */
        C3D_RenderTargetClear(draw_target, C3D_CLEAR_ALL, capture?0:0x000000FF, 0);
        C3D_FrameDrawOn(draw_target);
        /* Never stretch: fit the projection aspect (native 5:3 unless built
         * with authored-aspect letterboxing). GPU coordinates are rotated:
         * viewport height is display width. */
        float aspect = camera->aspect;
        unsigned display_width = 400, display_height = 240;
        if (aspect < 400.0f/240.0f) display_width = (unsigned)(240.0f * aspect + 0.5f);
        else display_height = (unsigned)(400.0f / aspect + 0.5f);
        if(capture) C3D_SetViewport(0,0,CONFUSION_WIDTH,CONFUSION_HEIGHT);
        else if (offscreen) C3D_SetViewport(0, 0, 256, 256);
        else C3D_SetViewport((240-display_height)/2, (400-display_width)/2, display_height, display_width);
        C3D_BindProgram(&program);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, capture ? &capture_mvp : offscreen ? &tv_mvp : &mvp);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, color_pass_location, 1, 0, 0, 0);
        C3D_AttrInfo *attributes = C3D_GetAttrInfo();
        AttrInfo_Init(attributes);
        AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
        AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
        AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 4);
        AttrInfo_AddLoader(attributes, 3, GPU_FLOAT, 2);
        AttrInfo_AddLoader(attributes, 4, GPU_FLOAT, 3);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        static const GPU_TESTFUNC depth_function[8] = {
            GPU_NEVER, GPU_GREATER, GPU_EQUAL, GPU_GEQUAL,
            GPU_LESS, GPU_NOTEQUAL, GPU_LEQUAL, GPU_ALWAYS};
        /* The original title retains its authored batch order. A multipart field
         * submits all opaque geometry before transparent overlays, so an actor
         * cannot disappear behind a late opaque pack. Within each class, source
         * model/batch order is retained; dynamic depth sorting is not invented. */
        unsigned passes = !capture && !offscreen && count > 1 ? 2u : 1u;
        for (unsigned pass = 0; pass < passes; ++pass) {
            for (size_t model = 0; model < count; ++model) {
                if (!capture && tv.configured && (offscreen ? model != tv.model : model == tv.model)) continue;
                const C3Title *title = models[model];
                const SceneBinding *binding = &scenes[model];
                /* The getter marks this state dirty. Reacquire it for each pack:
                 * mutating a cached C3D_BufInfo after a draw would keep the previous
                 * pack's GPU binding even though the CPU struct had changed. */
                C3D_BufInfo *buffers = C3D_GetBufInfo();
                BufInfo_Init(buffers);
                BufInfo_Add(buffers, vertices + binding->first_vertex, sizeof(*vertices), 5, 0x43210);
                /* GS flags0x3010: each model opaque, texture edge, translucent. */
                for(unsigned model_pass=0;model_pass<(capture?3u:1u);++model_pass){
                for (uint32_t i = 0; i < title->batch_count; ++i) {
                    const C3TitleBatch *batch = &title->batches[i];
                    const C3TitleMaterialState *material = &title->pose.materials[i];
                    unsigned transparent = !!(batch->flags & (C3_TITLE_TRANSLUCENT | C3_TITLE_ADDITIVE |
                        C3_TITLE_SUBTRACT | C3_TITLE_ADDITIVE_ONE));
                    /* HSD DObjLoad derives OPA/TEXEDGE/XLU from the original
                     * MObj mode, independently of custom pixel-engine overrides.
                     * Keep GSmodel's pass order 1,4,2; retain the native PE below. */
                    uint32_t source_class=batch->render_mode&0x60000000u;
                    unsigned capture_class=source_class==0?0u:source_class==0x40000000u?1u:2u;
                    if ((capture && capture_class!=model_pass) || (passes == 2 && transparent != pass) || material->visible < 0.5f ||
                        ((batch->flags & C3_TITLE_CULL_BACK) && (batch->flags & C3_TITLE_CULL_FRONT))) continue;
                    C3D_CullFace(batch->flags & C3_TITLE_CULL_BACK ? GPU_CULL_BACK_CCW :
                                 batch->flags & C3_TITLE_CULL_FRONT ? GPU_CULL_FRONT_CCW : GPU_CULL_NONE);
                    C3D_AlphaTest(batch->alpha_compare0 == 4, GPU_GREATER, batch->alpha_ref0);
                    C3D_DepthTest(true, depth_function[batch->depth_compare],
                        batch->flags & C3_TITLE_NO_DEPTH_WRITE ? GPU_WRITE_COLOR : GPU_WRITE_ALL);
                    if (batch->flags & C3_TITLE_SUBTRACT)
                        /* GX_BM_SUBTRACT ignores its stored factors and computes
                         * destination minus source, with ONE/ONE factors. */
                        C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_REVERSE_SUBTRACT,
                            GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
                    else if (batch->flags & C3_TITLE_ADDITIVE_ONE)
                        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
                    else if (transparent)
                        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA,
                            batch->flags & C3_TITLE_ADDITIVE ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA,
                            GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
                    else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                    /* Apply the scene fade once after framebuffer blending. Doing
                     * it in each material would fade before the specular addition
                     * saturates, making bright opaque surfaces too bright. */
                    combiner(batch, material, 1.0f);
                    apply_model_modulation(model);
                    C3D_Tex *base_texture = tv.configured && tv.enabled && !offscreen &&
                        model == tv.screen_model && batch->texture == tv.texture ?
                        &tv_texture : &textures[binding->textures[batch->texture]];
                    C3D_TexBind(0, base_texture);
                    /* Citro3D reads the texture type for units above zero before
                     * binding it. Keep a valid base texture on unused unit one;
                     * this material's combiner does not sample that unit. */
                    C3D_TexBind(1, batch->flags & (C3_TITLE_DUAL_MODULATE | C3_TITLE_REFLECTION_BLEND) ?
                        &textures[binding->textures[batch->texture2]] : base_texture);
                    C3D_DrawArrays(GPU_TRIANGLES, batch->first, batch->count);
                    if (stats) { stats->draw_calls++; stats->vertices += batch->count; }
                    if (batch->flags & C3_TITLE_SPECULAR) {
                        /* Add the independent GX specular raster channel after
                         * textured diffuse. Reuse this immutable vertex range;
                         * equal-depth and the original alpha test bound coverage.
                         * Fractional-alpha edges can differ from GX when diffuse
                         * plus specular saturates before GX's single alpha blend. */
                        C3D_FVUnifSet(GPU_VERTEX_SHADER, color_pass_location, 0, 1, 0, 0);
                        for (int stage = 0; stage < 5; ++stage) {
                            C3D_TexEnv *env = C3D_GetTexEnv(stage);
                            C3D_TexEnvSrc(env, C3D_RGB, stage ? GPU_PREVIOUS : GPU_PRIMARY_COLOR,
                                GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                            C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
                        }
                        if((batch->flags&C3_TITLE_REFLECTION_BLEND)&&!(batch->flags&C3_TITLE_SOURCE_ALPHA_MASK)){
                            /* EXT interpolation follows diffuse+specular in
                             * HSD. Only the prior lighting contribution is
                             * attenuated; its texture term was drawn once. */
                            C3D_TexEnv *ext=C3D_GetTexEnv(4);
                            float remaining=1.f-batch->texture2_blend;
                            C3D_TexEnvColor(ext,color4(remaining,remaining,remaining,1));
                            C3D_TexEnvSrc(ext,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PRIMARY_COLOR);
                            C3D_TexEnvFunc(ext,C3D_RGB,GPU_MODULATE);
                        }
                        apply_model_modulation(model);
                        C3D_DepthTest(true, batch->flags&C3_TITLE_NO_DEPTH_WRITE ? depth_function[batch->depth_compare] : GPU_EQUAL, GPU_WRITE_COLOR);
                        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, transparent ? GPU_SRC_ALPHA : GPU_ONE,
                            GPU_ONE, GPU_ZERO, GPU_ONE);
                        C3D_DrawArrays(GPU_TRIANGLES, batch->first, batch->count);
                        C3D_FVUnifSet(GPU_VERTEX_SHADER, color_pass_location, 1, 0, 0, 0);
                        if (stats) { stats->draw_calls++; stats->vertices += batch->count; }
                    }
                }
                } /* per-model capture material classes */
            }
        }
    } /* The texture-backed pass precedes the main camera in one GPU queue. */
    if(confusion_active&&!confusion_draw(&mvp,stats,error,capacity)){
        stage_frame_abort();return C3_RENDER_ERROR;
    }
    if(!draw_particles(camera,&mvp,stats,error,capacity)){
        stage_frame_abort();return C3_RENDER_ERROR;
    }
    draw_battle_hud(stats);
    fade = clamp01(fade);
    if (fade < 1.0f) {
        C3D_Mtx identity;
        Mtx_Identity(&identity);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, &identity);
        for (int stage = 0; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
        C3D_TexEnv *overlay = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(overlay, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(overlay, C3D_Both, GPU_REPLACE);
        /* The overlay samples only primary color. Retain valid texture
         * bindings: Citro3D cannot accept NULL on texture unit one. */
        C3D_CullFace(GPU_CULL_NONE);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
        /* Immediate attributes live in the command buffer, so this fullscreen
         * triangle does not overwrite any submitted model vertex storage. */
        static const float clip[3][2] = {{-1,-1},{3,-1},{-1,3}};
        C3D_ImmDrawBegin(GPU_TRIANGLES);
        for (unsigned i = 0; i < 3; ++i) {
            C3D_ImmSendAttrib(clip[i][0], clip[i][1], 0, 1);
            C3D_ImmSendAttrib(0, 0, 0, 1);
            C3D_ImmSendAttrib(0, 0, 0, 1-fade);
            C3D_ImmSendAttrib(0, 0, 0, 1);
            C3D_ImmSendAttrib(0, 0, 0, 1);
        }
        C3D_ImmDrawEnd();
        if (stats) { stats->draw_calls++; stats->vertices += 3; }
    }
    if (stats) {
        uint64_t submit_end = stage_now_us();
        stats->draw_prepare_us = (svcGetSystemTick() - start) * 1000000ull / SYSCLOCK_ARM11;
        stats->gpu_buffer_bytes = gpu_buffer_bytes;
        /* Cache flushes are reported once, as upload time, not twice. */
        stats->prepare_us = (uint32_t)(submit_start - prepare_start - prepare_upload_us);
        stats->submit_us = (uint32_t)(submit_end - submit_start - (frame_upload_us - prepare_upload_us));
        stats->upload_us = (uint32_t)frame_upload_us;
        stats->upload_bytes = (uint32_t)frame_upload_bytes;
    }
    /* The top framebuffer is GPU-written and the bottom screen's owner
     * flushes when it redraws, so no CPU framebuffer flush belongs here. */
    stage_frame_end(stats);
    record_submission(stats);
    stage_frame_next(stats);
    if (error && capacity) error[0] = '\0';
    return 0;
}

int c3_title_renderer_draw(const C3Title *title, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity)
{
    return c3_stage_renderer_draw(&title, 1, title ? &title->pose.camera : NULL,
                                  fade, stats, error, capacity);
}

void c3_title_renderer_shutdown(void)
{
    /* An owned frame holds no commands: FrameBegin already proved the queue
     * idle, so closing it empty keeps the documented quiescence barrier. */
    if (frame_acquired) { C3D_FrameEnd(0); frame_acquired = false; }
    scene_count = 0;
    memset(scenes, 0, sizeof(scenes));
    memset(vertex_cache, 0, sizeof vertex_cache);
    memset(model_modulation, 0, sizeof model_modulation);
    /* Target deletion drains the linked GPU queue. Do not use FrameSync here:
     * HOME cancellation can shut down while VBlank callbacks are suspended. */
    confusion_shutdown();
    if (target) { C3D_RenderTargetDelete(target); target = NULL; }
    if (tv_target) { C3D_RenderTargetDelete(tv_target); tv_target = NULL; }
    if (tv.configured) C3D_TexDelete(&tv_texture);
    memset(&tv_texture, 0, sizeof(tv_texture));
    memset(&tv, 0, sizeof(tv));
    for (uint32_t i = 0; i < texture_count; ++i) C3D_TexDelete(&textures[i]);
    texture_count = 0;
    memset(texture_sources, 0, sizeof(texture_sources));
    vertex_count = 0;
    if (vertices) { linearFree(vertices); vertices = NULL; }
    if (hud_vertices) { linearFree(hud_vertices); hud_vertices = NULL; }
    if (particle_vertices) { linearFree(particle_vertices); particle_vertices=NULL; }
    particle_pack=NULL;particle_system=NULL;particle_srt_system=NULL;particle_order_system=NULL;particle_render_frame=0;
    memset(particle_bindings,0,sizeof particle_bindings);
    memset(particle_binding_textures,0,sizeof particle_binding_textures);particle_binding_count=0;
    particle_line_shutdown();
    gpu_buffer_bytes = 0;
}

void c3_stage_renderer_shutdown(void)
{
    c3_title_renderer_shutdown();
}

void c3_stage_renderer_fini(void)
{
    /* Only the application shutdown path finalizes the driver. Scene callers
     * must still acquire the existing idle barrier before releasing a bundle.
     * Finalize Citro3D while its current program remains valid. */
    c3_title_renderer_shutdown();
    if (completion_hooks) {
        gspSetEventCallback(GSPGPU_EVENT_PSC0, NULL, NULL, false);
        gspSetEventCallback(GSPGPU_EVENT_P3D, NULL, NULL, false);
        gspSetEventCallback(GSPGPU_EVENT_PPF, NULL, NULL, false);
        completion_hooks = false;
    }
    if (c3d_started) { C3D_Fini(); c3d_started = false; }
    particle_line_fini();
    if (program_started) { shaderProgramFree(&program); program_started = false; }
    if (shader) { DVLB_Free(shader); shader = NULL; }
}

bool c3_stage_renderer_is_idle(void)
{
    if (!c3d_started) return true;
    /* A frame the pacing code already owns is the same proof of quiescence.
     * Any other caller arrives from a busy wait, a recovery or a scene
     * change: stage every tracked batch again on the next draw. */
    if (!frame_acquired) {
        if (!C3D_FrameBegin(C3D_FRAME_NONBLOCK)) return false;
        for (size_t i = 0; i < scene_count; ++i) c3_stage_cache_reset(&vertex_cache[i]);
    }
    /* Close the acquired frame without adding GPU commands. Existing scene
     * memory remains owned until the caller chooses to resume or shut down.
     * Keep this frame empty; GPU-producing frame-end hooks are not allowed. */
    C3D_FrameEnd(0);
    frame_acquired = false;
    return true;
}

bool c3_stage_renderer_try_shutdown(void)
{
    if (!c3_stage_renderer_is_idle()) return false;
    c3_title_renderer_shutdown();
    return true;
}

#ifdef C3_GPU_DIAGNOSTICS
int c3_stage_renderer_probe_clear(void)
{
    if (!target || !scene_count) return C3_RENDER_ERROR;
    if (!frame_acquired && !C3D_FrameBegin(C3D_FRAME_NONBLOCK)) return C3_RENDER_BUSY;
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x204060FF, 0);
    C3D_FrameDrawOn(target);
    gfxFlushBuffers();
    C3D_FrameEnd(0);
    frame_acquired = false;
    record_submission(NULL);
    return C3_RENDER_OK;
}
#endif
