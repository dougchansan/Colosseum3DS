#include "colosseum3ds/field_story.h"
#include "colosseum3ds/battle.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define PAD_A 0x0100u
#define PAD_B 0x0200u
#define PAD_DIRECTIONS 0x000fu
#define MAX_WAYPOINTS 64u /* Phenac actor 24 allocates 36 points (bank 013E0E00 fn 34). */

typedef enum { W_NONE, W_SECONDS, W_TASK, W_MOTION, W_MOVE, W_CAMERA,
               W_CAMERA_MOVE, W_FADE, W_MESSAGE, W_CHOICE, W_TV, W_BATTLE } Wait;
typedef struct {
    uint16_t key, child;
    C3StoryCall call;
    Wait wait;
    float remaining;
    uint32_t value, stage;
    int child_finished;
} RunTask;
typedef struct Person {
    C3StoryActor public;
    const C3AreaActor *source;
    float target[3], look[3], old_yaw, speed, turn_target, turn_speed;
    double turn_fraction;
    int moving, turning, talking, looking;
    float waypoints[MAX_WAYPOINTS][3];
    uint32_t waypoint_capacity, waypoint_count, waypoint_cursor;
    int waypoint_active, waypoint_loop;
    struct Person *parent;
    float local_position[3],local_rotation[3],world_rotation[3],world_scale[3];
} Person;
typedef struct {
    C3Story vm;
    C3GameState *game;
    C3FieldMap *map;
    const C3Area *area;
    C3FieldPlayer *hero;
    C3Title *hero_model;
    const C3AreaCamera *camera_track;
    Person actors[C3_AREA_MAX_ACTORS], hero_actor;
    RunTask tasks[C3_STORY_TASKS];
    C3FieldStoryView view;
    C3BattleData battle_data;
    C3BattleRequest battle_request;
    int battle_loaded;
    FILE *trace;
    uint16_t setup_key, talk_key, event_key, post_key, main_key, exit_fade_key;
    int setup_stage, preparing, entered, hero_ready, event_depth, departure_started;
    int camera_mode, camera_playing;
    float fade_elapsed, fade_duration, fade_from, fade_to, camera_rate;
    float camera_from[3],camera_to[3],camera_direction[3],camera_move_time,camera_move_duration;
    float target_from[3],target_to[3],target_time,target_duration;
    C3TitleCamera return_from,return_to;
    float return_time,return_duration;
    Person *camera_person;
    int target_moving,camera_returning;
    uint32_t sound_id, previous_floor, pending_floor, pending_spawn;
    uint32_t fight_result; /* fn_801EF634: last original fight result ID */
    uint32_t whiteout_floor,whiteout_previous,whiteout_spawn;int whiteout_valid;
    uint16_t (*random16)(void *context);void *random_context;
    Person partner_actor; /* (0,101); invisible until native 23 */
    int (*floor_supported)(uint32_t floor);
    uint32_t dialog_owner;
    size_t page_start, page_end;
    char full_message[C3_STORY_MESSAGE], hero_name[32], speaker[128];
    uint32_t cosmetic_logged[8];
} Impl;

static int fail(char *error,const char *format,...)
{
    if(error){va_list ap;va_start(ap,format);vsnprintf(error,C3_STORY_ERROR,format,ap);va_end(ap);}return 0;
}
static void log_line(Impl *s,const char *format,...)
{
    if(!s->trace)return;
    va_list ap;va_start(ap,format);vfprintf(s->trace,format,ap);va_end(ap);fputc('\n',s->trace);fflush(s->trace);
}
static float as_float(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static float angle_delta(float x){return remainderf(x,2*PI);}
/* The pinned native table describes words, not C pointers. Validate the
 * complete call before changing any scene state; absent arguments must not
 * acquire the VM's zero-filled defaults. Float masks follow the table's type2.
 * Bounds here are this adapter's admission limits, not a new GSVM format. */
static int validate_native(const Impl *s,const C3StoryCall *c,char *error)
{
    static const struct {uint8_t id,argc,floats;} signature[]={
        {0x03,6,0},{0x04,1,0},{0x06,1,0},{0x07,3,0},{0x08,1,0},{0x0b,0,0},
        {0x1c,2,2},{0x1d,1,0},{0x1e,0,0},{0x1f,0,0},{0x20,0,0},{0x21,0,0},
        {0x24,1,0},{0x25,2,0},{0x26,3,7},{0x27,3,7},{0x28,3,7},{0x29,3,7},
        {0x2a,4,0},{0x2b,0,0},{0x2c,0,0},{0x2d,0,0},{0x2e,1,0},{0x30,4,8},
        {0x32,3,0},{0x33,5,0},{0x34,2,0},{0x35,3,0},{0x36,3,0},{0x37,2,0},
        {0x3a,6,60},{0x3b,5,28},{0x3c,5,28},{0x3d,6,56},{0x3e,3,0},
        {0x3f,3,0},{0x40,3,0},{0x41,2,0},{0x42,5,0},{0x43,2,0},{0x44,3,4},{0x45,3,4},
        {0x46,5,0},{0x47,2,0},{0x48,3,0},{0x49,5,28},{0x4a,3,0},
        {0x4c,2,0},{0x4d,2,0},{0x4e,1,0},{0x4f,1,0},{0x50,2,0},{0x51,1,0},
        {0x5c,0,0},{0x5d,0,0},{0x5e,0,0},{0x5f,2,0},{0x64,2,0},{0x65,4,14},
        {0x67,0,0},{0x68,3,0},{0x69,3,0},{0x6a,2,0},{0x6d,2,0},{0x72,2,0},{0x76,2,0},
        {0x7d,3,0},{0x7f,3,7},{0x88,0,0},{0x8a,4,0},{0x8c,5,30},{0x98,3,0},
        {0x99,1,0},{0x9d,1,1},{0xb3,2,1},{0xb6,1,0},{0xd1,5,0},{0xd3,0,0},{0xe4,0,0},
        /* Phenac City exterior bank 013E0E00 (native table lbl_802E1CF0). */
        {0x02,1,0},{0x16,2,0},{0x23,1,0},{0x31,0,0},{0x66,1,0},{0x7e,2,0},
        {0x82,3,0},{0x83,3,0},{0x89,3,0},{0xa1,3,0},{0xa4,6,0x3c},{0xa5,5,0x10},
        {0xa9,2,0},{0xb4,0,0},{0xda,1,0}
    };
    for(unsigned i=0;i<sizeof signature/sizeof signature[0];++i){
        if(signature[i].id!=c->native)continue;
        /* D1 is the original synchronous script executor: the four child
         * arguments may be omitted (retail floor init uses only scriptID),
         * and the VM supplies zero for those missing trailing words. */
        /* Phenac main calls 4C/4D with no arguments: the dispatcher's zeroed
         * register image addresses person (0,0), which does not exist. */
        if((c->native==0x4c||c->native==0x4d)&&!c->argc)return 1;
        if((c->native==0xd1?c->argc<1||c->argc>5:c->argc!=signature[i].argc))return fail(error,"Native %02lx expects %u arguments, received %lu",
            (unsigned long)c->native,signature[i].argc,(unsigned long)c->argc);
        for(unsigned j=0;j<c->argc;++j){
            if(!(signature[i].floats&(1u<<j)))continue;
            float value=as_float(c->args[j]),minimum=-100000,maximum=100000,scale=s->map->scale;
            switch(c->native){
            case 0x1c:case 0x30:case 0xb3:minimum=0;maximum=60;scale=1;break;
            case 0x9d: minimum=0;maximum=3600;scale=1;break;
            case 0x29: minimum=-100;maximum=100;scale=1;break;
            case 0x3c:case 0x44:minimum=-100;maximum=100;scale=PI/180.0f;break;
            case 0x45:minimum=0;maximum=100;scale=1;break;
            case 0xa4:if(j==5){minimum=0;maximum=100;scale=1;}break;
            case 0xa5:minimum=0;maximum=100;scale=1;break;
            case 0x7f:if(j==2){minimum=-100;maximum=100;scale=1;}break;
            case 0x8c:if(j==4){minimum=0;maximum=60;scale=1;}break;
            default:break;
            }
            float converted=value*scale;
            if(!isfinite(value)||!isfinite(converted)||converted<minimum||converted>maximum)
                return fail(error,"Native %02lx argument %u is outside its finite range",(unsigned long)c->native,j);
        }
        return 1;
    }
    return fail(error,"Unported native %02lx in GSVM %08lx at %08lx",(unsigned long)c->native,
                (unsigned long)c->script_id,(unsigned long)c->pc);
}
static int flag(Impl *s,uint32_t id,uint32_t *out,char *error)
{return c3_game_state_get_flag(s->game,id,out)||fail(error,"Original flag %04lx is unavailable",(unsigned long)id);}
static int set_flag(Impl *s,uint32_t id,uint32_t value,char *error)
{
    if(!c3_game_state_set_flag(s->game,id,value))return fail(error,"Original flag %04lx cannot be written",(unsigned long)id);
    log_line(s,"story_flag id=%04lx value=%lu",(unsigned long)id,(unsigned long)value);return 1;
}
static Person *person(Impl *s,uint32_t group,uint32_t id)
{
    if(!group&&id==100)return &s->hero_actor;
    if(!group&&id==101)return s->partner_actor.public.model?&s->partner_actor:NULL;
    if(group!=s->area->group)return NULL;
    for(uint32_t i=0;i<s->area->actor_count;++i)if(s->actors[i].public.index==id)return s->actors+i;
    return NULL;
}
static C3FieldPlayer *pose(Impl *s,Person *p)
{return p==&s->hero_actor?s->hero:&p->public.pose;}
static float clip_seconds(const C3StoryActor *p)
{
    if(!p->model||p->clip>=p->model->clip_count)return 0;
    return (float)(p->model->clips[p->clip].frame_count-1u)/p->model->fps;
}
static void note_presentation(Impl *s,uint32_t native,const char *reason)
{
    uint32_t mask=1u<<(native&31u),i=native>>5;
    if(i>=8||s->cosmetic_logged[i]&mask)return;
    s->cosmetic_logged[i]|=mask;
    log_line(s,"story_presentation native=%02lx status=unrendered reason=%s",(unsigned long)native,reason);
}
static int start(Impl *s,uint32_t script,const uint32_t *args,uint32_t argc,uint16_t *key,char *error)
{
    if(!script){*key=0;return 1;}
    RunTask *t=NULL;
    for(unsigned i=0;i<C3_STORY_TASKS;++i)
        if(!s->tasks[i].key||!c3_story_active(&s->vm,s->tasks[i].key)){t=s->tasks+i;break;}
    if(!t)return fail(error,"Native story task table is full");
    uint16_t next;
    if(!c3_story_start(&s->vm,script,args,argc,&next,error))return 0;
    memset(t,0,sizeof(*t));t->key=next;*key=next;
    log_line(s,"story_start key=%u script=%08lx argc=%lu",next,(unsigned long)script,(unsigned long)argc);return 1;
}
static int resume(Impl *s,RunTask *t,uint32_t value,char *error)
{
    t->wait=W_NONE;
    return c3_story_resume(&s->vm,t->key,value)||fail(error,"Cannot resume original story task %u",t->key);
}
static void page(Impl *s)
{
    const char *begin=s->full_message+s->page_start;
    const char *end=strchr(begin,'\f');
    s->page_end=end?(size_t)(end-s->full_message):strlen(s->full_message);
    size_t n=s->page_end-s->page_start;
    memcpy(s->view.message,begin,n);s->view.message[n]=0;
}
static int message(Impl *s,RunTask *t,uint32_t id,const char *speaker,char *error)
{
    if(s->dialog_owner&&s->dialog_owner!=t->key)return fail(error,"Concurrent original field messages are unsupported");
    if(!c3_story_message(&s->vm,id,s->hero_name,"",speaker,s->full_message,sizeof s->full_message,error))return 0;
    s->dialog_owner=t->key;s->page_start=0;s->view.message_id=id;s->view.dialogue=1;s->view.choice=0;
    page(s);t->wait=W_MESSAGE;t->remaining=.12f;
    log_line(s,"story_message script=%08lx pc=%08lx id=%05lx",(unsigned long)t->call.script_id,
             (unsigned long)t->call.pc,(unsigned long)id);return 1;
}
static void close_message(Impl *s)
{s->view.dialogue=0;s->view.choice=0;s->dialog_owner=0;s->view.message_id=0;s->view.message[0]=0;}
static void restore_speakers(Impl *s)
{
    for(uint32_t i=0;i<s->area->actor_count;++i){Person *p=s->actors+i;
        if(p->talking&&p->source->talk_end==1)p->public.pose.yaw=p->old_yaw;
        p->talking=0;
    }
}
static int set_motion(Impl *s,Person *p,uint32_t source_clip,uint32_t frame,uint32_t loop,char *error)
{
    if(!p)return 1; /* Retail missing people return false/no-op. */
    uint32_t clip=source_clip;
    if(p==&s->hero_actor){
        if(source_clip>3)return fail(error,"Wes source motion %lu is not prepared",(unsigned long)source_clip);
        /* prepare_area preserves [source1,source2,source3] and appends the
         * distinct source0 at bundle3. Never alias that animation to idle. */
        clip=source_clip==0?3:source_clip==1?s->map->idle_clip:source_clip==2?s->map->walk_clip:s->map->run_clip;
    }
    if(!p->public.model||clip>=p->public.model->clip_count)
        return fail(error,"Actor %lu source motion %lu is not prepared",(unsigned long)p->public.index,(unsigned long)source_clip);
    C3FieldPlayer *q=pose(s,p);
    int restart=p->public.clip!=clip||!p->public.playing||(!p->public.loop&&q->animation_seconds>=clip_seconds(&p->public));
    p->public.clip=clip;p->public.loop=!!loop;p->public.playing=1;
    if(restart)q->animation_seconds=(float)frame/30.0f;
    return 1;
}
static void start_turn(Impl *s,Person *p,float yaw,float speed)
{
    /* peopleStartTurn / fn_8018805C: retain the model's revolution. */
    yaw+=6.2831855f*(int32_t)(pose(s,p)->yaw/6.2831855f);
    p->turn_target=yaw;p->turn_speed=speed;p->turn_fraction=0;p->turning=1;
}
static int tv_setup(Impl *s,char *error)
{
    /* Native D3 selects later reports from source story flags. This adapter
     * admits the first report only; later saves cannot silently select it. */
    static const uint32_t later[]={0x0ADA,0x0ADB,0x042C,0x0AD3,0x0AD0,0x0ACF};
    uint32_t value;
    for(unsigned i=0;i<sizeof later/sizeof later[0];++i){
        if(!flag(s,later[i],&value,error))return 0;
        if(value)return fail(error,"Later TV report requires its original assets and native branch");
    }
    if(!set_flag(s,0x0A05,1,error))return 0;
    s->view.tv_enabled=1;return 1;
}
static int run_native(Impl *s,RunTask *t,char *error)
{
    C3StoryCall *c=&t->call;uint32_t *a=c->args,value=0;Person *p;
    if(c->kind==C3_STORY_WAIT_FRAMES){t->wait=W_SECONDS;t->remaining=(float)a[0]/60.0f;return 1;}
    if(c->kind!=C3_STORY_NATIVE)return fail(error,"Original GSVM print request needs a string adapter");
    if(!validate_native(s,c,error))return 0;
    if(c->native!=0x51&&c->native!=0x47&&c->native!=0x1f&&c->native!=0x3a)
        log_line(s,"story_native task=%u script=%08lx pc=%08lx id=%02lx argc=%lu a=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx",
            t->key,(unsigned long)c->script_id,(unsigned long)c->pc,(unsigned long)c->native,(unsigned long)c->argc,
            (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2],(unsigned long)a[3],(unsigned long)a[4],(unsigned long)a[5]);
    switch(c->native){
    case 0x03:
        if(!start(s,a[0],a+2,4,&t->child,error))return 0;
        value=t->child;break;
    case 0x04: c3_story_stop(&s->vm,(uint16_t)a[0]);break;
    case 0x06:
        t->child=(uint16_t)a[0];t->child_finished=0;
        if(c3_story_active(&s->vm,t->child)){t->wait=W_TASK;return 1;}break;
    case 0x07:
        if(c->argc<3||a[1]!=1)return fail(error,"Only blocking original field messages are supported");
        return message(s,t,a[0],s->speaker,error);
    case 0x08: close_message(s);break;
    case 0x0b:
        s->dialog_owner=t->key;s->view.dialogue=1;s->view.choice=1;s->view.choice_no=0;
        t->wait=W_CHOICE;t->remaining=.12f;return 1;
    case 0x1c:
        if(a[0]!=2&&a[0]!=3)return fail(error,"Original white fade effect %lu is not prepared",(unsigned long)a[0]);
        s->fade_from=s->view.fade;s->fade_to=a[0]==2?1.0f:0.0f;
        s->fade_elapsed=0;s->fade_duration=as_float(a[1]);
        if(!s->fade_duration)s->view.fade=s->fade_to;
        break;
    case 0x1d:
        if(s->fade_elapsed<s->fade_duration){if(a[0]){t->wait=W_FADE;return 1;}value=1;}break;
    case 0x1e: break; /* Hero initialization alone does not execute movement. */
    case 0x1f: s->hero_ready=1;t->wait=W_SECONDS;t->remaining=1.0f/60.0f;return 1;
    case 0x20: ++s->event_depth;s->hero_ready=0;break;
    case 0x21: if(s->event_depth)--s->event_depth;break;
    case 0x24:
        if(a[0]!=0&&a[0]!=3&&a[0]!=4)return fail(error,"Camera mode %lu is not prepared",(unsigned long)a[0]);
        value=(uint32_t)s->camera_mode;s->camera_mode=(int)a[0];
        s->view.camera_active=s->camera_mode!=0;break;
    case 0x25:
        if(a[0]||a[1]!=100)return fail(error,"Camera target %lu:%lu is not prepared",(unsigned long)a[0],(unsigned long)a[1]);
        s->camera_person=&s->hero_actor;
        break;
    case 0x26: for(unsigned i=0;i<3;++i)s->view.camera.target[i]=as_float(a[i])*s->map->scale;break;
    case 0x27:
        if(as_float(a[0])!=0||as_float(a[2])!=0)return fail(error,"Nonvertical follow-camera offset is not prepared");
        s->map->camera_target_height=as_float(a[1])*s->map->scale;break;
    case 0x28: for(unsigned i=0;i<3;++i)s->view.camera.eye[i]=as_float(a[i])*s->map->scale;break;
    case 0x29: {
        float x=as_float(a[0]),y=as_float(a[1]);
        if(as_float(a[2])!=0)return fail(error,"Original camera roll is not prepared");
        s->view.camera.target[0]=s->view.camera.eye[0]-sinf(y)*cosf(x);
        s->view.camera.target[1]=s->view.camera.eye[1]+sinf(x);
        s->view.camera.target[2]=s->view.camera.eye[2]-cosf(y)*cosf(x);break;
    }
    case 0x2a:
        if(a[0]!=s->area->group||a[1]!=0x00e51800u||!s->camera_track||!s->camera_track->frames)
            return fail(error,"Original camera %08lx is not prepared",(unsigned long)a[1]);
        s->view.camera_seconds=(float)a[2]/30.0f;s->camera_playing=1;s->camera_rate=1;
        s->view.camera_active=1;s->camera_mode=4;break;
    case 0x2b:s->view.camera_active=0;s->camera_playing=0;break;
    case 0x2c:s->camera_playing=0;break;
    case 0x2d:s->camera_playing=1;break;
    case 0x2e:
        if(s->view.camera_active&&s->camera_track&&s->camera_track->frames&&
           s->view.camera_seconds<(float)(s->camera_track->frame_count-1u)/s->camera_track->fps){
            if(a[0]){t->wait=W_CAMERA;return 1;}value=1;
        }break;
    case 0x30:
        p=person(s,a[1],a[2]);
        if(!p||s->camera_mode!=3)return fail(error,"Original target interpolation outside the prepared free-camera scene is not ported");
        s->camera_person=p;s->target_time=0;s->target_duration=as_float(a[3]);s->target_moving=1;
        memcpy(s->target_from,s->view.camera.target,sizeof s->target_from);
        memcpy(s->target_to,pose(s,p)->position,sizeof s->target_to);break;
    case 0x32:
        p=person(s,a[0],a[1]);if(p){p->public.visible=!!a[2];
            if(a[2]&&!p->public.model)return fail(error,"Visible actor %lu is not prepared",(unsigned long)a[1]);}break;
    case 0x33:
        p=person(s,a[0],a[1]);if(!set_motion(s,p,a[2],a[3],a[4],error))return 0;value=p!=NULL;break;
    case 0x34:p=person(s,a[0],a[1]);if(p)p->public.playing=0;break;
    case 0x35:
        p=person(s,a[0],a[1]);
        if(p&&p->public.playing&&pose(s,p)->animation_seconds<clip_seconds(&p->public)){
            if(!a[2])value=1;
            else if(!p->public.loop){t->wait=W_MOTION;return 1;}
        }break;
    case 0x36:
        p=person(s,a[0],a[1]);s->speaker[0]=0;
        if(p&&p->source){
            uint32_t known=1,msg=p->source->name_message;
            if(p->source->hear_flag&&!flag(s,p->source->hear_flag,&known,error))return 0;
            if(!known)msg=0x0fa3;
            if(msg&&!c3_story_message(&s->vm,msg,s->hero_name,"","",s->speaker,sizeof s->speaker,error))return 0;
            if(!p->talking){p->old_yaw=p->public.pose.yaw;p->talking=1;}
            if(p->source->talk_start==2)p->public.pose.yaw=atan2f(s->hero->position[0]-p->public.pose.position[0],s->hero->position[2]-p->public.pose.position[2]);
        }
        return message(s,t,a[2],s->speaker,error);
    case 0x37:close_message(s);restore_speakers(s);break;
    case 0x3a:
        p=person(s,a[0],a[1]);if(p){C3FieldPlayer *q=pose(s,p);float x=q->position[0]/s->map->scale,z=q->position[2]/s->map->scale;
            value=x>=fminf(as_float(a[2]),as_float(a[4]))&&x<=fmaxf(as_float(a[2]),as_float(a[4]))&&
                  z>=fminf(as_float(a[3]),as_float(a[5]))&&z<=fmaxf(as_float(a[3]),as_float(a[5]));}break;
    case 0x3b:
        p=person(s,a[0],a[1]);if(p){for(unsigned i=0;i<3;++i)
            (p->parent?p->local_position:pose(s,p)->position)[i]=as_float(a[i+2])*s->map->scale;}break;
    case 0x3c:
        p=person(s,a[0],a[1]);
        if(p){
            if(p->parent){for(unsigned i=0;i<3;++i)p->local_rotation[i]=as_float(a[i+2])*PI/180.0f;}
            else {if(as_float(a[2])!=0||as_float(a[4])!=0)return fail(error,"Actor pitch/roll is not prepared");
                p->turn_target=pose(s,p)->yaw=as_float(a[3])*PI/180.0f;}
        }break;
    case 0x3d:
        p=person(s,a[0],a[1]);if(p){for(unsigned i=0;i<3;++i)p->target[i]=as_float(a[i+3])*s->map->scale;
            if(p->parent)return fail(error,"Attached actor walking is not prepared");
            p->moving=1;p->waypoint_active=0;p->speed=1;p->public.flags|=8u;
            start_turn(s,p,atan2f(p->target[0]-pose(s,p)->position[0],p->target[2]-pose(s,p)->position[2]),1);
            if(!a[2])p->turning=0; /* Original keepFacing==0 drops the turn, not the destination. */
            uint32_t motion=p->source?p->source->motions[1]:2;
            if(!set_motion(s,p,motion,0,1,error))return 0;
        }break;
    case 0x3e:
        p=person(s,a[0],a[1]);if(p&&(p->moving||p->turning)){if(a[2]){t->wait=W_MOVE;return 1;}value=1;}break;
    case 0x3f:case 0x40:
        p=person(s,a[0],a[1]);if(p){if(c->native==0x3f)p->public.flags|=a[2];else p->public.flags&=~a[2];}break;
    case 0x41:case 0x42:case 0x43:
        p=person(s,a[0],a[1]);if(p){p->looking=c->native!=0x43;
            for(unsigned i=0;i<3;++i)p->look[i]=c->native==0x41?s->hero->position[i]:(float)(int32_t)a[i+2]*s->map->scale;
            note_presentation(s,c->native,"procedural_head_aim");}break;
    case 0x44:p=person(s,a[0],a[1]);if(p)start_turn(s,p,as_float(a[2])*PI/180.0f,p->speed);break;
    case 0x45:p=person(s,a[0],a[1]);if(p)p->speed=as_float(a[2]);break;
    case 0x46:{
        p=person(s,a[0],a[1]);Person *parent=person(s,a[2],a[3]);
        if(!p||!parent)break;
        if(!parent->source||parent->source->resource!=0x010b0400u||a[4]!=1||
           !parent->public.model||!parent->source->part1_bone_plus_one||
           parent->source->part1_bone_plus_one>parent->public.model->bone_count||parent->parent||p==parent)
            return fail(error,"Original attachment part %lu on actor %lu is not prepared",(unsigned long)a[4],(unsigned long)a[3]);
        /* GSmodelAttachToGSpart(type7,updateOnce0,reset1). The reviewed
         * vehicle has an identity source root and one sampled part node. */
        p->parent=parent;p->moving=p->turning=p->waypoint_active=0;
        memset(p->local_position,0,sizeof p->local_position);memset(p->local_rotation,0,sizeof p->local_rotation);
        value=1;break;
    }
    case 0x47:t->wait=W_SECONDS;t->remaining=1.0f/60.0f;return 1;
    case 0x48:
        if(!a[2]||a[2]>MAX_WAYPOINTS)return fail(error,"Original waypoint capacity exceeds the prepared bound");
        p=person(s,a[0],a[1]);if(p){p->waypoint_capacity=a[2];p->waypoint_count=p->waypoint_cursor=0;
            p->waypoint_active=p->moving=0;value=1;}break;
    case 0x49:
        p=person(s,a[0],a[1]);if(p&&p->waypoint_count<p->waypoint_capacity){
            for(unsigned i=0;i<3;++i)p->waypoints[p->waypoint_count][i]=as_float(a[i+2])*s->map->scale;
            ++p->waypoint_count;value=1;}break;
    case 0x4a:
        p=person(s,a[0],a[1]);if(p){
            if(p->parent||!p->waypoint_capacity||p->waypoint_count!=p->waypoint_capacity)
                return fail(error,"Original waypoint list is incomplete or attached");
            p->waypoint_cursor=0;p->waypoint_active=1;p->waypoint_loop=!!a[2];p->moving=1;p->speed=1;
            memcpy(p->target,p->waypoints[0],sizeof p->target);
            start_turn(s,p,atan2f(p->target[0]-pose(s,p)->position[0],p->target[2]-pose(s,p)->position[2]),1);
            if(!set_motion(s,p,p->source?p->source->motions[1]:2,0,1,error))return 0;
            value=1;
        }break;
    case 0x4c:case 0x4d:
        p=person(s,a[0],a[1]);if(p){
            if(c->native==0x4c)p->public.flags=(p->public.flags&~0x500u)|0x80000000u;
            else p->public.flags=(p->public.flags&~0x80000000u)|0x500u;
        }break;
    case 0x4e:if(!set_flag(s,a[0],1,error))return 0;break;
    case 0x4f:if(!set_flag(s,a[0],0,error))return 0;break;
    case 0x50:if(!set_flag(s,a[0],a[1],error))return 0;break;
    case 0x51:if(!flag(s,a[0],&value,error))return 0;break;
    case 0x5c:value=s->area->floor;break;
    case 0x5d:value=s->area->group;break;
    case 0x5e:value=s->previous_floor;break;
    case 0x5f:case 0x7d:
        if(s->floor_supported?!s->floor_supported(a[0]):(a[0]!=1&&a[0]!=2)){snprintf(s->view.blocked,sizeof s->view.blocked,"Area %lu is not prepared in this build.",(unsigned long)a[0]);t->wait=W_BATTLE;return 1;}
        s->pending_floor=a[0];s->pending_spawn=a[1];break;
    case 0x64:
        p=person(s,s->area->group,a[0]);if(p){p->public.visible=!!a[1];
            if(a[1]&&!p->public.model)return fail(error,"Original actor %lu is visible but its pack is missing",(unsigned long)a[0]);}break;
    case 0x65:
        p=person(s,s->area->group,a[0]);if(p){for(unsigned i=0;i<3;++i)
            (p->parent?p->local_position:pose(s,p)->position)[i]=as_float(a[i+1])*s->map->scale;}break;
    case 0x02:
        /* fadeEffectGetRandom: ((second draw << 16) | first draw) % limit
         * from the shared 16-bit game RNG. */
        if(!s->random16)return fail(error,"Native 02 needs the game RNG, which this scene did not bind");
        if(!a[0])return fail(error,"Native 02 was called with a zero limit");
        {uint32_t low=s->random16(s->random_context)&0xffffu,high=s->random16(s->random_context)&0xffffu;
         value=((high<<16)|low)%a[0];}
        break;
    case 0x16:
        /* menuNameEntryOpen pushes the name-entry floor 0x390 and returns its
         * result. That menu is not ported: the saved default name is kept. */
        log_line(s,"story_name_entry mode=%lu index=%lu status=default_name_kept menu_ported=0",
                 (unsigned long)a[0],(unsigned long)a[1]);
        value=1;break;
    case 0x23:
        /* fn_801C9F00: heroSetStatus(0,0x18,active) and the follower state. */
        if(!c3_game_state_set_partner(s->game,a[0]!=0))return fail(error,"The partner state cannot be written");
        s->partner_actor.public.visible=a[0]!=0;
        log_line(s,"story_partner present=%d",a[0]!=0);
        break;
    case 0x31:
        /* cameraUpdate: recompute the field camera now. The scene derives
         * its camera from the current state on every frame. */
        break;
    case 0x66:case 0x98:
        if(c->native==0x66){a[1]=0;a[2]=0;}
        s->view.battle_id=a[0];
        if(s->battle_loaded&&a[0]==10){
            size_t bytes=0;const uint8_t *body=c3_game_state_body(s->game,&bytes);
            if(!c3_battle_request(&s->battle_request,&s->battle_data,a,body,bytes,error))return 0;
            log_line(s,"story_battle_snapshot encounter=%lu player_party=%u status=waiting_backend save_unchanged=1",
                     (unsigned long)a[0],s->battle_request.player_party_count);
        }
        snprintf(s->view.blocked,sizeof s->view.blocked,"Original battle %lu requested.\nCombat backend is not ported yet.\nNo result or victory flag was written.",(unsigned long)a[0]);
        log_line(s,"story_battle encounter=%lu status=waiting_backend result_written=0",(unsigned long)a[0]);
        t->wait=W_BATTLE;return 1;
    case 0x67:
        /* fn_801EF634 (native table row 0x67): the result ID the last fight
         * left in the fight-floor global. Zero until a battle has finished. */
        value=s->fight_result;break;
    case 0x68:case 0x69:
        s->sound_id=a[0];note_presentation(s,c->native,"field_audio_backend_pending");break;
    case 0x6a:case 0x6d:case 0x8a:case 0x99:
        note_presentation(s,c->native,"field_audio_backend_pending");break;
    case 0x72:
        /* Current entry enables the already visible exterior map resource. */
        if(a[1]!=1)return fail(error,"Dynamic map-resource visibility is not prepared");
        break;
    case 0x76:
        value=c3_field_set_object_enabled(s->map,a[0],!!a[1])?0:2;break;
    case 0x7f:
        if(as_float(a[0])>=0)s->map->camera_height=as_float(a[0])*s->map->scale;
        if(as_float(a[1])>=0)s->map->camera_distance=as_float(a[1])*s->map->scale;
        if(as_float(a[2])>=0)s->map->camera_yaw=as_float(a[2]);
        break;
    case 0x88:
        /* Original disables field-camera auto tracking. The explicit scene
         * camera already owns it; mode/position natives follow this call. */
        break;
    case 0x8c:
        s->camera_move_duration=as_float(a[4]);s->camera_move_time=0;
        if(!isfinite(s->camera_move_duration)||s->camera_move_duration<0||s->camera_move_duration>60)
            return fail(error,"Invalid original camera movement duration");
        for(unsigned i=0;i<3;++i){s->camera_from[i]=s->view.camera.eye[i];s->camera_to[i]=as_float(a[i+1])*s->map->scale;
            s->camera_direction[i]=s->view.camera.target[i]-s->view.camera.eye[i];}
        break;
    case 0x7e:
        /* heroMoveSetNeckMode: procedural head tracking of a party member. */
        note_presentation(s,c->native,"head_tracking_unrendered");
        value=a[1]<=1&&(a[0]==0||(a[0]==1&&s->partner_actor.public.visible));break;
    case 0x82:
        /* fn_80165668 pauses the BGM, plays a jingle and blocks until it ends.
         * The length lives in the sound data; without the audio backend the
         * script waits a fixed substitute time. */
        note_presentation(s,c->native,"jingle_backend_pending_wait_2s");
        t->wait=W_SECONDS;t->remaining=2.0f;return 1;
    case 0x83:
        /* fn_80181EB0: "!" emote attached to the person's head part. */
        note_presentation(s,c->native,"emote_bubble_unrendered");break;
    case 0x89:case 0xda:
        note_presentation(s,c->native,"field_audio_backend_pending");break;
    case 0xa1:
        /* fn_8011416C: temporary whiteout return point (floor, previous
         * floor, entrance) consumed by fn_80113FE8 after a lost battle. */
        s->whiteout_floor=a[0];s->whiteout_previous=a[1];s->whiteout_spawn=a[2]&0xffu;s->whiteout_valid=1;break;
    case 0xa4:
        /* fn_80187D48: turn toward a point; y is ignored. */
        p=person(s,a[0],a[1]);
        if(p)start_turn(s,p,atan2f(as_float(a[2])*s->map->scale-pose(s,p)->position[0],
                                   as_float(a[4])*s->map->scale-pose(s,p)->position[2]),as_float(a[5]));
        break;
    case 0xa5:{
        /* fn_80187A60: turn toward another person's model. */
        Person *target=person(s,a[2],a[3]);p=person(s,a[0],a[1]);
        if(p&&target)start_turn(s,p,atan2f(pose(s,target)->position[0]-pose(s,p)->position[0],
                                           pose(s,target)->position[2]-pose(s,p)->position[2]),as_float(a[4]));
        }break;
    case 0xa9:{
        /* peopleGazeHeroCheck = fn_80186284(group,index,3.0,0,100,30.0):
         * hero within 30 source units and within 15 degrees of the person's
         * facing. The original also requires a clear NPC-wall segment
         * (fn_8010F188); that query is not ported, so walls do not block. */
        p=person(s,a[0],a[1]);
        if(p&&p->public.visible){
            float dx=s->hero->position[0]-pose(s,p)->position[0],dy=s->hero->position[1]-pose(s,p)->position[1],
                  dz=s->hero->position[2]-pose(s,p)->position[2];
            float distance=sqrtf(dx*dx+dy*dy+dz*dz);
            if(distance<=30.0f*s->map->scale){
                float away=fabsf(angle_delta(atan2f(dx,dz)-pose(s,p)->yaw));
                value=away<=15.0f*PI/180.0f;
            }
        }
        }break;
    case 0xb4:
        /* floorGetNextFloorID: destination of the most recent link; on
         * arrival it still names the current floor. */
        value=s->pending_floor?s->pending_floor:s->area->floor;break;
    case 0x9d:
        t->remaining=as_float(a[0]);if(!isfinite(t->remaining)||t->remaining<0||t->remaining>3600)return fail(error,"Invalid story wait duration");
        t->wait=W_SECONDS;return 1;
    case 0xb3:
        s->camera_mode=0;s->camera_playing=0;s->camera_person=&s->hero_actor;
        s->return_from=s->view.camera;c3_field_camera(s->map,s->hero,&s->return_to);
        s->return_duration=as_float(a[0]);s->return_time=0;s->camera_returning=1;s->view.camera_active=1;
        s->target_moving=0;s->camera_move_time=s->camera_move_duration;
        if(a[1]){t->wait=W_CAMERA_MOVE;return 1;}break;
    case 0xb6:
        if(!tv_setup(s,error))return 0;
        ++s->event_depth;t->wait=W_TV;t->stage=0;t->remaining=3.0f;return 1;
    case 0xd1:
        if(!start(s,a[0],a+1,4,&t->child,error))return 0;
        t->child_finished=0;t->wait=W_TASK;return 1;
    case 0xd3:if(!tv_setup(s,error))return 0;value=1;break;
    case 0xe4:value=s->sound_id;break;
    default:return fail(error,"Unported native %02lx in GSVM %08lx at %08lx",(unsigned long)c->native,(unsigned long)c->script_id,(unsigned long)c->pc);
    }
    return resume(s,t,value,error);
}

static int wait_task(Impl *s,RunTask *t,float seconds,uint16_t *pressed,char *error)
{
    Person *p;
    switch(t->wait){
    case W_NONE:return 1;
    case W_SECONDS:
        t->remaining-=seconds;if(t->remaining>0)return 1;
        return resume(s,t,0,error);
    case W_TASK:
        if(t->child_finished)return resume(s,t,t->value,error);
        if(c3_story_active(&s->vm,t->child))return 1;
        return resume(s,t,c3_story_result(&s->vm,t->child),error);
    case W_MOTION:
        p=person(s,t->call.args[0],t->call.args[1]);
        if(p&&p->public.playing&&!p->public.loop&&pose(s,p)->animation_seconds<clip_seconds(&p->public))return 1;
        return resume(s,t,0,error);
    case W_MOVE:
        p=person(s,t->call.args[0],t->call.args[1]);if(p&&(p->moving||p->turning))return 1;
        return resume(s,t,0,error);
    case W_CAMERA:
        if(s->view.camera_active&&s->camera_track&&s->camera_track->frames&&
           s->view.camera_seconds<(float)(s->camera_track->frame_count-1u)/s->camera_track->fps)return 1;
        return resume(s,t,0,error);
    case W_CAMERA_MOVE:
        if(s->camera_returning||s->target_moving||s->camera_move_time<s->camera_move_duration)return 1;
        return resume(s,t,0,error);
    case W_FADE:
        if(s->fade_elapsed<s->fade_duration)return 1;
        return resume(s,t,0,error);
    case W_TV:
        if(!t->stage){
            t->remaining-=seconds;if(t->remaining>0)return 1;
            if(!message(s,t,0x426e,"",error))return 0;
            t->wait=W_TV;t->stage=1;return 1;
        }
        /* Same original blocking window service, with TV-native cleanup. */
        /* fall through */
    case W_MESSAGE:
        t->remaining-=seconds;if(t->remaining>0||!(*pressed&(PAD_A|PAD_B)))return 1;
        *pressed&=~(PAD_A|PAD_B);
        if(s->full_message[s->page_end]=='\f'){
            s->page_start=s->page_end+1;page(s);t->remaining=.12f;return 1;
        }
        if(t->wait==W_TV){close_message(s);if(s->event_depth)--s->event_depth;return resume(s,t,1,error);}
        /* Keep the last page visible through a following choice. */
        s->view.dialogue=0;s->dialog_owner=0;
        return resume(s,t,0,error);
    case W_CHOICE:
        t->remaining-=seconds;
        if(*pressed&PAD_DIRECTIONS){s->view.choice_no=!s->view.choice_no;*pressed&=~PAD_DIRECTIONS;}
        if(t->remaining>0||!(*pressed&(PAD_A|PAD_B)))return 1;
        if(*pressed&PAD_B)s->view.choice_no=1;
        *pressed&=~(PAD_A|PAD_B);
        {uint32_t answer=(uint32_t)s->view.choice_no;
         log_line(s,"story_choice answer=%lu",(unsigned long)answer);
         close_message(s);return resume(s,t,answer,error);}
    case W_BATTLE:return 1;
    }
    return fail(error,"Invalid native story wait state");
}

void c3_field_story_close(C3FieldStory *story)
{
    if(!story||!story->impl)return;
    Impl *s=story->impl;c3_story_free(&s->vm);free(s);story->impl=NULL;
}
void c3_field_story_set_random(C3FieldStory *story,uint16_t (*next16)(void *context),void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->random16=next16;s->random_context=context;}
}
void c3_field_story_set_floor_support(C3FieldStory *story,int (*supported)(uint32_t floor))
{
    Impl *s=story?story->impl:NULL;if(s)s->floor_supported=supported;
}
int c3_field_story_battle_pending(const C3FieldStory *story,uint32_t *encounter)
{
    const Impl *s=story?story->impl:NULL;
    if(!s)return 0;
    for(unsigned i=0;i<C3_STORY_TASKS;++i){const RunTask *t=s->tasks+i;
        if(t->key&&t->wait==W_BATTLE&&(t->call.native==0x98||t->call.native==0x66)){if(encounter)*encounter=t->call.args[0];return 1;}
    }
    return 0;
}
/* fn_801CA5C4 returns (u16)fn_8020DAD0(encounter), which is the fight result
 * global fn_801EF634. After a hero win fn_8020DAD0 fades the field back in
 * over half a second (fadeSet(0.5f,2)); the script then waits for that fade
 * with native 1D and reads the result again through native 67. */
int c3_field_story_battle_result(C3FieldStory *story,uint32_t result,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!result||result>0xffffu)return fail(error,"Invalid fight result");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_BATTLE||(t->call.native!=0x98&&t->call.native!=0x66))continue;
        s->fight_result=result;s->view.blocked[0]=0;s->view.battle_id=0;
        s->fade_from=0;s->fade_to=1;s->fade_elapsed=0;s->fade_duration=.5f;s->view.fade=0;
        log_line(s,"story_battle_result encounter=%lu result=%lu native98_resumed=1",
                 (unsigned long)t->call.args[0],(unsigned long)result);
        return resume(s,t,result,error);
    }
    return fail(error,"No original battle request is waiting for a result");
}
int c3_field_story_bind_battle(C3FieldStory *story,const char *path,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||s->entered)return fail(error,"Battle data must be bound before story entry");
    if(!c3_battle_load(&s->battle_data,path,error))return 0;
    s->battle_loaded=1;return 1;
}

int c3_field_story_load(C3FieldStory *story,const char *path,C3GameState *game,
                        C3FieldMap *map,const C3Area *area,C3FieldPlayer *hero,
                        C3Title *hero_model,C3Title *const *actor_models,
                        const C3AreaCamera *camera,FILE *trace,char *error)
{
    C3GameSummary summary;
    if(!story||!game||!map||!area||!hero||!hero_model||!actor_models||
       map->floor!=area->floor||area->actor_count>C3_AREA_MAX_ACTORS||!c3_game_state_summary(game,&summary))
        return fail(error,"Invalid field story binding");
    Impl *s=calloc(1,sizeof(*s));if(!s)return fail(error,"Field story allocation failed");
    if(!c3_story_load(&s->vm,path,error)){free(s);return 0;}
    if(c3_story_floor(&s->vm)!=area->floor){c3_story_free(&s->vm);free(s);return fail(error,"Story and field pack floors disagree");}
    s->game=game;s->map=map;s->area=area;s->hero=hero;s->hero_model=hero_model;
    s->camera_track=camera;s->trace=trace;s->previous_floor=summary.previous_floor;
    s->view.fade=1;s->view.camera=map->camera;s->camera_rate=1;
    snprintf(s->hero_name,sizeof s->hero_name,"%s",summary.name);
    s->hero_actor.public.model=hero_model;s->hero_actor.public.index=100;
    s->hero_actor.public.visible=1;s->hero_actor.public.loop=1;s->hero_actor.public.playing=1;
    s->hero_actor.public.clip=map->idle_clip;s->hero_actor.speed=1;
    for(uint32_t i=0;i<area->actor_count;++i){
        Person *p=s->actors+i;const C3AreaActor *a=area->actors+i;p->source=a;
        p->public.model=actor_models[i];p->public.index=a->index;
        memcpy(p->public.pose.position,a->position,sizeof a->position);p->public.pose.yaw=a->yaw;
        p->public.visible=(a->flags>>31)&1;p->public.clip=a->motions[0];
        p->public.flags=0x0cu|(a->talk_start==1?0x10u:a->talk_start==2?0x20u:0u)|(a->talk_end==1?0x40u:0u);
        p->public.loop=1;p->public.playing=1;p->speed=1;
        if(p->public.model&&p->public.clip>=p->public.model->clip_count){
            c3_story_free(&s->vm);free(s);return fail(error,"Actor idle motion is absent");
        }
    }
    for(uint32_t i=0;i<area->transition_count;++i)
        if(map->object_count<=area->transitions[i].object)map->object_count=area->transitions[i].object+1u;
    for(uint32_t i=0;i<area->interaction_count;++i)
        if(map->object_count<=area->interactions[i].object)map->object_count=area->interactions[i].object+1u;
    c3_field_story_close(story);story->impl=s;return 1;
}
int c3_field_story_prepare(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||s->preparing||s->view.prepared)return fail(error,"Story preparation called out of sequence");
    s->preparing=1;s->setup_stage=0;
    return start(s,s->area->load_script,NULL,0,&s->setup_key,error);
}
int c3_field_story_enter(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!s->view.prepared||s->entered)return fail(error,"Story entry called out of sequence");
    s->entered=1;
    for(uint32_t i=0;i<s->area->actor_count;++i){Person *p=s->actors+i;uint16_t key;
        if(p->public.visible&&p->source->move_script){
            uint32_t args[4]={s->area->group,p->public.index,0,0};
            if(!start(s,p->source->move_script,args,4,&key,error))return 0;
        }
    }
    return start(s,s->area->main_script,NULL,0,&s->main_key,error);
}
const C3FieldStoryView *c3_field_story_view(const C3FieldStory *story)
{const Impl *s=story?story->impl:NULL;return s?&s->view:NULL;}
C3StoryActor *c3_field_story_actor(C3FieldStory *story,uint32_t index)
{Impl *s=story?story->impl:NULL;Person *p=s?person(s,s->area->group,index):NULL;return p?&p->public:NULL;}

static float normalize_yaw(float yaw)
{
    float result=fmodf(yaw,6.2831855f);
    return result<0?result+6.2831855f:result;
}
static int advance_turn(Impl *s,Person *p,float elapsed,char *error)
{
    if(!p->turning)return 1;
    double ticks;p->turn_fraction=modf(p->turn_fraction+(double)elapsed*60.0,&ticks);
    if(ticks<1)return 1;
    C3FieldPlayer *q=pose(s,p);
    /* people.c fn_80184A90, GC6E01: nine degrees per 60-Hz tick at speed1.
     * A repeated shortest-path increment has a constant size; combine ticks
     * without an unbounded catch-up loop after a slow active frame. */
    if(!(p->public.flags&0x40000000u)||(p->public.flags&8u)){
        float yaw=normalize_yaw(q->yaw),target=normalize_yaw(p->turn_target);
        float difference=target-yaw;
        if(fabs((double)difference)>=3.141592653589793)
            difference=difference<0?(float)(6.283185307179586+difference):(float)(difference-6.283185307179586);
        double step=(double)(.15707964f*p->turn_speed)*ticks;
        if(fabs((double)difference)<=step){q->yaw=target;p->turning=0;}
        else q->yaw=yaw+(float)(difference>0?step:-step);
    }else{
        float yaw=q->yaw;
        if(yaw-p->turn_target>3.2986722f)yaw-=6.2831855f;
        else if(p->turn_target-yaw>3.2986722f)yaw+=6.2831855f;
        float step=(p->turn_target-yaw)/(10.0f*(2.6f-p->turn_speed));
        if(!isfinite(step))return fail(error,"Original following turn has an invalid rate");
        if(fabsf(step)<=.017453292f)step=step<0?-.017453292f:.017453292f;
        double next=(double)yaw+step*ticks;
        if((step<0&&next<=p->turn_target)||(step>=0&&next>=p->turn_target)){
            q->yaw=p->turn_target;p->turning=0;
        }else if(!isfinite(next)||fabs(next)>100000)return fail(error,"Original following turn exceeded its bound");
        else q->yaw=(float)next;
    }
    return 1;
}
static int advance_people(Impl *s,float elapsed,char *error)
{
    float movement_dt=fminf(elapsed,.25f);
    for(uint32_t i=0;i<=s->area->actor_count;++i){Person *p=i==s->area->actor_count?&s->hero_actor:s->actors+i;
        C3FieldPlayer *q=pose(s,p);
        if(p->public.playing&&(p!=&s->hero_actor||!s->view.control))q->animation_seconds+=elapsed;
        if(!advance_turn(s,p,elapsed,error))return 0;
        if(!p->moving)continue;
        float walk,run;
        if(p->source){
            uint32_t clip=p->source->motions[1];
            if(!p->public.model||clip>=p->public.model->clip_count||p->public.model->clips[clip].frame_count<2)
                return fail(error,"Actor walking rate is not prepared");
            float frames=(float)(p->public.model->clips[clip].frame_count-1u)*30.0f/p->public.model->fps;
            walk=p->source->walk_distance/frames;run=walk;
            if(p->speed>1){
                clip=p->source->motions[2];
                if(clip>=p->public.model->clip_count||p->public.model->clips[clip].frame_count<2)
                    return fail(error,"Actor running rate is not prepared");
                frames=(float)(p->public.model->clips[clip].frame_count-1u)*30.0f/p->public.model->fps;
                run=p->source->run_distance/frames;
            }
        }else{walk=s->map->walk_distance/s->map->walk_frames;run=s->map->run_distance/s->map->run_frames;}
        /* peopleMoveAlongAngle uses the prepared WALK/RUN clip duration,
         * independent of the actor's current presentation animation. */
        float step=p->speed<=.4f?p->speed/.4f*(walk/2):p->speed<=1?
            (p->speed-.4f)/.6f*(walk-walk/2)+walk/2:(p->speed-1)*(run-walk)+walk;
        step*=60.0f*movement_dt;
        if(!isfinite(step)||step<0)return fail(error,"Actor movement has an invalid rate");
        for(unsigned crossed=0;;++crossed){
            if(crossed>MAX_WAYPOINTS*2)return fail(error,"Original looping waypoint path exceeds its movement bound");
            float dx=p->target[0]-q->position[0],dy=p->target[1]-q->position[1],dz=p->target[2]-q->position[2];
            float distance=sqrtf(dx*dx+dy*dy+dz*dz);
            if(distance>step&&distance>=.0001f){
                for(unsigned j=0;j<3;++j)q->position[j]+=(p->target[j]-q->position[j])*step/distance;
                break;
            }
            memcpy(q->position,p->target,sizeof p->target);step=fmaxf(0,step-distance);
            if(p->waypoint_active){
                if(++p->waypoint_cursor>=p->waypoint_count){
                    if(p->waypoint_loop)p->waypoint_cursor=0;
                    else p->waypoint_active=0;
                }
                if(p->waypoint_active){
                    memcpy(p->target,p->waypoints[p->waypoint_cursor],sizeof p->target);
                    start_turn(s,p,atan2f(p->target[0]-q->position[0],p->target[2]-q->position[2]),2);
                    if(step>0)continue;
                    break;
                }
            }
            p->moving=0;
            if(!set_motion(s,p,p->source?p->source->motions[0]:1,0,1,error))return 0;
            break;
        }
    }
    return 1;
}
int c3_field_story_tick(C3FieldStory *story,float seconds,uint16_t pressed,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!isfinite(seconds)||seconds<0)return fail(error,"Invalid field story tick");
    /* Animation, camera, waits and fades share active elapsed time. Only
     * collision-sensitive translation retains a bounded movement step. */
    if(!advance_people(s,seconds,error))return 0;
    if(s->camera_playing&&s->view.camera_active)s->view.camera_seconds+=seconds*s->camera_rate;
    if(s->camera_move_time<s->camera_move_duration){
        s->camera_move_time=fminf(s->camera_move_duration,s->camera_move_time+seconds);
        float f=s->camera_move_time/s->camera_move_duration;
        for(unsigned i=0;i<3;++i){s->view.camera.eye[i]=s->camera_from[i]+(s->camera_to[i]-s->camera_from[i])*f;
            s->view.camera.target[i]=s->view.camera.eye[i]+s->camera_direction[i];}
    }
    if(s->target_moving){
        s->target_time=fminf(s->target_duration,s->target_time+seconds);
        float f=s->target_duration>0?s->target_time/s->target_duration:1;
        /* Mode3 is the source free camera: its authored eye/rotation wins
         * over target interpolation. Preserve the clock without changing it. */
        if(s->camera_mode!=3)for(unsigned i=0;i<3;++i)
            s->view.camera.target[i]=s->target_from[i]+(s->target_to[i]-s->target_from[i])*f;
        if(s->target_time>=s->target_duration)s->target_moving=0;
    }
    if(s->camera_returning){
        s->return_time=fminf(s->return_duration,s->return_time+seconds);
        float f=s->return_duration>0?s->return_time/s->return_duration:1;
        for(unsigned i=0;i<3;++i){
            s->view.camera.eye[i]=s->return_from.eye[i]+(s->return_to.eye[i]-s->return_from.eye[i])*f;
            s->view.camera.target[i]=s->return_from.target[i]+(s->return_to.target[i]-s->return_from.target[i])*f;
        }
        if(s->return_time>=s->return_duration){s->camera_returning=0;s->view.camera_active=0;}
    }
    if(s->view.camera_active&&s->camera_mode==4&&s->camera_track&&s->camera_track->frames)
        if(!c3_area_camera_sample(s->camera_track,s->view.camera_seconds,&s->view.camera))return fail(error,"Original room camera sample failed");
    s->fade_elapsed=fminf(s->fade_duration,s->fade_elapsed+seconds);
    if(s->fade_duration>0)s->view.fade=s->fade_from+(s->fade_to-s->fade_from)*s->fade_elapsed/s->fade_duration;
    while(s->preparing&&!c3_story_active(&s->vm,s->setup_key)){
        ++s->setup_stage;
        if(s->setup_stage>2){s->preparing=0;s->view.prepared=1;log_line(s,"story_prepared floor=%lu",(unsigned long)s->area->floor);break;}
        uint32_t script=s->setup_stage==1?s->area->init_script:s->area->pre_script;
        if(!start(s,script,NULL,0,&s->setup_key,error))return 0;
    }
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||!c3_story_active(&s->vm,t->key))continue;
        if(!wait_task(s,t,seconds,&pressed,error))return 0;
        if(t->wait!=W_NONE)continue;
        for(unsigned calls=0;calls<128;++calls){
            C3StoryStatus status=c3_story_step(&s->vm,t->key,1024,&t->call,error);
            if(status==C3_STORY_FAILED)return 0;
            if(status==C3_STORY_FINISHED){
                uint32_t result=c3_story_result(&s->vm,t->key);
                /* A later task can reuse the finished VM slot before its
                 * synchronous parent is scheduled next. Retain its result. */
                for(unsigned j=0;j<C3_STORY_TASKS;++j){RunTask *parent=s->tasks+j;
                    if(parent->wait==W_TASK&&parent->child==t->key){parent->value=result;parent->child_finished=1;}
                }
                log_line(s,"story_end key=%u result=%lu",t->key,(unsigned long)result);break;
            }
            if(status==C3_STORY_RUNNING)break;
            if(!run_native(s,t,error))return 0;
            if(t->wait!=W_NONE)break;
            if(calls==127)return fail(error,"Story task exceeded one-frame native budget");
        }
    }
    if(s->talk_key&&!c3_story_active(&s->vm,s->talk_key)){
        log_line(s,"story_talk_complete task=%u",s->talk_key);
        s->talk_key=0;close_message(s);restore_speakers(s);
    }
    if(s->event_key&&!c3_story_active(&s->vm,s->event_key))s->event_key=0;
    if(s->pending_floor&&!s->departure_started){
        /* The original scene runs its post callback on departure. Suspend
         * other floor tasks before it so no more input or event can race it. */
        for(unsigned i=0;i<C3_STORY_TASKS;++i)if(s->tasks[i].key)c3_story_stop(&s->vm,s->tasks[i].key);
        if(!start(s,0x05960009u,NULL,0,&s->exit_fade_key,error))return 0;
        if(!start(s,s->area->post_script,NULL,0,&s->post_key,error))return 0;
        s->departure_started=1;
    }
    if(s->pending_floor&&s->departure_started&&!c3_story_active(&s->vm,s->post_key)&&
       !c3_story_active(&s->vm,s->exit_fade_key)&&s->fade_elapsed>=s->fade_duration&&s->view.fade<=.0001f){
        s->view.next_floor=s->pending_floor;s->view.next_spawn=s->pending_spawn;
        log_line(s,"story_floor_link from=%lu to=%lu spawn=%lu post_complete=1",(unsigned long)s->area->floor,
                 (unsigned long)s->view.next_floor,(unsigned long)s->view.next_spawn);
    }
    s->view.control=s->entered&&s->hero_ready&&!s->event_depth&&!s->talk_key&&!s->event_key&&
        !s->view.dialogue&&!s->pending_floor&&!s->view.blocked[0];
    return 1;
}

static float dot3(const float a[3],const float b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static int line_triangle(const float from[3],const float to[3],const C3FieldTriangle *t)
{
    float u[3],v[3],d[3],w[3],p[3];
    for(unsigned i=0;i<3;++i){u[i]=t->vertices[3+i]-t->vertices[i];v[i]=t->vertices[6+i]-t->vertices[i];
        d[i]=to[i]-from[i];w[i]=from[i]-t->vertices[i];}
    float denom=dot3(d,t->normal);if(fabsf(denom)<1e-7f)return 0;
    float f=-dot3(w,t->normal)/denom;if(f<0||f>1)return 0;
    for(unsigned i=0;i<3;++i)p[i]=w[i]+f*d[i];
    double uu=dot3(u,u),uv=dot3(u,v),vv=dot3(v,v),pu=dot3(p,u),pv=dot3(p,v),det=uu*vv-uv*uv;
    if(det<1e-12)return 0;
    double x=(pu*vv-pv*uv)/det,y=(pv*uu-pu*uv)/det;return x>=0&&y>=0&&x+y<=1;
}
int c3_field_story_interact(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;if(!s||!s->view.control)return 1;
    Person *best=NULL;float best_score=10000,best_angle=0;
    for(uint32_t i=0;i<s->area->actor_count;++i){Person *p=s->actors+i;
        if(!p->public.visible||!p->source->talk_script||p->source->talk_start==3||(p->public.flags&1))continue;
        float dx=p->public.pose.position[0]-s->hero->position[0],dz=p->public.pose.position[2]-s->hero->position[2];
        float distance=hypotf(dx,dz),angle=angle_delta(atan2f(dx,dz)-s->hero->yaw);
        if(distance>12*s->map->scale+s->map->radius+p->source->radius||fabsf(angle)>40.0f*PI/180.0f)continue;
        float from[3]={s->hero->position[0],s->hero->position[1],s->hero->position[2]};
        float to[3]={p->public.pose.position[0],p->public.pose.position[1],p->public.pose.position[2]};
        int wall=0;
        /* people.c fn_8018CD08 consults player walls only when the original
         * floor-character TalkWallThrough bit is clear. Counters stay solid. */
        if(!(p->source->flags&0x10000000u))for(uint32_t j=0;j<s->map->wall_count&&!wall;++j)
            if(c3_field_object_enabled(s->map,s->map->walls[j].object))wall=line_triangle(from,to,s->map->walls+j);
        if(wall)continue;
        float score=distance*distance*fabsf(angle)/2;
        if(!best||score<best_score||(score==best_score&&(fabsf(best_angle)<fabsf(angle)||(fabsf(best_angle)==fabsf(angle)&&angle>0)))){
            best=p;best_score=score;best_angle=angle;
        }
    }
    if(best){uint32_t args[4]={s->area->group,best->public.index,0,0};
        s->hero->yaw=atan2f(best->public.pose.position[0]-s->hero->position[0],best->public.pose.position[2]-s->hero->position[2]);
        s->hero->motion=0;s->hero->animation_seconds=0;
        log_line(s,"story_talk actor=%lu script=%08lx",(unsigned long)best->public.index,(unsigned long)best->source->talk_script);
        s->view.control=0;return start(s,best->source->talk_script,args,4,&s->talk_key,error);
    }
    float from[3]={s->hero->position[0],s->hero->position[1]+.5f,s->hero->position[2]};
    float to[3]={from[0]+sinf(s->hero->yaw),from[1],from[2]+cosf(s->hero->yaw)};
    for(uint32_t i=0;i<s->area->interaction_count;++i)if(c3_field_object_enabled(s->map,s->area->interactions[i].object)&&
                                                                    line_triangle(from,to,s->area->interactions+i)){
        const C3AreaLink *link=c3_area_link(s->area,3,s->area->interactions[i].source_id);
        if(link){s->view.control=0;return start(s,link->script,link->args,4,&s->event_key,error);}
    }
    return 1;
}
int c3_field_story_transition(C3FieldStory *story,uint32_t source_id,char *error)
{
    Impl *s=story?story->impl:NULL;if(!s||!s->view.control)return 1;
    const C3AreaLink *link=c3_area_link(s->area,1,source_id);
    if(!link)return 1;
    log_line(s,"story_door source=%lu script=%08lx",(unsigned long)source_id,(unsigned long)link->script);
    s->view.control=0;return start(s,link->script,link->args,4,&s->event_key,error);
}
int c3_field_story_menu_event(C3FieldStory *story,uint32_t script,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!script||!s->view.control||s->event_key||s->talk_key)return fail(error,"The field cannot accept this mail callback.");
    if(!start(s,script,NULL,0,&s->event_key,error))return 0;
    s->view.control=0;return 1;
}
void c3_field_story_cancel_menu_event(C3FieldStory *story)
{
    Impl *s=story?story->impl:NULL;if(!s||!s->event_key)return;
    c3_story_stop(&s->vm,s->event_key);s->event_key=0;
}
void c3_field_story_collide(C3FieldStory *story,const float before[3])
{
    Impl *s=story?story->impl:NULL;if(!s||!before)return;
    float moved_from[3];memcpy(moved_from,s->hero->position,sizeof moved_from);
    for(uint32_t i=0;i<s->area->actor_count;++i){Person *p=s->actors+i;
        if(!p->public.visible||!p->public.model||p->source->collision_type!=1)continue;
        float dy=fabsf(s->hero->position[1]-p->public.pose.position[1]);if(dy>1.7f)continue;
        float dx=s->hero->position[0]-p->public.pose.position[0],dz=s->hero->position[2]-p->public.pose.position[2];
        float distance=hypotf(dx,dz),radius=s->map->radius+p->source->radius;
        if(distance>=radius)continue;
        if(distance<.00001f){memcpy(s->hero->position,before,3*sizeof(float));continue;}
        float next[3]={p->public.pose.position[0]+dx/distance*(radius+.0001f),s->hero->position[1],p->public.pose.position[2]+dz/distance*(radius+.0001f)},height;
        if(c3_field_ground(s->map,next,&height)){next[1]=height;memcpy(s->hero->position,next,sizeof next);}
        else memcpy(s->hero->position,before,3*sizeof(float));
        ++s->hero->collision_count;
    }
    if(memcmp(moved_from,s->hero->position,sizeof moved_from))
        (void)c3_field_resolve_position(s->map,s->hero,before);
}
static void rotation_matrix(const float euler[3],float r[9])
{
    /* fn_800E06EC / HSD quatlib: XYZ Euler, column-vector Rz*Ry*Rx. */
    float sx=sinf(euler[0]),cx=cosf(euler[0]),sy=sinf(euler[1]),cy=cosf(euler[1]);
    float sz=sinf(euler[2]),cz=cosf(euler[2]);
    r[0]=cz*cy;r[1]=cz*sy*sx-sz*cx;r[2]=cz*sy*cx+sz*sx;
    r[3]=sz*cy;r[4]=sz*sy*sx+cz*cx;r[5]=sz*sy*cx-cz*sx;
    r[6]=-sy;r[7]=cy*sx;r[8]=cy*cx;
}
static int attachment_pose(Impl *s,Person *p,char *error)
{
    Person *parent=p->parent;if(!parent)return 1;
    const float *m=parent->public.model->pose.matrices[parent->source->part1_bone_plus_one-1u];
    float scale[3],r[9],angles[3];
    for(unsigned col=0;col<3;++col){
        scale[col]=sqrtf(m[col]*m[col]+m[4+col]*m[4+col]+m[8+col]*m[8+col]);
        if(!isfinite(scale[col])||scale[col]<.0001f)return fail(error,"Original attachment part has a singular scale");
        for(unsigned row=0;row<3;++row)r[row*3+col]=m[row*4+col]/scale[col];
    }
    /* The admitted vehicle's source root is identity; its part1 rotations
     * stay within +/-0.033rad. GSpartGetTransform SUMS ancestor Eulers;
     * decomposing a parent-yaw * joint matrix would give a different answer. */
    angles[0]=atan2f(r[7],r[8]);angles[1]=asinf(fmaxf(-1,fminf(1,-r[6])));angles[2]=atan2f(r[3],r[0]);
    for(unsigned i=0;i<3;++i)if(!isfinite(angles[i])||fabsf(angles[i])>.25f)
        return fail(error,"Original attachment part exceeds its reviewed Euler range");
    angles[1]+=parent->public.pose.yaw;rotation_matrix(angles,r);
    float sine=sinf(parent->public.pose.yaw),cosine=cosf(parent->public.pose.yaw);
    float origin[3]={parent->public.pose.position[0]+cosine*m[3]+sine*m[11],
                     parent->public.pose.position[1]+m[7],
                     parent->public.pose.position[2]-sine*m[3]+cosine*m[11]};
    for(unsigned row=0;row<3;++row){
        /* modelUpdateAttachments type7 intentionally does not multiply the
         * local translation by parent scale. Child scale defaults to one. */
        pose(s,p)->position[row]=origin[row]+r[row*3]*p->local_position[0]+
            r[row*3+1]*p->local_position[1]+r[row*3+2]*p->local_position[2];
        p->world_rotation[row]=angles[row]+p->local_rotation[row];p->world_scale[row]=scale[row];
    }
    pose(s,p)->yaw=p->world_rotation[1];return 1;
}
static void transform(C3Title *model,const C3FieldPlayer *q,const Person *p)
{
    if(!p->parent){
        /* Ordinary field actors need only a rigid Y rotation. Preserve the
         * cheap path on ARM11; a rigid rotation keeps unit normals unit. */
        float sine=sinf(q->yaw),cosine=cosf(q->yaw);
        for(uint32_t i=0;i<model->vertex_count;++i){C3TitleDrawVertex *v=model->draw_vertices+i;
            float x=v->pos[0],z=v->pos[2],nx=v->normal[0],nz=v->normal[2];
            v->pos[0]=cosine*x+sine*z+q->position[0];v->pos[1]+=q->position[1];v->pos[2]=-sine*x+cosine*z+q->position[2];
            v->normal[0]=cosine*nx+sine*nz;v->normal[2]=-sine*nx+cosine*nz;
        }
        for(uint32_t i=0;i<model->batch_count;++i){float *origin=model->pose.materials[i].specular_origin,x=origin[0],z=origin[2];
            origin[0]=cosine*x+sine*z+q->position[0];origin[1]+=q->position[1];origin[2]=-sine*x+cosine*z+q->position[2];}
        return;
    }
    float euler[3]={0,q->yaw,0},scale[3]={1,1,1},r[9];
    if(p->parent){memcpy(euler,p->world_rotation,sizeof euler);memcpy(scale,p->world_scale,sizeof scale);}
    rotation_matrix(euler,r);
    for(uint32_t i=0;i<model->vertex_count;++i){C3TitleDrawVertex *v=model->draw_vertices+i;
        float pos[3],normal[3];
        for(unsigned j=0;j<3;++j){pos[j]=v->pos[j]*scale[j];normal[j]=v->normal[j]/scale[j];}
        for(unsigned row=0;row<3;++row){
            v->pos[row]=r[row*3]*pos[0]+r[row*3+1]*pos[1]+r[row*3+2]*pos[2]+q->position[row];
            v->normal[row]=r[row*3]*normal[0]+r[row*3+1]*normal[1]+r[row*3+2]*normal[2];
        }
        float length=sqrtf(dot3(v->normal,v->normal));
        if(length>0)for(unsigned j=0;j<3;++j)v->normal[j]/=length;
    }
    /* The renderer constructs specular half-vectors from this owning joint's
     * world position. Keep it in the same space as the transformed vertices. */
    for(uint32_t i=0;i<model->batch_count;++i){
        float *origin=model->pose.materials[i].specular_origin,local[3];
        for(unsigned j=0;j<3;++j)local[j]=origin[j]*scale[j];
        for(unsigned row=0;row<3;++row)origin[row]=r[row*3]*local[0]+r[row*3+1]*local[1]+r[row*3+2]*local[2]+q->position[row];
    }
}
int c3_field_story_sample(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;if(!s)return fail(error,"Missing story sample context");
    for(uint32_t i=0;i<=s->area->actor_count;++i){Person *p=i==s->area->actor_count?&s->hero_actor:s->actors+i;
        C3Title *model=p->public.model;if(!model)continue;
        uint32_t clip=p->public.clip;
        if(p==&s->hero_actor&&s->view.control)clip=s->hero->motion==2?s->map->run_clip:s->hero->motion==1?s->map->walk_clip:s->map->idle_clip;
        if(clip>=model->clip_count)return fail(error,"Actor clip is out of range");
        model->clips[clip].loop=(uint32_t)p->public.loop;
        if(!c3_title_animate(model,clip,pose(s,p)->animation_seconds))return fail(error,"Original actor animation sample failed");
    }
    /* All parent samples are current before resolving a passenger. Original
     * actor-table order places passengers before the vehicle. */
    for(uint32_t i=0;i<=s->area->actor_count;++i)
        if(!attachment_pose(s,i==s->area->actor_count?&s->hero_actor:s->actors+i,error))return 0;
    for(uint32_t i=0;i<=s->area->actor_count;++i){Person *p=i==s->area->actor_count?&s->hero_actor:s->actors+i;
        C3Title *model=p->public.model;if(!model)continue;
        transform(model,pose(s,p),p);
        if(!p->public.visible)for(uint32_t b=0;b<model->batch_count;++b)model->pose.materials[b].visible=0;
    }
    return 1;
}
