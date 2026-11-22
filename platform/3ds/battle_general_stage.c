#include "battle_general_stage.h"
#include "colosseum3ds/battle_general_view.h"
#include "save_codec.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ARENA UINT32_MAX
#define EFFECT (UINT32_MAX - 1u)

static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const unsigned char *p){uint32_t u=word(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}

/* battleGridUpdate's one-trainer layout in arena units (0.1), the same grid
 * the encounter-10 scene uses: hero side at -x facing +x. Singles stand on
 * the centre line. */
static void slot_position(unsigned slot,unsigned per_side,float out[4])
{
    static const float doubles[4][3]={{-3.5f,0,-2.625f},{-3.5f,0,2.625f},{3.5f,0,2.625f},{3.5f,0,-2.625f}};
    if(slot<4){memcpy(out,doubles[slot],12);if(per_side<2)out[2]=0;}
    /* The hero trainer stands behind his team but off the command camera's
     * centre line (the source grid has him at z 0, in front of that view). */
    else{out[0]=slot==4?-5.6875f:5.6875f;out[1]=0;out[2]=slot==4?-4.2f:0;}
    out[3]=slot==4||slot<2?(float)M_PI/2:-(float)M_PI/2;
}

const char *c3_battle_general_stage_missing(const C3Battle3dIndex *x,const C3BattleGeneralCast *c)
{
    if(!x||!x->bytes)return "no index";
    if(!c3_battle3d_floor(x,c->floor,NULL))return "floor";
    for(unsigned i=0;i<4;++i)if(c->species[i]&&!c3_battle3d_species(x,c->species[i],NULL))return "species";
    if(!c3_battle3d_trainer(x,c->trainer[0],NULL))return "hero trainer";
    if(!c3_battle3d_trainer(x,c->trainer[1],NULL))return "trainer";
    return NULL;
}

static int read_file(const char *path,unsigned char *out,size_t cap,size_t *bytes)
{
    FILE *f=fopen(path,"rb");if(!f)return 0;
    *bytes=fread(out,1,cap,f);int extra=fgetc(f),io=ferror(f);fclose(f);
    return !io&&extra==EOF;
}

static void wzx_close(C3BattleGeneralStage *s);
static void release(C3BattleGeneralStage *s)
{
    wzx_close(s);
    c3_particle_cancel(&s->particles);c3_wzx_damage_cancel(&s->damage);
    for(uint32_t i=s->count;i>0;--i)c3_title_close(&s->models[i-1]);
    s->count=0;free(s->particle_bytes);s->particle_bytes=NULL;s->damage_available=0;
}

static int model_for_slot(const C3BattleGeneralStage *s,unsigned slot)
{
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==slot)return (int)i;
    return -1;
}
static int sound(C3BattleGeneralStage *s,uint32_t id,char *e,size_t n);
#include "battle_general_wzx.inc"

/* Load a pack once; a second cast member with the same pack borrows it. */
static int load_actor(C3BattleGeneralStage *s,const char *root,const C3Battle3dModel *m,unsigned slot,char *e,size_t n)
{
    char path[512];
    if(s->count>=C3_GSTAGE_MODELS)return fail(e,n,"Battle stage model limit.");
    uint32_t i=s->count;int shared=-1;
    for(uint32_t j=0;j<i;++j)if(s->slots[j]<6&&!s->models[j].borrowed_storage&&!strcmp(s->files[j],m->file)){shared=(int)j;break;}
    if(shared>=0){if(!c3_title_instance(&s->models[i],&s->models[shared],e,n))return 0;}
    else{
        if(snprintf(path,sizeof path,"%s/%s",root,m->file)>=(int)sizeof path)return fail(e,n,"Battle model path too long.");
        if(!c3_title_load(&s->models[i],path,e,n))return 0;
    }
    snprintf(s->files[i],sizeof s->files[i],"%s",m->file);
    s->count=i+1;s->slots[i]=slot;
    c3_battle3d_roles(m,s->roles[i]);
    C3Title *t=&s->models[i];
    if(s->roles[i][0]==C3_BATTLE3D_NO_CLIP||s->roles[i][0]>=t->clip_count)return fail(e,n,"Battle model has no idle clip.");
    for(unsigned r=0;r<9;++r)if(s->roles[i][r]!=C3_BATTLE3D_NO_CLIP&&s->roles[i][r]>=t->clip_count)return fail(e,n,"Battle model role clip out of range.");
    s->clips[i]=s->roles[i][0];
    slot_position(slot,s->cast.active_per_side,s->transforms[i]);
    s->transforms[i][4]=m->scale;
    s->appear[i]=1;s->draw[i]=t;
    if(slot<6){memcpy(s->part_bone[slot],m->part_bone,17);memcpy(s->timing[slot],m->timing,sizeof s->timing[slot]);
        memcpy(s->entry_clip[slot],m->entry_clip,sizeof s->entry_clip[slot]);}
    return 1;
}

static int damage_load(C3BattleGeneralStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(!root)return 1;
    if(snprintf(path,sizeof path,"%s/bite_damage.c3w",root)>=(int)sizeof path)return 0;
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect the damage effect descriptor.");
    fclose(f);
    if(s->count>=C3_GSTAGE_MODELS)return fail(e,n,"Battle stage model limit (effect).");
    if(!c3_wzx_damage_load(&s->damage_program,path,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/bite_damage.c3t",root)>=(int)sizeof path)return 0;
    uint32_t i=s->count;
    if(!c3_title_load(&s->models[i],path,e,n))return 0;
    s->count=i+1;s->damage_model=i;s->slots[i]=EFFECT;
    C3Title *m=&s->models[i];uint8_t hash[20];
    c3_save_codec_sha1(hash,m->file_data,(uint32_t)m->file_bytes);
    if(memcmp(hash,s->damage_program.model_sha1,20)||m->clip_count!=1||m->clips[0].loop||m->bone_count<=s->damage_program.particle_bone)
        return fail(e,n,"Damage effect model differs from its descriptor.");
    if(!c3_title_animate(m,0,0))return fail(e,n,"Cannot sample the damage effect.");
    memcpy(s->damage_particle_local,(const float[3]){m->pose.matrices[s->damage_program.particle_bone][3],
        m->pose.matrices[s->damage_program.particle_bone][7],m->pose.matrices[s->damage_program.particle_bone][11]},12);
    s->draw[i]=m;s->transforms[i][4]=1;
    for(unsigned j=0;j<9;++j)s->roles[i][j]=0;
    if(snprintf(path,sizeof path,"%s/bite_damage.c3p",root)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return fail(e,n,"Missing damage particle pack.");
    int ok=fseek(f,0,SEEK_END)==0;long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->particle_bytes=malloc((size_t)length);ok=s->particle_bytes&&fread(s->particle_bytes,1,(size_t)length,f)==(size_t)length;}
    fclose(f);
    if(!ok)return fail(e,n,"Cannot read the damage particle pack.");
    if(!c3_particle_pack_decode(&s->particle_pack,s->particle_bytes,(size_t)length,s->damage_program.particles_sha1,e,n))return 0;
    s->damage_available=1;
    return 1;
}

/* Bring the renderer up over the current models (load, and after a swap). */
static int renderer_start(C3BattleGeneralStage *s,char *error,size_t cap)
{
#ifndef C3_NO_GPU_SKIN
    {uint64_t actors=0;for(uint32_t i=0;i<s->count;++i)if(s->slots[i]<6u)actors|=UINT64_C(1)<<i;
     c3_stage_renderer_request_gpu_skin(actors);}
#endif
    if(c3_stage_renderer_init(s->draw,s->count,error,cap)!=C3_RENDER_OK)return 0;
    s->initialized=1;
    if(s->damage_available&&c3_stage_renderer_particle_pack(&s->particle_pack,error,cap)!=C3_RENDER_OK)return 0;
    wzx_register_banks(s,error,cap);
    for(uint32_t i=0;i<s->count;++i){
        const float *t=s->transforms[i];C3Title *m=&s->models[i];
        if(s->slots[i]==ARENA&&m->clip_count==1&&m->clips[0].frame_count==1&&
           t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&t[4]==1){
            s->immutable[i]=1;
            if(!c3_stage_renderer_set_static(i,true))return fail(error,cap,"Static arena admission failed.");
        }
    }
    return 1;
}

int c3_battle_general_stage_load(C3BattleGeneralStage *s,const C3Battle3dIndex *x,const C3BattleGeneralCast *cast,
                                 const C3BattlePack *pack,const char *root,const char *effects,
                                 const uint16_t hp[4],FILE *trace,char *error,size_t cap)
{
    unsigned char data[128+128*16];char path[512],name[32];size_t bytes=0;
    if(!s||!x||!cast||!root||!hp)return fail(error,cap,"Invalid battle stage arguments.");
    memset(s,0,sizeof *s);s->trace=trace;s->cast=*cast;s->pack=pack;s->message_ms=1100;
    const char *missing=c3_battle_general_stage_missing(x,cast);
    if(missing){snprintf(error,cap,"No prepared 3D %s.",missing);return 0;}
    c3_battle3d_floor(x,cast->floor,name);
    if(snprintf(path,sizeof path,"%s/%s",root,name)>=(int)sizeof path)return fail(error,cap,"Battle path too long.");
    if(!read_file(path,data,sizeof data,&bytes))return fail(error,cap,"Missing or oversized battle floor descriptor.");
    if(bytes<128||memcmp(data,"C3BV",4)||word(data+4)!=1||word(data+8)!=bytes||word(data+16)!=0)
        return fail(error,cap,"Invalid battle floor descriptor header.");
    uint32_t arenas=word(data+12);
    if(!arenas||arenas>8||bytes!=128u+128u*arenas)return fail(error,cap,"Battle floor descriptor bounds.");
    float camera[12];for(unsigned i=0;i<12;++i){camera[i]=real(data+32+4*i);if(!isfinite(camera[i]))return fail(error,cap,"Nonfinite battle floor camera.");}
    memcpy(s->camera.eye,camera,12);memcpy(s->camera.target,camera+3,12);s->camera.roll=camera[6];
    s->camera.fov_y_degrees=camera[7];s->camera.near_plane=camera[8];s->camera.far_plane=camera[9];s->camera.aspect=camera[10];
    if(camera[7]<=0||camera[7]>=180||camera[8]<=0||camera[9]<=camera[8]||camera[10]<=0)return fail(error,cap,"Invalid battle floor camera.");
    for(uint32_t i=0;i<arenas;++i){
        const unsigned char *r=data+128+128*i;uint32_t slot=word(r+48),shared=word(r+52);
        if(!memchr(r,0,48)||!r[0]||strchr((const char *)r,'/')||strchr((const char *)r,'\\')||strstr((const char *)r,"..")||slot!=ARENA)goto invalid;
        uint32_t m=s->count;
        for(unsigned j=0;j<5;++j){s->transforms[m][j]=real(r+96+4*j);if(!isfinite(s->transforms[m][j]))goto invalid;}
        if(s->transforms[m][4]<=0)goto invalid;
        if(shared!=UINT32_MAX){if(shared>=m||s->models[shared].borrowed_storage)goto invalid;
            if(!c3_title_instance(&s->models[m],&s->models[shared],error,cap))goto bad;}
        else{if(snprintf(path,sizeof path,"%s/%s",root,(const char *)r)>=(int)sizeof path)goto invalid;
            if(!c3_title_load(&s->models[m],path,error,cap))goto bad;}
        s->count=m+1;s->slots[m]=ARENA;
        s->clips[m]=word(r+56);if(s->clips[m]>=s->models[m].clip_count)goto invalid;
        for(unsigned j=0;j<9;++j)s->roles[m][j]=s->clips[m];
        s->draw[m]=&s->models[m];s->appear[m]=1;
    }
    for(unsigned slot=0;slot<6;++slot){
        C3Battle3dModel model;
        int ok=slot<4?cast->species[slot]&&c3_battle3d_species(x,cast->species[slot],&model):
                      c3_battle3d_trainer(x,cast->trainer[slot-4],&model);
        if(!ok)continue;
        if(!load_actor(s,root,&model,slot,error,cap))goto bad;
    }
    if(model_for_slot(s,0)<0||model_for_slot(s,2)<0)goto invalid;
    /* Without the original base camera (battle_home.c3h, loaded with the
     * WZX layer), the command-phase view of the encounter-10 scene: behind
     * and above the hero's side looking across at the opponents, through
     * the 30-degree lens that geometry was framed for. A floor's own scene
     * camera has any lens (floor 7: 5.4 degrees from far away), so only its
     * roll and clip planes are kept. */
    {
        float player[3]={0,0,0},enemy[3]={0,0,0};
        for(unsigned slot=0;slot<4;++slot){float p[4];slot_position(slot,2,p);float *side=slot<2?player:enemy;
            for(unsigned j=0;j<3;++j)side[j]+=p[j]*.5f;}
        float axis[2]={enemy[0]-player[0],enemy[2]-player[2]};
        float span=sqrtf(axis[0]*axis[0]+axis[1]*axis[1]);
        axis[0]/=span;axis[1]/=span;
        s->camera.eye[0]=player[0]-axis[0]*7.5f;s->camera.eye[1]=player[1]+3.2f;s->camera.eye[2]=player[2]-axis[1]*7.5f;
        s->camera.target[0]=player[0]+axis[0]*span*.75f;s->camera.target[1]=player[1]+.9f;s->camera.target[2]=player[2]+axis[1]*span*.75f;
        s->camera.fov_y_degrees=30.f;
    }
    if(!damage_load(s,effects,error,cap))goto bad;
    if(wzx_open(s,root)){wzx_load_persistent(s,root);if(trace){fprintf(trace,"wzx_layer rows=%lu persistent=%u\n",(unsigned long)s->wzx->index.rows,s->wzx->persistent_sequences);fflush(trace);}}
    c3_battle_presentation_init(&s->presentation,hp);
    if(!c3_battle_presentation_wait_limit(&s->presentation,20000))goto invalid;
    if(!renderer_start(s,error,cap)){if(!s->initialized)goto bad;return 0;}
    if(trace){fprintf(trace,"general_stage_loaded floor=%u descriptor=%s models=%lu species=%u,%u,%u,%u trainers=%u,%u damage_effect=%u\n",
        cast->floor,name,(unsigned long)s->count,cast->species[0],cast->species[1],cast->species[2],cast->species[3],
        cast->trainer[0],cast->trainer[1],s->damage_available);fflush(trace);}
    return 1;
invalid:fail(error,cap,"Invalid battle floor or cast descriptor.");
bad:release(s);return 0;
}

/* Best effort: a sound the audio backend has not prepared plays nothing
 * (logged once per id) rather than stopping the battle. */
static int sound(C3BattleGeneralStage *s,uint32_t id,char *e,size_t n)
{
    char problem[160];
    if(!id)return 1;
    /* MusyX positions battle sounds at their owner: pan by where the owner
     * stands across the current view. */
    if(s->sound_cue_pan){
        float pan=0;int m=s->sound_owner>=0?model_for_slot(s,(unsigned)s->sound_owner):-1;
        if(m>=0){const C3TitleCamera *c=&s->camera;
            float f[3]={c->target[0]-c->eye[0],0,c->target[2]-c->eye[2]};
            float d[3]={s->transforms[m][0]-c->eye[0],0,s->transforms[m][2]-c->eye[2]};
            float right[2]={-f[2],f[0]},rl=sqrtf(right[0]*right[0]+right[1]*right[1]),dl=sqrtf(d[0]*d[0]+d[2]*d[2]);
            if(rl>1e-4f&&dl>1e-4f)pan=.8f*(d[0]*right[0]+d[2]*right[1])/(rl*dl);}
        if(s->sound_cue_pan(id,pan))return 1;
    }
    if(!s->sound_cue)return 1;
    if(!s->sound_cue(++s->cue_serial,id,problem,sizeof problem)&&s->trace&&s->missing_sound!=id){
        s->missing_sound=id;fprintf(s->trace,"general_sound_unavailable id=%lx reason=%s\n",(unsigned long)id,problem);fflush(s->trace);}
    (void)e;(void)n;return 1;
}

static void start_clip(C3BattleGeneralStage *s,int m,unsigned role,int hold)
{
    if(m<0)return;
    uint32_t clip=s->roles[m][role];
    if(clip==C3_BATTLE3D_NO_CLIP)return;
    s->clips[m]=clip;s->clocks[m]=0;s->hold[m]=(uint8_t)hold;
    const C3Title *t=&s->models[m];
    if(clip!=s->roles[m][0]&&!t->clips[clip].loop){
        c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    }
}

static int damage_begin(C3BattleGeneralStage *s,int target,uint32_t sequence,char *e,size_t n)
{
    if(!s->damage_available||target<0)return 1;
    if(s->damage.active){c3_wzx_damage_cancel(&s->damage);c3_particle_cancel(&s->particles);}
    if(!c3_wzx_damage_begin(&s->damage,&s->damage_program))return fail(e,n,"Damage clock initialization failed.");
    const float *owner=s->transforms[target],*a=s->damage_program.anchor;
    float *t=s->transforms[s->damage_model],co=cosf(owner[3]),si=sinf(owner[3]);
    t[0]=owner[0]+owner[4]*(co*a[0]+si*a[2]);t[1]=owner[1]+owner[4]*a[1];
    t[2]=owner[2]+owner[4]*(-si*a[0]+co*a[2]);t[3]=owner[3];t[4]=.75f;
    s->damage_owner=(uint32_t)target;s->damage_sequence=sequence;s->particles_started=0;
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    return 1;
}

static void event(const C3BattlePreviewEvent *e,int finished,void *user)
{
    C3BattleGeneralStage *s=user;
    if(finished||s->error[0])return;
    switch(e->kind){
    case C3_PREVIEW_EVENT_INTRO:
        s->presentation.duration_ms=s->message_ms+400;
        /* fight_action.c intro: the floor's appear sequence, then the
         * opponent trainer's intro with its message. */
        if(s->wzx){
            int floor=s->cast.intro_wzx&&wzx_special(s,s->cast.intro_wzx,5,0,0)!=NULL;
            if(!floor)(void)wzx_special(s,TRAINER_INTRO,5,0,0);
            if(wzx_running(s,4)){s->wzx_attack_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;}
        }
        break;
    case C3_PREVIEW_EVENT_STATUS:
        s->presentation.duration_ms=s->message_ms;
        /* fn_8022E6F0's partner cut-in (model 0x32 = trainer sequence 50
         * playing special 0xA0 in the hero trainer's place): that model is
         * not among the prepared battle3d packs, so the aura plays alone. */
        if(e->cutin&&s->trace){fprintf(s->trace,"general_partner_cutin owner=%u special=0xA0 model=trn050 drawn=0\n",(unsigned)e->target);fflush(s->trace);}
        /* The status condition's or stat change's own battle special. */
        if(s->wzx&&e->effect&&e->target<4&&s->presentation.visible[e->target]){
            const char *name=c3_wzx_index_special(&s->wzx->index,e->effect);
            s->wzx_id=0x1000u|e->effect;
            if(name&&wzx_begin(s,name,3,e->target,e->actor)){s->wzx_attack_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;}
        }
        break;
    case C3_PREVIEW_EVENT_MESSAGE:case C3_PREVIEW_EVENT_MISS:case C3_PREVIEW_EVENT_CRITICAL:
        s->presentation.duration_ms=s->message_ms;
        /* A recall (fightOutPokemonModosuEffect): the ball's in special. */
        if(e->kind==C3_PREVIEW_EVENT_MESSAGE&&(e->effect&0xFF00)==0x100&&e->actor<4&&s->presentation.visible[e->actor]&&wzx_special(s,ball_special(e->effect&0xFF,0),e->actor,0,0)){
            s->wzx_attack_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
        }
        break;
    case C3_PREVIEW_EVENT_SEND_OUT:{
        int m=e->target<4?model_for_slot(s,e->target):-1;
        if(m>=0){s->presentation.visible[e->target]=1;s->appear[m]=0;s->clips[m]=s->roles[m][0];s->clocks[m]=0;s->hold[m]=0;}
        s->presentation.duration_ms=s->message_ms;
        /* fightOutPokemonDasuEffect: ball open, then out with the cry. */
        {uint16_t voice=e->target<4?c3_battle_pack_species_voice(s->pack,s->cast.species[e->target]):0;
         if(e->target<4&&wzx_special(s,ball_special(e->effect,1),e->target,ball_special(e->effect,2),voice)){
             s->wzx_attack_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
         }else if(voice&&!sound(s,voice,s->error,sizeof s->error))return;}
        break;}
    case C3_PREVIEW_EVENT_MOVE:{
        int m=e->actor<4?model_for_slot(s,e->actor):-1;
        unsigned role=c3_general_view_move_role(s->pack,e->move);
        s->presentation.duration_ms=s->message_ms;
        s->wzx_move=0;s->wzx_damage[0]=0;s->wzx_damage_run=NULL;
        if(s->wzx&&e->actor<4){
            unsigned pkx=slot_pkx(s,e->actor);
            const char *attack=c3_wzx_index_move(&s->wzx->index,e->move,1,(uint16_t)pkx,0);
            unsigned kind=1;
            if(!attack){attack=c3_wzx_index_move(&s->wzx->index,e->move,3,(uint16_t)pkx,0);kind=3;}
            for(uint32_t j=s->presentation.cursor+1;j<s->presentation.count&&s->presentation.events[j].group==e->group;++j){
                const C3BattlePreviewEvent *h=&s->presentation.events[j];
                if(h->kind==C3_PREVIEW_EVENT_HP&&h->target<4&&h->hp[h->target]<h->hp_before[h->target]){
                    const char *damage=c3_wzx_index_move(&s->wzx->index,e->move,2,(uint16_t)slot_pkx(s,h->target),0);
                    if(damage){snprintf(s->wzx_damage,sizeof s->wzx_damage,"%s",damage);s->wzx_damage_target=h->target;}
                    break;
                }
            }
            s->wzx_id=e->move;
            if(attack&&wzx_begin(s,attack,kind,e->actor,e->target)){
                s->wzx_move=1;s->wzx_attack_wait=1;
                c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
                break;
            }
            if(s->wzx_damage[0])s->wzx_move=1;
        }
        start_clip(s,m,role?role:2,0);
        break;}
    case C3_PREVIEW_EVENT_HP:{
        int m=e->target<4?model_for_slot(s,e->target):-1;
        s->presentation.duration_ms=450;
        if(e->target<4&&e->hp[e->target]<e->hp_before[e->target]){
            if(s->wzx_move&&e->actor<4){
                WzxRun *r=s->wzx_damage[0]&&s->wzx_damage_target==e->target?wzx_begin(s,s->wzx_damage,2,e->target,e->actor):NULL;
                s->wzx_damage[0]=0;s->wzx_damage_run=r;
                if(r){s->wzx_hp_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;break;}
                start_clip(s,m,3,0);break;
            }
            start_clip(s,m,3,0);
            if(e->actor<4&&!damage_begin(s,m,e->sequence,s->error,sizeof s->error))return;
        }
        break;}
    case C3_PREVIEW_EVENT_FAINT:{
        int m=e->target<4?model_for_slot(s,e->target):-1;
        s->presentation.duration_ms=s->message_ms;
        if(m>=0&&s->roles[m][4]!=C3_BATTLE3D_NO_CLIP)start_clip(s,m,4,1);
        /* fightOutPokemonKizetuEffect: the ball's downin special. */
        if(e->target<4&&wzx_special(s,ball_special(e->effect,3),e->target,0,0)){s->wzx_attack_wait=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;}
        break;}
    case C3_PREVIEW_EVENT_OUTCOME:{
        int won=e->outcome==C3_PREVIEW_PLAYER_WON,lost=e->outcome==C3_PREVIEW_PLAYER_LOST;
        s->presentation.duration_ms=s->message_ms+600;
        if(won||lost){start_clip(s,model_for_slot(s,4),won?7:8,1);start_clip(s,model_for_slot(s,5),won?8:7,1);
            /* fight_action.c: wzx_win / wzx_lose on the hero's side. */
            if(s->wzx)(void)wzx_special(s,won?BATTLE_WIN:BATTLE_LOSE,4,0,0);}
        break;}
    default:break;
    }
}

int c3_battle_general_stage_submit(C3BattleGeneralStage *s,const C3BattlePreviewEvent *e,uint32_t count)
{
    if(!s||!s->initialized||!c3_battle_presentation_submit(&s->presentation,e,count))return 0;
    for(uint32_t i=0;i<count;++i)if(e[i].kind==C3_PREVIEW_EVENT_INTRO){memset(s->presentation.visible,0,sizeof s->presentation.visible);break;}
    return 1;
}

int c3_battle_general_stage_idle(const C3BattleGeneralStage *s)
{
    return s&&s->presentation.cursor>=s->presentation.count;
}

static int damage_step(C3BattleGeneralStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->damage.active)return 1;
    uint32_t sounds=s->damage.sound_requests;
    if(!c3_wzx_damage_step(&s->damage,ms))return fail(e,n,"Damage clock overload.");
    const float *b=s->damage_particle_local,*t=s->transforms[s->damage_model];
    float co=cosf(t[3]),si=sinf(t[3]),position[3]={10*(t[0]+t[4]*(co*b[0]+si*b[2])),10*(t[1]+t[4]*b[1]),10*(t[2]+t[4]*(-si*b[0]+co*b[2]))};
    if(s->damage.particle_starts&&!s->particles_started){
        if(!c3_particle_begin_position_flags(&s->particles,&s->particle_pack.bank,s->damage_program.root_generator,
            0x42495445u^s->damage_sequence,position,t[3],.75f,.75f,0x10A1,e,n))return 0;
        s->particles_started=1;
    }else if(s->particles_started&&(!c3_particle_position(&s->particles,position,e,n)||!c3_particle_step(&s->particles,ms,0,e,n)))return 0;
    if(sounds!=s->damage.sound_requests&&!sound(s,s->damage_program.sound_id,e,n))return 0;
    if(c3_wzx_damage_finish(&s->damage,s->damage.model_finished,
        s->particles_started&&c3_particle_finished(&s->particles),s->damage.floor_finished)){
        if(s->trace){fprintf(s->trace,"general_damage_complete owner=%lu particles=%lu\n",(unsigned long)s->slots[s->damage_owner],(unsigned long)s->particles.emitted);fflush(s->trace);}
    }
    return 1;
}

int c3_battle_general_stage_step(C3BattleGeneralStage *s,uint32_t ms,char *error,size_t cap)
{
    if(!s||!s->initialized)return fail(error,cap,"Battle stage is not initialized.");
    if(ms>1000)ms=1000; /* a stall (load, suspend) is not battle time */
    float dt=(float)ms/1000.f;
    int active=0;
    for(uint32_t i=0;i<s->count;++i){
        if(s->immutable[i]&&s->sampled[i])continue;
        if(s->appear[i]<1){s->appear[i]+=dt/.35f;if(s->appear[i]>1)s->appear[i]=1;}
        if(s->slots[i]==EFFECT){s->clocks[i]=s->damage.model_seconds;continue;}
        if(s->slots[i]==WZX_EFFECT_SLOT)continue;
        C3Title *m=&s->models[i];const C3TitleClip *clip=&m->clips[s->clips[i]];
        float length=(float)clip->frame_count/m->fps;
        float last=length>1.f/m->fps?length-1.f/m->fps:0;
        if(s->hold[i]&&s->clocks[i]>=last){s->clocks[i]=last;continue;} /* held on its last frame */
        s->clocks[i]+=dt;
        if(!clip->loop&&s->clocks[i]>=length){
            if(s->hold[i])s->clocks[i]=last;
            else if(s->clips[i]!=s->roles[i][0]){s->clips[i]=s->roles[i][0];s->clocks[i]=0;}
            else s->clocks[i]=0;
        }else if(s->slots[i]<6&&s->clips[i]!=s->roles[i][0]&&!clip->loop)active=1;
    }
    if(!damage_step(s,ms,error,cap))return 0;
    wzx_step(s,ms);
    if(s->wzx_attack_wait&&!wzx_running(s,1)&&!wzx_running(s,3)&&!wzx_running(s,4))s->wzx_attack_wait=0;
    if(s->wzx_hp_wait){
        WzxRun *r=s->wzx_damage_run;
        if(!r||!r->active||c3_wzx_player_reached(&r->player,C3_WZX_TIME_DAMAGE)){s->wzx_hp_wait=0;c3_battle_presentation_wait(&s->presentation,0);}
    }
    /* The damage effect holds the timeline only until its HP gate; its
     * particles and floor flash finish behind the following events. */
    else if(s->presentation.waiting&&!active&&!s->wzx_attack_wait&&(!s->damage.active||s->damage.hp_gates))c3_battle_presentation_wait(&s->presentation,0);
    if(!c3_battle_presentation_step(&s->presentation,ms,event,s))return fail(error,cap,"Battle presentation timeline failed.");
    if(s->error[0])return fail(error,cap,s->error);
    for(uint32_t i=0;i<s->count;++i){
        C3Title *m=&s->models[i];
        const uint8_t *floor=s->wzx&&s->wzx->floor_tint?s->wzx->floor_rgb:s->damage.active&&s->damage.floor_enabled?s->damage.floor_rgb:NULL;
        if(s->slots[i]==ARENA&&!c3_stage_renderer_set_modulation(i,floor))
            return fail(error,cap,"Battle floor modulation failed.");
        if(s->wzx&&s->slots[i]<4&&!c3_stage_renderer_set_modulation(i,s->wzx->owner_tint[s->slots[i]]?s->wzx->owner_rgb[s->slots[i]]:NULL))
            return fail(error,cap,"Battle actor modulation failed.");
        if(s->immutable[i]&&s->sampled[i])continue;
        float *t=s->transforms[i];
        int visible=s->slots[i]==EFFECT?s->damage.active&&s->damage.model_visible:
                    s->slots[i]==WZX_EFFECT_SLOT?wzx_model_visible(s,i):
                    s->slots[i]>=4?1:s->presentation.visible[s->slots[i]];
        m->retained=1;m->hidden=(uint8_t)!visible;
#ifndef C3_NO_GPU_SKIN
        m->gpu_batches=c3_stage_renderer_gpu_skin_mask(i);
#endif
        float scale=t[4]*(s->appear[i]<1?.15f+.85f*s->appear[i]:1);
        if(t[0]==0&&t[1]==0&&t[2]==0&&t[3]==0&&scale==1)m->placement.enabled=0;
        else c3_title_place_yaw(m,t,cosf(t[3]),sinf(t[3]),scale,0);
        if(!c3_title_animate(m,s->clips[i],s->clocks[i]>0?s->clocks[i]:0))return fail(error,cap,"Battle clip sampling failed.");
        s->sampled[i]=1;
        if(!visible)for(uint32_t j=0;j<m->batch_count;++j)m->pose.materials[j].visible=0;
    }
    static const float identity[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};
    if(s->wzx&&!wzx_bind_systems(s)&&s->trace&&!s->particle_bind_logged){
        s->particle_bind_logged=1;fprintf(s->trace,"wzx_particle_slot_refused\n");fflush(s->trace);}
    if(s->particles_started&&s->damage.active){
        if(!c3_stage_renderer_particles(&s->particles,identity))return fail(error,cap,"Damage particle binding failed.");
    }else c3_stage_renderer_particles(NULL,NULL);
    return 1;
}

int c3_battle_general_stage_swap(C3BattleGeneralStage *s,const C3Battle3dIndex *x,unsigned slot,uint16_t species,
                                 const char *root,char *error,size_t cap)
{
    C3Battle3dModel model;char path[512];
    if(!s||slot>=4||!species)return fail(error,cap,"Invalid battle swap.");
    if(!c3_battle3d_species(x,species,&model))return fail(error,cap,"No prepared 3D species for the swap.");
    int m=model_for_slot(s,slot);
    if(m<0)return fail(error,cap,"Swap position has no model.");
    for(uint32_t j=0;j<s->count;++j)if((int)j!=m&&s->models[j].borrowed_storage&&s->slots[j]<6&&!strcmp(s->files[j],s->files[m]))
        return fail(error,cap,"Swap of a shared model needs a reload.");
    if(s->initialized){if(!c3_stage_renderer_try_shutdown())return fail(error,cap,"Renderer busy during swap.");s->initialized=0;}
    c3_particle_cancel(&s->particles);c3_wzx_damage_cancel(&s->damage);
    c3_title_close(&s->models[m]);memset(&s->models[m],0,sizeof s->models[m]);
    int shared=-1;
    for(uint32_t j=0;j<s->count;++j)if((int)j!=m&&s->slots[j]<6&&!s->models[j].borrowed_storage&&!strcmp(s->files[j],model.file)){shared=(int)j;break;}
    int ok;
    if(shared>=0)ok=c3_title_instance(&s->models[m],&s->models[shared],error,cap);
    else ok=snprintf(path,sizeof path,"%s/%s",root,model.file)<(int)sizeof path&&c3_title_load(&s->models[m],path,error,cap);
    if(!ok){if(!error[0])fail(error,cap,"Swap pack load failed.");return 0;}
    snprintf(s->files[m],sizeof s->files[m],"%s",model.file);
    c3_battle3d_roles(&model,s->roles[m]);
    C3Title *t=&s->models[m];
    if(s->roles[m][0]>=t->clip_count)return fail(error,cap,"Swapped model has no idle clip.");
    for(unsigned r=0;r<9;++r)if(s->roles[m][r]!=C3_BATTLE3D_NO_CLIP&&s->roles[m][r]>=t->clip_count)s->roles[m][r]=s->roles[m][0];
    s->clips[m]=s->roles[m][0];s->clocks[m]=0;s->hold[m]=0;s->appear[m]=1;s->sampled[m]=0;
    s->transforms[m][4]=model.scale;s->cast.species[slot]=species;s->presentation.visible[slot]=0;
    memcpy(s->part_bone[slot],model.part_bone,17);memcpy(s->timing[slot],model.timing,sizeof s->timing[slot]);
    memcpy(s->entry_clip[slot],model.entry_clip,sizeof s->entry_clip[slot]);
    s->draw[m]=t;
    return renderer_start(s,error,cap);
}

int c3_battle_general_stage_prepare(C3BattleGeneralStage *s,const C3BattlePreviewEvent *e,uint32_t count,
                                    const char *root,char *error,size_t cap)
{
    if(!s||!s->wzx)return 1;
    int moves=0;for(uint32_t i=0;i<count;++i)moves|=e[i].kind==C3_PREVIEW_EVENT_MOVE||(e[i].kind==C3_PREVIEW_EVENT_STATUS&&e[i].effect);
    if(!moves&&s->count==s->wzx->base_count)return 1;
    if(s->initialized){if(!c3_stage_renderer_try_shutdown())return fail(error,cap,"Renderer busy during effect preparation.");s->initialized=0;}
    c3_particle_cancel(&s->particles);c3_wzx_damage_cancel(&s->damage);
    wzx_prepare(s,e,count,root);
    for(uint32_t i=s->wzx->base_count;i<s->count;++i)s->sampled[i]=0;
    return renderer_start(s,error,cap);
}

void c3_battle_general_stage_command_phase(C3BattleGeneralStage *s,int on)
{
    if(!s||!s->wzx)return;
    struct C3BattleWzxLayer *w=s->wzx;
    if(w->command_phase==(uint8_t)(on!=0))return;
    w->command_phase=(uint8_t)(on!=0);
    if(w->camera_ok){if(w->sequence_camera){c3_wzx_camera_end(&w->camera);w->sequence_camera=0;}c3_wzx_camera_idle_reset(&w->camera,on);}
    if(on)s->camera=w->home;
}

int c3_battle_general_stage_draw(C3BattleGeneralStage *s,C3TitleRenderStats *stats,char *error,size_t cap)
{
    return c3_stage_renderer_draw(s->draw,s->count,&s->camera,1,stats,error,cap);
}

int c3_battle_general_stage_close(C3BattleGeneralStage *s)
{
    c3_particle_cancel(&s->particles);c3_wzx_damage_cancel(&s->damage);
    if(s->initialized&&!c3_stage_renderer_try_shutdown())return 0;
    s->initialized=0;release(s);return 1;
}
