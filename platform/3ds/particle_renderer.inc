/* Included by the sole Citro3D owner. No independent frame or GPU queue. */
static const C3ParticlePack *particle_pack;
static const C3ParticleSystem *particle_system;
static float particle_attachment[12];
static TitleGPUVertex *particle_vertices;
static uint32_t particle_textures[32];
static C3ParticleQuad particle_quads[C3_PARTICLE_MAX_PARTICLES];
static uint32_t particle_texids[C3_PARTICLE_MAX_PARTICLES];

int c3_stage_renderer_particle_pack(const C3ParticlePack *pack,char *error,size_t cap)
{
    if(!pack||particle_pack||!scene_count||scene_drawn||
       pack->texture_count>32||texture_count+pack->texture_count>C3_STAGE_MAX_TEXTURES)
        return fail(error,cap,"Particle upload requires a fresh bounded scene.");
    particle_vertices=linearAlloc(C3_PARTICLE_MAX_PARTICLES*6u*sizeof *particle_vertices);
    if(!particle_vertices)return fail(error,cap,"Particle vertex allocation failed.");
    gpu_buffer_bytes+=C3_PARTICLE_MAX_PARTICLES*6u*sizeof *particle_vertices;
    for(uint32_t i=0;i<pack->texture_count;++i){
        const C3ParticlePackTexture *p=&pack->textures[i];
        C3TitleTexture source={p->width,p->height,0,0,p->rgba,(size_t)p->width*p->height*4};
        if(!upload_texture(&textures[texture_count],&source))return fail(error,cap,"Particle texture upload failed.");
        particle_textures[i]=texture_count++;
        gpu_buffer_bytes+=source.size;
    }
    particle_pack=pack;return C3_RENDER_OK;
}
int c3_stage_renderer_particles(const C3ParticleSystem *system,const float attachment[12])
{
    if(system&&(!particle_pack||!attachment))return 0;
    if(attachment){for(unsigned i=0;i<12;++i)if(!isfinite(attachment[i]))return 0;
        memcpy(particle_attachment,attachment,sizeof particle_attachment);}
    particle_system=system;return 1;
}
static uint32_t particle_color(const uint8_t c[4])
{return (uint32_t)c[0]|(uint32_t)c[1]<<8|(uint32_t)c[2]<<16|(uint32_t)c[3]<<24;}
static int draw_particles(const C3TitleCamera *camera,const C3D_Mtx *mvp,
                          C3TitleRenderStats *stats,char *error,size_t cap)
{
    if(!particle_system||!particle_pack)return 1;
    C3D_Mtx clip,view;C3ParticleCamera pc={0};
    if(!camera_matrices(camera,&clip,&view,false))return 0;
    for(unsigned i=0;i<4;++i){
        C3D_FVec basis=FVec4_New(i==0,i==1,i==2,i==3);
        C3D_FVec c=Mtx_MultiplyFVec4(&clip,basis);
        pc.clip[i]=c.x;pc.clip[4+i]=c.y;pc.clip[8+i]=c.z;pc.clip[12+i]=c.w;
        if(i<3){C3D_FVec v=Mtx_MultiplyFVec3(&view,FVec3_New(i==0,i==1,i==2));
            pc.right[i]=v.x;pc.up[i]=v.y;}
    }
    uint32_t count=0;C3ParticleQuad *quads=particle_quads;
    uint32_t *texids=particle_texids;
    for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i){
        int result=c3_particle_quad(&particle_system->particles[i],particle_attachment,&pc,&quads[count],error,cap);
        if(result<0)return 0;
        if(!result)continue;
        const C3ParticleQuad *q=&quads[count];uint32_t texture=UINT32_MAX;
        for(uint32_t j=0;j<particle_pack->texture_count;++j)
            if(particle_pack->textures[j].group==q->texture_group&&particle_pack->textures[j].pose==q->texture_frame){texture=particle_textures[j];break;}
        if(texture==UINT32_MAX){fail(error,cap,"Reached particle texture is absent.");return 0;}
        texids[count]=texture;
        static const unsigned corners[6]={0,1,2,0,2,3};
        for(unsigned j=0;j<6;++j){const C3ParticleQuadVertex *v=&q->vertices[corners[j]];
            TitleGPUVertex *d=&particle_vertices[count*6+j];memset(d,0,sizeof *d);
            memcpy(d->pos,v->position,sizeof d->pos);memcpy(d->uv,v->uv,sizeof d->uv);
            for(unsigned k=0;k<4;++k)d->rgba[k]=v->rgba[k]/255.0f;}
        ++count;
    }
    if(!count)return 1;
    GSPGPU_FlushDataCache(particle_vertices,count*6u*sizeof *particle_vertices);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,mvp_location,mvp);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,color_pass_location,1,0,0,0);
    C3D_BufInfo *buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,particle_vertices,sizeof *particle_vertices,5,0x43210);
    C3D_CullFace(GPU_CULL_NONE);
    for(uint32_t i=0;i<count;++i){const C3ParticleQuad *q=&quads[i];
        for(int j=0;j<6;++j)C3D_TexEnvInit(C3D_GetTexEnv(j));
        C3D_TexEnv *env=C3D_GetTexEnv(0);
        C3D_TexEnvColor(env,particle_color(q->prim_env?q->env:q->prim));
        C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT,GPU_TEXTURE0,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_Both,q->prim_env?GPU_REPLACE:GPU_MODULATE);
        if(q->prim_env){env=C3D_GetTexEnv(1);C3D_TexEnvColor(env,particle_color(q->prim));
            C3D_TexEnvSrc(env,C3D_Both,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE0);
            C3D_TexEnvFunc(env,C3D_Both,GPU_INTERPOLATE);}
        env=C3D_GetTexEnv(2);
        C3D_TexEnvSrc(env,C3D_Alpha,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env,C3D_Alpha,GPU_MODULATE);
        C3D_AlphaTest(true,GPU_GREATER,q->alpha_reference);
        C3D_DepthTest(q->depth_test,GPU_GEQUAL,q->depth_write?GPU_WRITE_ALL:GPU_WRITE_COLOR);
        if(q->blend==2)C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT,GPU_BLEND_REVERSE_SUBTRACT,GPU_ONE,GPU_ONE,GPU_ONE,GPU_ONE);
        else if(q->blend==3)C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_DST_COLOR,GPU_ZERO,GPU_DST_ALPHA,GPU_ZERO);
        else C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,q->blend==1?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA,GPU_SRC_ALPHA,q->blend==1?GPU_ONE:GPU_ONE_MINUS_SRC_ALPHA);
        C3D_Tex *tex=&textures[texids[i]];
        C3D_TexSetWrap(tex,q->mirror_s?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE,q->mirror_t?GPU_MIRRORED_REPEAT:GPU_CLAMP_TO_EDGE);
        C3D_TexSetFilter(tex,q->filter_nearest?GPU_NEAREST:GPU_LINEAR,q->filter_nearest?GPU_NEAREST:GPU_LINEAR);
        C3D_TexBind(0,tex);C3D_TexBind(1,tex);C3D_DrawArrays(GPU_TRIANGLES,i*6,6);
    }
    stats->particles=count;stats->particle_vertices=count*6;stats->draw_calls+=count;stats->vertices+=count*6;
    return 1;
}
