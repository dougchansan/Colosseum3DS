#include "battle_stage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const unsigned char *p){uint32_t u=word(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void release(C3BattleStage *s){for(uint32_t i=s->count;i>0;--i)c3_title_close(&s->models[i-1]);s->count=0;}
int c3_battle_stage_load(C3BattleStage *s,const char *root,const uint16_t hp[4],FILE *trace,char *error,size_t cap)
{
    unsigned char data[128+128*C3_BATTLE_STAGE_MODELS];char path[512];
    if(!s||!root||!hp)return fail(error,cap,"Invalid battle stage arguments.");
    memset(s,0,sizeof *s);s->trace=trace;
    if(snprintf(path,sizeof path,"%s/scene.c3v",root)>=(int)sizeof path)return fail(error,cap,"Battle path too long.");
    FILE *f=fopen(path,"rb");if(!f)return fail(error,cap,"Missing prepared battle scene.c3v; prepare source assets.");
    size_t bytes=fread(data,1,sizeof data,f);int extra=fgetc(f);int io=ferror(f);fclose(f);
    if(io||extra!=EOF||bytes<128||memcmp(data,"C3BV",4)||word(data+4)!=1||word(data+8)!=bytes||word(data+16)!=6)return fail(error,cap,"Invalid battle scene descriptor header.");
    uint32_t count=word(data+12),seen=0,arenas=0;
    if(!count||count>C3_BATTLE_STAGE_MODELS||bytes!=128u+128u*count)return fail(error,cap,"Battle descriptor record bounds.");
    for(unsigned i=20;i<32;++i)if(data[i])return fail(error,cap,"Battle descriptor reserved header.");
    for(unsigned i=80;i<128;++i)if(data[i])return fail(error,cap,"Battle descriptor reserved camera.");
    float camera[12];for(unsigned i=0;i<12;++i){camera[i]=real(data+32+4*i);if(!isfinite(camera[i]))return fail(error,cap,"Nonfinite battle camera.");}
    memcpy(s->camera.eye,camera,12);memcpy(s->camera.target,camera+3,12);s->camera.roll=camera[6];s->camera.fov_y_degrees=camera[7];s->camera.near_plane=camera[8];s->camera.far_plane=camera[9];s->camera.aspect=camera[10];
    float view=0;for(unsigned i=0;i<3;++i){float d=camera[i]-camera[3+i];view+=d*d;}
    if(!isfinite(view)||view<=0)return fail(error,cap,"Degenerate battle camera direction.");
    if(camera[7]<=0||camera[7]>=180||camera[8]<=0||camera[9]<=camera[8]||camera[10]<=0||camera[11]!=0)return fail(error,cap,"Invalid battle camera frustum.");
    for(uint32_t i=0;i<count;++i){const unsigned char *r=data+128+128*i;uint32_t slot=word(r+48),shared=word(r+52);
        if(!memchr(r,0,48)||!r[0]||strchr((const char *)r,'/')||strchr((const char *)r,'\\')||strstr((const char *)r,"..")||(slot>=6&&slot!=UINT32_MAX))goto invalid;
        for(size_t j=strlen((const char *)r)+1;j<48;++j)if(r[j])goto invalid;
        if(slot<6){if(seen&(1u<<slot))goto invalid;seen|=1u<<slot;}else ++arenas;
        for(unsigned j=116;j<128;++j)if(r[j])goto invalid;
        s->slots[i]=slot;
        for(unsigned j=0;j<5;++j){s->transforms[i][j]=real(r+96+4*j);if(!isfinite(s->transforms[i][j]))goto invalid;}
        if(s->transforms[i][4]<=0)goto invalid;
        s->count=i+1;
        if(shared!=UINT32_MAX){if(shared>=i||s->models[shared].borrowed_storage)goto invalid;
            if(!c3_title_instance(&s->models[i],&s->models[shared],error,cap))goto bad;
        }else{if(snprintf(path,sizeof path,"%s/%s",root,(const char *)r)>=(int)sizeof path)goto invalid;
            if(!c3_title_load(&s->models[i],path,error,cap))goto bad;}
        for(unsigned j=0;j<9;++j){uint32_t clip=word(r+60+4*j);if(clip!=UINT32_MAX&&clip>=s->models[i].clip_count)goto invalid;s->roles[i][j]=clip;}
        s->clips[i]=word(r+56);if(s->clips[i]>=s->models[i].clip_count||s->roles[i][0]!=s->clips[i])goto invalid;
        s->draw[i]=&s->models[i];
    }
    if(seen!=63||!arenas)goto invalid;
    c3_battle_presentation_init(&s->presentation,hp);
    if(c3_stage_renderer_init(s->draw,s->count,error,cap)!=C3_RENDER_OK)goto bad;
    s->initialized=1;
    if(trace){fprintf(trace,"battle_stage_loaded models=%lu actors=6 camera=source descriptor_version=1\n",(unsigned long)s->count);fflush(trace);}return 1;
invalid:fail(error,cap,"Invalid battle model/clip/transform descriptor.");
bad:release(s);return 0;
}
static void event(const C3BattlePreviewEvent *e,int finished,void *user)
{
    C3BattleStage *s=user;if(finished)return;
    unsigned role=0,slot=e->actor;
    switch(e->kind){case C3_PREVIEW_EVENT_SEND_OUT:role=5;break;
    case C3_PREVIEW_EVENT_MOVE:
        /* Qualified source move WZX roots select these PKX animation entries.
         * This samples actor motion only: the original WZX effects remain unsupported. */
        switch(e->move){
        case 44:case 216:case 33:case 165:role=1;break; /* PKX entry2: Bite, Tackle. */
        case 290:case 289:case 93:case 270:case 45:role=2;break; /* PKX entry1. */
        case 115:case 269:case 39:role=0;break; /* Source explicitly keeps idle entry0. */
        default:role=6;break; /* Unqualified move binding fails explicitly. */
        }break;
    case C3_PREVIEW_EVENT_HP:role=3;slot=e->target;break;
    case C3_PREVIEW_EVENT_FAINT:role=4;slot=e->target;break;
    case C3_PREVIEW_EVENT_OUTCOME:
        if(e->outcome==C3_PREVIEW_DRAW)return;
        role=e->outcome==C3_PREVIEW_PLAYER_WON?7:8;slot=4;break;
    default:return;}
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==slot||(e->kind==C3_PREVIEW_EVENT_OUTCOME&&s->slots[i]==5)||(e->kind==C3_PREVIEW_EVENT_SEND_OUT&&s->slots[i]<4&&(e->target_mask&(1u<<s->slots[i])))){
        unsigned actor_role=e->kind==C3_PREVIEW_EVENT_OUTCOME&&s->slots[i]==5?(role==7?8:7):role;
        if(s->roles[i][actor_role]==UINT32_MAX){snprintf(s->error,sizeof s->error,"Unsupported source clip: actor %u role %u.",(unsigned)s->slots[i],actor_role);return;}
        s->clips[i]=s->roles[i][actor_role];s->clocks[i]=0;s->fresh[i]=1;
        if(e->kind==C3_PREVIEW_EVENT_SEND_OUT){
            s->presentation.visible[s->slots[i]]=1;
            if(s->trace){fprintf(s->trace,"battle_source_sendout actor=%lu pose=source_idle appearance_effect=unsupported cameras=unsupported audio=unsupported\n",(unsigned long)s->slots[i]);fflush(s->trace);}
        }
        C3Title *m=&s->models[i];
        if(actor_role==0||(actor_role==5&&s->clips[i]==s->roles[i][0]))continue;
        if(m->clips[s->clips[i]].loop){snprintf(s->error,sizeof s->error,"Action role %u is looping; source completion unavailable.",actor_role);return;}
        c3_battle_presentation_wait(&s->presentation,1);
        if(e->kind==C3_PREVIEW_EVENT_MOVE||e->kind==C3_PREVIEW_EVENT_FAINT||e->kind==C3_PREVIEW_EVENT_OUTCOME)
            s->presentation.duration_ms=0; /* Authored clip completion owns action timing. */
    }
}
int c3_battle_stage_submit(C3BattleStage *s,const C3BattlePreviewEvent *e,uint32_t count)
{
    if(!c3_battle_presentation_submit(&s->presentation,e,count))return 0;
    for(uint32_t i=0;i<count;++i)if(e[i].kind==C3_PREVIEW_EVENT_INTRO){memset(s->presentation.visible,0,sizeof s->presentation.visible);break;}
    return 1;
}
int c3_battle_stage_step(C3BattleStage *s,uint32_t ms,char *error,size_t cap)
{
    if(ms>100)ms=100; /* Preserve event visibility after slow frames; never fast-forward clips. */
    if(s->presentation.waiting){
        int active=0;
        for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];
            if(s->clips[i]!=s->roles[i][0]&&s->clocks[i]*m->fps<(float)m->clips[s->clips[i]].frame_count)active=1;
        }
        if(!active)c3_battle_presentation_wait(&s->presentation,0);
    }
    if(!c3_battle_presentation_step(&s->presentation,ms,event,s))return fail(error,cap,"Battle presentation event timeline failed.");
    if(s->error[0])return fail(error,cap,s->error);
    for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];if(!s->fresh[i])s->clocks[i]+=(float)ms/1000.0f;s->fresh[i]=0;
        C3TitleClip *clip=&m->clips[s->clips[i]];
        if(!clip->loop&&s->clocks[i]*m->fps>=(float)clip->frame_count&&s->clips[i]!=s->roles[i][4]){s->clips[i]=s->roles[i][0];s->clocks[i]=0;}
        if(!c3_title_animate(m,s->clips[i],s->clocks[i]))return fail(error,cap,"Battle clip sampling failed.");
        float *t=s->transforms[i],co=cosf(t[3]),si=sinf(t[3]);
        int visible=s->slots[i]>=4||s->presentation.visible[s->slots[i]];
        for(uint32_t v=0;v<m->vertex_count;++v){C3TitleDrawVertex *d=&m->draw_vertices[v];float x=d->pos[0]*t[4],z=d->pos[2]*t[4],nx=d->normal[0],nz=d->normal[2];
            d->pos[0]=co*x+si*z+t[0];d->pos[1]=d->pos[1]*t[4]+t[1];d->pos[2]=-si*x+co*z+t[2];d->normal[0]=co*nx+si*nz;d->normal[2]=-si*nx+co*nz;
            for(unsigned axis=0;axis<3;++axis)if(!isfinite(d->pos[axis])||!isfinite(d->normal[axis]))return fail(error,cap,"Battle actor transform overflow.");}
        if(!visible)for(uint32_t j=0;j<m->batch_count;++j)m->pose.materials[j].visible=0;
    }return 1;
}
int c3_battle_stage_draw(C3BattleStage *s,C3TitleRenderStats *stats,char *error,size_t cap){return c3_stage_renderer_draw(s->draw,s->count,&s->camera,1,stats,error,cap);}
int c3_battle_stage_close(C3BattleStage *s){if(s->initialized&&!c3_stage_renderer_try_shutdown())return 0;s->initialized=0;release(s);return 1;}
