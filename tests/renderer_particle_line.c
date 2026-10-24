/* Execute production mixed particle staging/order and GPU-state contracts. */
#include "stage_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
typedef unsigned u32;
typedef struct {float pos[3],uv[2],rgba[4],uv2[2],specular[3];} TitleGPUVertex;
typedef struct {float x,y,z,w;} C3D_FVec;
typedef int C3D_Mtx,C3D_Tex,C3D_AttrInfo,C3D_BufInfo,C3D_TexEnv;
typedef struct {int numDVLE,DVLE[1];} DVLB_s;
typedef struct {int *vertexShader;} shaderProgram_s;
static shaderProgram_s program,*bound_program;
static int mvp_location,color_pass_location;
static unsigned scene_count=1,scene_drawn,texture_count=1,gpuCmdBufOffset,gpuCmdBufSize=C3_RENDER_COMMAND_CAPACITY_WORDS;
static C3D_Tex textures[C3_STAGE_MAX_TEXTURES];
static size_t gpu_buffer_bytes;
static unsigned draws,quad_states,line_states,allocation_failure,shader_failure,allocations,frees;
static void *bound_vertices;
static unsigned bound_stride,comparison;
enum {GPU_VERTEX_SHADER,GPU_FLOAT,GPU_CULL_NONE,GPU_TRIANGLES,GPU_CONSTANT,
 GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS,C3D_Both,C3D_Alpha,GPU_REPLACE,
 GPU_MODULATE,GPU_INTERPOLATE,GPU_GREATER,GPU_GEQUAL,GPU_WRITE_ALL,GPU_WRITE_COLOR,
 GPU_BLEND_REVERSE_SUBTRACT,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_DST_COLOR,GPU_DST_ALPHA,
 GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_MIRRORED_REPEAT,GPU_CLAMP_TO_EDGE,GPU_NEAREST,GPU_LINEAR};
#define R_FAILED(result) ((result)<0)
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return -1;}
static void *linearAlloc(size_t n){++allocations;return allocation_failure?NULL:malloc(n);}
static void linearFree(void *p){++frees;free(p);}
static int upload_texture(C3D_Tex *t,const C3TitleTexture *s){return 1;}
static DVLB_s *DVLB_ParseFile(u32 *p,size_t n){if(shader_failure==1)return NULL;DVLB_s *d=calloc(1,sizeof *d);assert(d);d->numDVLE=1;return d;}
static void DVLB_Free(DVLB_s *d){free(d);}
static int shaderProgramInit(shaderProgram_s *p){return shader_failure==2?-1:0;}
static int shaderProgramSetVsh(shaderProgram_s *p,int *v){p->vertexShader=v;return shader_failure==3?-1:0;}
static void shaderProgramFree(shaderProgram_s *p){p->vertexShader=NULL;}
static void C3D_BindProgram(shaderProgram_s *p){bound_program=p;++gpuCmdBufOffset;}
#define FVec3_New(x,y,z) ((C3D_FVec){x,y,z,0})
#define FVec4_New(x,y,z,w) ((C3D_FVec){x,y,z,w})
static C3D_FVec Mtx_MultiplyFVec3(const C3D_Mtx *m,C3D_FVec v){return v;}
static C3D_FVec Mtx_MultiplyFVec4(const C3D_Mtx *m,C3D_FVec v){return (C3D_FVec){v.x*.1f,v.y*.1f,v.z*.1f,v.w};}
static bool camera_matrices(const C3TitleCamera *c,C3D_Mtx *clip,C3D_Mtx *view,bool tilt){return true;}
#define NOOP(...) ((void)++gpuCmdBufOffset)
#define C3D_FVUnifMtx4x4 NOOP
#define C3D_FVUnifSet NOOP
#define C3D_GetBufInfo() NULL
#define C3D_GetAttrInfo() NULL
#define C3D_GetTexEnv(...) NULL
#define BufInfo_Init NOOP
#define AttrInfo_Init NOOP
#define AttrInfo_AddLoader NOOP
#define C3D_CullFace NOOP
#define C3D_SetViewport NOOP
#define C3D_TexEnvInit NOOP
#define C3D_TexEnvColor NOOP
#define C3D_TexEnvSrc NOOP
#define C3D_TexEnvFunc NOOP
#define C3D_DepthTest NOOP
#define C3D_AlphaBlend NOOP
#define C3D_TexSetWrap NOOP
#define C3D_TexSetFilter NOOP
#define C3D_TexBind NOOP
#define GSPGPU_FlushDataCache(...) ((void)0)
static void BufInfo_Add(C3D_BufInfo *b,void *p,unsigned stride,unsigned attrs,unsigned mask){bound_vertices=p;bound_stride=stride;++gpuCmdBufOffset;}
static void C3D_AlphaTest(bool enabled,int function,unsigned ref){assert(enabled);comparison=(unsigned)function;++gpuCmdBufOffset;}
static void C3D_DrawArrays(int primitive,unsigned first,unsigned count);
#include "particle_renderer.inc"
#include "particle_line_renderer.inc"
static void C3D_DrawArrays(int primitive,unsigned first,unsigned count)
{
 assert(bound_program==&particle_line_program&&primitive==GPU_TRIANGLES&&count==6&&first==draws*6);
 assert(bound_stride==sizeof(ParticleLineGPUVertex));
 ParticleLineGPUVertex *v=(ParticleLineGPUVertex *)bound_vertices+first;
 float mean_x=0;for(unsigned j=0;j<6;++j){assert(isfinite(v[j].clip[3])&&v[j].clip[3]==1);mean_x+=v[j].clip[0]/6;}
 assert(fabsf(mean_x-(-.3f+.2f*draws))<.0001f);
 if(draws&1u){assert(comparison==GPU_GEQUAL);++line_states;}else{assert(comparison==GPU_GREATER);++quad_states;}
 ++draws;gpuCmdBufOffset+=32;
}
int main(void)
{
 static C3ParticleSystem system;static C3ParticlePack pack;static uint8_t bytes;
 pack.bank.bytes=&bytes;pack.texture_count=1;pack.textures[0].group=pack.textures[0].pose=0;
 particle_pack=&pack;particle_system=&system;
 particle_attachment[0]=particle_attachment[5]=particle_attachment[10]=.1f;
 for(unsigned i=0;i<4;++i){C3Particle *p=&system.particles[i];p->active=1;p->size=1;p->trail=.5f;p->position[0]=-30+20.f*i;p->position[2]=-50;p->velocity[1]=1;p->kind=i&1u?0x40500002u:0x00400480u;memset(p->prim,255,4);}
 C3TitleCamera camera={0};C3D_Mtx mvp=0;C3TitleRenderStats stats={0};char e[192];
 for(unsigned i=1;i<=3;++i){shader_failure=i;assert(!draw_particles(&camera,&mvp,&stats,e,sizeof e));assert(!particle_line_shader&&!particle_line_program_started&&!particle_line_vertices&&!draws&&!gpuCmdBufOffset);}
 shader_failure=0;allocation_failure=1;assert(!draw_particles(&camera,&mvp,&stats,e,sizeof e));assert(!particle_line_vertices&&!gpu_buffer_bytes&&!draws);
 allocation_failure=0;gpuCmdBufSize=8192;assert(!draw_particles(&camera,&mvp,&stats,e,sizeof e));assert(!draws&&!gpuCmdBufOffset);
 gpuCmdBufSize=C3_RENDER_COMMAND_CAPACITY_WORDS;assert(draw_particles(&camera,&mvp,&stats,e,sizeof e));
 assert(draws==4&&quad_states==2&&line_states==2&&bound_program==&program);
 assert(stats.particles==4&&stats.particle_vertices==24&&stats.draw_calls==4&&stats.particle_command_words==gpuCmdBufOffset);
 assert(gpu_buffer_bytes==C3_PARTICLE_MAX_PARTICLES*6u*sizeof(ParticleLineGPUVertex));
 particle_line_shutdown();assert(!gpu_buffer_bytes&&!particle_line_vertices&&frees==1);particle_line_fini();assert(!particle_line_shader&&!particle_line_program_started);
 puts("Mixed particle GPU contract: source order quad/line/quad/line, pos4 shader, material state, allocation rollback, live segment preflight, restoration and cleanup passed");
}
