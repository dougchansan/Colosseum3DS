/* Execute the actual optional capture resource/grid/composite implementation. */
#include "stage_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
typedef struct {float pos[3],uv[2],rgba[4],uv2[2],specular[3];} TitleGPUVertex;
typedef struct {float x,y,z,w;} C3D_FVec;
typedef struct {int unused;} C3D_Mtx;
typedef int C3D_Tex,C3D_RenderTarget,C3D_BufInfo,C3D_TexEnv;
static unsigned scene_count=1,scene_drawn,gpuCmdBufOffset,gpuCmdBufSize=C3_RENDER_COMMAND_CAPACITY_WORDS;
static size_t gpu_buffer_bytes;
static unsigned allocations,frees,draws,flushed,fail_allocation;
static int mvp_location,color_pass_location;
enum {GPU_RGBA8,GPU_LINEAR,GPU_CLAMP_TO_EDGE,GPU_TEXFACE_2D,GPU_RB_DEPTH24_STENCIL8,
GPU_VERTEX_SHADER,C3D_Both,GPU_TEXTURE0,GPU_REPLACE,GPU_CULL_NONE,GPU_ALWAYS,
GPU_WRITE_COLOR,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_TRIANGLES};
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return -1;}
static void *linearAlloc(size_t n){++allocations;return fail_allocation==allocations?NULL:malloc(n);}
static void linearFree(void *p){++frees;free(p);}
static int C3D_TexInitVRAM(C3D_Tex *t,unsigned w,unsigned h,int format){++allocations;return fail_allocation!=allocations;}
static C3D_RenderTarget *C3D_RenderTargetCreateFromTex(C3D_Tex *t,int face,int level,int depth){++allocations;return fail_allocation==allocations?NULL:malloc(sizeof(int));}
static void C3D_RenderTargetDelete(C3D_RenderTarget *t){++frees;free(t);}
static void C3D_TexDelete(C3D_Tex *t){++frees;}
#define C3D_TexSetFilter(...) ((void)0)
#define C3D_TexSetWrap(...) ((void)0)
#define FVec3_New(x,y,z) ((C3D_FVec){x,y,z,0})
#define FVec4_New(x,y,z,w) ((C3D_FVec){x,y,z,w})
static C3D_FVec Mtx_MultiplyFVec3(const C3D_Mtx *m,C3D_FVec v){return v;}
static C3D_FVec Mtx_MultiplyFVec4(const C3D_Mtx *m,C3D_FVec v){return v;}
static void GSPGPU_FlushDataCache(void *p,size_t n){assert(n==5505024);++flushed;}
#define C3D_FVUnifMtx4x4(...) ((void)0)
#define C3D_FVUnifSet(...) ((void)0)
#define C3D_GetBufInfo() NULL
#define BufInfo_Init(...) ((void)0)
#define BufInfo_Add(...) ((void)0)
#define C3D_GetTexEnv(...) NULL
#define C3D_TexEnvInit(...) ((void)0)
#define C3D_TexEnvSrc(...) ((void)0)
#define C3D_TexEnvFunc(...) ((void)0)
static void C3D_TexBind(unsigned unit,C3D_Tex *t){assert(t);}
static void C3D_CullFace(int face){assert(face==GPU_CULL_NONE);}
static void C3D_AlphaTest(int enable,int fn,int ref){assert(!enable);}
static void C3D_DepthTest(int enable,int fn,int write){assert(!enable&&write==GPU_WRITE_COLOR);}
static void C3D_AlphaBlend(int rgb,int alpha,int src,int dst,int asrc,int adst){assert(src==GPU_ONE&&dst==GPU_ZERO&&asrc==GPU_ONE&&adst==GPU_ZERO);}
static void C3D_DrawArrays(int mode,unsigned first,unsigned count){assert(first==draws*24576u&&count==24576u);++draws;}
#include "confusion_renderer.inc"
int main(void){
    char e[192];C3WzxConfusionProgram p={0};C3D_Mtx m={0};
    /* Original admitted phase0 profile, tiny view-facing quad. */
    C3ConfusionType12 state={0};state.active=1;state.radius=.25f;state.indirect=.25f;
    state.profile=(C3ConfusionType12Profile){0,44,90,0,145,62,.15f,0,.25f};
    for(unsigned failure=1;failure<=3;++failure){allocations=frees=0;fail_allocation=failure;
        assert(!c3_stage_renderer_confusion_program(&p,e,sizeof e));
        assert(!confusion_program&&!confusion_vertices&&!confusion_target&&!gpu_buffer_bytes);
        assert(frees==failure-1);
    }
    allocations=frees=0;fail_allocation=0;
    assert(c3_stage_renderer_confusion_program(&p,e,sizeof e));
    assert(gpu_buffer_bytes==6553600u&&allocations==3);
    assert(c3_stage_renderer_confusion_program(&p,e,sizeof e)&&allocations==3);
    assert(confusion_prepare(&m,&m,e,sizeof e)==0&&!flushed);
    assert(c3_stage_renderer_confusion(&state));
    assert(confusion_prepare(&m,&m,e,sizeof e)==1&&flushed==1);
    assert(fabsf(confusion_vertices[0].pos[0]+.25f)<1e-6f);
    /* Reverse top-origin capture V exactly once for the native RTT layout. */
    assert(fabsf(confusion_vertices[0].uv[0]-.375f)<1e-6f);
    assert(fabsf(confusion_vertices[0].uv[1]-.375f)<1e-6f);
    assert(c3_stage_renderer_confusion(NULL));
    assert(confusion_prepare(&m,&m,e,sizeof e)==0&&flushed==1);
    C3TitleRenderStats stats={0};
    assert(confusion_draw(&m,&stats,e,sizeof e));
    assert(draws==4&&stats.draw_calls==4&&stats.vertices==98304u);
    gpuCmdBufSize=8192;draws=0;
    assert(!confusion_draw(&m,&stats,e,sizeof e)&&!draws);
    confusion_shutdown();assert(frees==3&&!confusion_program&&!confusion_vertices);
    puts("Confusion resources: allocation rollback, inactive writes, opaque depthless stripes and split budget passed");
}
