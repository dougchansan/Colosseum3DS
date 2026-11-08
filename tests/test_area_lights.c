#include "colosseum3ds/area_lights.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void put(unsigned char *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8u*i)); }

int main(void)
{
    unsigned char data[88]={0};
    memcpy(data,"C3L1",4);put(data+4,1);put(data+8,sizeof data);put(data+12,64);
    put(data+16,2);put(data+20,3);put(data+24,0x40000000u);put(data+28,1);
    for(unsigned frame=0;frame<3;++frame)
        for(unsigned i=0;i<8;++i)data[64+frame*8u+i]=(unsigned char)(frame*80u+i);
    C3AreaLights track={0};C3Title model={0};char error[100];
    model.light_count=2;model.lights[0].pos[0]=123.0f;
    assert(c3_area_lights_parse(&track,data,sizeof data,error,sizeof error));
    assert(!error[0]);
    assert(c3_area_lights_apply(&track,.25f,&model));
    assert(model.lights[0].rgba[0]==0.0f);
    assert(c3_area_lights_apply(&track,.5f,&model));
    assert(fabsf(model.lights[0].rgba[0]-80.0f/255.0f)<1e-6f);
    assert(c3_area_lights_apply(&track,1.0f,&model));
    assert(model.lights[0].rgba[0]==0.0f&&model.lights[0].pos[0]==123.0f);
    assert(c3_area_lights_apply(&track,-FLT_MAX,&model));
    assert(c3_area_lights_apply(&track,FLT_MAX,&model));
    assert(!c3_area_lights_apply(&track,NAN,&model));
    assert(!c3_area_lights_apply(&track,INFINITY,&model));
    model.light_count=1;assert(!c3_area_lights_apply(&track,0,&model));model.light_count=2;
    for(unsigned i=0;i<sizeof data;++i) {
        unsigned char *owned=track.colors;
        assert(!c3_area_lights_parse(&track,data,i,error,sizeof error));
        assert(track.colors==owned);
    }
    put(data+28,0);assert(c3_area_lights_parse(&track,data,sizeof data,error,sizeof error));
    assert(c3_area_lights_apply(&track,FLT_MAX,&model));
    assert(fabsf(model.lights[1].rgba[3]-167.0f/255.0f)<1e-6f);
    const unsigned offsets[]={16,20,24,28};
    const uint32_t values[]={9,4097,0x7fc00000u,2};
    for(unsigned i=0;i<4;++i) {
        unsigned char bad[88];memcpy(bad,data,sizeof bad);put(bad+offsets[i],values[i]);
        assert(!c3_area_lights_parse(&track,bad,sizeof bad,error,sizeof error));
    }
    c3_area_lights_close(&track);c3_area_lights_close(&track);
    assert(!track.colors&&!track.light_count);
    puts("Area light bounds, quantized samples, loop and ownership checks passed");
    return 0;
}
