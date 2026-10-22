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

int main(void)
{
    unsigned char *p=fixture();
    C3Title title={0};
    char error[80];
    size_t i;
    assert(c3_title_parse(&title,p,FILE_BYTES,error,sizeof error));
    assert(!error[0] && title.bone_count==1 && title.vertex_count==3 && title.frame_count==3);
    assert(title.textures[0].pixels>=title.file_data && title.resident_bytes>FILE_BYTES);
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
        const uint32_t excessive[]={65,32769,65,33,4,4097,9};
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
    p[112]=1; rejects(p,FILE_BYTES); p[112]=0;
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
    puts("Native title bounds, ownership, animation and normal checks passed");
    return 0;
}
