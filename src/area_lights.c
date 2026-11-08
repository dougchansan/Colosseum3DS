#include "colosseum3ds/area_lights.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t word(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static float real(const unsigned char *p)
{ uint32_t v=word(p); float f; memcpy(&f,&v,sizeof f); return f; }
static int fail(char *error,size_t capacity,const char *message)
{ if(error&&capacity)snprintf(error,capacity,"%s",message);return 0; }

void c3_area_lights_close(C3AreaLights *track)
{
    if(!track)return;
    free(track->colors);memset(track,0,sizeof(*track));
}

int c3_area_lights_parse(C3AreaLights *track,const void *data,size_t bytes,char *error,size_t capacity)
{
    const unsigned char *p=data;
    C3AreaLights next={0};
    if(!track||!p||bytes<64u||bytes>C3_AREA_LIGHTS_MAX_BYTES||memcmp(p,"C3L1",4)||
       word(p+4)!=1u||word(p+8)!=bytes||word(p+12)!=64u)
        return fail(error,capacity,"Invalid area light header.");
    next.light_count=word(p+16);next.frame_count=word(p+20);next.fps=real(p+24);next.loop=word(p+28);
    if(!next.light_count||next.light_count>C3_TITLE_MAX_LIGHTS||!next.frame_count||
       next.frame_count>C3_AREA_LIGHTS_MAX_FRAMES||!isfinite(next.fps)||next.fps<1.0f||
       next.fps>120.0f||next.loop>1u||64u+(size_t)next.light_count*next.frame_count*4u!=bytes)
        return fail(error,capacity,"Invalid area light sample extent or timing.");
    next.colors=malloc(bytes-64u);
    if(!next.colors)return fail(error,capacity,"Area light allocation failed.");
    memcpy(next.colors,p+64u,bytes-64u);
    c3_area_lights_close(track);*track=next;
    if(error&&capacity)error[0]=0;
    return 1;
}

int c3_area_lights_load(C3AreaLights *track,const char *path,char *error,size_t capacity)
{
    if(!track||!path)return fail(error,capacity,"Invalid area light arguments.");
    FILE *file=fopen(path,"rb");
    if(!file)return fail(error,capacity,"Prepared area light samples were not found.");
    long length;
    if(fseek(file,0,SEEK_END)||(length=ftell(file))<64||
       (unsigned long)length>C3_AREA_LIGHTS_MAX_BYTES||fseek(file,0,SEEK_SET)) {
        fclose(file);return fail(error,capacity,"Invalid area light file size.");
    }
    unsigned char *data=malloc((size_t)length);
    if(!data){fclose(file);return fail(error,capacity,"Area light read allocation failed.");}
    int ok=fread(data,1,(size_t)length,file)==(size_t)length&&!ferror(file);
    if(fclose(file))ok=0;
    if(ok)ok=c3_area_lights_parse(track,data,(size_t)length,error,capacity);
    else fail(error,capacity,"Area light read failed.");
    free(data);return ok;
}

int c3_area_lights_apply(const C3AreaLights *track,float seconds,C3Title *model)
{
    if(!track||!track->colors||!model||!isfinite(seconds)||!track->frame_count||
       model->light_count!=track->light_count||track->light_count>C3_TITLE_MAX_LIGHTS)return 0;
    double frame=seconds>0.0f?(double)seconds*track->fps:0.0;
    if(track->frame_count==1u)frame=0.0;
    else if(track->loop)frame=fmod(frame,(double)(track->frame_count-1u));
    else if(frame>track->frame_count-1u)frame=track->frame_count-1u;
    const unsigned char *colors=track->colors+(size_t)(uint32_t)frame*track->light_count*4u;
    for(uint32_t i=0;i<track->light_count;++i)
        for(unsigned j=0;j<4;++j)model->lights[i].rgba[j]=colors[i*4u+j]/255.0f;
    return 1;
}
