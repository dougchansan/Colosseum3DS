#ifdef C3_REALGAM_STAGE_PROBE
/* Isolated verification of the source battle-floor assets. The bounded C3BV
 * parsing follows battle_general_stage.c's arena section only. */
#include "realgam_stage_probe.h"
#include "stage_renderer.h"
#include "render_recovery.h"
#include "n3ds_platform.h"
#include "colosseum3ds/battle3d_index.h"
#include <3ds.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STAGE_ROOT "sdmc:/3ds/Colosseum3DS/boot/battle3d"
#define SHOT_ROOT "sdmc:/3ds/Colosseum3DS/realgam-stages"
#define MAX_ARENAS 8u

static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const unsigned char *p)
{uint32_t bits=word(p);float f;memcpy(&f,&bits,4);return f;}
static int failed(char *error,size_t capacity,const char *reason)
{if(error&&capacity)snprintf(error,capacity,"%s",reason);return -1;}
static int read_descriptor(const char *path,unsigned char *data,size_t limit,size_t *size)
{
    FILE *file=fopen(path,"rb");if(!file)return 0;
    *size=fread(data,1,limit,file);
    int ok=!ferror(file)&&fgetc(file)==EOF;fclose(file);return ok;
}

static int probe_floor(const C3Battle3dIndex *index,unsigned floor,FILE *trace,char *error,size_t capacity)
{
    unsigned char data[128+128*MAX_ARENAS];size_t bytes=0,count=0;
    C3Title *models=calloc(MAX_ARENAS,sizeof *models);
    const C3Title *draw[MAX_ARENAS]={0};uint32_t clips[MAX_ARENAS]={0};
    float transforms[MAX_ARENAS][5];C3TitleCamera camera={0};
    char path[512],name[32];unsigned frames=0;uint32_t last_draws=0;
    int result=-1,renderer_attempted=0;uint64_t started=0;
    if(!models)return failed(error,capacity,"Stage probe allocation failed.");
    if(!c3_battle3d_floor(index,(uint16_t)floor,name)){
        failed(error,capacity,"Stage probe floor missing from index.");goto done;}
    if(snprintf(path,sizeof path,"%s/%s",STAGE_ROOT,name)>=(int)sizeof path||
       !read_descriptor(path,data,sizeof data,&bytes)){
        failed(error,capacity,"Stage probe descriptor missing or oversized.");goto done;}
    if(bytes<128||memcmp(data,"C3BV",4)||word(data+4)!=1||word(data+8)!=bytes||word(data+16))goto invalid;
    uint32_t arenas=word(data+12);
    if(!arenas||arenas>MAX_ARENAS||bytes!=128+128*arenas)goto invalid;
    float fields[12];for(unsigned j=0;j<12;++j){fields[j]=real(data+32+4*j);if(!isfinite(fields[j]))goto invalid;}
    memcpy(camera.eye,fields,12);memcpy(camera.target,fields+3,12);camera.roll=fields[6];
    camera.fov_y_degrees=fields[7];camera.near_plane=fields[8];camera.far_plane=fields[9];camera.aspect=fields[10];
    if(fields[7]<=0||fields[7]>=180||fields[8]<=0||fields[9]<=fields[8]||fields[10]<=0)goto invalid;
    for(unsigned i=0;i<arenas;++i){
        const unsigned char *row=data+128+128*i;
        uint32_t shared=word(row+52);
        if(!memchr(row,0,48)||!row[0]||strchr((const char *)row,'/')||
           strchr((const char *)row,'\\')||strstr((const char *)row,"..")||word(row+48)!=UINT32_MAX)goto invalid;
        for(unsigned j=0;j<5;++j){transforms[i][j]=real(row+96+4*j);if(!isfinite(transforms[i][j]))goto invalid;}
        if(transforms[i][4]<=0)goto invalid;
        if(shared!=UINT32_MAX){
            if(shared>=i||models[shared].borrowed_storage)goto invalid;
            if(!c3_title_instance(models+i,models+shared,error,capacity))goto done;
        }else{
            if(snprintf(path,sizeof path,"%s/%s",STAGE_ROOT,(const char *)row)>=(int)sizeof path)goto invalid;
            if(!c3_title_load(models+i,path,error,capacity))goto done;
        }
        count=i+1;draw[i]=models+i;clips[i]=word(row+56);
        if(clips[i]>=models[i].clip_count)goto invalid;
    }
    renderer_attempted=1;
    if(c3_stage_renderer_init(draw,count,error,capacity)!=C3_RENDER_OK)goto done;
    started=osGetTime();
    while(frames<60||osGetTime()-started<2000){
        if(!aptMainLoop()){failed(error,capacity,"Stage probe application exit.");goto done;}
        float seconds=(float)(osGetTime()-started)/1000.f;
        for(unsigned i=0;i<count;++i){
            float *t=transforms[i];models[i].retained=1;
            if(t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&t[4]==1)models[i].placement.enabled=0;
            else c3_title_place_yaw(models+i,t,cosf(t[3]),sinf(t[3]),t[4],0);
            if(!c3_title_animate(models+i,clips[i],seconds)){
                failed(error,capacity,"Stage probe clip sampling failed.");goto done;}
        }
        C3TitleRenderStats stats={0};int status=c3_stage_renderer_draw(draw,count,&camera,1.f,&stats,error,capacity);
        if(status==C3_RENDER_BUSY){
            C3RenderRecoveryResult recovered=c3_render_recover(trace,"realgam-stage-probe",frames,C3_RECOVER_WAIT);
            if(recovered==C3_RECOVERY_EXIT||recovered==C3_RECOVERY_DRAINED)goto done;
            continue;
        }
        if(status!=C3_RENDER_OK)goto done;
        ++frames;last_draws=stats.draw_calls;
    }
    snprintf(path,sizeof path,SHOT_ROOT "/floor%02u.bmp",floor);
    if(!last_draws||c3_n3ds_capture_top(path)!=0){failed(error,capacity,"Stage probe capture failed or frame empty.");goto done;}
    if(trace){fprintf(trace,"realgam_stage floor=%u status=pass models=%u frames=%u seconds=%.3f draws=%lu shot=%s\n",
        floor,(unsigned)count,frames,(double)(osGetTime()-started)/1000.,(unsigned long)last_draws,path);fflush(trace);}
    result=0;goto done;
invalid:
    failed(error,capacity,"Invalid bounded arena-only stage descriptor.");
done:
    if(renderer_attempted){
        C3RenderRecoveryResult recovered=c3_render_recover(trace,"realgam-stage-probe",frames,C3_RECOVER_DRAIN);
        if(recovered==C3_RECOVERY_EXIT)result=-1;
    }
    for(unsigned i=MAX_ARENAS;i>0;--i)c3_title_close(models+i-1);
    free(models);
    if(result&&trace){fprintf(trace,"realgam_stage floor=%u status=fail frames=%u error=%s\n",floor,frames,error?error:"");fflush(trace);}
    return result;
}

int c3_realgam_stage_probe(FILE *trace,char *error,size_t capacity)
{
    static const unsigned floors[]={27,28,29,30,31,32,38,47,48};
    C3Battle3dIndex index={0};unsigned passed=0;int result=0;
    if(error&&capacity)error[0]=0;
    mkdir(SHOT_ROOT,0777);
    if(!c3_battle3d_index_load(&index,STAGE_ROOT "/index.c3n",error,capacity))return -1;
    for(unsigned i=0;i<sizeof floors/sizeof floors[0];++i){
        if(probe_floor(&index,floors[i],trace,error,capacity))result=-1;
        else ++passed;
        if(!aptMainLoop()){result=-1;break;}
    }
    c3_battle3d_index_free(&index);
    if(trace){fprintf(trace,"realgam_stage_end requested=9 passed=%u\n",passed);fflush(trace);}
    return result;
}
#endif
