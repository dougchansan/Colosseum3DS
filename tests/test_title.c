#include "colosseum3ds/title.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VERTEX_OFFSET=128, BATCH_OFFSET=248, TEXTURE_OFFSET=360,
       CLIP_OFFSET=384, LIGHT_OFFSET=400, FRAME_OFFSET=464,
       FRAME_BYTES=144, PIXEL_OFFSET=896, FILE_BYTES=1152 };

static void put32(unsigned char *p, uint32_t value)
{
    unsigned i;
    for (i=0;i<4;++i) p[i]=(unsigned char)(value>>(i*8u));
}

static void putf(unsigned char *p, float value)
{
    uint32_t bits;
    memcpy(&bits,&value,4);
    put32(p,bits);
}

static unsigned char *fixture(void)
{
    unsigned char *p=(unsigned char *)calloc(1,FILE_BYTES);
    unsigned i,j;
    const uint32_t counts[]={1,3,1,1,1,3,1};
    const uint32_t offsets[]={VERTEX_OFFSET,BATCH_OFFSET,TEXTURE_OFFSET,CLIP_OFFSET,LIGHT_OFFSET,FRAME_OFFSET,PIXEL_OFFSET};
    assert(p);
    memcpy(p,"C3T1",4); put32(p+4,1); put32(p+8,FILE_BYTES);
    for (i=0;i<7;++i) { put32(p+12+i*4u,counts[i]); put32(p+44+i*4u,offsets[i]); }
    putf(p+40,2); put32(p+72,FRAME_BYTES); put32(p+76,256);
    for (i=0;i<3;++i) {
        unsigned char *v=p+VERTEX_OFFSET+i*40u;
        putf(v,1); putf(v+4,2); putf(v+8,3);
        putf(v+12,0.70710678f); putf(v+16,0.70710678f);
        putf(v+24,.25f); putf(v+28,.75f);
        v[32]=11; v[33]=22; v[34]=33; v[35]=255;
    }
    put32(p+BATCH_OFFSET+4,3); put32(p+BATCH_OFFSET+12,C3_TITLE_CULL_BACK);
    put32(p+BATCH_OFFSET+16,4); put32(p+BATCH_OFFSET+20,3);
    for (i=0;i<9;++i) putf(p+BATCH_OFFSET+32+i*4u,1);
    putf(p+BATCH_OFFSET+68,50); putf(p+BATCH_OFFSET+72,1); putf(p+BATCH_OFFSET+76,1);
    put32(p+BATCH_OFFSET+88,7); put32(p+BATCH_OFFSET+92,7); put32(p+BATCH_OFFSET+100,3);
    put32(p+TEXTURE_OFFSET,8); put32(p+TEXTURE_OFFSET+4,8); put32(p+TEXTURE_OFFSET+8,PIXEL_OFFSET);
    put32(p+TEXTURE_OFFSET+12,256); put32(p+TEXTURE_OFFSET+20,1);
    put32(p+CLIP_OFFSET+4,3); put32(p+CLIP_OFFSET+8,1);
    put32(p+LIGHT_OFFSET,4);
    for (i=0;i<3;++i) putf(p+LIGHT_OFFSET+8+i*4u,1);
    putf(p+LIGHT_OFFSET+48,1); putf(p+LIGHT_OFFSET+52,1);
    for (i=0;i<3;++i) {
        unsigned char *f=p+FRAME_OFFSET+i*FRAME_BYTES;
        float mid=i==1 ? 1.0f:0.0f;
        putf(f,2); putf(f+20,1); putf(f+40,1); putf(f+12,mid*4);
        putf(f+48,2+mid*2); putf(f+52,3+mid*2);
        putf(f+56,-.1f+mid*.4f); putf(f+60,.2f+mid*.2f);
        putf(f+64,.25f+mid*.5f); putf(f+68,1-mid);
        for (j=0;j<3;++j) { putf(f+72+j*4u,.2f+mid*.6f); putf(f+84+j*4u,1); }
        putf(f+96+8,5+mid*4); putf(f+96+28,45+mid*10);
        putf(f+96+32,.1f); putf(f+96+36,100); putf(f+96+40,4.0f/3.0f);
    }
    memset(p+PIXEL_OFFSET,255,256);
    return p;
}

static void near_value(float actual, float expected)
{
    assert(fabsf(actual-expected)<0.0001f);
}

static void rejects(const unsigned char *p,size_t bytes)
{
    C3Title title={0};
    char error[80];
    assert(!c3_title_parse(&title,p,bytes,error,sizeof error));
    assert(error[0]);
    assert(!title.file_data && !title.vertices && !title.textures && !title.draw_vertices);
    c3_title_close(&title);
}

static unsigned char *extended_fixture(unsigned version, size_t *bytes)
{
    unsigned char *base=fixture(), *p;
    unsigned i;
    const unsigned vertex=128,batch=272,texture=384,clip=408,light=424;
    const unsigned frame=light+(version==4 ? 80u:64u);
    unsigned material_bytes=version>=3 ? 96u:64u;
    unsigned stride=96u+material_bytes,pixels=frame+3u*stride;
    const unsigned offsets[]={vertex,batch,texture,clip,light,frame,pixels};
    *bytes=pixels+256u;
    p=(unsigned char *)calloc(1,*bytes); assert(p);
    memcpy(p,base,128); memcpy(p,version==4 ? "C3T4":(version==3 ? "C3T3":"C3T2"),4);
    put32(p+4,version); put32(p+8,(uint32_t)*bytes); put32(p+72,stride);
    for(i=0;i<7;++i) put32(p+44+i*4u,offsets[i]);
    for(i=0;i<3;++i) {
        memcpy(p+vertex+i*48u,base+VERTEX_OFFSET+i*40u,40);
        putf(p+vertex+i*48u+40,.2f); putf(p+vertex+i*48u+44,.4f);
    }
    memcpy(p+batch,base+BATCH_OFFSET,112);
    put32(p+batch+12,C3_TITLE_CULL_BACK | (version>=3 ? C3_TITLE_REFLECTION_BLEND:
                                        C3_TITLE_DUAL_MODULATE|C3_TITLE_TEV_LERP));
    put32(p+batch+20,0);
    if(version>=3) putf(p+batch+108,.5f);
    memcpy(p+texture,base+TEXTURE_OFFSET,24); put32(p+texture+8,pixels);
    memcpy(p+clip,base+CLIP_OFFSET,16); memcpy(p+light,base+LIGHT_OFFSET,64);
    if(version==4) {
        put32(p+light,7);put32(p+light+64,3);putf(p+light+68,87.5f);
        putf(p+light+48,.25f);putf(p+light+52,10);putf(p+light+56,3);
    }
    for(i=0;i<3;++i) {
        unsigned char *f=p+frame+i*stride;
        memcpy(f,base+FRAME_OFFSET+i*FRAME_BYTES,96);
        putf(f+96,2); putf(f+100,3); putf(f+104,.1f); putf(f+108,-.2f);
        if(version>=3) {
            putf(f+112,.3f); putf(f+116,-.4f);
            putf(f+120,-.5f); putf(f+124,.6f);
            putf(f+128,7.0f+i); putf(f+132,8); putf(f+136,9);
        }
        memcpy(f+48u+material_bytes,base+FRAME_OFFSET+i*FRAME_BYTES+96u,48);
    }
    memset(p+pixels,255,256); free(base); return p;
}

static void extended_checks(void)
{
    unsigned version;
    for(version=2;version<=4;++version) {
        size_t bytes;
        unsigned char *p=extended_fixture(version,&bytes);
        C3Title title={0}; char error[100];
        assert(c3_title_parse(&title,p,bytes,error,sizeof error));
        assert(title.format_version==version);
        assert(title.material_frame_bytes==(version>=3 ? 96u:64u));
        assert(c3_title_animate(&title,0,.25f));
        near_value(title.draw_vertices[0].uv[0],version>=3 ? 1.075f:.85f);
        near_value(title.draw_vertices[0].uv[1],version>=3 ? 3.2f:3.3f);
        near_value(title.draw_vertices[0].uv2[0],version>=3 ? .3f:.5f);
        near_value(title.draw_vertices[0].uv2[1],version>=3 ? 1.12f:1.0f);
        near_value(title.pose.materials[0].specular_origin[0],version>=3 ? 7.5f:0.0f);
        near_value(title.batches[0].texture2_blend,version>=3 ? .5f:0.0f);
        if(version==4) {
            assert(title.lights[0].spot_function==3);near_value(title.lights[0].spot_cutoff,87.5f);
            near_value(title.lights[0].reference_distance,10);
        }
        c3_title_close(&title);
        put32(p+272+104,1); rejects(p,bytes); put32(p+272+104,0);
        putf(p+128+40,NAN); rejects(p,bytes); putf(p+128+40,.2f);
        if(version>=3) {
            unsigned frame=version==4 ? 504u:488u;
            putf(p+272+108,NAN); rejects(p,bytes);
            putf(p+272+108,1.1f); rejects(p,bytes); putf(p+272+108,.5f);
            put32(p+272+12,C3_TITLE_CULL_BACK); rejects(p,bytes);
            put32(p+272+12,C3_TITLE_DUAL_MODULATE|C3_TITLE_REFLECTION_BLEND|C3_TITLE_TEV_LERP);
            rejects(p,bytes); put32(p+272+12,C3_TITLE_REFLECTION_BLEND);
            put32(p+frame+48+92,1); rejects(p,bytes); put32(p+frame+48+92,0);
            putf(p+frame+48+80,INFINITY); rejects(p,bytes);putf(p+frame+48+80,7);
            if(version==4) {
                put32(p+424+64,7);rejects(p,bytes);put32(p+424+64,3);
                putf(p+424+68,NAN);rejects(p,bytes);putf(p+424+68,87.5f);
                put32(p+424+72,1);rejects(p,bytes);put32(p+424+72,0);
                put32(p+424,4);rejects(p,bytes);
            }
        } else {
            put32(p+272+12,C3_TITLE_REFLECTION_BLEND); rejects(p,bytes);
            put32(p+272+12,C3_TITLE_DUAL_MODULATE); rejects(p,bytes);
        }
        free(p);
    }
}

static void actor_clip_checks(void)
{
    /* Field actors retain original clip numbers, including Willie's 18
     * motions. Exercise the actual highest bounded slot and its frame range. */
    unsigned char *base=fixture();
    const unsigned clips=C3_TITLE_MAX_CLIPS;
    const unsigned light=CLIP_OFFSET+clips*16u,frame=light+64u;
    const unsigned pixels=frame+clips*FRAME_BYTES,bytes=pixels+256u;
    unsigned char *p=calloc(1,bytes); assert(p);
    memcpy(p,base,CLIP_OFFSET);
    put32(p+8,bytes); put32(p+28,clips); put32(p+32,clips);
    put32(p+60,light); put32(p+64,frame); put32(p+68,pixels);
    put32(p+TEXTURE_OFFSET+8,pixels);
    memcpy(p+light,base+LIGHT_OFFSET,64u);
    memcpy(p+pixels,base+PIXEL_OFFSET,256u);
    for(unsigned i=0;i<clips;++i) {
        put32(p+CLIP_OFFSET+i*16u,i); put32(p+CLIP_OFFSET+i*16u+4,1);
        put32(p+CLIP_OFFSET+i*16u+8,i&1u);
        memcpy(p+frame+i*FRAME_BYTES,base+FRAME_OFFSET,FRAME_BYTES);
        putf(p+frame+i*FRAME_BYTES+12,(float)i);
    }
    C3Title title={0}; char error[100];
    assert(c3_title_parse(&title,p,bytes,error,sizeof error));
    for(unsigned i=0;i<clips;++i) {
        assert(c3_title_animate(&title,i,1000.0f));
        near_value(title.draw_vertices[0].pos[0],2.0f+i);
    }
    assert(!c3_title_animate(&title,clips,0));
    c3_title_close(&title);
    put32(p+CLIP_OFFSET+(clips-1u)*16u,clips); rejects(p,bytes);
    free(base); free(p);
}

/* tools/pack_textures.py packs: header byte 112 = 1, wrap_s word carries the
 * PICA format (bits 8..15) and levels-1 (bits 16..23). */
static void gpu_native_textures(void)
{
    unsigned char *p=fixture();
    C3Title title={0};
    char error[80];
    p[112]=1;
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(title.textures[0].gpu_native==1 && title.textures[0].gpu_format==C3_TITLE_GPU_RGBA8 &&
           title.textures[0].levels==1 && title.textures[0].wrap_s==0 && title.textures[0].wrap_t==1 &&
           title.textures[0].size==256);
    c3_title_close(&title);
    put32(p+TEXTURE_OFFSET+16,C3_TITLE_GPU_RGB565<<8); rejects(p,FILE_BYTES); /* 565 is 128 bytes */
    put32(p+TEXTURE_OFFSET+16,5u<<8); rejects(p,FILE_BYTES);                 /* unsupported format */
    put32(p+TEXTURE_OFFSET+16,1u<<16); rejects(p,FILE_BYTES);                /* 8x8 has one level */
    put32(p+TEXTURE_OFFSET+16,1u<<24); rejects(p,FILE_BYTES);                /* reserved top byte */
    put32(p+TEXTURE_OFFSET+16,3u); rejects(p,FILE_BYTES);                    /* wrap mode 3 */
    put32(p+TEXTURE_OFFSET+16,0); p[112]=2; rejects(p,FILE_BYTES);
    p[112]=1; p[113]=1; rejects(p,FILE_BYTES);
    p[113]=0; p[112]=0; put32(p+TEXTURE_OFFSET+16,C3_TITLE_GPU_RGB565<<8); rejects(p,FILE_BYTES);
    /* A 565 image is half the bytes: shrink the pixel section to match. */
    p[112]=1; put32(p+TEXTURE_OFFSET+12,128); put32(p+8,FILE_BYTES-128); put32(p+76,128);
    assert(c3_title_parse(&title,p,FILE_BYTES-128,error,sizeof error));
    assert(title.textures[0].gpu_format==C3_TITLE_GPU_RGB565 && title.textures[0].size==128);
    c3_title_close(&title);
    assert(c3_title_texture_bytes(1024,1024,C3_TITLE_GPU_RGB565,8)==2796160u);
    assert(c3_title_texture_bytes(64,8,C3_TITLE_GPU_RGBA8,1)==2048u);
    free(p);
}

/* gpu_batches: animate leaves the batch's sampled vertices alone and exposes
 * the placed bone matrix that reproduces the CPU skin. */
static void gpu_skinned_batches(void)
{
    unsigned char *p=fixture();
    C3Title cpu={0},gpu={0};
    char error[80];
    assert(c3_title_parse(&cpu,p,FILE_BYTES,error,sizeof error)&&c3_title_parse(&gpu,p,FILE_BYTES,error,sizeof error));
    float position[3]={1.5f,-2,3};
    c3_title_place_yaw(&cpu,position,0.6f,0.8f,2,0);gpu.placement=cpu.placement;
    assert(c3_title_animate(&cpu,0,.25f));
    memset(gpu.draw_vertices,0x5a,gpu.vertex_count*sizeof *gpu.draw_vertices);
    gpu.retained=1;gpu.gpu_batches=1;
    C3TitlePerf perf;c3_title_perf_take(NULL);
    assert(c3_title_animate(&gpu,0,.25f));
    c3_title_perf_take(&perf);
    assert(perf.vertices_gpu==3&&perf.vertices_animated==0&&!gpu.batch_current[0]);
    for(uint32_t v=0;v<gpu.vertex_count;++v){
        const unsigned char *raw=(const unsigned char *)&gpu.draw_vertices[v];
        for(size_t k=0;k<sizeof gpu.draw_vertices[v];++k)assert(raw[k]==0x5a);
        const float *m=gpu.pose.skin[gpu.vertices[v].bone],*s=gpu.vertices[v].pos;
        for(unsigned r=0;r<3;++r)
            near_value(m[r*4]*s[0]+m[r*4+1]*s[1]+m[r*4+2]*s[2]+m[r*4+3],cpu.draw_vertices[v].pos[r]);
        const float *n=gpu.pose.skin_normal[gpu.vertices[v].bone],*q=gpu.vertices[v].normal;
        float out[3],length=0;
        for(unsigned r=0;r<3;++r){out[r]=n[r*3]*q[0]+n[r*3+1]*q[1]+n[r*3+2]*q[2];length+=out[r]*out[r];}
        length=sqrtf(length);
        for(unsigned r=0;r<3;++r)near_value(out[r]/length,cpu.draw_vertices[v].normal[r]);
    }
    /* Dropping the GPU flag makes the next retained call rebuild the batch. */
    gpu.gpu_batches=0;assert(c3_title_animate(&gpu,0,.25f));
    for(unsigned r=0;r<3;++r)near_value(gpu.draw_vertices[0].pos[r],cpu.draw_vertices[0].pos[r]);
    c3_title_close(&cpu);c3_title_close(&gpu);free(p);
}

int main(void)
{
    unsigned char *p=fixture();
    C3Title title={0};
    char error[80];
    size_t i;
    gpu_native_textures();
    gpu_skinned_batches();
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(!error[0] && title.bone_count==1 && title.vertex_count==3 && title.frame_count==3);
    assert(title.textures[0].pixels>=title.file_data && title.resident_bytes>FILE_BYTES);
    {
        C3Title instance={0}, nested={0};
        assert(c3_title_instance(&instance,&title,error,sizeof error));
        assert(instance.vertices==title.vertices && instance.textures==title.textures);
        assert(instance.file_data==title.file_data && instance.draw_vertices!=title.draw_vertices);
        assert(c3_title_animate(&instance,0,.25f));
        near_value(instance.draw_vertices[0].pos[0],4);
        near_value(title.draw_vertices[0].pos[0],2);
        assert(instance.active_frame!=title.active_frame);
        assert(!c3_title_instance(&nested,&instance,error,sizeof error));
        assert(!c3_title_instance(&title,&title,error,sizeof error));
        c3_title_close(&instance);
        assert(c3_title_animate(&title,0,0));
        c3_title_close(&instance);
    }
    near_value(title.draw_vertices[0].pos[0],2);
    near_value(title.draw_vertices[0].normal[0],1.0f/sqrtf(5.0f));
    near_value(title.draw_vertices[0].normal[1],2.0f/sqrtf(5.0f));
    assert(c3_title_animate(&title,0,.25f));
    near_value(title.draw_vertices[0].pos[0],4);
    near_value(title.draw_vertices[0].uv[0],.85f);
    near_value(title.draw_vertices[0].uv[1],3.3f);
    near_value(title.pose.materials[0].alpha,.5f);
    near_value(title.pose.materials[0].tev0[0],.2f);
    near_value(title.pose.camera.eye[2],7);
    assert(title.pose.materials[0].visible==1 && title.draw_vertices[0].rgba[1]==22);
    assert(c3_title_animate(&title,0,.5f));
    assert(title.pose.materials[0].visible==0);
    assert(c3_title_animate(&title,0,1.25f));
    near_value(title.draw_vertices[0].pos[0],4);
    {
        C3TitlePose pose=title.pose;
        C3TitleDrawVertex vertex=title.draw_vertices[0];
        const float invalid[]={NAN,INFINITY,-INFINITY};
        for (i=0;i<sizeof invalid/sizeof invalid[0];++i) assert(!c3_title_animate(&title,0,invalid[i]));
        assert(!c3_title_animate(&title,1,0));
        assert(!memcmp(&pose,&title.pose,sizeof pose));
        assert(!memcmp(&vertex,&title.draw_vertices[0],sizeof vertex));
    }
    {
        const float extremes[]={-1e-30f,-FLT_MIN,-0.0f,-FLT_MAX,FLT_MAX};
        for (i=0;i<sizeof extremes/sizeof extremes[0];++i) {
            assert(c3_title_animate(&title,0,extremes[i]));
            assert(isfinite(title.draw_vertices[0].pos[0]));
            if (extremes[i]<=0) near_value(title.draw_vertices[0].pos[0],2);
        }
    }
    /* A failed replacement leaves existing data, geometry and ownership live. */
    put32(p+44,UINT32_MAX);
    {
        unsigned char *owned=title.file_data;
        assert(!c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
        assert(title.file_data==owned);
        assert(c3_title_animate(&title,0,.25f));
        assert(c3_title_parse(&title,owned,FILE_BYTES,error,sizeof error));
        assert(title.file_data!=owned);
    }
    put32(p+44,VERTEX_OFFSET);
    p[VERTEX_OFFSET]=0; /* Caller input is not the retained file buffer. */
    near_value(title.vertices[0].pos[0],1);
    c3_title_close(&title); c3_title_close(&title);
    assert(!title.file_data && !title.vertex_count && !title.resident_bytes);
    free(p); p=fixture();
    for (i=0;i<FILE_BYTES;++i) rejects(p,i);
    rejects(p,C3_TITLE_MAX_FILE_BYTES+1u);
    {
        const uint32_t offsets[]={12,16,20,24,28,32,36};
        const uint32_t old[]={1,3,1,1,1,3,1};
        const uint32_t excessive[]={65,32769,65,33,C3_TITLE_MAX_CLIPS+1u,4097,9};
        for (i=0;i<7;++i) {
            put32(p+offsets[i],0); rejects(p,FILE_BYTES);
            put32(p+offsets[i],excessive[i]); rejects(p,FILE_BYTES);
            put32(p+offsets[i],old[i]);
        }
    }
    {
        const uint32_t offsets[]={44,48,52,56,60,64,68};
        const uint32_t old[]={VERTEX_OFFSET,BATCH_OFFSET,TEXTURE_OFFSET,CLIP_OFFSET,LIGHT_OFFSET,FRAME_OFFSET,PIXEL_OFFSET};
        for (i=0;i<7;++i) { put32(p+offsets[i],UINT32_MAX); rejects(p,FILE_BYTES); put32(p+offsets[i],old[i]); }
    }
    putf(p+40,NAN); rejects(p,FILE_BYTES); putf(p+40,2);
    putf(p+VERTEX_OFFSET,INFINITY); rejects(p,FILE_BYTES); putf(p+VERTEX_OFFSET,1);
    put32(p+VERTEX_OFFSET+36,1); rejects(p,FILE_BYTES); put32(p+VERTEX_OFFSET+36,0);
    put32(p+BATCH_OFFSET,3); rejects(p,FILE_BYTES); put32(p+BATCH_OFFSET,0);
    put32(p+BATCH_OFFSET+4,6); rejects(p,FILE_BYTES); put32(p+BATCH_OFFSET+4,3);
    put32(p+BATCH_OFFSET+8,1); rejects(p,FILE_BYTES); put32(p+BATCH_OFFSET+8,0);
    put32(p+BATCH_OFFSET+88,8); rejects(p,FILE_BYTES); put32(p+BATCH_OFFSET+88,7);
    put32(p+TEXTURE_OFFSET+8,UINT32_MAX); rejects(p,FILE_BYTES); put32(p+TEXTURE_OFFSET+8,PIXEL_OFFSET);
    put32(p+TEXTURE_OFFSET+12,257); rejects(p,FILE_BYTES); put32(p+TEXTURE_OFFSET+12,256);
    put32(p+CLIP_OFFSET,1); rejects(p,FILE_BYTES); put32(p+CLIP_OFFSET,0);
    put32(p+LIGHT_OFFSET+4,1); rejects(p,FILE_BYTES); put32(p+LIGHT_OFFSET+4,0);
    putf(p+FRAME_OFFSET+64,1.1f); rejects(p,FILE_BYTES); putf(p+FRAME_OFFSET+64,.25f);
    putf(p+FRAME_OFFSET+68,.5f); rejects(p,FILE_BYTES); putf(p+FRAME_OFFSET+68,1);
    putf(p+FRAME_OFFSET+96+32,-1); rejects(p,FILE_BYTES); putf(p+FRAME_OFFSET+96+32,.1f);
    p[127]=1; rejects(p,FILE_BYTES); p[127]=0; /* byte 112 is the texture encoding */
    /* Tiny near-uniform authored scales remain invertible at intermediate
     * samples, including a sign change between adjacent valid endpoints. */
    for(i=0;i<3;++i){
        unsigned char *frame=p+FRAME_OFFSET+i*FRAME_BYTES;
        putf(frame,1.32e-4f);putf(frame+20,1.32e-4f);putf(frame+40,1.32e-4f);
    }
    putf(p+FRAME_OFFSET+FRAME_BYTES,-.0428f);
    putf(p+FRAME_OFFSET+FRAME_BYTES+20,-.0428f);
    putf(p+FRAME_OFFSET+FRAME_BYTES+40,-.0428f);
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(c3_title_animate(&title,0,.00102233885f));
    assert(isfinite(title.draw_vertices[0].normal[0]));
    near_value(title.draw_vertices[0].normal[0],.70710678f);
    near_value(title.draw_vertices[0].normal[1],.70710678f);
    assert(title.pose.matrices[0][0]>0&&title.pose.matrices[0][0]<1e-4f);
    c3_title_close(&title);
    free(p);p=fixture();
    /* Endpoint matrices can be invertible while their interpolation is not.
     * Reject that sample atomically, without a NaN normal or partial pose. */
    putf(p+FRAME_OFFSET+FRAME_BYTES,-2); putf(p+FRAME_OFFSET+FRAME_BYTES+20,-1);
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(!c3_title_animate(&title,0,.25f));
    near_value(title.draw_vertices[0].pos[0],2);
    assert(title.active_frame==0);
    c3_title_close(&title);
    free(p); p=fixture();
    put32(p+CLIP_OFFSET+8,0); /* A non-looping clip holds its final endpoint. */
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(c3_title_animate(&title,0,FLT_MAX));
    assert(title.active_frame==2);
    c3_title_close(&title);
    assert(!c3_title_load(&title,"missing-title.c3t",error,sizeof error));
    free(p);
    p=fixture();
    /* A source accessory can collapse at a hidden sample. Keep its exact
     * zero affine so the preceding visible interval shrinks toward zero. */
    putf(p+FRAME_OFFSET+FRAME_BYTES,0);
    putf(p+FRAME_OFFSET+FRAME_BYTES+20,0);
    putf(p+FRAME_OFFSET+FRAME_BYTES+40,0);
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(c3_title_animate(&title,0,.25f));
    assert(title.pose.materials[0].visible==1);
    near_value(title.draw_vertices[0].pos[0],3);
    near_value(title.draw_vertices[0].pos[1],1);
    near_value(title.draw_vertices[0].pos[2],1.5f);
    assert(c3_title_animate(&title,0,.5f));
    assert(title.pose.materials[0].visible==0);
    near_value(title.pose.matrices[0][0],0);
    assert(isfinite(title.draw_vertices[0].normal[0]));
    c3_title_close(&title);
    /* The same collapsed bone must fail if any consuming batch is visible. */
    putf(p+FRAME_OFFSET+FRAME_BYTES+68,1);
    rejects(p,FILE_BYTES);
    free(p);
    extended_checks();
    actor_clip_checks();
    puts("Native title bounds, ownership, animation and normal checks passed");
    return 0;
}
