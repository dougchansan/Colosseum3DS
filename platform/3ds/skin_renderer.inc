/* Included by title_renderer.c, the sole Citro3D owner. GPU skinning.
 *
 * A batch whose bit is set in its title's gpu_batches is drawn from a static
 * bind-space vertex stream with the skin program (DVLE 1 of title_shader.shbin,
 * platform/3ds/title_skin_shader.pica). c3_title_animate then leaves its
 * sampled vertices alone and supplies pose.skin/skin_normal; this file
 * uploads at most C3_SKIN_MAX_BONES of them per draw with the model's lights
 * and the batch's material and UV rows (colosseum3ds/skin_uniforms.h).
 * Nothing about such a batch is staged on the CPU per frame.
 *
 * Eligibility is fixed when the scene is initialised: the model's lights must
 * fit the shader and the batch must be supported (no specular, no
 * normal-generated UVs, at most C3_SKIN_MAX_BONES bones). The caller opts in
 * by copying c3_stage_renderer_gpu_skin_mask into a retained title's
 * gpu_batches; the draw refuses masks outside it. */
#include "colosseum3ds/skin_uniforms.h"

typedef struct { float pos[3], normal[3], uv[2], uv2[2]; uint8_t rgba[4]; float slot; } SkinGPUVertex;
_Static_assert(sizeof(SkinGPUVertex) == 48, "Skin vertex stride");

static shaderProgram_s skin_program;
static bool skin_program_started;
static int skin_loc_mvp, skin_loc_bones, skin_loc_uv, skin_loc_light_vector, skin_loc_light_color,
           skin_loc_light_attenuation, skin_loc_base, skin_loc_mode, skin_loc_alpha;
static SkinGPUVertex *skin_vertices;
static uint32_t skin_first[C3_STAGE_MAX_MODELS];
static uint64_t skin_eligible[C3_STAGE_MAX_MODELS];
static C3SkinLights skin_lights[C3_STAGE_MAX_MODELS];
/* The skin program is bound; the title program's state must be restored
 * before the next CPU-path draw. */
static bool skin_bound;
/* Models (bit per index) the next init may give a GPU stream. Linear memory
 * is scarce in the battle (about 4 MB free before audio), so only animated
 * actors are requested; static arena packs are already cached on the CPU. */
static uint64_t skin_requested;

void c3_stage_renderer_request_gpu_skin(uint64_t models)
{
    skin_requested = models;
}

uint64_t c3_stage_renderer_gpu_skin_mask(size_t model)
{
    return model < scene_count ? skin_eligible[model] : 0;
}

static int skin_program_init(char *error, size_t capacity)
{
    if (skin_program_started) return 1;
    if (!shader || shader->numDVLE < 2) { fail(error, capacity, "Skin shader program is missing."); return 0; }
    if (R_FAILED(shaderProgramInit(&skin_program))) { fail(error, capacity, "Skin shader initialization failed."); return 0; }
    skin_program_started = true;
    if (R_FAILED(shaderProgramSetVsh(&skin_program, &shader->DVLE[1]))) { fail(error, capacity, "Skin vertex shader setup failed."); return 0; }
    shaderInstance_s *vs = skin_program.vertexShader;
    skin_loc_mvp = shaderInstanceGetUniformLocation(vs, "mvp");
    skin_loc_bones = shaderInstanceGetUniformLocation(vs, "bones");
    skin_loc_uv = shaderInstanceGetUniformLocation(vs, "uvRows");
    skin_loc_light_vector = shaderInstanceGetUniformLocation(vs, "lightVec");
    skin_loc_light_color = shaderInstanceGetUniformLocation(vs, "lightColor");
    skin_loc_light_attenuation = shaderInstanceGetUniformLocation(vs, "lightAtten");
    skin_loc_base = shaderInstanceGetUniformLocation(vs, "base");
    skin_loc_mode = shaderInstanceGetUniformLocation(vs, "mode");
    skin_loc_alpha = shaderInstanceGetUniformLocation(vs, "alphaMode");
    if (skin_loc_mvp < 0 || skin_loc_bones < 0 || skin_loc_uv < 0 || skin_loc_light_vector < 0 ||
        skin_loc_light_color < 0 || skin_loc_light_attenuation < 0 || skin_loc_base < 0 ||
        skin_loc_mode < 0 || skin_loc_alpha < 0) { fail(error, capacity, "Skin shader uniforms are missing."); return 0; }
    return 1;
}

/* Called by c3_stage_renderer_init once textures are bound. */
static int skin_init(const C3Title *const *models, size_t count, char *error, size_t capacity)
{
    memset(skin_eligible, 0, sizeof skin_eligible);
    skin_bound = false;
    uint64_t requested = skin_requested;
    skin_requested = 0; /* one request per scene */
    size_t total = 0;
    for (size_t m = 0; m < count; ++m) {
        if (m >= 64u || !((requested >> m) & 1u)) continue;
        C3SkinLights lights;
        if (!c3_skin_lights(models[m], &lights)) continue;
        uint64_t mask = 0;
        for (uint32_t b = 0; b < models[m]->batch_count; ++b)
            if (c3_skin_batch_supported(models[m], b)) mask |= UINT64_C(1) << b;
        if (!mask) continue;
        skin_eligible[m] = mask;
        /* Instances share their owner's immutable vertices: share the stream. */
        size_t k;
        for (k = 0; k < m; ++k) if (skin_eligible[k] == mask && models[k]->vertices == models[m]->vertices) break;
        if (k < m) skin_first[m] = skin_first[k];
        else { skin_first[m] = (uint32_t)total; total += models[m]->vertex_count; }
    }
    if (!total) return 1;
    if (!skin_program_init(error, capacity)) return 0;
    size_t bytes = total * sizeof(SkinGPUVertex);
    if (bytes > 32u * 1024u * 1024u - gpu_buffer_bytes) { fail(error, capacity, "Skin vertex stream exceeds GPU storage."); return 0; }
    skin_vertices = linearAlloc(bytes);
    if (!skin_vertices) { fail(error, capacity, "Skin vertex allocation failed."); return 0; }
    memset(skin_vertices, 0, bytes);
    gpu_buffer_bytes += bytes;
    for (size_t m = 0; m < count; ++m) {
        const C3Title *title = models[m];
        for (uint32_t b = 0; b < title->batch_count; ++b) {
            if (!((skin_eligible[m] >> b) & 1u)) continue;
            const C3TitleBatch *batch = &title->batches[b];
            for (uint32_t j = batch->first; j < batch->first + batch->count; ++j) {
                const C3TitleVertex *in = &title->vertices[j];
                SkinGPUVertex *out = &skin_vertices[skin_first[m] + j];
                memcpy(out->pos, in->pos, sizeof out->pos);
                memcpy(out->normal, in->normal, sizeof out->normal);
                memcpy(out->uv, in->uv, sizeof out->uv);
                memcpy(out->uv2, in->uv2, sizeof out->uv2);
                memcpy(out->rgba, in->rgba, sizeof out->rgba);
                out->slot = (float)(c3_skin_slot(title->batch_bones[b], in->bone) * C3_SKIN_ROWS_PER_BONE);
            }
        }
    }
    GSPGPU_FlushDataCache(skin_vertices, bytes);
    return 1;
}

/* Per frame and model, before staging: validate the opt-in and reduce lights. */
static int skin_model_prepare(size_t model, const C3Title *title, char *error, size_t capacity)
{
    if (!title->gpu_batches) return 1;
    if (!title->retained || (title->gpu_batches & ~skin_eligible[model]))
        { fail(error, capacity, "GPU-skinned batches need a retained title within the eligible mask."); return 0; }
    if (!c3_skin_lights(title, &skin_lights[model]))
        { fail(error, capacity, "GPU-skinned model lights exceed the skin shader."); return 0; }
    return 1;
}

static inline bool skin_batch_gpu(const C3Title *title, uint32_t batch)
{
    return (title->gpu_batches >> batch) & 1u;
}

static void skin_vec4(int location, const float v[4])
{
    C3D_FVUnifSet(GPU_VERTEX_SHADER, location, v[0], v[1], v[2], v[3]);
}

static void skin_draw(size_t model, const C3Title *title, uint32_t index, const C3D_Mtx *mvp)
{
    if (!skin_bound) {
        C3D_BindProgram(&skin_program);
        C3D_AttrInfo *attributes = C3D_GetAttrInfo();
        AttrInfo_Init(attributes);
        AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
        AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 3);
        AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 2);
        AttrInfo_AddLoader(attributes, 3, GPU_FLOAT, 2);
        AttrInfo_AddLoader(attributes, 4, GPU_UNSIGNED_BYTE, 4);
        AttrInfo_AddLoader(attributes, 5, GPU_FLOAT, 1);
        skin_bound = true;
    }
    /* The surrounding loop rebinds the CPU arena per model and pass, so the
     * buffer and the program-shared uniform registers are set every draw. */
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, skin_vertices + skin_first[model], sizeof(SkinGPUVertex), 6, 0x543210);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, skin_loc_mvp, mvp);
    const C3SkinLights *lights = &skin_lights[model];
    for (unsigned l = 0; l < C3_SKIN_MAX_LIGHTS; ++l) {
        skin_vec4(skin_loc_light_vector + (int)l, lights->vector[l]);
        skin_vec4(skin_loc_light_color + (int)l, lights->color[l]);
        skin_vec4(skin_loc_light_attenuation + (int)l, lights->attenuation[l]);
    }
    const C3TitleBatch *batch = &title->batches[index];
    static float rows[C3_SKIN_MAX_BONES * C3_SKIN_ROWS_PER_BONE][4];
    unsigned slots = c3_skin_palette(title, title->batch_bones[index], rows);
    for (unsigned r = 0; r < slots * C3_SKIN_ROWS_PER_BONE; ++r) skin_vec4(skin_loc_bones + (int)r, rows[r]);
    C3SkinBatchUniforms uniforms;
    c3_skin_batch_uniforms(batch, &title->pose.materials[index], lights, &uniforms);
    for (unsigned r = 0; r < 4; ++r) skin_vec4(skin_loc_uv + (int)r, uniforms.uv_rows[r]);
    skin_vec4(skin_loc_base, uniforms.base);
    skin_vec4(skin_loc_mode, uniforms.mode);
    skin_vec4(skin_loc_alpha, uniforms.alpha_mode);
    C3D_DrawArrays(GPU_TRIANGLES, batch->first, batch->count);
}

/* Back to the title program for a CPU-staged draw of this model. */
static void skin_restore(const SceneBinding *binding, const C3D_Mtx *mvp)
{
    if (!skin_bound) return;
    C3D_BindProgram(&program);
    C3D_AttrInfo *attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributes, 3, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 4, GPU_FLOAT, 3);
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, vertices + binding->first_vertex, sizeof(*vertices), 5, 0x43210);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, mvp);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, color_pass_location, 1, 0, 0, 0);
    skin_bound = false;
}

static void skin_shutdown(void)
{
    if (skin_vertices) { linearFree(skin_vertices); skin_vertices = NULL; }
    memset(skin_eligible, 0, sizeof skin_eligible);
    skin_bound = false;
}

static void skin_fini(void)
{
    if (skin_program_started) { shaderProgramFree(&skin_program); skin_program_started = false; }
}
