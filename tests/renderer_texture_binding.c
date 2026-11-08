/* Host fixture for the production stage draw function. GPU work is stubbed;
 * texture binding enforces Citro3D's unit > 0 non-NULL contract. */
#include "stage_renderer.h"
#include "stage_cache.h"
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

typedef struct { float data[16]; } C3D_Mtx;
typedef int C3D_AttrInfo, C3D_BufInfo, C3D_TexEnv, GPU_TESTFUNC;
typedef struct { unsigned id; } C3D_Tex;
typedef void C3D_RenderTarget;
typedef int SpecularLight;
typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;
typedef struct {
    const C3Title *asset;
    uint32_t first_vertex, vertex_count, batch_count, texture_count;
    uint32_t textures[C3_TITLE_MAX_TEXTURES];
    uint32_t specular_lights;
} SceneBinding;
static const C3ParticlePack *particle_pack;
static const C3ParticlePack *particle_bindings[C3_STAGE_MAX_PARTICLE_BANKS];
static uint32_t particle_binding_textures[C3_STAGE_MAX_PARTICLE_BANKS][32];
static unsigned particle_binding_count;
static const C3ParticleSystem *particle_system;
static C3ParticleSystem *particle_srt_system;
static C3ParticleSystem *particle_order_system;
static uint64_t particle_render_frame;
static TitleGPUVertex *particle_vertices;
static bool scene_drawn;
static SceneBinding scenes[C3_STAGE_MAX_MODELS];
static C3StageVertexCache vertex_cache[C3_STAGE_MAX_MODELS];
static uint8_t model_modulation[C3_STAGE_MAX_MODELS][4];
static size_t scene_count = 1, gpu_buffer_bytes;
static TitleGPUVertex storage[6], *vertices = storage, *hud_vertices;
static uint32_t vertex_count = 3;
static C3D_Tex textures[C3_STAGE_MAX_TEXTURES];
static const C3TitleTexture *texture_sources[C3_STAGE_MAX_TEXTURES];
static uint32_t texture_count = 2;
static bool c3d_started = true, program_started = true, queued_busy, frame_open;
static bool completion_hooks;
static void *target = (void *)1, *shader = (void *)2;
static int program, mvp_location, color_pass_location;
static unsigned draws, overlays, binds;
static unsigned gpu_writes, releases, frame_ends, prepared;
static float view_key;
static C3D_Tex *bound[2], *expected_second;
static unsigned alpha_reference, expected_alpha_reference;
static bool alpha_enabled;
typedef struct { bool configured, enabled; size_t model, screen_model; uint32_t texture; } TVBinding;
static TVBinding tv;
static C3D_Tex tv_texture;
static C3D_RenderTarget *tv_target, *active_target;
static bool test_tv,test_confusion;
static bool test_capture_classes;
static unsigned class_draws;
static unsigned capture_draws,composites,particle_calls;
static C3D_RenderTarget *confusion_target;
static unsigned offscreen_draws, main_draws;

enum {
    C3D_FRAME_NONBLOCK, C3D_CLEAR_ALL, C3D_RGB, C3D_Both,
    GPU_VERTEX_SHADER, GPU_FLOAT, GPU_ALWAYS, GPU_NEVER, GPU_GREATER,
    GPU_EQUAL, GPU_GEQUAL, GPU_LESS, GPU_NOTEQUAL, GPU_LEQUAL,
    GPU_WRITE_COLOR, GPU_WRITE_ALL, GPU_CULL_BACK_CCW, GPU_CULL_FRONT_CCW,
    GPU_CULL_NONE, GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD,
    GPU_ONE, GPU_ZERO, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
    GPU_TRIANGLES, GPU_PRIMARY_COLOR, GPU_PREVIOUS, GPU_REPLACE,
    GSPGPU_EVENT_PSC0, GSPGPU_EVENT_P3D, GSPGPU_EVENT_PPF,
    SYSCLOCK_ARM11 = 268123480
};
#define NOOP(...) ((void)++gpu_writes)
#define C3D_GetDrawingTime(...) 0.0f
#define C3D_GetAttrInfo(...) NULL
#define C3D_GetBufInfo(...) NULL
#define C3D_GetTexEnv(...) NULL
#define GSPGPU_FlushDataCache NOOP
#define C3D_RenderTargetClear NOOP
#define C3D_SetViewport NOOP
#define C3D_BindProgram NOOP
#define C3D_FVUnifSet NOOP
#define AttrInfo_Init NOOP
#define AttrInfo_AddLoader NOOP
#define BufInfo_Init NOOP
#define BufInfo_Add NOOP
#define C3D_CullFace NOOP
#define C3D_DepthTest NOOP
#define C3D_AlphaBlend NOOP
#define C3D_TexEnvSrc NOOP
#define C3D_TexEnvFunc NOOP
#define C3D_TexEnvInit NOOP
#define C3D_TexEnvColor NOOP
#define color4(r,g,b,a) ((void)(r),(void)(g),(void)(b),(void)(a),0u)
#define C3D_ImmSendAttrib NOOP
#define C3D_ImmDrawEnd NOOP
#define gfxFlushBuffers NOOP
#define prepare_specular NOOP
#define raster_color(...) ((void)++prepared)
#define raster_specular NOOP
#define view_normal(view, source, output) memcpy(output, source, sizeof(float)*3)
#define combiner NOOP
#define apply_model_modulation NOOP
#define record_submission(stats) ((void)(stats))
#define gspSetEventCallback NOOP

static uint64_t simulated_ticks;
static unsigned begin_attempts;
static uint64_t svcGetSystemTick(void)
{ simulated_ticks += SYSCLOCK_ARM11 / 1000; return simulated_ticks; }
static void Mtx_Identity(C3D_Mtx *matrix) { memset(matrix,0,sizeof *matrix);matrix->data[0]=matrix->data[5]=matrix->data[10]=matrix->data[15]=1; }
static void C3D_FVUnifMtx4x4(int shader_type, int location, const C3D_Mtx *matrix)
{ (void)matrix; ++gpu_writes; }
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
static bool camera_matrices(const C3TitleCamera *camera, C3D_Mtx *mvp, C3D_Mtx *view, bool tilted)
{ (void)camera; (void)tilted; memset(mvp,0,sizeof *mvp);memset(view,0,sizeof *view);view->data[0]=view_key; return true; }
static float clamp01(float value) { return fmaxf(0, fminf(1, value)); }
static void C3D_FrameDrawOn(C3D_RenderTarget *draw_target)
{ active_target=draw_target; ++gpu_writes; }

static void C3D_AlphaTest(bool enabled, int comparison, unsigned reference)
{ assert(comparison == GPU_ALWAYS || comparison == GPU_GREATER); alpha_enabled = enabled; alpha_reference = reference; }

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
    if(test_capture_classes){
        const unsigned expected[]={33,22,11,11,22,33};
        assert(class_draws<6&&alpha_reference==expected[class_draws]);
        assert(active_target==(class_draws<3?confusion_target:target));
        assert(alpha_enabled==(alpha_reference==33));
        ++class_draws;
    } else if(test_confusion){
        assert(bound[0]==&textures[4]||bound[0]==&textures[5]);
        if(active_target==confusion_target){assert(main_draws==0);++capture_draws;}
        else {assert(active_target==target&&capture_draws==2);++main_draws;}
    } else if(test_tv) {
        if(active_target==tv_target) {
            assert(tv.enabled && bound[0]==&textures[5] && bound[1]==&textures[5]);
            ++offscreen_draws;
        } else {
            assert(active_target==target && bound[0]==(tv.enabled?&tv_texture:&textures[4]));
            assert(bound[1]==bound[0]); ++main_draws;
        }
    } else {
        assert(bound[0] == &textures[4]);
        assert(bound[1] == expected_second);
    }
    if(!test_capture_classes){
        assert(alpha_enabled == (expected_alpha_reference != 0));
        assert(alpha_reference == expected_alpha_reference);
    }
    ++draws;
}
static void C3D_ImmDrawBegin(int primitive)
{
    assert(primitive == GPU_TRIANGLES);
    assert(bound[0] == &textures[4]);
    assert(bound[1] == expected_second);
    ++overlays;
}

/* Optional capture state is exercised separately; these stubs assert the
 * unbound API never submits work in the existing material regression suite. */
#define CONFUSION_WIDTH 512u
#define CONFUSION_HEIGHT 256u
static int confusion_prepare(const C3D_Mtx *clip,const C3D_Mtx *view,char *e,size_t n){assert(frame_open);return test_confusion||test_capture_classes;}
static int confusion_draw(const C3D_Mtx *mvp,C3TitleRenderStats *stats,char *e,size_t n){assert(frame_open);if(test_capture_classes)assert(class_draws==6);else assert(test_confusion&&capture_draws==2&&main_draws==2&&particle_calls==0);++composites;return 1;}
static void confusion_shutdown(void){}
static void particle_line_shutdown(void){}
static void particle_line_fini(void){}

/* Generated from the current production file by test_renderer_texture_binding.py. */
/* This fixture verifies the authored material/TV path with the optional battle
 * overlay disabled. The HUD has a separate real ARM integration gate. */
static void draw_battle_hud(C3TitleRenderStats *stats) { (void)stats; }
/* Particle submission is independently exercised by its own fixture; this
 * fixture continues executing the production authored-geometry draw path. */
static int draw_particles(const C3TitleCamera *camera,const C3D_Mtx *mvp,C3TitleRenderStats *stats,char *error,size_t capacity)
{(void)camera;(void)mvp;(void)stats;if(test_confusion){assert(composites==1&&active_target==target);++particle_calls;}if(getenv("C3_TEST_PARTICLE_OVERLOAD")){snprintf(error,capacity,"Particle command budget exceeded");return 0;}return 1;}
#include "title_renderer_draw.inc"

int main(int argc, char **argv)
{
    assert(argc == 3);
    unsigned flags = (unsigned)strtoul(argv[1], NULL, 10);
    float fade = strtof(argv[2], NULL);
    C3Title title = {0};
    C3TitleDrawVertex points[3] = {0};
    if (flags & C3_TITLE_DUAL_MODULATE) flags |= C3_TITLE_TEV_LERP;
    if (flags & C3_TITLE_REFLECTION_BLEND) flags |= C3_TITLE_LIT;
    C3TitleBatch batch = {.count = 3, .texture = 0, .texture2 = 1, .flags = flags,
        .color_mode = 4, .alpha_mode = 0, .alpha_compare0 = 7, .alpha_compare1 = 7};
    if(getenv("C3_TEST_MAIN_UNKNOWN_CLASS"))batch.render_mode=0x20000000u;
    const char *alpha = getenv("C3_TEST_ALPHA_REF");
    if (alpha) {
        expected_alpha_reference = (unsigned)strtoul(alpha, NULL, 10);
        batch.alpha_ref0 = batch.alpha_ref1 = expected_alpha_reference;
        batch.alpha_compare0 = batch.alpha_compare1 = 4;
    }
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
    if(getenv("C3_TEST_CAPTURE_CLASSES")){
        test_capture_classes=true;confusion_target=(void *)9;
        C3TitleBatch categories[3]={batch,batch,batch};
        /* Deliberately contradict source class with native custom-PE state. */
        categories[0].render_mode=0x60000000u;categories[0].flags=0;
        categories[1].render_mode=0x40000000u;categories[1].flags=C3_TITLE_TRANSLUCENT;
        categories[2].render_mode=0;categories[2].flags=C3_TITLE_TRANSLUCENT|C3_TITLE_ADDITIVE;
        for(unsigned i=0;i<3;++i){categories[i].alpha_ref0=(i+1)*11;title.pose.materials[i].visible=1;}
        categories[2].alpha_compare0=categories[2].alpha_compare1=4;
        categories[2].alpha_ref1=categories[2].alpha_ref0;
        title.batches=categories;title.batch_count=3;scenes[0].batch_count=3;
        if(getenv("C3_TEST_CAPTURE_CLASS_INVALID")){
            categories[0].render_mode=0x20000000u;
            assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_ERROR);
            assert(!frame_open&&frame_ends==1&&!class_draws&&!prepared&&!composites);
            assert(strstr(error,"transparency class"));
            puts("Unknown source capture class fails before geometry submission");return 0;
        }
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(class_draws==6&&composites==1&&stats.draw_calls==6);
        puts("Capture source classes follow OPA TEXEDGE XLU independently of PE; main order unchanged");return 0;
    }
    if(getenv("C3_TEST_CONFUSION")){
        test_confusion=true;confusion_target=(void *)9;
        C3Title actor=title;const C3Title *pair[]={&title,&actor};
        scene_count=2;vertex_count=6;scenes[1]=scenes[0];
        scenes[1].asset=&actor;scenes[1].first_vertex=3;scenes[1].textures[0]=5;
        queued_busy=true;
        assert(c3_stage_renderer_draw(pair,2,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_BUSY);
        assert(!gpu_writes&&!prepared&&!capture_draws&&!composites&&!particle_calls);
        queued_busy=false;
        assert(c3_stage_renderer_draw(pair,2,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(capture_draws==2&&main_draws==2&&composites==1&&particle_calls==1);
        assert(stats.draw_calls==4&&stats.vertices_prepared==6&&frame_ends==1);
        puts("Confusion captures every model before main, composite before particles, BUSY preserves storage");return 0;
    }
    if(getenv("C3_TEST_PARTICLE_OVERLOAD")){
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_ERROR);
        assert(!frame_open&&frame_ends==1&&strstr(error,"command budget"));
        assert(stats.command_capacity_words==C3_RENDER_COMMAND_CAPACITY_WORDS);
        puts("Particle overload closed acquired frame safely");return 0;
    }
    if(getenv("C3_TEST_STATIC")) {
        vertex_cache[0].enabled=true;
        for(unsigned frame=0;frame<3;++frame)
            assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==3 && draws==3); /* one staging pass, three real draws */
        assert(stats.cached_models==1&&stats.vertices_prepared==0);
        view_key=1;
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==3); /* diffuse cache is view independent */
        vertex_cache[0].view_dependent=true;view_key=2;
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==6&&stats.cached_models==0&&stats.vertices_prepared==3);
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==6);
        vertex_cache[0].valid=false;
        assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==9);
        vertex_cache[0].enabled=false;
        for(unsigned frame=0;frame<2;++frame)
            assert(c3_stage_renderer_draw(models,1,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(prepared==15 && draws==9);
        assert(c3_stage_renderer_try_shutdown());
        assert(!vertex_cache[0].valid && !vertex_cache[0].enabled);
        puts("Static staging reuse, camera invalidation, opt-out and teardown passed");
        return 0;
    }
    if(getenv("C3_TEST_TV")) {
        test_tv=true;
        C3Title television=title;
        const C3Title *pair[]={&title,&television};
        scene_count=2;vertex_count=6;
        scenes[1]=scenes[0];scenes[1].asset=&television;scenes[1].first_vertex=3;
        scenes[1].textures[0]=5;
        tv=(TVBinding){true,true,1,0,0};tv_target=(void *)3;
        for(unsigned frame=0;frame<3;++frame)
            assert(c3_stage_renderer_draw(pair,2,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(offscreen_draws==3 && main_draws==3 && draws==6 && stats.draw_calls==2);
        assert(storage[0].uv[1]==1 && storage[3].uv[1]==0);
        tv.enabled=false;
        assert(c3_stage_renderer_draw(pair,2,&title.pose.camera,1,&stats,error,sizeof error)==C3_RENDER_OK);
        assert(offscreen_draws==3 && main_draws==4 && draws==7 && stats.draw_calls==1);
        assert(storage[0].uv[1]==0);
        assert(c3_stage_renderer_try_shutdown());
        assert(!tv.configured && !tv_target);
        puts("TV renders before room without feedback; disabled TV stays hidden");
        return 0;
    }
    if (getenv("C3_TEST_GPU_BUSY")) {
        queued_busy = true;
        particle_vertices=storage+3;
        particle_pack=(const C3ParticlePack *)(uintptr_t)1;
        particle_system=(const C3ParticleSystem *)(uintptr_t)2;
        memset(storage, 0xA5, sizeof(storage));
        TitleGPUVertex original[6];
        memcpy(original, storage, sizeof(original));
        assert(c3_stage_renderer_draw(models, 1, &title.pose.camera, fade,
                                      &stats, error, sizeof(error)) == C3_RENDER_BUSY);
        assert(begin_attempts == 1 && frame_ends == 0 && gpu_writes == 0);
        assert(memcmp(original, storage, sizeof(original)) == 0);
        assert(draws == 0 && binds == 0 && overlays == 0);
        assert(!c3_stage_renderer_is_idle());
        assert(!c3_stage_renderer_try_shutdown());
        assert(releases == 0 && scene_count == 1 && vertices == storage && target);
        assert(particle_vertices==storage+3&&particle_pack&&particle_system);
        assert(texture_count == 2 && c3d_started && program_started && shader);
        assert(gpu_writes == 0 && frame_ends == 0);
        queued_busy = false;
        assert(c3_stage_renderer_is_idle());
        assert(frame_ends == 1 && gpu_writes == 0 && releases == 0);
        assert(c3_stage_renderer_try_shutdown());
        assert(frame_ends == 2 && releases == 5);
        assert(!vertices && !target && shader && c3d_started && program_started);
        assert(!scene_count && !texture_count&&!particle_vertices&&!particle_pack&&!particle_system);
        /* Scene teardown must retain the currently bound program/context.
         * Only application finalization may invalidate that SDK pointer. */
        c3_stage_renderer_fini();
        assert(releases == 8 && !shader && !c3d_started && !program_started);
        unsigned old_attempts = begin_attempts;
        assert(c3_stage_renderer_is_idle());
        assert(c3_stage_renderer_try_shutdown());
        assert(begin_attempts == old_attempts && releases == 8);
        puts("BUSY returned; ownership retained; idle teardown verified");
        return 0;
    }
    assert(valid_batch(&title, &batch));
    C3TitleBatch invalid = batch;
    invalid.alpha_compare0 = invalid.alpha_compare1 = 4;
    invalid.alpha_ref0 = 127; invalid.alpha_ref1 = 0;
    assert(!valid_batch(&title, &invalid));
    invalid.alpha_ref0 = invalid.alpha_ref1 = 256;
    assert(!valid_batch(&title, &invalid));
    for (unsigned frame = 0; frame < 5; ++frame) {
        unsigned old_draws = draws, old_overlays = overlays;
        assert(c3_stage_renderer_draw(models, 1, &title.pose.camera, fade,
                                      &stats, error, sizeof(error)) == C3_RENDER_OK);
        assert(draws - old_draws == 1);
        assert(overlays - old_overlays == (unsigned)(fade < 1.0f));
    }
    assert(draws == 5 && binds == 10 && frame_ends == 5);
    assert(overlays == 5 * (unsigned)(fade < 1.0f));
    assert(stats.draw_calls == 1 + (unsigned)(fade < 1.0f));
    assert(stats.vertices == 3 + 3 * (unsigned)(fade < 1.0f));
    assert(!error[0]);
    return 0;
}
