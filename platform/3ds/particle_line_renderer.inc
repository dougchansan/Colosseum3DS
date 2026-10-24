/* Include after particle_renderer.inc inside the sole Citro3D owner.
 * No frame begin/end, queue, camera shortcut, or billboard fallback here. */
#include "colosseum3ds/particle_line.h"
#include "particle_line_shader_shbin.h"
typedef struct {float clip[4],uv[2],rgba[4];} ParticleLineGPUVertex;
typedef struct {
 uint8_t prim[4],env[4],textured,prim_env,blend,depth_test,depth_write;
 uint8_t filter_nearest,mirror_s,mirror_t,alpha_reference;
 uint8_t is_line;
 uint32_t texture,quad_index;
} ParticleLineMaterial;
static DVLB_s *particle_line_shader;
static shaderProgram_s particle_line_program;
static bool particle_line_program_started;
static ParticleLineGPUVertex *particle_line_vertices;
static ParticleLineMaterial particle_line_materials[C3_PARTICLE_MAX_PARTICLES];

static void particle_line_shutdown(void)
{
 if(particle_line_vertices){linearFree(particle_line_vertices);particle_line_vertices=NULL;gpu_buffer_bytes-=C3_PARTICLE_MAX_PARTICLES*6u*sizeof *particle_line_vertices;}
}
/* Call after C3D_Fini, preserving Citro3D's borrowed shader pointer lifetime. */
static void particle_line_fini(void)
{
 particle_line_shutdown();
 if(particle_line_program_started){shaderProgramFree(&particle_line_program);particle_line_program_started=false;}
 if(particle_line_shader){DVLB_Free(particle_line_shader);particle_line_shader=NULL;}
}
/* Optional eager init while scene is idle; draw also checks this lazily. */
static int particle_line_init(char *error,size_t cap)
{
 if(particle_line_program_started)return 1;
 particle_line_shader=DVLB_ParseFile((u32 *)particle_line_shader_shbin,particle_line_shader_shbin_size);
 if(!particle_line_shader||!particle_line_shader->numDVLE)goto failed;
 if(R_FAILED(shaderProgramInit(&particle_line_program)))goto failed;
 particle_line_program_started=true;
 if(R_FAILED(shaderProgramSetVsh(&particle_line_program,&particle_line_shader->DVLE[0])))goto failed;
 return 1;
failed:
 if(particle_line_program_started){shaderProgramFree(&particle_line_program);particle_line_program_started=false;}
 if(particle_line_shader){DVLB_Free(particle_line_shader);particle_line_shader=NULL;}
 fail(error,cap,"POINT+TRAIL shader initialization failed.");return 0;
}
static void particle_line_restore_program(void)
{
 C3D_BindProgram(&program);
 C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
 AttrInfo_AddLoader(a,0,GPU_FLOAT,3);AttrInfo_AddLoader(a,1,GPU_FLOAT,2);
 AttrInfo_AddLoader(a,2,GPU_FLOAT,4);AttrInfo_AddLoader(a,3,GPU_FLOAT,2);AttrInfo_AddLoader(a,4,GPU_FLOAT,3);
}
static int draw_particle_mixed(const C3TitleCamera *camera,C3TitleRenderStats *stats,char *error,size_t cap)
{
 if(!particle_system||!particle_pack)return 1;
 C3D_Mtx clip,view;C3ParticleCamera pc={0};
 if(!camera_matrices(camera,&clip,&view,false)){fail(error,cap,"Invalid POINT+TRAIL camera.");return 0;}
 for(unsigned i=0;i<4;++i){C3D_FVec v=Mtx_MultiplyFVec4(&clip,FVec4_New(i==0,i==1,i==2,i==3));pc.clip[i]=v.x;pc.clip[4+i]=v.y;pc.clip[8+i]=v.z;pc.clip[12+i]=v.w;
  if(i<3){C3D_FVec basis=Mtx_MultiplyFVec3(&view,FVec3_New(i==0,i==1,i==2));pc.right[i]=basis.x;pc.up[i]=basis.y;}}
 /* Source battle viewport is 640x480 (battle_range_candidate_801ED780.c).
  * Source raster widths scale to the native unrotated 400x240 viewport. */
 const C3ParticleLineRaster raster={{400.f/640,240.f/480},-1,0};
 const float viewport[2]={400,240};
 if(!particle_line_init(error,cap))return 0;
 if(!particle_line_vertices){
  ParticleLineGPUVertex *allocated=linearAlloc(C3_PARTICLE_MAX_PARTICLES*6u*sizeof *allocated);
  if(!allocated){fail(error,cap,"POINT+TRAIL vertex arena allocation failed.");return 0;}
  particle_line_vertices=allocated;gpu_buffer_bytes+=C3_PARTICLE_MAX_PARTICLES*6u*sizeof *allocated;
 }
 uint32_t count=0,quad_count=0;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i){
  const C3Particle *p=&particle_system->particles[i];
  if(!(p->kind&0x40000000u)){
   C3ParticleQuad *q=&particle_quads[quad_count];
   int result=c3_particle_quad(p,particle_attachment,&pc,q,error,cap);
   if(result<0)return 0;
   if(!result)continue;
   uint32_t texture=UINT32_MAX;
   for(uint32_t j=0;j<particle_pack->texture_count;++j)
    if(particle_pack->textures[j].group==q->texture_group&&particle_pack->textures[j].pose==q->texture_frame){texture=particle_textures[j];break;}
   if(texture==UINT32_MAX){fail(error,cap,"Mixed polygon particle texture is absent.");return 0;}
   ParticleLineMaterial *m=&particle_line_materials[count];memset(m,0,sizeof *m);m->texture=texture;m->quad_index=quad_count++;
   static const unsigned corners[6]={0,1,2,0,2,3};
   for(unsigned j=0;j<6;++j){const C3ParticleQuadVertex *v=&q->vertices[corners[j]];ParticleLineGPUVertex *out=&particle_line_vertices[count*6+j];
    for(unsigned k=0;k<4;++k){out->clip[k]=pc.clip[4*k]*v->position[0]+pc.clip[4*k+1]*v->position[1]+pc.clip[4*k+2]*v->position[2]+pc.clip[4*k+3];if(!isfinite(out->clip[k])){fail(error,cap,"Mixed polygon particle projection overflow.");return 0;}}
    memcpy(out->uv,v->uv,sizeof out->uv);for(unsigned k=0;k<4;++k)out->rgba[k]=v->rgba[k]/255.f;}
   ++count;continue;
  }
  C3ParticleLine line;C3ParticleLineMesh mesh;
  int result=c3_particle_line(p,particle_attachment,&pc,&raster,&line,error,cap);
  if(result<0)return 0;
  if(!result)continue;
  if(c3_particle_line_triangles(&line,viewport,&mesh,error,cap)!=1)return 0;
  ParticleLineMaterial *m=&particle_line_materials[count];memset(m,0,sizeof *m);
  m->is_line=1;
  memcpy(m->prim,line.prim,4);memcpy(m->env,line.env,4);
  m->textured=line.textured;m->prim_env=line.prim_env;m->blend=line.blend;
  m->depth_test=line.depth_test;m->depth_write=line.depth_write;m->filter_nearest=line.filter_nearest;
  m->mirror_s=line.mirror_s;m->mirror_t=line.mirror_t;m->alpha_reference=line.alpha_reference;m->texture=UINT32_MAX;
  if(m->textured){
   for(uint32_t j=0;j<particle_pack->texture_count;++j)
    if(particle_pack->textures[j].group==line.texture_group&&particle_pack->textures[j].pose==line.texture_frame){m->texture=particle_textures[j];break;}
   if(m->texture==UINT32_MAX){fail(error,cap,"POINT+TRAIL texture frame is absent.");return 0;}
  }
  for(unsigned j=0;j<6;++j){ParticleLineGPUVertex *v=&particle_line_vertices[count*6+j];
   memcpy(v->clip,mesh.vertices[j].clip,sizeof v->clip);memcpy(v->uv,mesh.vertices[j].uv,sizeof v->uv);
   for(unsigned k=0;k<4;++k)v->rgba[k]=mesh.vertices[j].rgba[k]/255.f;}
  ++count;
 }
 if(!count)return 1;
 uint32_t command_start=gpuCmdBufOffset;
 /* Include a shader upload/restore allowance in addition to bounded draws.
  * Check the live segment before submitting any line commands. */
 if(!c3_stage_particle_segment_fits(command_start+4096u,count,gpuCmdBufSize)){
  fail(error,cap,"POINT+TRAIL command segment capacity exceeded.");return 0;
 }
 GSPGPU_FlushDataCache(particle_line_vertices,count*6u*sizeof *particle_line_vertices);
 C3D_BindProgram(&particle_line_program);
 C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);
 AttrInfo_AddLoader(a,0,GPU_FLOAT,4);AttrInfo_AddLoader(a,1,GPU_FLOAT,2);AttrInfo_AddLoader(a,2,GPU_FLOAT,4);
 C3D_BufInfo *b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,particle_line_vertices,sizeof *particle_line_vertices,3,0x210);
 C3D_CullFace(GPU_CULL_NONE);C3D_SetViewport(0,0,240,400);
 for(uint32_t i=0;i<count;++i){const ParticleLineMaterial *m=&particle_line_materials[i];
  if(!m->is_line){particle_quad_material(&particle_quads[m->quad_index],m->texture);C3D_DrawArrays(GPU_TRIANGLES,i*6,6);continue;}
  for(unsigned j=0;j<6;++j)C3D_TexEnvInit(C3D_GetTexEnv(j));
  C3D_TexEnv *env=C3D_GetTexEnv(0);
  if(!m->textured){C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);}
  else if(!m->prim_env){C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);}
  else{
   C3D_TexEnvColor(env,particle_color(m->env));C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
   env=C3D_GetTexEnv(1);C3D_TexEnvColor(env,particle_color(m->prim));C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);C3D_TexEnvFunc(env,C3D_Both,GPU_INTERPOLATE);
   env=C3D_GetTexEnv(2);C3D_TexEnvSrc(env,C3D_Alpha,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);
  }
  C3D_AlphaTest(true,GPU_GEQUAL,m->alpha_reference);
  C3D_DepthTest(m->depth_test,GPU_GEQUAL,m->depth_write?GPU_WRITE_ALL:GPU_WRITE_COLOR);
  if(m->blend==2)C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT,GPU_BLEND_REVERSE_SUBTRACT,GPU_ONE,GPU_ONE,GPU_ONE,GPU_ONE);
  else if(m->blend==3)C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_DST_COLOR,GPU_ZERO,GPU_DST_ALPHA,GPU_ZERO);
  else C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,m->blend==1?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_SRC_ALPHA,m->blend==1?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA);
  if(m->textured){C3D_Tex *tex=&textures[m->texture];C3D_TexSetWrap(tex,m->mirror_s?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE,m->mirror_t?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE);C3D_TexSetFilter(tex,m->filter_nearest?GPU_NEAREST:GPU_LINEAR,m->filter_nearest?GPU_NEAREST:GPU_LINEAR);C3D_TexBind(0,tex);C3D_TexBind(1,tex);}
  C3D_DrawArrays(GPU_TRIANGLES,i*6,6);
 }
 particle_line_restore_program();
 stats->particle_command_words=gpuCmdBufOffset-command_start;stats->particles=count;stats->particle_vertices=count*6;stats->draw_calls+=count;stats->vertices+=count*6;
 return 1;
}
