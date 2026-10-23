#include "battle_stage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "save_codec.h"
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
    /* Optional, exact-source attack slice. Absence preserves the accepted
     * actor-only build. A present but invalid descriptor is not ignored. */
    if(snprintf(path,sizeof path,"%s/bite.c3w",root)>=(int)sizeof path)goto invalid;
    errno=0;FILE *optional=fopen(path,"rb");
    if(optional){
        if(fclose(optional)||s->count>=C3_BATTLE_STAGE_MODELS)goto invalid;
        if(!c3_wzx_bite_load(&s->bite_program,path,error,cap))goto bad;
        if(snprintf(path,sizeof path,"%s/bite_attack.c3t",root)>=(int)sizeof path)goto invalid;
        uint32_t i=s->count++;s->bite_model=i;s->slots[i]=UINT32_MAX-1u;
        if(!c3_title_load(&s->models[i],path,error,cap))goto bad;
        C3Title *m=&s->models[i];uint8_t digest[20];
        if(m->file_bytes>UINT32_MAX||m->clip_count!=1||m->fps!=120||m->clips[0].loop||
           m->clips[0].frame_count!=2u*s->bite_program.model_frames+1u)goto invalid;
        c3_save_codec_sha1(digest,m->file_data,(uint32_t)m->file_bytes);
        if(memcmp(digest,s->bite_program.model_sha1,20)){
            fail(error,cap,"Bite effect pack differs from its prepared descriptor.");goto bad;
        }
        for(unsigned j=0;j<9;++j)s->roles[i][j]=0;
        s->draw[i]=m;s->transforms[i][4]=1;s->bite_available=1;
        for(uint32_t j=0;j<i;++j)if(s->slots[j]==0)s->bite_owner=j;
        if(s->roles[s->bite_owner][1]!=s->bite_program.owner_clip)goto invalid;
        if(trace){fprintf(trace,"wzx_bite_admitted model=1 floor_color=1 sound=unsupported damage_gpt1=unsupported camera=unsupported\n");fflush(trace);}
    }else if(errno!=ENOENT){fail(error,cap,"Cannot inspect optional Bite WZX pack.");goto bad;}
    c3_battle_presentation_init(&s->presentation,hp);
    if(c3_stage_renderer_init(s->draw,s->count,error,cap)!=C3_RENDER_OK)goto bad;
    s->initialized=1;
    for(uint32_t i=0;i<s->count;++i){
        const float *t=s->transforms[i];C3Title *m=&s->models[i];
        if(s->slots[i]==UINT32_MAX&&m->clip_count==1&&m->clips[0].frame_count==1&&
           t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&t[4]==1){
            s->immutable[i]=1;
            if(!c3_stage_renderer_set_static(i,true)){fail(error,cap,"Static arena admission failed.");return 0;}
        }
    }
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
    if(e->kind==C3_PREVIEW_EVENT_MOVE&&e->move==44&&e->actor==0&&s->bite_available){
        if(!c3_wzx_bite_begin(&s->bite,&s->bite_program)){
            snprintf(s->error,sizeof s->error,"Bite WZX initialization failed.");return;
        }
        const float *owner=s->transforms[s->bite_owner],*a=s->bite_program.anchor;
        float *t=s->transforms[s->bite_model],co=cosf(owner[3]),si=sinf(owner[3]);
        t[0]=owner[0]+owner[4]*(co*a[0]+si*a[2]);t[1]=owner[1]+owner[4]*a[1];
        t[2]=owner[2]+owner[4]*(-si*a[0]+co*a[2]);t[3]=owner[3];t[4]=owner[4];
        c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
        if(s->trace){fprintf(s->trace,"wzx_bite_start event=%lu start_tick=%lu stop_tick=%lu source_part=59 audio=unsupported\n",(unsigned long)e->sequence,(unsigned long)s->bite_program.model_start,(unsigned long)(s->bite_program.control_start+s->bite_program.control_delay));fflush(s->trace);}
    }
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
    if(!s||!s->initialized)return fail(error,cap,"Battle stage is not initialized.");
    /* The admitted WZX slice retains all elapsed time, including slow frames.
     * Other, still-development presentation paths retain their prior pacing. */
    if(s->bite.active){
        uint32_t before=s->bite.model_starts;
        if(!c3_wzx_bite_step(&s->bite,ms))return fail(error,cap,"Bite WZX clock failed.");
        if(s->trace&&s->bite.model_starts!=before){fprintf(s->trace,"wzx_bite_model_start tick=%lu sound85=unsupported\n",(unsigned long)s->bite_program.model_start);fflush(s->trace);}
        if(!s->bite.active){
            s->clips[s->bite_owner]=s->roles[s->bite_owner][0];s->clocks[s->bite_owner]=0;
            if(s->trace){fprintf(s->trace,"wzx_bite_stop tick=%lu model_hidden=1 floor_restored=1\n",(unsigned long)(s->bite_program.control_start+s->bite_program.control_delay));fflush(s->trace);}
        }
    }
    if(ms>100)ms=100;
    if(s->presentation.waiting){
        int active=s->bite.active;
        for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];
            if(s->bite_available&&i==s->bite_model)continue;
            if(s->clips[i]!=s->roles[i][0]&&s->clocks[i]*m->fps<(float)m->clips[s->clips[i]].frame_count)active=1;
        }
        if(!active)c3_battle_presentation_wait(&s->presentation,0);
    }
    if(!c3_battle_presentation_step(&s->presentation,ms,event,s))return fail(error,cap,"Battle presentation event timeline failed.");
    if(s->error[0])return fail(error,cap,s->error);
    for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];
        if(s->slots[i]==UINT32_MAX&&!c3_stage_renderer_set_modulation(i,s->bite.floor_enabled?s->bite.floor_rgb:NULL))
            return fail(error,cap,"Battle floor modulation failed.");
        if(s->immutable[i]&&s->sampled[i])continue;
        int effect=s->bite_available&&i==s->bite_model;
        if(effect)s->clocks[i]=s->bite.model_seconds;
        else if(s->bite.active&&i==s->bite_owner)s->clocks[i]=s->bite.owner_seconds;
        else if(!s->fresh[i])s->clocks[i]+=(float)ms/1000.0f;
        s->fresh[i]=0;
        C3TitleClip *clip=&m->clips[s->clips[i]];
        if(!clip->loop&&s->clocks[i]*m->fps>=(float)clip->frame_count&&s->clips[i]!=s->roles[i][4]){s->clips[i]=s->roles[i][0];s->clocks[i]=0;}
        if(!c3_title_animate(m,s->clips[i],s->clocks[i]))return fail(error,cap,"Battle clip sampling failed.");
        float *t=s->transforms[i],co=cosf(t[3]),si=sinf(t[3]);
        int visible=effect?s->bite.model_visible:(s->slots[i]>=4||s->presentation.visible[s->slots[i]]);
        int identity=t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&t[4]==1;
        if(!identity)for(uint32_t v=0;v<m->vertex_count;++v){C3TitleDrawVertex *d=&m->draw_vertices[v];float x=d->pos[0]*t[4],z=d->pos[2]*t[4],nx=d->normal[0],nz=d->normal[2];
            d->pos[0]=co*x+si*z+t[0];d->pos[1]=d->pos[1]*t[4]+t[1];d->pos[2]=-si*x+co*z+t[2];d->normal[0]=co*nx+si*nz;d->normal[2]=-si*nx+co*nz;
            for(unsigned axis=0;axis<3;++axis)if(!isfinite(d->pos[axis])||!isfinite(d->normal[axis]))return fail(error,cap,"Battle actor transform overflow.");}
        s->sampled[i]=1;
        if(!visible)for(uint32_t j=0;j<m->batch_count;++j)m->pose.materials[j].visible=0;
    }return 1;
}
int c3_battle_stage_draw(C3BattleStage *s,C3TitleRenderStats *stats,char *error,size_t cap){return c3_stage_renderer_draw(s->draw,s->count,&s->camera,1,stats,error,cap);}
int c3_battle_stage_close(C3BattleStage *s){c3_wzx_bite_cancel(&s->bite);if(s->initialized&&!c3_stage_renderer_try_shutdown())return 0;s->initialized=0;release(s);return 1;}
