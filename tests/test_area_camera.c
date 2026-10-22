#include "colosseum3ds/area_camera.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void put(unsigned char *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8u*i)); }
static void putf(unsigned char *p,float v)
{ uint32_t bits;memcpy(&bits,&v,4);put(p,bits); }
int main(void)
{
    unsigned char data[152]={0};
    memcpy(data,"C3C1",4);put(data+4,1);put(data+8,sizeof data);put(data+12,64);put(data+16,2);putf(data+20,2);
    const float values[11]={0,1,5,0,1,0,0,30,.01f,100,1.18f};
    for(unsigned frame=0;frame<2;++frame)
        for(unsigned i=0;i<11;++i)putf(data+64+frame*44u+i*4u,values[i]);
    putf(data+64+44,4);putf(data+64+44+12,4);
    C3AreaCamera track={0};C3TitleCamera camera={0};char error[100];
    assert(c3_area_camera_parse(&track,data,sizeof data,error,sizeof error));
    assert(c3_area_camera_sample(&track,.25f,&camera));assert(camera.eye[0]==2&&camera.target[0]==2);
    assert(c3_area_camera_sample(&track,FLT_MAX,&camera));assert(camera.eye[0]==4);
    assert(c3_area_camera_sample(&track,-FLT_MAX,&camera));assert(camera.eye[0]==0);
    C3TitleCamera before=camera;
    assert(!c3_area_camera_sample(&track,NAN,&camera));assert(!memcmp(&camera,&before,sizeof camera));
    for(unsigned i=0;i<sizeof data;++i) {
        float *owned=track.frames;
        assert(!c3_area_camera_parse(&track,data,i,error,sizeof error));assert(track.frames==owned);
    }
    put(data+24,1);assert(c3_area_camera_parse(&track,data,sizeof data,error,sizeof error));
    assert(c3_area_camera_sample(&track,.75f,&camera));assert(camera.eye[0]==2);
    putf(data+64+8,0);assert(!c3_area_camera_parse(&track,data,sizeof data,error,sizeof error));putf(data+64+8,5);
    putf(data+64+32,NAN);assert(!c3_area_camera_parse(&track,data,sizeof data,error,sizeof error));putf(data+64+32,.01f);
    put(data+16,4097);assert(!c3_area_camera_parse(&track,data,sizeof data,error,sizeof error));
    c3_area_camera_close(&track);c3_area_camera_close(&track);
    puts("Area camera bounds, interpolation, loop and ownership checks passed");
    return 0;
}
