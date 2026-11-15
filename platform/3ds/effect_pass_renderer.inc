/* General WZX effect passes of the stage renderer (battle_wzx_effects.inc):
 * a per-model world pre-transform, extra translucent/additive draws of a
 * model's current batches (instances), world-space untextured triangles,
 * and attaching the type-12 capture program after the first draw. Included
 * by title_renderer.c after confusion_renderer.inc. Every list is copied;
 * the GPU reads only ranges written after FrameBegin drained the queue. */
static bool effect_matrix_enabled[C3_STAGE_MAX_MODELS];
static C3D_Mtx effect_matrix[C3_STAGE_MAX_MODELS];
static C3StageInstance effect_instances[C3_STAGE_MAX_INSTANCES];
static unsigned effect_instance_count;
static C3StageEffectVertex effect_source[C3_STAGE_MAX_EFFECT_VERTICES];
static unsigned effect_source_count;
static TitleGPUVertex *effect_vertices;

static void effect_affine(C3D_Mtx *out, const float m[12])
{
    Mtx_Identity(out);
    for (unsigned r = 0; r < 3; ++r) {
        out->r[r].x = m[r*4+0]; out->r[r].y = m[r*4+1]; out->r[r].z = m[r*4+2]; out->r[r].w = m[r*4+3];
    }
}
static bool effect_finite(const float *v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (!isfinite(v[i]) || fabsf(v[i]) > 1e5f) return false;
    return true;
}
static void effect_pass_reset(void)
{
    memset(effect_matrix_enabled, 0, sizeof effect_matrix_enabled);
    effect_instance_count = 0; effect_source_count = 0;
}
static void effect_pass_shutdown(void)
{
    effect_pass_reset();
    if (effect_vertices) { linearFree(effect_vertices); effect_vertices = NULL; }
}

int c3_stage_renderer_set_model_matrix(size_t model, const float matrix[12])
{
    if (model >= scene_count || (matrix && !effect_finite(matrix, 12))) return 0;
    effect_matrix_enabled[model] = matrix != NULL;
    if (matrix) effect_affine(&effect_matrix[model], matrix);
    return 1;
}
int c3_stage_renderer_set_instances(const C3StageInstance *list, unsigned count)
{
    if (count > C3_STAGE_MAX_INSTANCES || (count && !list)) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (list[i].model >= scene_count || !effect_finite(list[i].matrix, 12) || !effect_finite(list[i].rgba, 4)) return 0;
    if (count) memcpy(effect_instances, list, count * sizeof *list);
    effect_instance_count = count;
    return 1;
}
int c3_stage_renderer_set_effect_triangles(const C3StageEffectVertex *v, unsigned count)
{
    if (count > C3_STAGE_MAX_EFFECT_VERTICES || count % 3 || (count && !v) || !scene_count) return 0;
    for (unsigned i = 0; i < count; ++i) if (!effect_finite(v[i].pos, 3) || !effect_finite(v[i].rgba, 4)) return 0;
    if (count && !effect_vertices) {
        /* Allocated on first use; lives until the scene's guarded shutdown. */
        effect_vertices = linearAlloc(C3_STAGE_MAX_EFFECT_VERTICES * sizeof *effect_vertices);
        if (!effect_vertices) return 0;
        gpu_buffer_bytes += C3_STAGE_MAX_EFFECT_VERTICES * sizeof *effect_vertices;
    }
    if (count) memcpy(effect_source, v, count * sizeof *v);
    effect_source_count = count;
    return 1;
}
/* The capture resources of confusion_renderer.inc, attached while the scene
 * runs (a general type-12 node starts mid-battle). Only new allocations are
 * made; nothing the GPU may still read is touched. A second program only
 * swaps the CPU mask the next prepare reads. */
int c3_stage_renderer_distortion_program(const C3WzxConfusionProgram *p, char *error, size_t capacity)
{
    if (!p || !scene_count) return fail(error, capacity, "Distortion program requires an initialized scene.");
    if (confusion_program) { confusion_program = p; return 1; }
    bool drawn = scene_drawn;
    scene_drawn = false;
    int ok = c3_stage_renderer_confusion_program(p, error, capacity);
    scene_drawn = drawn;
    return ok;
}
/* A model's mvp for the main and capture passes. */
static const C3D_Mtx *effect_model_mvp(size_t model, const C3D_Mtx *pass, C3D_Mtx *storage)
{
    if (model >= C3_STAGE_MAX_MODELS || !effect_matrix_enabled[model]) return pass;
    Mtx_Multiply(storage, pass, &effect_matrix[model]);
    return storage;
}
static bool effect_budget(uint32_t words)
{
    return (uint64_t)gpuCmdBufOffset + words + C3_STAGE_COMMAND_RESERVE_WORDS <= gpuCmdBufSize;
}

/* After the scene (and any type-12 composite), on the main target. Draws
 * that exceed the frame's command budget are skipped, never an error. */
static void effect_pass_draw(const C3Title *const *models, const C3D_Mtx *mvp, C3TitleRenderStats *stats)
{
    static const GPU_TESTFUNC depth_function[8] = {
        GPU_NEVER, GPU_GREATER, GPU_EQUAL, GPU_GEQUAL, GPU_LESS, GPU_NOTEQUAL, GPU_LEQUAL, GPU_ALWAYS};
    for (unsigned k = 0; k < effect_instance_count; ++k) {
        const C3StageInstance *inst = &effect_instances[k];
        if (inst->model >= scene_count || inst->rgba[3] <= 0.0f) continue;
        const C3Title *title = models[inst->model];
        const SceneBinding *binding = &scenes[inst->model];
        C3D_Mtx local, inst_mvp;
        effect_affine(&local, inst->matrix);
        Mtx_Multiply(&inst_mvp, mvp, &local);
        for (uint32_t i = 0; i < title->batch_count; ++i) {
            const C3TitleBatch *batch = &title->batches[i];
            const C3TitleMaterialState *material = &title->pose.materials[i];
            if (material->visible < 0.5f || (batch->flags & C3_TITLE_SUBTRACT) ||
                ((batch->flags & C3_TITLE_CULL_BACK) && (batch->flags & C3_TITLE_CULL_FRONT))) continue;
            if (!effect_budget(skin_batch_gpu(title, i) ? 4096u : 512u)) return;
            C3D_CullFace(batch->flags & C3_TITLE_CULL_BACK ? GPU_CULL_BACK_CCW :
                         batch->flags & C3_TITLE_CULL_FRONT ? GPU_CULL_FRONT_CCW : GPU_CULL_NONE);
            C3D_AlphaTest(true, GPU_GREATER, 0);
            C3D_DepthTest(true, depth_function[batch->depth_compare], GPU_WRITE_COLOR);
            bool add = inst->additive || (batch->flags & (C3_TITLE_ADDITIVE | C3_TITLE_ADDITIVE_ONE));
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, add ? GPU_ONE : GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
            combiner(batch, material, 1.0f);
            /* Stage 5: the model's floor/owner modulation (2*color*previous)
             * times this draw's colour, and alpha scaled by its coverage. */
            C3D_TexEnv *env = C3D_GetTexEnv(5);
            float r = inst->rgba[0], g = inst->rgba[1], b = inst->rgba[2];
            bool modulated = model_modulation[inst->model][3] != 0;
            if (modulated) {
                r *= model_modulation[inst->model][0] / 255.0f; g *= model_modulation[inst->model][1] / 255.0f;
                b *= model_modulation[inst->model][2] / 255.0f;
            }
            C3D_TexEnvColor(env, color4(clamp01(r), clamp01(g), clamp01(b), clamp01(inst->rgba[3])));
            C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
            C3D_TexEnvScale(env, C3D_RGB, modulated ? GPU_TEVSCALE_2 : GPU_TEVSCALE_1);
            if (inst->additive == 2) { /* flat: the colour itself over the model's coverage */
                C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_CONSTANT, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
                C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_1);
            }
            C3D_Tex *base = &textures[binding->textures[batch->texture]];
            C3D_TexBind(0, base);
            C3D_TexBind(1, batch->flags & (C3_TITLE_DUAL_MODULATE | C3_TITLE_REFLECTION_BLEND) ?
                &textures[binding->textures[batch->texture2]] : base);
            if (skin_batch_gpu(title, i)) skin_draw(inst->model, title, i, &inst_mvp);
            else {
                skin_restore(binding, &inst_mvp);
                C3D_BufInfo *buffers = C3D_GetBufInfo();
                BufInfo_Init(buffers);
                BufInfo_Add(buffers, vertices + binding->first_vertex, sizeof(*vertices), 5, 0x43210);
                C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, &inst_mvp);
                C3D_DrawArrays(GPU_TRIANGLES, batch->first, batch->count);
            }
            stats->draw_calls++; stats->vertices += batch->count;
        }
    }
    skin_restore(&scenes[0], mvp);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, mvp);
    if (effect_source_count && effect_vertices && effect_budget(1024u)) {
        for (unsigned i = 0; i < effect_source_count; ++i) {
            TitleGPUVertex *v = &effect_vertices[i];
            memset(v, 0, sizeof *v);
            memcpy(v->pos, effect_source[i].pos, sizeof v->pos);
            memcpy(v->rgba, effect_source[i].rgba, sizeof v->rgba);
        }
        GSPGPU_FlushDataCache(effect_vertices, effect_source_count * sizeof *effect_vertices);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mvp_location, mvp);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, color_pass_location, 1, 0, 0, 0);
        C3D_BufInfo *buffers = C3D_GetBufInfo();
        BufInfo_Init(buffers);
        BufInfo_Add(buffers, effect_vertices, sizeof *effect_vertices, 5, 0x43210);
        for (int stage = 0; stage < 6; ++stage) C3D_TexEnvInit(C3D_GetTexEnv(stage));
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
        C3D_DrawArrays(GPU_TRIANGLES, 0, effect_source_count);
        stats->draw_calls++; stats->vertices += effect_source_count;
    }
}
