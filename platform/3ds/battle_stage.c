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
static void confusion_cancel(C3BattleStage *s);
static void release(C3BattleStage *s){confusion_cancel(s);c3_wzx_confusion_sequence_close(&s->confusion_program);free(s->confusion_bytes[0]);s->confusion_bytes[0]=NULL;free(s->confusion_bytes[1]);s->confusion_bytes[1]=NULL;free(s->confusion_damage_particles);s->confusion_damage_particles=NULL;s->confusion_available=0;c3_wzx_reflect_cancel(&s->reflect);c3_wzx_reflect_program_close(&s->reflect_program);free(s->reflect_bytes);s->reflect_bytes=NULL;c3_battle_camera_cancel(&s->source_camera);c3_battle_camera_close(&s->camera_program);for(uint32_t i=s->count;i>0;--i)c3_title_close(&s->models[i-1]);s->count=0;free(s->particle_bytes);s->particle_bytes=NULL;}
static int stage_sound(C3BattleStage *s,uint32_t id,char *e,size_t n)
{
    if(!s->sound_cue)return 1;
    if(s->cue_serial==UINT32_MAX)return fail(e,n,"Audio cue identity exhausted.");
    return s->sound_cue(++s->cue_serial,id,e,n);
}
#include "battle_damage_effect.inc"
#include "battle_reflect_effect.inc"
#include "battle_wzx_confusion.inc"
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
        if(trace){fprintf(trace,"wzx_bite_admitted model=1 floor_color=1 sound_request=85 attack_binding=accepted_local_72_142_165\n");fflush(trace);}
    }else if(errno!=ENOENT){fail(error,cap,"Cannot inspect optional Bite WZX pack.");goto bad;}
    if(!damage_load(s,root,error,cap))goto bad;
    if(!reflect_load(s,root,error,cap)||!confusion_load(s,root,error,cap))goto bad;
    c3_battle_presentation_init(&s->presentation,hp);
    if(!c3_battle_presentation_wait_limit(&s->presentation,20000))goto invalid;
    if(c3_stage_renderer_init(s->draw,s->count,error,cap)!=C3_RENDER_OK)goto bad;
    s->initialized=1;
    if(s->damage_available&&c3_stage_renderer_particle_pack(&s->particle_pack,error,cap)!=C3_RENDER_OK)return 0;
    if(s->reflect_available&&c3_stage_renderer_particle_pack(&s->reflect_pack,error,cap)!=C3_RENDER_OK)return 0;
    if(s->confusion_available){
        for(unsigned p=0;p<2;++p)if(c3_stage_renderer_particle_pack(&s->confusion_pack[p],error,cap)!=C3_RENDER_OK)return 0;
        if(!c3_stage_renderer_confusion_program(&s->confusion_mask,error,cap))return 0;
    }
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
    C3BattleStage *s=user;
    if(finished){
        if(e->kind==C3_PREVIEW_EVENT_HP&&s->confusion_active&&e->group==s->confusion_group)c3_battle_presentation_wait(&s->presentation,1);
        if(e->kind==C3_PREVIEW_EVENT_HP&&s->damage.active&&e->group==s->bite_group)
            c3_battle_presentation_wait(&s->presentation,1);
        return;
    }
    if(e->kind==C3_PREVIEW_EVENT_HP&&s->damage_available&&s->damage_hit&&e->group==s->bite_group)return;
    if(e->kind==C3_PREVIEW_EVENT_CRITICAL&&s->damage_hit&&e->group==s->bite_group){s->presentation.duration_ms=0;return;}
    if(e->kind==C3_PREVIEW_EVENT_MOVE&&e->move==115&&e->actor==1&&s->reflect_available){
        if(!reflect_event(s,e,s->error,sizeof s->error))return;
    }
    if(e->kind==C3_PREVIEW_EVENT_HP&&s->confusion_available&&s->confusion_hit&&e->group==s->confusion_group)return;
    if(e->kind==C3_PREVIEW_EVENT_CRITICAL&&s->confusion_hit&&e->group==s->confusion_group){s->presentation.duration_ms=0;return;}
    if(e->kind==C3_PREVIEW_EVENT_MOVE&&e->move==93&&e->actor==1&&s->confusion_available){
        if(!confusion_event(s,e,s->error,sizeof s->error))return;
    }
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
        s->bite_sequence=e->sequence;s->bite_group=e->group;s->damage_hit=0;s->damage_pending=0;
        if(s->damage_available){
            /* Select actual committed HP target, including combat retargets.
             * Misses have no HP event and never start the damage resource. */
            for(uint32_t j=s->presentation.cursor+1;j<s->presentation.count&&s->presentation.events[j].group==e->group;++j){
                const C3BattlePreviewEvent *next=&s->presentation.events[j];
                if(next->kind==C3_PREVIEW_EVENT_HP&&next->actor==e->actor&&next->target>=2&&next->target<4){
                    for(uint32_t k=0;k<s->count;++k)if(s->slots[k]==next->target){s->damage_owner=k;s->damage_hit=1;s->damage_pending=1;}
                    break;
                }
            }
        }
        if(!camera_begin(s,C3_BATTLE_CAMERA_ATTACK,e->actor,s->damage_hit?s->slots[s->damage_owner]:e->target,s->error,sizeof s->error))return;
        const float *owner=s->transforms[s->bite_owner],*a=s->bite_program.anchor;
        float *t=s->transforms[s->bite_model],co=cosf(owner[3]),si=sinf(owner[3]);
        t[0]=owner[0]+owner[4]*(co*a[0]+si*a[2]);t[1]=owner[1]+owner[4]*a[1];
        t[2]=owner[2]+owner[4]*(-si*a[0]+co*a[2]);t[3]=owner[3];t[4]=owner[4];
        c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
        if(s->trace){fprintf(s->trace,"wzx_bite_start event=%lu start_tick=%lu stop_tick=%lu source_part=59 audio=%s\n",(unsigned long)e->sequence,(unsigned long)s->bite_program.model_start,(unsigned long)(s->bite_program.control_start+s->bite_program.control_delay),s->sound_cue?"prepared":"unavailable");fflush(s->trace);}
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
    /* Retain every elapsed millisecond while bounding catch-up work. Geometry is
     * sampled only after the event/clock timeline reaches its final state. */
    if(ms>4000)return fail(error,cap,"Battle timeline overload: step exceeds 4000 ms.");
    uint32_t remaining=ms;
    int endpoint=1;
    do {
        if(s->presentation.paused)break;
        /* Settle queued starts at this endpoint before advancing any clock.
         * Each zero-duration completion advances the bounded event cursor;
         * waits and positive-duration events leave it unchanged. */
        for(unsigned settle=0;settle<C3_PREVIEW_EVENT_LIMIT;++settle){
            uint32_t cursor=s->presentation.cursor;
            if(!c3_battle_presentation_step(&s->presentation,0,event,s))return fail(error,cap,"Battle presentation event timeline failed.");
            if(s->error[0])return fail(error,cap,s->error);
            for(uint32_t i=0;i<s->count;++i)s->fresh[i]=0;
            if(s->presentation.cursor==cursor)break;
        }
        uint32_t tick=endpoint?0:1;
        if(s->bite.active){
            uint32_t before=s->bite.model_starts,sound=s->bite.sound_requests;
            if(!c3_wzx_bite_step(&s->bite,tick))return fail(error,cap,"Bite WZX clock failed.");
            if(sound!=s->bite.sound_requests&&s->sound_cue&&!stage_sound(s,0x85,error,cap))return 0;
            if(s->trace&&s->bite.model_starts!=before){fprintf(s->trace,"wzx_bite_model_start tick=%lu sound85=%s\n",(unsigned long)s->bite_program.model_start,s->sound_cue?"requested":"unavailable");fflush(s->trace);}
            if(!s->bite.active){
                s->clips[s->bite_owner]=s->roles[s->bite_owner][0];s->clocks[s->bite_owner]=0;s->fresh[s->bite_owner]=1;
                if(s->trace){fprintf(s->trace,"wzx_bite_stop tick=%lu model_hidden=1 floor_restored=1\n",(unsigned long)(s->bite_program.control_start+s->bite_program.control_delay));fflush(s->trace);}
                if(s->damage_pending&&!damage_begin(s,error,cap))return 0;
                else if(!s->damage.active&&s->source_camera.active){c3_battle_camera_cancel(&s->source_camera);s->camera=s->source_camera.camera;}
            }
        }
        else if(!damage_step(s,tick,error,cap))return 0;
        if(!reflect_step(s,tick,error,cap)||!confusion_step(s,tick,error,cap))return 0;
        if(s->source_camera.active){C3BattleCameraActor actors[4];camera_actors(s,actors);
            if(!c3_battle_camera_step(&s->source_camera,tick,actors,error,cap))return 0;
            s->camera=s->source_camera.camera;
        }
        /* Completion observes the clocks at this tick, before advancing events. */
        for(uint32_t i=0;i<s->count;++i){
            if(s->immutable[i]&&s->sampled[i])continue;
            if(s->confusion_active&&i==(s->confusion_phase?s->confusion_target:s->confusion_owner))s->clocks[i]=confusion_owner_seconds(s);
            else if(s->reflect_available&&i==s->reflect_model)s->clocks[i]=s->reflect.model_seconds;
            else if(s->reflect.active&&i==s->reflect_owner)s->clocks[i]=s->reflect.owner_seconds;
            else if(s->bite_available&&i==s->bite_model)s->clocks[i]=s->bite.model_seconds;
            else if(s->damage_available&&i==s->damage_model)s->clocks[i]=s->damage.model_seconds;
            else if(s->damage.active&&i==s->damage_owner)s->clocks[i]=s->damage.owner_seconds;
            else if(s->bite.active&&i==s->bite_owner)s->clocks[i]=s->bite.owner_seconds;
            else if(!s->fresh[i])s->clocks[i]+=(float)tick/1000.0f;
        }
        if(s->presentation.waiting){
            int source_move=s->damage_hit&&s->presentation.cursor<s->presentation.count&&
                s->presentation.events[s->presentation.cursor].kind==C3_PREVIEW_EVENT_MOVE&&
                s->presentation.events[s->presentation.cursor].group==s->bite_group;
            int active=source_move?(s->bite.active||s->damage_pending||!s->damage.hp_gates):(s->bite.active||s->damage.active);
            active|=s->reflect.active;
            if(s->confusion_hit&&s->presentation.cursor<s->presentation.count&&s->presentation.events[s->presentation.cursor].kind==C3_PREVIEW_EVENT_MOVE&&s->presentation.events[s->presentation.cursor].group==s->confusion_group)active|=s->confusion_pending||!s->confusion_hp_gate;
            else active|=s->confusion_active;
            for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];
                if(s->confusion_active&&(i==s->confusion_owner||i==s->confusion_target))continue;
                if(s->reflect_available&&i==s->reflect_model)continue;
                if(s->bite_available&&i==s->bite_model)continue;
                if(s->damage_available&&(i==s->damage_model||(s->damage_hit&&i==s->damage_owner)))continue;
                if(s->clips[i]!=s->roles[i][0]&&s->clocks[i]*m->fps<(float)m->clips[s->clips[i]].frame_count)active=1;
            }
            if(!active)c3_battle_presentation_wait(&s->presentation,0);
        }
        if(!c3_battle_presentation_step(&s->presentation,tick,event,s))return fail(error,cap,"Battle presentation event timeline failed.");
        if(s->error[0])return fail(error,cap,s->error);
        for(uint32_t i=0;i<s->count;++i){
            C3Title *m=&s->models[i];
            s->fresh[i]=0;
            if((s->damage.active&&i==s->damage_owner)||(s->confusion_active&&(i==s->confusion_owner||i==s->confusion_target)))continue;
            C3TitleClip *clip=&m->clips[s->clips[i]];
            if(!clip->loop&&s->clocks[i]*m->fps>=(float)clip->frame_count&&s->clips[i]!=s->roles[i][4]){s->clips[i]=s->roles[i][0];s->clocks[i]=0;}
        }
        if(endpoint){
            if(!remaining)break;
            endpoint=0;
        }else{
            --remaining;endpoint=1;
        } /* Every source endpoint settles, including newly started resources. */
    } while(1);
    for(uint32_t i=0;i<s->count;++i){C3Title *m=&s->models[i];
        if(s->slots[i]==UINT32_MAX&&!c3_stage_renderer_set_modulation(i,s->confusion_floor_enabled?s->confusion_floor_rgb:s->reflect.floor_enabled?s->reflect.floor_rgb:s->damage.floor_enabled?s->damage.floor_rgb:s->bite.floor_enabled?s->bite.floor_rgb:NULL))
            return fail(error,cap,"Battle floor modulation failed.");
        if(s->immutable[i]&&s->sampled[i])continue;
        int effect=s->bite_available&&i==s->bite_model;
        int damage_effect=s->damage_available&&i==s->damage_model;
        if(!c3_title_animate(m,s->clips[i],s->clocks[i]))return fail(error,cap,"Battle clip sampling failed.");
        float *t=s->transforms[i],co=cosf(t[3]),si=sinf(t[3]);
        int visible=(s->reflect_available&&i==s->reflect_model)?s->reflect.model_visible:effect?s->bite.model_visible:damage_effect?s->damage.model_visible:(s->slots[i]>=4||s->presentation.visible[s->slots[i]]);
        int identity=t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&t[4]==1;
        if(!identity)for(uint32_t v=0;v<m->vertex_count;++v){C3TitleDrawVertex *d=&m->draw_vertices[v];float x=d->pos[0]*t[4],z=d->pos[2]*t[4],nx=d->normal[0],nz=d->normal[2];
            d->pos[0]=co*x+si*z+t[0];d->pos[1]=d->pos[1]*t[4]+t[1];d->pos[2]=-si*x+co*z+t[2];d->normal[0]=co*nx+si*nz;d->normal[2]=-si*nx+co*nz;
            for(unsigned axis=0;axis<3;++axis)if(!isfinite(d->pos[axis])||!isfinite(d->normal[axis]))return fail(error,cap,"Battle actor transform overflow.");}
        s->sampled[i]=1;
        if(!visible)for(uint32_t j=0;j<m->batch_count;++j)m->pose.materials[j].visible=0;
    }
    if(!c3_stage_renderer_confusion(s->confusion_type12.active?&s->confusion_type12:NULL))return fail(error,cap,"Confusion capture state binding failed.");
    if(s->confusion_active)return confusion_bind_particles(s,error,cap);
    return s->reflect.active?reflect_bind_particles(s,error,cap):damage_bind_particles(s,error,cap);
}
int c3_battle_stage_draw(C3BattleStage *s,C3TitleRenderStats *stats,char *error,size_t cap){return c3_stage_renderer_draw(s->draw,s->count,&s->camera,1,stats,error,cap);}
int c3_battle_stage_close(C3BattleStage *s){confusion_cancel(s);c3_wzx_bite_cancel(&s->bite);c3_wzx_damage_cancel(&s->damage);c3_wzx_reflect_cancel(&s->reflect);c3_particle_cancel(&s->particles);if(s->initialized&&!c3_stage_renderer_try_shutdown())return 0;s->initialized=0;release(s);return 1;}
