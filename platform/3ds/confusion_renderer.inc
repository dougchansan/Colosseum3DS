/* Source type12 uses the shared frame owner and immutable per-frame ranges.
 * 640x480 NTSC screen fractions adapt to a full 512x256 texture; camera FOV
 * and aspect remain authored, so backing pixel aspect does not alter the view. */
#include "colosseum3ds/confusion_indirect.h"
#define CONFUSION_WIDTH 512u
#define CONFUSION_HEIGHT 256u
#define CONFUSION_VERTICES (128u*128u*6u)
#define CONFUSION_VERTEX_BYTES (CONFUSION_VERTICES*sizeof(TitleGPUVertex))
static const C3WzxConfusionProgram *confusion_program;
static const C3ConfusionType12 *confusion_state;
static C3D_Tex confusion_texture;
static C3D_RenderTarget *confusion_target;
static TitleGPUVertex *confusion_vertices;
static C3ConfusionGridVertex confusion_row[128u*6u];
static void confusion_shutdown(void)
{
    if(confusion_target){C3D_RenderTargetDelete(confusion_target);confusion_target=NULL;}
    if(confusion_program)C3D_TexDelete(&confusion_texture);
    if(confusion_vertices){linearFree(confusion_vertices);confusion_vertices=NULL;}
    memset(&confusion_texture,0,sizeof confusion_texture);
    confusion_program=NULL;confusion_state=NULL;
}
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *error,size_t capacity)
{
    if(p&&p==confusion_program)return 1;
    if(!p||!scene_count||scene_drawn||confusion_program){fail(error,capacity,"Confusion upload requires an idle initialized scene before first draw.");return 0;}
    /* 5,505,024 linear vertex bytes + 524,288 RGBA8 + 524,288 D24S8.
     * Never reuse a stripe range while the GPU can still reference it. */
    confusion_vertices=linearAlloc(CONFUSION_VERTEX_BYTES);
    if(!confusion_vertices){fail(error,capacity,"Confusion vertex allocation failed.");return 0;}
    if(!C3D_TexInitVRAM(&confusion_texture,CONFUSION_WIDTH,CONFUSION_HEIGHT,GPU_RGBA8)){
        linearFree(confusion_vertices);confusion_vertices=NULL;
        fail(error,capacity,"Confusion capture texture allocation failed.");return 0;}
    C3D_TexSetFilter(&confusion_texture,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&confusion_texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    confusion_target=C3D_RenderTargetCreateFromTex(&confusion_texture,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
    if(!confusion_target){C3D_TexDelete(&confusion_texture);linearFree(confusion_vertices);confusion_vertices=NULL;
        memset(&confusion_texture,0,sizeof confusion_texture);fail(error,capacity,"Confusion capture depth allocation failed.");return 0;}
    confusion_program=p;
    gpu_buffer_bytes+=CONFUSION_VERTEX_BYTES+CONFUSION_WIDTH*CONFUSION_HEIGHT*8u;
    if(error&&capacity)error[0]=0;
    return 1;
}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s)
{
    if(s&&!confusion_program)return 0;
    confusion_state=s;return 1;
}
/* Return -1 for invalid active geometry, zero for hidden, one for prepared. */
static int confusion_prepare(const C3D_Mtx *clip,const C3D_Mtx *view,char *error,size_t capacity)
{
    if(!confusion_state||!confusion_state->active||confusion_state->radius<=0)return 0;
    float right[3],up[3],projected[2][2];
    for(unsigned i=0;i<3;++i){
        C3D_FVec v=Mtx_MultiplyFVec3(view,FVec3_New(i==0,i==1,i==2));
        right[i]=v.x;up[i]=v.y;
    }
    for(unsigned corner=0;corner<2;++corner){
        float sign=corner?1.0f:-1.0f;float w[3];
        for(unsigned i=0;i<3;++i)w[i]=confusion_state->center[i]+sign*confusion_state->radius*(right[i]+up[i]);
        C3D_FVec v=Mtx_MultiplyFVec4(clip,FVec4_New(w[0],w[1],w[2],1));
        if(!isfinite(v.w)||fabsf(v.w)<1e-8f){fail(error,capacity,"Confusion quad projection is singular.");return -1;}
        projected[corner][0]=.5f+.5f*v.x/v.w;
        projected[corner][1]=.5f-.5f*v.y/v.w;
    }
    C3ConfusionType12Frame frame;
    if(!c3_confusion_type12_frame(confusion_state,right,up,(const float (*)[2])projected,
        CONFUSION_WIDTH,CONFUSION_HEIGHT,&frame,error,capacity))return -1;
    C3ConfusionSamplerProfile profile=c3_confusion_sampler_source_profile();
    for(unsigned row=0;row<128;++row){
        if(!c3_confusion_grid_stripe(confusion_program,&frame,&profile,CONFUSION_WIDTH,
            CONFUSION_HEIGHT,row,1,confusion_row,128u*6u,error,capacity))return -1;
        for(unsigned j=0;j<128u*6u;++j){TitleGPUVertex *v=&confusion_vertices[row*128u*6u+j];
            memset(v,0,sizeof *v);memcpy(v->pos,confusion_row[j].world,sizeof v->pos);
            v->uv[0]=confusion_row[j].uv[0];
            /* Same RTT bottom-origin convention as the verified television. */
            v->uv[1]=1.0f-confusion_row[j].uv[1];
            for(unsigned k=0;k<4;++k)v->rgba[k]=1;
        }
    }
    GSPGPU_FlushDataCache(confusion_vertices,CONFUSION_VERTEX_BYTES);return 1;
}
static int confusion_draw(const C3D_Mtx *mvp,C3TitleRenderStats *stats,char *error,size_t capacity)
{
    /* Current segment can shrink after Citro3D implicitly splits a frame. */
    if((uint64_t)gpuCmdBufOffset+4096u+C3_STAGE_COMMAND_RESERVE_WORDS>gpuCmdBufSize){
        fail(error,capacity,"Confusion composite command budget exceeded.");return 0;}
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,mvp_location,mvp);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,color_pass_location,1,0,0,0);
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,confusion_vertices,sizeof *confusion_vertices,5,0x43210);
    for(int stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    C3D_TexEnv *env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_TexBind(0,&confusion_texture);C3D_TexBind(1,&confusion_texture);
    C3D_CullFace(GPU_CULL_NONE);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    /* 32-row draw stripes stay below the PICA index/count boundary. */
    for(unsigned row=0;row<128;row+=32){
        C3D_DrawArrays(GPU_TRIANGLES,row*128u*6u,32u*128u*6u);
        ++stats->draw_calls;stats->vertices+=32u*128u*6u;
    }
    return 1;
}
