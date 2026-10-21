#include "colosseum3ds/model.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t n)
{ for (unsigned i=0;i<4;i++) p[i]=(unsigned char)(n>>(i*8)); }
static void putf(unsigned char *p, float f)
{ uint32_t n; memcpy(&n,&f,4); put32(p,n); }
static unsigned char *fixture(size_t *bytes)
{
    const unsigned vertices=66, bo=128+66*32, to=bo+16, mo=to+24, po=mo+2*12*4;
    unsigned char *p;
    *bytes=po+8*8*4; p=calloc(1,*bytes); assert(p);
    memcpy(p,"C3M1",4); put32(p+4,1); put32(p+8,(uint32_t)*bytes);
    put32(p+12,1); put32(p+16,vertices); put32(p+20,1); put32(p+24,1); put32(p+28,2); putf(p+32,1);
    put32(p+36,128); put32(p+40,bo); put32(p+44,to); put32(p+48,mo); put32(p+52,po);
    for (unsigned i=0;i<3;i++) { putf(p+56+i*4,-10); putf(p+68+i*4,10); }
    for (unsigned i=0;i<vertices;i++) {
        unsigned char *v=p+128+i*32;
        putf(v,1.25f); putf(v+4,2); putf(v+8,-3); putf(v+12,0.25f); putf(v+16,0.75f);
        v[24]=11; v[25]=22; v[26]=33; v[27]=255;
    }
    put32(p+bo+4,vertices); put32(p+to,8); put32(p+to+4,8); put32(p+to+8,po); put32(p+to+12,256);
    for (unsigned frame=0;frame<2;frame++) {
        unsigned char *mat=p+mo+frame*48;
        putf(mat,1); putf(mat+20,1); putf(mat+40,1); putf(mat+12,frame*4.0f);
    }
    memset(p+po,255,256); return p;
}
static void reject(unsigned char *p,size_t bytes)
{
    C3Model m={0}; char error[80];
    assert(c3_model_parse(&m,p,bytes,error,sizeof(error))!=0);
    assert(error[0]); assert(!m.file_data && !m.vertices && !m.draw_vertices);
    c3_model_close(&m);
}
int main(void)
{
    size_t bytes; unsigned char *p=fixture(&bytes); C3Model m={0}; char error[80];
    assert(c3_model_parse(&m,p,bytes,error,sizeof(error))==0);
    assert(m.vertex_count==66 && m.vertices[65].pos[0]==1.25f);
    assert(m.draw_vertices[65].x==1.25f && m.draw_vertices[65].rgba[1]==22);
    assert(m.textures[0].pixels>=m.file_data && m.resident_bytes>bytes && m.peak_load_bytes==m.resident_bytes+bytes);
    {
        unsigned char *owned=m.file_data;
        assert(c3_model_parse(&m,owned,bytes,error,sizeof(error))!=0);
        assert(m.file_data==owned && m.vertex_count==66);
        assert(c3_model_parse(&m,owned+1,bytes-1,error,sizeof(error))!=0);
        assert(m.file_data==owned && m.draw_vertices[65].x==1.25f);
    }
    p[128]=0; /* Parsing owns an independent copy. */
    assert(m.vertices[0].pos[0]==1.25f);
    c3_model_animate(&m,0.5f); assert(fabsf(m.draw_vertices[65].x-3.25f)<0.0001f);
    assert(m.draw_vertices[65].y==2 && m.draw_vertices[65].z==-3 && m.draw_vertices[65].u==0.25f);
    c3_model_animate(&m,1.5f); assert(fabsf(m.draw_vertices[65].x-3.25f)<0.0001f);
    c3_model_animate(&m,-0.5f); assert(fabsf(m.draw_vertices[65].x-3.25f)<0.0001f);
    c3_model_close(&m); assert(!m.file_data && !m.vertex_count && !m.resident_bytes);
    for (size_t cut=0;cut<bytes;cut++) reject(p,cut);
    reject(p,16u*1024u*1024u+1);
    {
        const unsigned offsets[]={12,16,20,24,28};
        const uint32_t original[]={1,66,1,1,2};
        const uint32_t excessive[]={129,8193,33,17,241};
        for (unsigned i=0;i<5;i++) {
            put32(p+offsets[i],0); reject(p,bytes);
            put32(p+offsets[i],excessive[i]); reject(p,bytes);
            put32(p+offsets[i],original[i]);
        }
    }
    put32(p+16,UINT32_MAX); reject(p,bytes); put32(p+16,66);
    put32(p+36,UINT32_MAX); reject(p,bytes); put32(p+36,128);
    put32(p+128+20,1); reject(p,bytes); put32(p+128+20,0);
    put32(p+128+28,1); reject(p,bytes); put32(p+128+28,0);
    put32(p+32,0x7f800000u); reject(p,bytes); putf(p+32,1);
    put32(p+128,0x7fc00000u); reject(p,bytes); putf(p+128,1.25f);
    putf(p+128,FLT_MAX); reject(p,bytes); putf(p+128,1.25f);
    putf(p+128+12,1000001); reject(p,bytes); putf(p+128+12,0.25f);
    putf(p+56,-10001); reject(p,bytes); putf(p+56,-10);
    put32(p+128+66*32+4,65); reject(p,bytes); put32(p+128+66*32+4,66);
    put32(p+128+66*32+16+8,UINT32_MAX); reject(p,bytes);
    free(p); p=fixture(&bytes);
    put32(p+128+66*32+16,7); reject(p,bytes);
    free(p); p=fixture(&bytes);
    put32(p+128+66*32+16+24,0x7fc00000u); reject(p,bytes);
    free(p); p=fixture(&bytes);
    putf(p+128+66*32+16+24,FLT_MAX); reject(p,bytes);
    free(p); p=fixture(&bytes);
    p[112]=1; reject(p,bytes); p[112]=0;
    put32(p+128+66*32+12,4); reject(p,bytes); put32(p+128+66*32+12,0);
    put32(p+128+66*32+16+12,255); reject(p,bytes);
    free(p);
    assert(c3_model_load(&m,"this-model-does-not-exist.c3m",error,sizeof(error))!=0);
    puts("Native model validation and animation checks passed"); return 0;
}
