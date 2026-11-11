#include "colosseum3ds/area_camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t word(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static float real(const unsigned char *p)
{ uint32_t v=word(p);float f;memcpy(&f,&v,sizeof f);return f; }
static int fail(char *error,size_t capacity,const char *message)
{ if(error&&capacity)snprintf(error,capacity,"%s",message);return 0; }
static int valid(const float *v)
{
    for(unsigned i=0;i<11;++i)if(!isfinite(v[i])||fabsf(v[i])>1000000.0f)return 0;
    double length=0;
    for(unsigned i=0;i<3;++i){double d=(double)v[i]-v[i+3u];length+=d*d;}
    return length>1e-16&&v[7]>1.0f&&v[7]<179.0f&&v[8]>0&&v[9]>v[8]&&v[10]>=.1f&&v[10]<=10.0f;
}
void c3_area_camera_close(C3AreaCamera *track)
{ if(track){free(track->frames);memset(track,0,sizeof(*track));} }

int c3_area_camera_parse(C3AreaCamera *track,const void *data,size_t bytes,char *error,size_t capacity)
{
    const unsigned char *p=data;C3AreaCamera next={0};
    if(!track||!p||bytes<64u||bytes>C3_AREA_CAMERA_MAX_BYTES||memcmp(p,"C3C1",4)||
       word(p+4)!=1u||word(p+8)!=bytes||word(p+12)!=64u||(word(p+28)&&(word(p+28)&0xffffu)!=0x1800u))
        return fail(error,capacity,"Invalid area camera header.");
    next.frame_count=word(p+16);next.fps=real(p+20);next.loop=word(p+24);next.resource=word(p+28);
    if(!next.frame_count||next.frame_count>C3_AREA_CAMERA_MAX_FRAMES||next.loop>1u||
       !isfinite(next.fps)||next.fps<1||next.fps>120||64u+(size_t)next.frame_count*44u!=bytes)
        return fail(error,capacity,"Invalid area camera extent or clock.");
    next.frames=malloc((size_t)next.frame_count*11u*sizeof(float));
    if(!next.frames)return fail(error,capacity,"Area camera allocation failed.");
    for(uint32_t frame=0;frame<next.frame_count;++frame) {
        float *v=next.frames+frame*11u;
        for(unsigned i=0;i<11;++i)v[i]=real(p+64u+frame*44u+i*4u);
        if(!valid(v)){c3_area_camera_close(&next);return fail(error,capacity,"Invalid area camera sample.");}
    }
    c3_area_camera_close(track);*track=next;
    if(error&&capacity)error[0]=0;
    return 1;
}

int c3_area_camera_load(C3AreaCamera *track,const char *path,char *error,size_t capacity)
{
    if(!track||!path)return fail(error,capacity,"Invalid area camera arguments.");
    FILE *file=fopen(path,"rb");
    if(!file)return fail(error,capacity,"Prepared area camera was not found.");
    long length;
    if(fseek(file,0,SEEK_END)||(length=ftell(file))<64||
       (unsigned long)length>C3_AREA_CAMERA_MAX_BYTES||fseek(file,0,SEEK_SET)) {
        fclose(file);return fail(error,capacity,"Invalid area camera file size.");
    }
    unsigned char *data=malloc((size_t)length);
    if(!data){fclose(file);return fail(error,capacity,"Area camera read allocation failed.");}
    int ok=fread(data,1,(size_t)length,file)==(size_t)length&&!ferror(file);
    if(fclose(file))ok=0;
    if(ok)ok=c3_area_camera_parse(track,data,(size_t)length,error,capacity);
    else fail(error,capacity,"Area camera read failed.");
    free(data);return ok;
}

int c3_area_camera_sample(const C3AreaCamera *track,float seconds,C3TitleCamera *camera)
{
    if(!track||!track->frames||!camera||!track->frame_count||!isfinite(seconds))return 0;
    double frame=seconds>0?(double)seconds*track->fps:0.0;
    if(track->frame_count==1u)frame=0;
    else if(track->loop)frame=fmod(frame,(double)(track->frame_count-1u));
    else if(frame>track->frame_count-1u)frame=track->frame_count-1u;
    uint32_t first=(uint32_t)frame,last=first+1u;
    if(last>=track->frame_count)last=first;
    float fraction=(float)(frame-first),values[11];
    for(unsigned i=0;i<11;++i) {
        float start=track->frames[first*11u+i],end=track->frames[last*11u+i];
        values[i]=start+(end-start)*fraction;
    }
    if(!valid(values))return 0;
    memcpy(camera->eye,values,3u*sizeof(float));memcpy(camera->target,values+3,3u*sizeof(float));
    camera->roll=values[6];camera->fov_y_degrees=values[7];camera->near_plane=values[8];
    camera->far_plane=values[9];camera->aspect=values[10];
    return 1;
}
