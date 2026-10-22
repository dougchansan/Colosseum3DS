/* Host fixture for the production stage draw function. GPU work is stubbed;
 * texture binding enforces Citro3D's unit > 0 non-NULL contract. */
#include "stage_renderer.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* No-op GPU stubs deliberately leave production parameters/state unused. */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

typedef int C3D_Mtx, C3D_AttrInfo, C3D_BufInfo, C3D_TexEnv, GPU_TESTFUNC;
typedef struct { unsigned id; } C3D_Tex;
typedef int SpecularLight;
typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;
typedef struct {
    const C3Title *asset;
    uint32_t first_vertex, vertex_count, batch_count, texture_count;
    uint32_t textures[C3_TITLE_MAX_TEXTURES];
    uint32_t specular_lights;
} SceneBinding;
static SceneBinding scenes[C3_STAGE_MAX_MODELS];
static size_t scene_count = 1, gpu_buffer_bytes;
static TitleGPUVertex storage[3], *vertices = storage;
static uint32_t vertex_count = 3;
static C3D_Tex textures[C3_STAGE_MAX_TEXTURES];
static const C3TitleTexture *texture_sources[C3_STAGE_MAX_TEXTURES];
static uint32_t texture_count = 2;
static bool c3d_started = true, program_started = true, queued_busy, frame_open;
static void *target = (void *)1, *shader = (void *)2;
static int program, mvp_location, color_pass_location;
static unsigned draws, overlays, binds;
static unsigned gpu_writes, releases, frame_ends;
static C3D_Tex *bound[2], *expected_second;

enum {
    C3D_FRAME_NONBLOCK, C3D_CLEAR_ALL, C3D_RGB, C3D_Both,
    GPU_VERTEX_SHADER, GPU_FLOAT, GPU_ALWAYS, GPU_NEVER, GPU_GREATER,
    GPU_EQUAL, GPU_GEQUAL, GPU_LESS, GPU_NOTEQUAL, GPU_LEQUAL,
    GPU_WRITE_COLOR, GPU_WRITE_ALL, GPU_CULL_BACK_CCW, GPU_CULL_FRONT_CCW,
    GPU_CULL_NONE, GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD,
    GPU_ONE, GPU_ZERO, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
    GPU_TRIANGLES, GPU_PRIMARY_COLOR, GPU_PREVIOUS, GPU_REPLACE,
    SYSCLOCK_ARM11 = 268123480
};
#define NOOP(...) ((void)++gpu_writes)
#define C3D_GetDrawingTime(...) 0.0f
#define C3D_GetAttrInfo(...) NULL
#define C3D_GetBufInfo(...) NULL
#define C3D_GetTexEnv(...) NULL
#define GSPGPU_FlushDataCache NOOP
#define C3D_RenderTargetClear NOOP
#define C3D_FrameDrawOn NOOP
#define C3D_SetViewport NOOP
#define C3D_BindProgram NOOP
#define C3D_FVUnifMtx4x4 NOOP
#define C3D_FVUnifSet NOOP
#define AttrInfo_Init NOOP
#define AttrInfo_AddLoader NOOP
#define BufInfo_Init NOOP
#define BufInfo_Add NOOP
#define C3D_AlphaTest NOOP
#define C3D_CullFace NOOP
#define C3D_DepthTest NOOP
#define C3D_AlphaBlend NOOP
#define C3D_TexEnvSrc NOOP
#define C3D_TexEnvFunc NOOP
#define C3D_TexEnvInit NOOP
#define Mtx_Identity NOOP
#define C3D_ImmSendAttrib NOOP
#define C3D_ImmDrawEnd NOOP
#define gfxFlushBuffers NOOP
#define prepare_specular NOOP
#define raster_color NOOP
#define raster_specular NOOP
#define view_normal(view, source, output) memcpy(output, source, sizeof(float)*3)
#define combiner NOOP

static uint64_t simulated_ticks;
static unsigned begin_attempts;
static uint64_t svcGetSystemTick(void)
{ simulated_ticks += SYSCLOCK_ARM11 / 1000; return simulated_ticks; }
static bool C3D_FrameBegin(unsigned flags)
{
    assert(flags == C3D_FRAME_NONBLOCK);
    assert(c3d_started && !frame_open);
    ++begin_attempts;
    if (queued_busy) return false;
    frame_open = true;
    return true;
}
static void C3D_FrameEnd(unsigned flags)
{
    assert(flags == 0 && frame_open);
    frame_open = false;
    ++frame_ends;
}
static void release_resource(const void *resource)
{ assert(resource && !queued_busy && !frame_open); ++releases; }
#define C3D_RenderTargetDelete release_resource
#define C3D_TexDelete release_resource
#define linearFree release_resource
#define shaderProgramFree release_resource
#define DVLB_Free release_resource
#define C3D_Fini() release_resource(&c3d_started)

static int fail(char *error, size_t capacity, const char *message)
{ snprintf(error, capacity, "%s", message); return -1; }
static bool camera_matrices(const C3TitleCamera *camera, C3D_Mtx *mvp, C3D_Mtx *view)
{ (void)camera; *mvp = *view = 0; return true; }
static bool valid_batch(const C3Title *title, const C3TitleBatch *batch)
{ (void)title; (void)batch; return true; }
static float clamp01(float value) { return fmaxf(0, fminf(1, value)); }

static void C3D_TexBind(unsigned unit, C3D_Tex *texture)
{
    /* Mirrors the pinned SDK's dereference in C3D_TexGetType for unit > 0.
     * Fail explicitly so the old renderer produces a readable regression. */
    if (unit > 0 && !texture) {
        fprintf(stderr, "NULL texture on Citro3D unit %u\n", unit);
        exit(90);
    }
    assert(unit < 2);
    bound[unit] = texture;
    ++binds;
}
static void C3D_DrawArrays(int primitive, unsigned first, unsigned count)
{
    assert(primitive == GPU_TRIANGLES && first == 0 && count == 3);
    assert(bound[0] == &textures[4]);
    assert(bound[1] == expected_second);
    ++draws;
}
static void C3D_ImmDrawBegin(int primitive)
{
    assert(primitive == GPU_TRIANGLES);
    assert(bound[0] == &textures[4]);
    assert(bound[1] == expected_second);
    ++overlays;
}

/* Generated from the current production file by test_renderer_texture_binding.py. */
#include "title_renderer_draw.inc"

int main(int argc, char **argv)
{
    assert(argc == 3);
    unsigned flags = (unsigned)strtoul(argv[1], NULL, 10);
    float fade = strtof(argv[2], NULL);
    C3Title title = {0};
    C3TitleDrawVertex points[3] = {0};
    C3TitleBatch batch = {.count = 3, .texture = 0, .texture2 = 1, .flags = flags};
    title.draw_vertices = points;
    title.batches = &batch;
    title.vertex_count = 3;
    title.batch_count = 1;
    title.texture_count = 2;
    title.pose.materials[0].visible = 1;
    title.pose.camera.aspect = 4.0f / 3.0f;
    scenes[0] = (SceneBinding){.asset = &title, .vertex_count = 3,
        .batch_count = 1, .texture_count = 2, .textures = {4, 7}};
    expected_second = &textures[flags & (C3_TITLE_DUAL_MODULATE | C3_TITLE_REFLECTION_BLEND) ? 7 : 4];
    C3TitleRenderStats stats;
    char error[128] = {0};
    const C3Title *models[] = {&title};
    if (getenv("C3_TEST_GPU_BUSY")) {
        queued_busy = true;
        memset(storage, 0xA5, sizeof(storage));
        TitleGPUVertex original[3];
        memcpy(original, storage, sizeof(original));
        assert(c3_stage_renderer_draw(models, 1, &title.pose.camera, fade,
                                      &stats, error, sizeof(error)) == C3_RENDER_BUSY);
        assert(begin_attempts == 1 && frame_ends == 0 && gpu_writes == 0);
        assert(memcmp(original, storage, sizeof(original)) == 0);
        assert(draws == 0 && binds == 0 && overlays == 0);
        assert(!c3_stage_renderer_is_idle());
        assert(!c3_stage_renderer_try_shutdown());
        assert(releases == 0 && scene_count == 1 && vertices == storage && target);
        assert(texture_count == 2 && c3d_started && program_started && shader);
        assert(gpu_writes == 0 && frame_ends == 0);
        queued_busy = false;
        assert(c3_stage_renderer_is_idle());
        assert(frame_ends == 1 && gpu_writes == 0 && releases == 0);
        assert(c3_stage_renderer_try_shutdown());
        assert(frame_ends == 2 && releases == 7);
        assert(!vertices && !target && !shader && !c3d_started && !program_started);
        assert(!scene_count && !texture_count);
        unsigned old_attempts = begin_attempts;
        assert(c3_stage_renderer_is_idle());
        assert(c3_stage_renderer_try_shutdown());
        assert(begin_attempts == old_attempts && releases == 7);
        puts("BUSY returned; ownership retained; idle teardown verified");
        return 0;
    }
    for (unsigned frame = 0; frame < 3; ++frame) {
        assert(c3_stage_renderer_draw(models, 1, &title.pose.camera, fade,
                                      &stats, error, sizeof(error)) == C3_RENDER_OK);
    }
    assert(draws == 3 && binds == 6 && frame_ends == 3);
    assert(overlays == 3 * (unsigned)(fade < 1.0f));
    assert(stats.draw_calls == 1 + (unsigned)(fade < 1.0f));
    assert(stats.vertices == 3 + 3 * (unsigned)(fade < 1.0f));
    assert(!error[0]);
    return 0;
}
