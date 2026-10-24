/* Execute the production scene initialization/teardown with a Citro3D
 * shader-pointer contract. The real SDK keeps that pointer until final Fini. */
#include "stage_renderer.h"
#include "stage_cache.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;
typedef struct { const C3Title *asset; uint32_t first_vertex, vertex_count, batch_count,
    texture_count, textures[C3_TITLE_MAX_TEXTURES], specular_lights; } SceneBinding;
typedef struct { size_t size; void *data; } C3D_Tex;
typedef struct { int unused; } C3D_RenderTarget;
typedef struct { int numDVLE, DVLE[1]; } DVLB_s;
typedef struct { int *vertexShader; } shaderProgram_s;
typedef unsigned u32;
static const C3ParticlePack *particle_pack;
static const C3ParticlePack *particle_bindings[2];
static uint32_t particle_binding_textures[2][32];
static unsigned particle_binding_count;
static const C3ParticleSystem *particle_system;
static TitleGPUVertex *particle_vertices;
static bool scene_drawn;
static SceneBinding scenes[C3_STAGE_MAX_MODELS];
static C3StageVertexCache vertex_cache[C3_STAGE_MAX_MODELS];
static uint8_t model_modulation[C3_STAGE_MAX_MODELS][4];
static size_t scene_count;
static bool c3d_started, program_started, driver_live;
static bool completion_hooks;
static C3D_RenderTarget *target;
static DVLB_s *shader;
static shaderProgram_s program;
static shaderProgram_s *sdk_program;
static int mvp_location, color_pass_location;
static TitleGPUVertex *vertices;
static TitleGPUVertex *hud_vertices;
static C3D_Tex textures[C3_STAGE_MAX_TEXTURES];
static const C3TitleTexture *texture_sources[C3_STAGE_MAX_TEXTURES];
static uint32_t texture_count, vertex_count;
static size_t gpu_buffer_bytes;
static C3StageBattleHud battle_hud;
static uint32_t hud_texture;
typedef struct { bool configured, enabled; size_t model, screen_model; uint32_t texture; } TVBinding;
static TVBinding tv;
static C3D_Tex tv_texture;
static C3D_RenderTarget *tv_target;
static unsigned init_calls, fini_calls, shader_inits, shader_frees, targets, buffers, images;
static unsigned fail_at, call_index;
static bool should_fail(void) { return ++call_index == fail_at; }
static const unsigned char title_shader_shbin[4] = {0};
static const unsigned title_shader_shbin_size = sizeof(title_shader_shbin);
enum { C3D_DEFAULT_CMDBUF_SIZE, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8,
    GFX_TOP, GFX_LEFT, GX_TRANSFER_FMT_RGBA8, GX_TRANSFER_FMT_RGB8,
    GX_TRANSFER_SCALE_NO, C3D_FRAME_NONBLOCK, GSPGPU_EVENT_PSC0,
    GSPGPU_EVENT_P3D, GSPGPU_EVENT_PPF, GPU_RGBA8, GPU_LINEAR, GPU_CLAMP_TO_EDGE, GPU_TEXFACE_2D };
#define GX_TRANSFER_FLIP_VERT(x) 0
#define GX_TRANSFER_OUT_TILED(x) 0
#define GX_TRANSFER_RAW_COPY(x) 0
#define GX_TRANSFER_IN_FORMAT(x) 0
#define GX_TRANSFER_OUT_FORMAT(x) 0
#define GX_TRANSFER_SCALING(x) 0
#define R_FAILED(x) ((x) < 0)
static int fail(char *error, size_t capacity, const char *message)
{ snprintf(error, capacity, "%s", message); return -1; }
static int validate_asset(const C3Title *title, char *error, size_t capacity)
{ (void)title; (void)error; (void)capacity; return 0; }
static uint32_t specular_light_mask(const C3Title *title) { (void)title; return 0; }
static bool C3D_Init(size_t size)
{ assert(size==C3_RENDER_COMMAND_BUFFER_BYTES); assert(!driver_live); ++init_calls; if (should_fail()) return false; driver_live = true; return true; }
static void C3D_Fini(void)
{
    assert(driver_live);
    if (sdk_program) assert(sdk_program->vertexShader);
    driver_live = false; ++fini_calls;
    /* Deliberately retain sdk_program like Citro3D1.7.1. */
}
static DVLB_s *DVLB_ParseFile(u32 *data, unsigned size)
{ (void)data; (void)size; if (should_fail()) return NULL; DVLB_s *d = calloc(1, sizeof(*d)); assert(d); d->numDVLE = 1; return d; }
static void DVLB_Free(DVLB_s *d) { assert(!driver_live); free(d); }
static int shaderProgramInit(shaderProgram_s *p)
{ if (should_fail()) return -1; memset(p, 0, sizeof(*p)); ++shader_inits; return 0; }
static int shaderProgramSetVsh(shaderProgram_s *p, int *dvle)
{ (void)dvle; if (should_fail()) return -1; p->vertexShader = malloc(sizeof(int)); assert(p->vertexShader); return 0; }
static void shaderProgramFree(shaderProgram_s *p)
{ assert(!driver_live); free(p->vertexShader); p->vertexShader = NULL; ++shader_frees; }
static int shaderInstanceGetUniformLocation(int *instance, const char *name)
{ (void)name; assert(instance); return should_fail() ? -1 : 0; }
static C3D_RenderTarget *C3D_RenderTargetCreate(int w, int h, int c, int d)
{ (void)w;(void)h;(void)c;(void)d; if (should_fail()) return NULL; ++targets; return calloc(1, sizeof(C3D_RenderTarget)); }
static void C3D_RenderTargetSetOutput(C3D_RenderTarget *t, int s, int side, int flags)
{ (void)s;(void)side;(void)flags; assert(t); }
static void C3D_RenderTargetDelete(C3D_RenderTarget *t) { assert(targets); --targets; free(t); }
static void *linearAlloc(size_t size) { if (should_fail()) return NULL; ++buffers; return malloc(size); }
static void linearFree(void *p) { assert(buffers); --buffers; free(p); }
static bool upload_texture(C3D_Tex *gpu, const C3TitleTexture *source)
{ if (should_fail()) return false; gpu->size = source->size; gpu->data = malloc(gpu->size); assert(gpu->data); ++images; return true; }
static void C3D_TexDelete(C3D_Tex *t) { assert(images); --images; free(t->data); t->data = NULL; }
static bool C3D_TexInitVRAM(C3D_Tex *t, unsigned w, unsigned h, unsigned format)
{ assert(w==256 && h==256 && format==GPU_RGBA8); if(should_fail())return false;
  t->size=w*h*4; t->data=malloc(t->size); assert(t->data); ++images; return true; }
#define C3D_TexSetFilter(...) ((void)0)
#define C3D_TexSetWrap(...) ((void)0)
static C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t,unsigned face,unsigned level,unsigned depth)
{ assert(t->data && face==GPU_TEXFACE_2D && level==0 && depth==GPU_RB_DEPTH24_STENCIL8);
  if(should_fail())return NULL;
  ++targets; return calloc(1,sizeof(C3D_RenderTarget)); }
static bool C3D_FrameBegin(int flags) { assert(flags == C3D_FRAME_NONBLOCK && driver_live); return true; }
static void C3D_FrameEnd(unsigned flags) { assert(flags == 0); }
static void install_completion_counters(void) { completion_hooks = true; }
#define gspSetEventCallback(...) ((void)0)

#include "renderer_lifecycle.inc"

int main(int argc, char **argv)
{
    fail_at = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
    C3Title title = {0}; unsigned char pixels[256] = {0};
    C3TitleTexture texture = {.width=8,.height=8,.pixels=pixels,.size=sizeof(pixels)};
    title.vertex_count = 3; title.batch_count = 1; title.texture_count = 1; title.textures = &texture;
    const C3Title *models[] = {&title}; char error[192] = {0};
    if (argc > 1 && !strcmp(argv[1], "tv")) {
        C3TitleBatch batch={.count=3};title.batches=&batch;
        const C3Title *pair[]={&title,&title};
        assert(c3_stage_renderer_init(pair,2,error,sizeof error)==0);
        size_t prior_bytes=gpu_buffer_bytes;unsigned prior_images=images;
        assert(!c3_stage_renderer_set_tv(0,0,0,true,error,sizeof error));
        assert(!c3_stage_renderer_set_tv(0,1,1,true,error,sizeof error));
        fail_at=call_index+1;
        assert(!c3_stage_renderer_set_tv(0,1,0,true,error,sizeof error));
        assert(!tv.configured && !tv_target && gpu_buffer_bytes==prior_bytes && images==prior_images);
        fail_at=call_index+2;
        assert(!c3_stage_renderer_set_tv(0,1,0,true,error,sizeof error));
        assert(!tv.configured && !tv_target && !tv_texture.data && images==prior_images);
        fail_at=0;
        assert(c3_stage_renderer_set_tv(0,1,0,true,error,sizeof error));
        assert(tv.configured && tv.enabled && targets==2 && images==prior_images+1);
        assert(gpu_buffer_bytes==prior_bytes+256u*256u*8u);
        assert(!c3_stage_renderer_set_tv(1,0,0,true,error,sizeof error));
        assert(tv.model==0 && tv.screen_model==1);
        assert(c3_stage_renderer_set_tv(0,1,0,false,error,sizeof error));
        assert(!tv.enabled && tv.configured && targets==2);
        assert(c3_stage_renderer_try_shutdown());
        assert(!tv.configured && !tv_target && !targets && !images && !buffers);
        c3_stage_renderer_fini();
        puts("TV allocation failures, stable identities and drained ownership verified");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "capacity")) {
        const C3Title *many[C3_STAGE_MAX_MODELS + 1];
        for (unsigned i = 0; i < C3_STAGE_MAX_MODELS + 1; ++i) many[i] = &title;
        assert(c3_stage_renderer_init(many, C3_STAGE_MAX_MODELS + 1, error, sizeof(error)) == -1);
        assert(!init_calls && !targets && !buffers && !images);
        assert(c3_stage_renderer_init(many, C3_STAGE_MAX_MODELS, error, sizeof(error)) == 0);
        assert(scene_count == C3_STAGE_MAX_MODELS && vertex_count == C3_STAGE_MAX_MODELS * 3);
        assert(texture_count == 1 && images == 1);
        assert(gpu_buffer_bytes == vertex_count * sizeof(TitleGPUVertex) + sizeof(pixels));
        for (unsigned i = 0; i < C3_STAGE_MAX_MODELS; ++i) {
            assert(scenes[i].asset == &title && scenes[i].first_vertex == i * 3);
            assert(scenes[i].textures[0] == 0);
        }
        assert(c3_stage_renderer_try_shutdown());
        c3_stage_renderer_fini();
        assert(!targets && !buffers && !images && !scene_count);
        puts("Maximum model count retains disjoint vertices and shared texture ownership");
        return 0;
    }
    if (fail_at) {
        assert(c3_stage_renderer_init(models, 1, error, sizeof(error)) == -1);
        assert(error[0] && !target && !vertices && !scene_count && !texture_count);
        c3_stage_renderer_fini();
        assert(!c3d_started && !program_started && !shader && !driver_live);
        assert(!targets && !buffers && !images);
        assert(fini_calls == (fail_at > 1));
        assert(shader_frees == shader_inits);
        return 0;
    }
    for (unsigned transition = 0; transition < 4; ++transition) {
        assert(c3_stage_renderer_init(models, 1, error, sizeof(error)) == 0);
        assert(driver_live && program.vertexShader && shader);
        /* Model the SDK's persistent pointer identity during production binds. */
        if (sdk_program) assert(sdk_program == &program && sdk_program->vertexShader);
        sdk_program = &program;
        assert(c3_stage_renderer_init(models, 1, error, sizeof(error)) == -1);
        /* Model the optional HUD arena in alternate scene bundles. Production
         * shutdown must release it after the same idle barrier as geometry. */
        if (transition & 1u) {
            hud_vertices = linearAlloc(2358u * sizeof *hud_vertices);
            assert(hud_vertices && buffers == 2);
        }
        /* Optional particle ownership follows the same sole-scene drain. */
        particle_vertices=linearAlloc(6u*sizeof *particle_vertices);
        assert(particle_vertices&&buffers==((transition&1u)?3u:2u));
        particle_pack=(const C3ParticlePack *)(uintptr_t)1;
        particle_system=(const C3ParticleSystem *)(uintptr_t)2;
        assert(c3_stage_renderer_try_shutdown());
        assert(!hud_vertices&&!particle_vertices&&!particle_pack&&!particle_system);
        assert(!targets && !buffers && !images && !scene_count);
        assert(driver_live && program.vertexShader && shader);
        assert(init_calls == 1 && shader_inits == 1 && !fini_calls && !shader_frees);
    }
    c3_stage_renderer_fini();
    assert(fini_calls == 1 && shader_frees == 1 && !c3d_started && !program_started && !shader);
    c3_stage_renderer_fini();
    assert(fini_calls == 1 && shader_frees == 1);
    puts("Four scene replacements retain a valid shader; final teardown runs once");
    return 0;
}
