#include "colosseum3ds/field_story.h"
#include "colosseum3ds/hero_follow.h"
#include "colosseum3ds/battle.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define PAD_A 0x0100u
#define PAD_B 0x0200u
#define PAD_DIRECTIONS 0x000fu
#define DYNAMIC_MARKER 0x43334459u /* C3DY: private prepared model reservation. */
/* C3SM: a scripted model of an event scene (tools/prepare_event_scene.py).
 * Natives address it by its original resource ID; aux2 is the GSpart index
 * its scripts attach to and part1_bone_plus_one that part's sampled bone. */
#define SCENE_MODEL_MARKER 0x4333534Du
#define MAX_WAYPOINTS 64u /* Phenac actor 24 allocates 36 points (bank 013E0E00 fn 34). */

typedef enum { W_NONE, W_SECONDS, W_TASK, W_MOTION, W_MOVE, W_CAMERA,
               W_ASIDE, /* native E8: the follower steps aside */
               W_CAMERA_MOVE, W_FADE, W_MESSAGE, W_CHOICE, W_BATTLE, W_NAME, W_ITEM, W_DOOR, W_SHOP, W_MENU_FLOOR, W_JINGLE, W_DIALOG,
               W_RELIVE, W_FALL, W_ITEM_MENU, W_OBJECT, W_HEAL, W_LIFT } Wait;
typedef struct {
    uint16_t key, child;
    C3StoryCall call;
    Wait wait;
    float remaining;
    uint32_t value, stage;
    int child_finished;
    uint32_t pending_message; const char *pending_speaker; /* W_DIALOG */
    /* Native menu programs (etctool.c 54-57): their state machine runs in
     * this task; each message or yes/no choice resumes the program. */
    uint32_t program; int prog_state, prog_yes, prog_no, prog_asking;
    uint32_t relive_mode, relive_slot; /* native C2's arguments and the picked slot */
    uint32_t jingle; float jingle_seconds; /* W_JINGLE / the item jingle of W_ITEM */
    float fall_ground; /* native B2: the person's ground height (W_FALL) */
} RunTask;
typedef struct Person {
    C3StoryActor public;
    const C3AreaActor *source;
    float target[3], look[3], old_yaw, speed, turn_target, turn_speed;
    double turn_fraction;
    int moving, turning, talking, looking, talk_lock;
    uint16_t thread_key; /* its own script thread (fn_801837D8 keeps it at person+0x4C) */
    /* Streamed models (field_scene loads people after the room): what the
     * scripts asked for while the model was absent, applied on attach. */
    int dynamic_active;
    int want_visible, pending_motion;uint32_t pending_clip,pending_frame,pending_loop;
    float waypoints[MAX_WAYPOINTS][3];
    uint32_t waypoint_capacity, waypoint_count, waypoint_cursor;
    int waypoint_active, waypoint_loop;
    struct Person *parent;
    uint32_t parent_bone_plus_one;int parent_root;
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
    const C3AreaCamera *cameras; uint32_t camera_count; /* c3_field_story_bind_cameras */
    Person actors[C3_AREA_MAX_ACTORS], hero_actor;
    RunTask tasks[C3_STORY_TASKS];
    C3FieldStoryView view;
    C3BattleData battle_data;
    C3BattleRequest battle_request;
    int battle_loaded;
    FILE *trace;
    uint16_t setup_key, talk_key, event_key, post_key, main_key, exit_fade_key;
    uint16_t talk_held; /* the talked-to person's thread, suspended for the talk */
    int setup_stage, preparing, entered, hero_ready, event_depth, departure_started;
    int event_depth_base; /* event_depth when the current talk/event script started */
    int hero_event; /* heroMoveInitEvent (native 20) until heroMoveTermEvent (21) */
    int camera_mode, camera_playing;
    float fade_elapsed, fade_duration, fade_from, fade_to, camera_rate;
    float camera_from[3],camera_to[3],camera_direction[3],camera_move_time,camera_move_duration;
    float camera_rotation[2],rotation_from[2],rotation_to[2],rotation_time,rotation_duration;
    float target_from[3],target_to[3],target_time,target_duration;
    C3TitleCamera return_from,return_to;
    float return_time,return_duration;
    Person *camera_person;
    int target_moving,camera_returning;
    uint8_t dynamic_serial;
    uint32_t sound_id, previous_floor, pending_floor, pending_spawn, arrival_spawn;
    uint16_t tick_pressed; /* this tick's newly pressed buttons (native 6F) */
    uint32_t fight_result; /* fn_801EF634: last original fight result ID */
    uint32_t whiteout_floor,whiteout_previous,whiteout_spawn;int whiteout_valid;
    int name_entry_enabled;
    int shop_enabled;
    uint32_t menu_floors[8]; unsigned menu_floor_count; /* c3_field_story_enable_menu_floor */
    uint16_t (*random16)(void *context);void *random_context;
    int (*species_name)(void *context,unsigned species,char *out,size_t capacity);void *species_context;
    C3FieldStoryReceive receive;void *receive_context;
    C3FieldStoryItemChange item_change;void *item_change_context;
    C3FieldStoryTake take;void *take_context;
    C3FieldStoryAudio audio;void *audio_context;
    const C3AreaDoor *door_rows;C3StoryDoor doors[C3_AREA_MAX_DOORS];uint32_t door_count;
    /* Native C5 (fn_801C9B6C -> floorEventCtrlElevator / fn_801C8E14): the
     * command waiting on its animations and walks, the party that rides
     * (lbl_8047B3C4) and the people attached to the cabin part. */
    struct { uint32_t pos, cmd, value; int party, stage; float timer, partner_target[3]; } lift;
    int lift_party;
    struct { int on, party; uint32_t pos; float offset[2][3]; } ride;
    /* Native E8 (fn_801C7730): the follower's walk aside, its second
     * (axis) spot and the 3-second wait of the current attempt. */
    struct { int stage, has_alt; float timer, alt[3]; } aside;
    /* Native 53 mode 0 (fn_801D0DB0): the ball models the scene loaded and
     * the catch ball each shows, then the running sequence's state. */
    C3Title *heal_models[C3_HEAL_BALL_SLOTS];uint8_t heal_bound[C3_HEAL_BALL_SLOTS];uint32_t heal_model_count;
    struct { uint32_t stage,count,ball,machine,group,id;float timer,yaw;int person;
             uint8_t shown[C3_HEAL_BALL_SLOTS];float at[C3_HEAL_BALL_SLOTS][3]; } heal;
    C3FieldStoryRestore restore;void *restore_context;
    C3FieldStoryRestore clear_poison;void *poison_context;
    C3FieldStoryGift gift;void *gift_context;
    C3FieldStoryPurify purify;void *purify_context;
    C3FieldStoryPurified purified; /* the last purification, for its messages */
    uint32_t message_values[256]; /* msgctrlSetValue: one word per control byte */
    char item_name[64],number_text[24],value_text[64];const char *message_speaker;
    C3StoryNamer namer;void *namer_context;
    char value_strings[C3_STORY_CONTROLS][32]; /* string variables (msgctrlSetValue of a GSchar pointer) */
    char species_text[32];
    Person partner_actor; /* (0,101); invisible until native 23 */
    C3AreaActor partner_source; /* its PeopleInfo stand-in: pack clips, Wes's distances */
    C3Follow follow;            /* hero_move.c follower history */
    int (*floor_supported)(uint32_t floor);
    uint32_t dialog_owner;
    size_t page_start, page_end;
    char full_message[C3_STORY_MESSAGE], hero_name[32], partner_name[32], speaker[128];
    uint32_t cosmetic_logged[8];
    uint32_t mail_sent; /* native AD calls that added mail */
    struct { uint16_t species; uint32_t personality; } temochi[6]; /* lbl_804670B4 (E1/E2) */
    uint32_t fade_begin, fade_poll; /* floorSetFadeScript (native 90) */
    uint32_t auto_event[5];         /* heroMoveAddAutoEvent (native 22) */
    /* fn_800190D8 (native 11): item -> script pairs the pause menu runs
     * when that item is used (lbl_803A1B90, eight entries). */
    struct { uint16_t item; uint32_t script; } item_events[8];
    unsigned item_event_count;
    int item_menu_enabled;
    uint32_t pending_item_script;   /* queued by an item use, started when the event ends */
    uint32_t pending_item;          /* its first argument (fn_800F7434(script,4,item,value,0,0)) */
    float hero_regions[32][4];      /* native 3A boxes asked about the hero (x0,z0,x1,z1, map units) */
    unsigned hero_region_count;
    /* The floor's treasure people (floor.c _floorInitTresure, 0x7FFF0000|n). */
    C3StoryTreasure treasure[32];
    int8_t treasure_display[32];    /* floorEventSetTresureDisp: -1 none, else shown */
    unsigned treasure_count;
    RunTask treasure_task;          /* floorEventCtrlTresure mode 2, run as a program */
    int treasure_active;
    unsigned treasure_row;
    int treasure_result;
    int move_input;                /* scriptIsMoveButtonPush (native 6F) */
} Impl;
/* floorEventChangeTresure rewrites a row of the common treasure table, which
 * stays in memory for the whole session (common REL125 is never unloaded):
 * the change outlives the floor that made it, as on the console. */
static struct { uint8_t changed, count; uint32_t item; } treasure_session[512];

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
/* A jingle (fn_80165668) and the wait for its end (fn_801666BC != 2).
 * Bounded at ten seconds; the music resumes when it ends. */
static int start_jingle(Impl *s,RunTask *t,uint32_t sound,uint32_t fade,uint32_t volume)
{
    if(!s->audio||s->audio(s->audio_context,C3_STORY_AUDIO_JINGLE,sound,fade,volume)<=0)return 0;
    t->jingle=sound;t->jingle_seconds=0;t->wait=W_JINGLE;
    log_line(s,"story_jingle sound=%03lx status=playing",(unsigned long)sound);
    return 1;
}
static int jingle_playing(Impl *s,RunTask *t,float seconds)
{
    t->jingle_seconds+=seconds;
    /* The stream starts on the worker thread: give it a moment to report. */
    int playing=s->audio(s->audio_context,C3_STORY_AUDIO_STATUS,t->jingle,0,0)==2;
    if((playing||t->jingle_seconds<0.25f)&&t->jingle_seconds<10.0f)return 1;
    s->audio(s->audio_context,C3_STORY_AUDIO_JINGLE_END,t->jingle,0,0);
    log_line(s,"story_jingle sound=%03lx status=ended seconds=%.2f",(unsigned long)t->jingle,(double)t->jingle_seconds);
    t->jingle=0;return 0;
}
static float angle_delta(float x){return remainderf(x,2*PI);}
/* fn_801C8834 / fn_801C8688: species of a DNA disc item (0 for others). */
static unsigned disc_species(unsigned item)
{
    static const uint16_t species[9]={0x99,0x9c,0x9f,0xb9,0xc8,0x11f,0xf3,0xf4,0xf5};
    return item>=0x200&&item<=0x211?species[(item-0x200)/2]:0;
}
/* The pinned native table describes words, not C pointers. Validate the
 * complete call before changing any scene state; absent arguments must not
 * acquire the VM's zero-filled defaults. Float masks follow the table's type2.
 * Bounds here are this adapter's admission limits, not a new GSVM format. */
static int validate_native(const Impl *s,const C3StoryCall *c,char *error)
{
    static const struct {uint8_t id,argc,floats;} signature[]={
        {0x00,0,0},{0x03,6,0},{0x04,1,0},{0x06,1,0},{0x07,3,0},{0x08,1,0},{0x0b,0,0},
        {0x1c,2,2},{0x1d,1,0},{0x1e,0,0},{0x1f,0,0},{0x20,0,0},{0x21,0,0},
        {0x24,1,0},{0x25,2,0},{0x26,3,7},{0x27,3,7},{0x28,3,7},{0x29,3,7},
        {0x2a,4,0},{0x2b,0,0},{0x2c,0,0},{0x2d,0,0},{0x2e,1,0},{0x30,4,8},
        {0x32,3,0},{0x33,5,0},{0x34,2,0},{0x35,3,0},{0x36,3,0},{0x37,2,0},
        {0x3a,6,60},{0x3b,5,28},{0x3c,5,28},{0x3d,6,56},{0x3e,3,0},
        {0x3f,3,0},{0x40,3,0},{0x41,2,0},{0x42,5,0},{0x43,2,0},{0x44,3,4},{0x45,3,4},
        {0x46,5,0},{0x47,2,0},{0x48,3,0},{0x49,5,28},{0x4a,3,0},
        {0x19,1,0},{0x4c,2,0},{0x4d,2,0},{0x4e,1,0},{0x4f,1,0},{0x50,2,0},{0x51,1,0},{0x52,1,0},
        {0x5c,0,0},{0x5d,0,0},{0x5e,0,0},{0x5f,2,0},{0x64,2,0},{0x65,4,14},
        {0x70,1,0},{0x71,1,0},{0xdd,3,0},{0xa6,3,0},
        {0x67,0,0},{0x68,3,0},{0x69,3,0},{0x6a,2,0},{0x6d,2,0},{0x72,2,0},{0x76,2,0},
        {0x7d,3,0},{0x7f,3,7},{0x88,0,0},{0x8a,4,0},{0x8c,5,30},{0x98,3,0},{0xd9,1,0},{0xe0,2,0},{0xc3,0,0},
        {0xc2,2,0},{0x99,1,0},{0x9d,1,1},{0xb3,2,1},{0xb6,1,0},{0xd1,5,0},{0xd3,0,0},{0xe4,0,0},
        /* Phenac City exterior bank 013E0E00 (native table lbl_802E1CF0). */
        {0x02,1,0},{0x16,2,0},{0x23,1,0},{0x31,0,0},{0x66,1,0},{0x7e,2,0},
        {0x82,3,0},{0x83,3,0},{0x89,3,0},{0xa1,3,0},{0xa4,6,0x3c},{0xa5,5,0x10},
        {0x77,2,0},{0x84,2,0},{0x60,3,0},{0x73,4,0},{0x74,1,0},{0x75,2,0},{0x8d,5,0x1e},{0x9a,5,0},{0xa0,1,0},{0xb5,4,0},{0xa9,2,0},{0xb1,6,0x10},{0x6f,0,0},{0xf6,3,0},{0xb2,3,4},{0xb4,0,0},{0xd8,1,0},{0xda,1,0},{0xe8,2,0},
        /* Whole-game survey (tests/test_world_story.c), table lbl_802E1CF0. */
        {0x97,3,0},{0xaa,7,0x3c},{0xab,1,0},{0xad,1,0},{0xe1,0,0},
        {0xa2,0,0},{0xac,3,0},{0xcb,3,0},{0xd4,0,0},{0x61,5,0},{0xe2,0,0},{0x90,2,0},{0x7b,0,0},{0xc5,4,0},
        {0xe7,3,0},{0x22,5,0},
        {0x62,1,0}, /* fn_800FF730 menu floor (systems lane) */
        {0x78,1,0},{0x7c,0,0},{0xf1,0,0},{0xf2,0,0},{0xa8,2,0},{0xd6,2,0},{0x53,3,0},{0x9b,3,0},{0xc7,5,0},
        {0xc4,0,0},{0xdc,0,0},{0xc8,2,0},{0x80,3,0},{0xbd,0,0},{0x9e,1,0},{0x54,0,0},{0x55,0,0},{0x56,0,0},{0x57,0,0},
        {0xdb,2,0},{0x86,0,0},{0xb7,1,0},{0xbb,0,0},{0xbc,1,0},{0xca,0,0},{0xeb,0,0},{0x85,1,0},
        {0xd0,0,0},{0x11,2,0},{0xcc,3,0},{0xcd,1,0},{0xcf,0,0},{0xbe,2,0},{0xbf,4,0},{0xc1,2,0},{0xf3,2,0}
    };
    for(unsigned i=0;i<sizeof signature/sizeof signature[0];++i){
        if(signature[i].id!=c->native)continue;
        /* D1 is the original synchronous script executor: the four child
         * arguments may be omitted (retail floor init uses only scriptID),
         * and the VM supplies zero for those missing trailing words. */
        /* Phenac's banks call 4C/4D with fewer arguments than the table row.
         * The dispatcher (fn_800F10E8) zero-fills its register image, so the
         * missing words are zero: person (group,0) or (0,0). */
        if((c->native==0x4c||c->native==0x4d)&&c->argc<2)return 1;
        /* Some banks call 32 with (group,index) only: visible reads zero. */
        if(c->native==0x32&&c->argc==2)return 1;
        /* Realgam bar function 7 omits the loop word of 33. The original
         * dispatcher leaves that trailing register zero (nonlooping). */
        if(c->native==0x33&&c->argc==4)return 1;
        /* Floor 022 starts a battle with the encounter alone (98 with one
         * word); the zero-filled register image gives the other two zero. */
        if(c->native==0x98&&c->argc>=1)return 1;
        /* The Pyrite windmill bank (floor 017) calls B5 (fn_80188FA0, head
         * aim) as (group, index, target) with three words; the fourth is the
         * zero of the register image, so the target is person (target, 0). */
        if(c->native==0xb5&&c->argc==3)return 1;
        /* Extra script words are legal: the dispatcher reads only the table
         * row's typed arguments and the call pops the rest (the mayor's-house
         * bank passes five words to the three-argument native 36). Missing
         * words stay an error here except for the reviewed cases above. */
        if((c->native==0xd1?c->argc<1||c->argc>5:c->argc<signature[i].argc))return fail(error,"Native %02lx expects %u arguments, received %lu",
            (unsigned long)c->native,signature[i].argc,(unsigned long)c->argc);
        for(unsigned j=0;j<c->argc&&j<signature[i].argc;++j){
            if(!(signature[i].floats&(1u<<j)))continue;
            float value=as_float(c->args[j]),minimum=-100000,maximum=100000,scale=s->map->scale;
            switch(c->native){
            case 0x1c:case 0x30:case 0xb3:minimum=0;maximum=60;scale=1;break;
            case 0x9d: minimum=-3600;maximum=3600;scale=1;break; /* negative: no wait */
            case 0x29: minimum=-100;maximum=100;scale=1;break;
            case 0x3c:case 0x44:minimum=-100;maximum=100;scale=PI/180.0f;break;
            case 0x45:minimum=0;maximum=100;scale=1;break;
            case 0xa4:if(j==5){minimum=0;maximum=100;scale=1;}break;
            case 0xa5:minimum=0;maximum=100;scale=1;break;
            case 0xb1:minimum=0;maximum=1000;scale=1;break;
            case 0x7f:if(j==2){minimum=-100;maximum=100;scale=1;}break;
            case 0x8c:if(j==4){minimum=0;maximum=60;scale=1;}break;
            case 0x8d:scale=1;if(j==4){minimum=0;maximum=60;}else{minimum=-100;maximum=100;}break;
            default:break;
            }
            float converted=value*scale;
            if(!isfinite(value)||!isfinite(converted)||converted<minimum||converted>maximum)
                return fail(error,"Native %02lx argument %u is outside its finite range (%g)",(unsigned long)c->native,j,(double)value);
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
    /* people.c peopleFindSelf: an exact group match first, then the index
     * in any group. Floor actors are the only other people here. */
    for(uint32_t i=0;i<s->area->actor_count;++i)if(s->actors[i].public.index==id&&
        (s->actors[i].source->aux3!=DYNAMIC_MARKER||s->actors[i].dynamic_active))return s->actors+i;
    /* fn_801CB954 and its siblings take a map-model resource where no person
     * matches: an event scene's scripted model answers to its resource. */
    if(id>0xffffu)for(uint32_t i=0;i<s->area->actor_count;++i)
        if(s->actors[i].source->aux3==SCENE_MODEL_MARKER&&s->actors[i].source->resource==id)return s->actors+i;
    return NULL;
}
/* The bound door or map object row whose model is this map resource. */
static C3StoryDoor *map_object(Impl *s,uint32_t resource,uint32_t *position)
{
    for(uint32_t i=0;i<s->door_count;++i)
        if(s->door_rows[i].resource==resource&&s->doors[i].model){if(position)*position=i;return s->doors+i;}
    return NULL;
}
/* fn_801D0DB0's tables (0x8036E030): battleIntroModels (area group ->
 * healing machine resource) and battleIntroObjectIds (catch ball id -> Poke
 * Ball model; ids 0 and 2 share one). */
static const struct { uint32_t group,resource; } heal_machines[]={
    {0x20,0x00651003},{0x37,0x00A31009},{0x6E,0x002B1004},{0x89,0x00511004},{0x94,0x02371003},{0x41,0x00C21005},
    {0x49,0x01DC1003},{0x5E,0x02E41006},{0x5F,0x02E51005},{0x3B,0x00931003},{0x633,0x10BB1005}};
static const uint32_t heal_ball_models[13]={0x03640400,0x03650400,0x03640400,0x036B0400,0x03360400,0x036A0400,
    0x03660400,0x03400400,0x03670400,0x03690400,0x036C0400,0x03630400,0x03680400};
uint32_t c3_field_heal_machine(uint32_t group)
{
    for(size_t i=0;i<sizeof heal_machines/sizeof heal_machines[0];++i)if(heal_machines[i].group==group)return heal_machines[i].resource;
    return 0;
}
int c3_field_heal_ball(const char *prefix,uint32_t catch_ball,char *out,size_t capacity)
{
    if(!prefix||!out||catch_ball>=13)return 0;
    int n=snprintf(out,capacity,"%s_ball_%04lx.c3t",prefix,(unsigned long)(heal_ball_models[catch_ball]>>16));
    return n>0&&(size_t)n<capacity;
}
/* GSpartGetTransform position: the world translation of machine part 9-14,
 * kept as the pack's last six sampled bones. */
static void heal_part(const C3Title *machine,uint32_t part,float out[3])
{
    const float *m=machine->pose.matrices[machine->bone_count-C3_HEAL_BALL_SLOTS+(part-9u)];
    out[0]=m[3];out[1]=m[7];out[2]=m[11];
}
/* fn_801D0AA0: the catch ball of party slot i, 0 for an empty slot. */
static uint32_t party_ball(Impl *s,uint32_t slot)
{
    uint8_t record[0x138];
    return c3_game_state_pokemon(s->game,slot,record)&&(record[0]|record[1])?record[0x0f]:0u;
}
static void start_turn(Impl *s,Person *p,float yaw,float speed);
static void partner_place_behind(Impl *s);
/* Every person the scene draws: the floor's people, Wes, then the partner. */
static Person *person_at(Impl *s,uint32_t i)
{
    return i<s->area->actor_count?s->actors+i:i==s->area->actor_count?&s->hero_actor:&s->partner_actor;
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
    if(key==&s->talk_key||key==&s->event_key)s->event_depth_base=s->event_depth;
    RunTask *t=NULL;
    for(unsigned i=0;i<C3_STORY_TASKS;++i)
        if(!s->tasks[i].key||!c3_story_active(&s->vm,s->tasks[i].key)){t=s->tasks+i;break;}
    if(!t)return fail(error,"Native story task table is full");
    uint16_t next;
    if(!c3_story_start(&s->vm,script,args,argc,&next,error))return 0;
    memset(t,0,sizeof(*t));t->key=next;*key=next;
    log_line(s,"story_start key=%u script=%08lx argc=%lu",next,(unsigned long)script,(unsigned long)argc);return 1;
}
static int program_step(Impl *s,RunTask *t,uint32_t answer,char *error);
static int resume(Impl *s,RunTask *t,uint32_t value,char *error)
{
    t->wait=W_NONE;
    if(t->program)return program_step(s,t,value,error);
    return c3_story_resume(&s->vm,t->key,value)||fail(error,"Cannot resume original story task %u",t->key);
}
/* gs_floor.c starts each registered floor script (init, pre, main, post)
 * with four arguments: floorDataBiosGetGroupID(entry), 0, 0, 0. The load
 * script is run by floor.c fn_801129CC with none. */
static int start_floor_script(Impl *s,uint32_t script,uint16_t *key,char *error)
{
    uint32_t args[4]={s->area->group,0,0,0};
    return start(s,script,args,4,key,error);
}
static void page(Impl *s)
{
    const char *begin=s->full_message+s->page_start;
    const char *end=strchr(begin,'\f');
    s->page_end=end?(size_t)(end-s->full_message):strlen(s->full_message);
    size_t n=s->page_end-s->page_start;
    memcpy(s->view.message,begin,n);s->view.message[n]=0;
}
/* Value controls of the message decoder (the control byte is the msgctrl
 * variable, msgctrlcode modes 1/2). Every control a field message can use
 * is answered here; c3_field_story_message_control() lists them for the
 * message sweep (tests/test_message_sweep.c). */
static int message_source(void *context,uint8_t control,C3StoryValue *v)
{
    Impl *s=context;uint32_t value=s->message_values[control];
    if(c3_story_format_number(control,s->message_values,s->number_text,sizeof s->number_text)){v->text=s->number_text;return 1;}
    switch(control){
    case 0x2b:v->text=s->hero_name;return 1;   /* msgctrlHero */
    case 0x2c:v->text=s->partner_name;return 1;/* msgctrlHizuki */
    case 0x59:                                 /* msgctrlNpc: the speaker */
        if(s->message_speaker&&*s->message_speaker){v->text=s->message_speaker;return 1;}
        if(value){v->message=value;return 1;}
        v->text="";return 1;
    /* msgctrlMsgID / MenuMsgID / MenuMsgID2: print that message. */
    case 0x31:case 0x55:case 0x56:if(!value)return 0;v->message=value;return 1;
    /* msgctrlItem / Item2: the item's name message (2B6E when it has none). */
    case 0x2d:case 0x2e:
        if(control==0x2d&&s->item_name[0]){v->text=s->item_name;return 1;}
        if(s->namer&&s->namer(s->namer_context,C3_STORY_NAME_ITEM,value,v))return 1;
        return 0;
    /* msgctrlPokemon: the purified Pokemon's nickname (reliveCeremonyAll's
     * msgctrlSetValue(0x32)); msgctrlWaza: a move name. */
    case 0x32:if(!s->purified.nickname[0])return 0;v->text=s->purified.nickname;return 1;
    case 0x39:
        if(s->purified.move_name[0]){v->text=s->purified.move_name;return 1;}
        return s->namer&&s->namer(s->namer_context,C3_STORY_NAME_MOVE,value,v);
    /* msgctrlTribe: the trainer class prefix ("COOLTRAINER"). */
    case 0x58:return s->namer&&s->namer(s->namer_context,C3_STORY_NAME_TRAINER_KIND,value,v);
    /* String variables a native or menu stored with msgctrlSetValue
     * (TrainerName, MenuPokemon, MenuMsg, MenuMsg2, String, String2,
     * EvStrBuf0-2). */
    case 0x0d:case 0x0e:case 0x23:case 0x41:case 0x33:case 0x36:case 0x37:case 0x51:case 0x4d:case 0x57:
        if(!s->value_strings[control][0])return 0;
        v->text=s->value_strings[control];return 1;
    /* msgctrlPokemonID: a species name. */
    case 0x4e:
        if(s->namer&&s->namer(s->namer_context,C3_STORY_NAME_SPECIES,value,v))return 1;
        if(s->species_name&&s->species_name(s->species_context,value,s->value_text,sizeof s->value_text)){v->text=s->value_text;return 1;}
        return 0;
    default:return 0;
    }
}
int c3_field_story_message_control(uint8_t control)
{
    static const uint8_t controls[]={0x0d,0x0e,0x23,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,
        0x37,0x39,0x41,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x55,0x56,0x57,0x58,0x59};
    for(size_t i=0;i<sizeof controls;++i)if(controls[i]==control)return 1;
    return 0;
}
int c3_field_story_set_message_string(C3FieldStory *story,uint8_t control,const char *text)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!c3_story_control_is_value(control))return 0;
    snprintf(s->value_strings[control],sizeof s->value_strings[control],"%s",text?text:"");return 1;
}
void c3_field_story_set_namer(C3FieldStory *story,C3StoryNamer namer,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->namer=namer;s->namer_context=context;}
}
static int message(Impl *s,RunTask *t,uint32_t id,const char *speaker,char *error)
{
    if(s->dialog_owner&&s->dialog_owner!=t->key)return fail(error,"Concurrent original field messages are unsupported");
    /* Control 2C prints the partner's saved name (hero +0xAC2), which the
     * name entry may have just changed. */
    C3GameSummary names;
    if(!c3_game_state_summary(s->game,&names))return fail(error,"Game state names are unavailable");
    snprintf(s->partner_name,sizeof s->partner_name,"%s",names.partner);s->message_speaker=speaker;
    if(!c3_story_render(&s->vm,id,message_source,s,C3_STORY_TEXT_MARKUP,s->full_message,sizeof s->full_message,error))return 0;
    s->dialog_owner=t->key;s->page_start=0;s->view.message_id=id;s->view.dialogue=1;s->view.choice=0;
    page(s);t->wait=W_MESSAGE;t->remaining=.12f;
    log_line(s,"story_message script=%08lx pc=%08lx id=%05lx",(unsigned long)t->call.script_id,
             (unsigned long)t->call.pc,(unsigned long)id);return 1;
}
/* The original message window is one shared window: a script that opens a
 * message while another script's message is showing waits for the window
 * (winMsgOpen blocks its thread) instead of failing. */
static int message_when_free(Impl *s,RunTask *t,uint32_t id,const char *speaker,char *error)
{
    if(s->dialog_owner&&s->dialog_owner!=t->key){
        t->pending_message=id;t->pending_speaker=speaker;t->wait=W_DIALOG;return 1;
    }
    return message(s,t,id,speaker,error);
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
    if(p==&s->partner_actor){
        /* The partner pack's clip order (floor_profiles.PARTNER["clips"]). */
        static const uint8_t order[]={1,2,3,0,4,7,14,21,22,23,27,28,29,42,43,44};
        clip=UINT32_MAX;
        for(uint32_t i=0;i<sizeof order;++i)if(order[i]==source_clip)clip=i;
        if(clip==UINT32_MAX){note_presentation(s,0x33,"partner_motion_not_prepared_kept_current");return 1;}
    }
    if(!p->public.model&&p!=&s->hero_actor){
        p->pending_motion=1;p->pending_clip=source_clip;p->pending_frame=frame;p->pending_loop=loop;return 1;
    }
    if(!p->public.model||clip>=p->public.model->clip_count){
        /* Wes's prepared motions are reviewed and stay strict. */
        if(p==&s->hero_actor)
            return fail(error,"Actor %lu source motion %lu is not prepared",(unsigned long)p->public.index,(unsigned long)source_clip);
        /* A clip another actor's prepared pack lacks (or a stand-in model):
         * the actor keeps its current motion, and the gap is recorded. */
        note_presentation(s,0x33,"actor_motion_missing_kept_current");return 1;
    }
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
/* The free camera looks along its X/Y Euler rotation (native 29). */
static void aim_camera(Impl *s,float x,float y)
{
    s->view.camera.target[0]=s->view.camera.eye[0]-sinf(y)*cosf(x);
    s->view.camera.target[1]=s->view.camera.eye[1]+sinf(x);
    s->view.camera.target[2]=s->view.camera.eye[2]-cosf(y)*cosf(x);
    for(unsigned i=0;i<3;++i)s->camera_direction[i]=s->view.camera.target[i]-s->view.camera.eye[i];
}
/* fn_80117AE4: select the floor's field-camera texture entry id
 * (colosseum3ds/field_tv.h); the scene draws it into the screen. The
 * current id changes nothing; another restarts that entry's clip. The
 * shop's reviewed television (C3_FLOOR_TV) holds entry 0 only. */
static void tv_select(Impl *s,uint32_t id)
{
    if(s->view.tv_entry!=id){s->view.tv_entry=id;++s->view.tv_serial;}
    s->view.tv_enabled=id==0;
    log_line(s,"story_tv entry=%lu serial=%lu",(unsigned long)id,(unsigned long)s->view.tv_serial);
}
/* The news reports in story order (gs_range_801C766C.c fn_801C9F5C and
 * fn_801CA358 test them in this order): the story flag that makes a report
 * current, the flag native B6 sets once it has been watched, and the
 * entries fn_801CA358 selects before and after that. */
static const struct {uint16_t story,watched;uint8_t unwatched,seen;} tv_reports[]={
    {0x0ADA,0x0A0F,11,12},{0x0ADB,0x0483,11,12},{0x042C,0x042D,3,4},
    {0x0AD3,0x0A08,3,4},{0x0AD0,0x0A07,3,4},{0x0ACF,0x0A06,1,2}};
#define TV_REPORTS (sizeof tv_reports/sizeof tv_reports[0])
/* Index of the current report in tv_reports, TV_REPORTS for none. */
static int tv_current(Impl *s,unsigned *index,char *error)
{
    uint32_t value;
    for(*index=0;*index<TV_REPORTS;++*index){
        if(!flag(s,tv_reports[*index].story,&value,error))return 0;
        if(value)return 1;
    }
    return 1;
}
/* fn_801CA358 (native D3): select the current report's entry, the
 * watched one once its flag is set; with no report it sets 0A05 and
 * selects entry 0. */
static int tv_report(Impl *s,char *error)
{
    unsigned i;uint32_t value;
    if(!tv_current(s,&i,error))return 0;
    if(i==TV_REPORTS){if(!set_flag(s,0x0A05,1,error))return 0;tv_select(s,0);return 1;}
    if(!flag(s,tv_reports[i].watched,&value,error))return 0;
    tv_select(s,value?tv_reports[i].seen:tv_reports[i].unwatched);return 1;
}
/* ---- Native C5: elevators (floor_event.c floorEventCtrlElevator and
 * gs_range_801C766C.c fn_801C8E14, chosen by fn_801C9B6C). Offsets are
 * original units (x map scale); the cabin part is the object pack's
 * retained bone (C3D part_bone_plus_one). */
static int lift_door(const Impl *s,uint32_t row,uint32_t *pos)
{
    for(uint32_t i=0;i<s->door_count;++i)
        if(s->door_rows[i].index==row&&!(row&C3_AREA_MAP_OBJECT_ROW)){*pos=i;return 1;}
    return 0;
}
/* GSpartGetTransform position of the row's part, as the cabin stands now. */
static int lift_part(const Impl *s,uint32_t pos,float out[3])
{
    const C3StoryDoor *door=s->doors+pos;const C3AreaDoor *row=s->door_rows+pos;float m[12];
    if(!door->model||!row->part_bone_plus_one||row->part_bone_plus_one>door->model->bone_count||
       !c3_title_sample_bone(door->model,door->clip,door->seconds,row->part_bone_plus_one-1u,m))return 0;
    out[0]=m[3];out[1]=m[7];out[2]=m[11];return 1;
}
static float lift_clip_seconds(const C3Title *model,int clip)
{
    if(!model||clip<0||(uint32_t)clip>=model->clip_count||model->clips[clip].frame_count<2u||!(model->fps>0))return 0;
    return (float)(model->clips[clip].frame_count-1u)/model->fps;
}
/* fieldModelStartAnim: play a clip of the cabin from frame 0. */
static void lift_play(Impl *s,uint32_t pos,int clip)
{
    C3StoryDoor *door=s->doors+pos;
    if(!door->model||clip<0||(uint32_t)clip>=door->model->clip_count)return;
    door->clip=(uint32_t)clip;door->seconds=0;door->playing=1;door->loop=0;door->rate=1;
}
static Person *lift_partner(Impl *s)
{
    Person *p=person(s,0,101);return p&&p->public.visible?p:NULL;
}
static void lift_place(Impl *s,Person *p,const float at[3])
{
    C3FieldPlayer *q=pose(s,p);float spot[3]={at[0],at[1],at[2]},ground;
    if(c3_field_ground(s->map,spot,&ground))spot[1]=ground;
    memcpy(q->position,spot,sizeof spot);memcpy(q->proposed,spot,sizeof spot);
    p->moving=p->turning=0;
}
/* fn_8018AACC(group, id, 1, pos): walk there (native 3D with its facing). */
static int lift_walk(Impl *s,Person *p,const float at[3],char *error)
{
    float ground;memcpy(p->target,at,sizeof p->target);
    if(c3_field_ground(s->map,p->target,&ground))p->target[1]=ground;
    p->moving=1;p->waypoint_active=0;p->speed=1;p->public.flags|=8u;
    start_turn(s,p,atan2f(p->target[0]-pose(s,p)->position[0],p->target[2]-pose(s,p)->position[2]),1);
    return set_motion(s,p,p->source?p->source->motions[1]:2,0,1,error);
}
/* fn_801845E4 + fn_801860F8: attach Wes (and Rui) to the cabin part with
 * their offsets; fn_801C8E14 places the pair 5 apart along z. */
static void ride_attach(Impl *s,uint32_t pos,int party,uint32_t direction)
{
    static const float pair[4][3]={{0,0,5},{0,0,-5},{5,0,0},{-5,0,0}};
    memset(&s->ride,0,sizeof s->ride);s->ride.on=1;s->ride.pos=pos;s->ride.party=party;
    if(party){
        unsigned k=direction>=2&&direction<=5?direction-2u:direction?0u:1u;
        for(unsigned i=0;i<3;++i){s->ride.offset[0][i]=pair[k][i]*s->map->scale;s->ride.offset[1][i]=-pair[k][i]*s->map->scale;}
    }
    log_line(s,"story_lift_attach row=%lu party=%d",(unsigned long)s->door_rows[pos].index,party);
}
static void ride_update(Impl *s)
{
    float at[3];if(!s->ride.on||!lift_part(s,s->ride.pos,at))return;
    Person *who[2]={&s->hero_actor,s->ride.party?lift_partner(s):NULL};
    for(unsigned k=0;k<2;++k){if(!who[k])continue;C3FieldPlayer *q=pose(s,who[k]);
        for(unsigned i=0;i<3;++i)q->position[i]=at[i]+s->ride.offset[k][i];
        memcpy(q->proposed,q->position,sizeof q->position);who[k]->moving=0;}
}
/* Without the cabin model: the field entrance nearest the surface whose
 * common door script 05960006 calls this row, so the hero is never left
 * where the floor link put him (entrance 0). */
static int lift_spawn(const Impl *s,uint32_t row,float out[3],float *yaw)
{
    float centre[3]={0,0,0};unsigned n=0;
    for(uint32_t i=0;i<s->area->link_count&&!n;++i){const C3AreaLink *l=s->area->links+i;
        if(l->kind!=2||l->script!=0x05960006u||l->args[0]!=row)continue;
        for(uint32_t j=0;j<s->area->transition_count;++j){const C3FieldTriangle *tr=s->area->transitions+j;
            if(tr->source_id!=l->source_id)continue;
            for(unsigned v=0;v<3;++v){centre[0]+=tr->vertices[v*3];centre[2]+=tr->vertices[v*3+2];++n;}}
    }
    if(!n||!s->map->spawn_count)return 0;
    centre[0]/=(float)n;centre[2]/=(float)n;
    uint32_t best=0;float best_d=INFINITY;
    for(uint32_t i=0;i<s->map->spawn_count;++i){const float *q=s->map->spawns[i].position;
        float d=(q[0]-centre[0])*(q[0]-centre[0])+(q[2]-centre[2])*(q[2]-centre[2]);if(d<best_d){best_d=d;best=i;}}
    memcpy(out,s->map->spawns[best].position,3*sizeof(float));*yaw=s->map->spawns[best].yaw;return 1;
}
/* The animated and walking commands: 1/0x81 walk into the cabin, 2/0x82
 * step out of it, 0xC0 wait for the cabin's clip. */
static int lift_step(Impl *s,RunTask *t,float seconds,char *error)
{
    const float sc=s->map->scale;C3StoryDoor *door=s->doors+s->lift.pos;const C3AreaDoor *row=s->door_rows+s->lift.pos;
    Person *hero=&s->hero_actor,*partner=s->lift.party?lift_partner(s):NULL;float at[3],spot[3],yaw;
    for(unsigned guard=0;guard<8;++guard)switch(s->lift.stage){
    case 0: /* enterAnim played to its end, then the part */
        if(door->model&&door->playing)return 1;
        /* Stepping out ends 30 in front of the part. A part whose step-out
         * point has no floor under it (the 3F cabin's path joint stands 70
         * behind its doors: the spline's own joint offset is not applied,
         * as in HSD) takes the entrance fallback below. */
        int part=lift_part(s,s->lift.pos,at);
        if(part&&!(s->lift.cmd&1)){float out[3]={at[0],at[1],at[2]+30*sc},ground;part=c3_field_ground(s->map,out,&ground);}
        if(part){
            if(s->lift.cmd&1){
                spot[0]=at[0];spot[1]=at[1];spot[2]=at[2]-5*sc;if(!lift_walk(s,hero,spot,error))return 0;
                s->lift.partner_target[0]=at[0];s->lift.partner_target[1]=at[1];s->lift.partner_target[2]=at[2]+5*sc;
            }else{
                s->ride.on=0;
                spot[0]=at[0];spot[1]=at[1];spot[2]=at[2]+5*sc;lift_place(s,hero,spot);
                if(partner){spot[2]=at[2]-5*sc;lift_place(s,partner,spot);}
                spot[2]=at[2]+30*sc;if(!lift_walk(s,hero,spot,error))return 0;
                s->lift.partner_target[0]=at[0];s->lift.partner_target[1]=at[1];s->lift.partner_target[2]=at[2]+20*sc;
            }
            s->lift.timer=.2f;s->lift.stage=1;continue;
        }
        if(!(s->lift.cmd&1)){
            s->ride.on=0;
            if(lift_spawn(s,row->index,spot,&yaw)){
                lift_place(s,hero,spot);s->hero->yaw=yaw;
                if(partner&&partner==&s->partner_actor)partner_place_behind(s);
            }
            log_line(s,"story_lift_fallback row=%lu step_out=spawn",(unsigned long)row->index);
        }
        s->lift.stage=3;continue;
    case 1: /* 0.2 s, then Rui follows */
        s->lift.timer-=seconds;if(s->lift.timer>0)return 1;
        if(partner&&!lift_walk(s,partner,s->lift.partner_target,error))return 0;
        s->lift.stage=2;continue;
    case 2: /* peopleMoveCheck for both; walking in ends facing yaw 0 (fn_8018805C) */
        if(hero->moving||hero->turning||(partner&&partner->moving))return 1;
        if(s->lift.cmd&1){start_turn(s,hero,0,.5f);if(partner)start_turn(s,partner,0,.5f);s->lift.stage=4;continue;}
        s->lift.stage=3;continue;
    case 4:
        if(hero->turning||(partner&&partner->turning))return 1;
        s->lift.stage=3;continue;
    case 3: /* exitAnim */
        lift_play(s,s->lift.pos,row->close_anim);s->lift.stage=5;continue;
    case 5:
        if(door->model&&door->playing)return 1;
        log_line(s,"story_lift_done row=%lu cmd=%lx",(unsigned long)row->index,(unsigned long)s->lift.cmd);
        return resume(s,t,s->lift.value,error);
    default:return fail(error,"Invalid elevator step");
    }
    return 1;
}
static int lift_native(Impl *s,RunTask *t,const uint32_t a[],uint32_t *value)
{
    uint32_t pos,cmd=a[2]&0xffffu,direction=a[3]&0xffu;
    if(!lift_door(s,a[1],&pos)||!s->door_rows[pos].resource){*value=UINT32_MAX;return 1;}
    const C3AreaDoor *row=s->door_rows+pos;C3StoryDoor *door=s->doors+pos;Person *partner=lift_partner(s);
    /* fn_801C9B6C: walking in with Rui on the field, or direction 1 with
     * her, makes the pair ride (fn_801C8E14); otherwise Wes alone. */
    if(cmd&1)s->lift_party=partner!=NULL;
    if(direction==1&&partner)s->lift_party=1;
    int party=s->lift_party||(direction>=2&&direction<=5);
    log_line(s,"story_lift row=%lu cmd=%lx direction=%lu party=%d model=%d part_bone=%u",(unsigned long)row->index,
             (unsigned long)cmd,(unsigned long)direction,party,door->model!=NULL,(unsigned)row->part_bone_plus_one);
    if(direction>=2&&direction<=5){
        ride_attach(s,pos,1,direction);lift_play(s,pos,(int)cmd);ride_update(s);*value=0;return 1;
    }
    memset(&s->lift,0,sizeof s->lift);s->lift.pos=pos;s->lift.cmd=cmd;s->lift.party=party;
    switch(cmd){
    case 1:case 2:case 0x81:case 0x82:
        lift_play(s,pos,row->open_anim);
        if(cmd&0x80)note_presentation(s,0xc5,"elevator_sound_backend_pending");
        t->wait=W_LIFT;return 2;
    case 0xc0:s->lift.stage=5;t->wait=W_LIFT;return 2;
    case 0x100:*value=0;return 1;
    default:{
        int clip=cmd&4?row->alternate_a:cmd&8?row->alternate_b:-1;
        if(clip<0||!door->model||(uint32_t)clip>=door->model->clip_count){*value=UINT32_MAX;return 1;}
        if(cmd&0x20)ride_attach(s,pos,party,direction);
        float end=lift_clip_seconds(door->model,clip);
        door->clip=(uint32_t)clip;door->seconds=cmd&0x10?end:0;door->playing=1;door->loop=0;door->rate=1;
        ride_update(s);
        /* 100 x (2 x frames / 60): the scripts wait value/100 - 0.5 s. */
        s->lift.value=(uint32_t)lroundf(200.0f*end);*value=s->lift.value;
        if(cmd&0x40){s->lift.stage=5;t->wait=W_LIFT;return 2;}
        return 1;}
    }
}
/* ---- Native E8: fn_801C7730 (gs_range_801C766C.c). Before a person walks
 * up to Wes (the sight trainers call it right after B1), the follower walks
 * out of the corridor between Wes and that person. Lengths are original
 * units x map scale (lbl_8047E100..E11C: 1, 20, .5, pi/4, 8, 0, 4, 3). */
typedef struct { int index, blocked; float pos[3], distance; } AsideSpot;
static float aside_distance(const float a[3],const float b[3])
{return sqrtf((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));}
/* fn_8018D680: point within `width` across segment a..b and within its half
 * length along it, measured from the midpoint in a frame turned about +Y by
 * the segment heading (C_QUATRotAxisRad, then GSvecTransformQuat). Retail
 * turns by +heading, which mirrors the box about z for a diagonal segment;
 * kept as retail does it. */
static int aside_between(const float a[3],const float b[3],const float point[3],float width)
{
    float mid[3],d[3];for(unsigned i=0;i<3;++i){mid[i]=(a[i]+b[i])*.5f;d[i]=point[i]-mid[i];}
    float heading=atan2f(b[0]-a[0],b[2]-a[2]),c=cosf(heading),s=sinf(heading);
    float across=d[0]*c+d[2]*s,along=d[2]*c-d[0]*s;
    return width>=fabsf(across)&&aside_distance(mid,b)>=fabsf(along);
}
/* fieldSegmentBlocked: a segment of 20 or more is tested as two halves. */
static int aside_blocked(const float from[3],const float to[3],const float center[3],float width,float scale)
{
    if(aside_distance(from,to)<20.0f*scale)return aside_between(from,to,center,width);
    float mid[3];for(unsigned i=0;i<3;++i)mid[i]=(from[i]+to[i])*.5f;
    return aside_between(from,mid,center,width)||aside_between(mid,to,center,width);
}
/* fieldPlaneCrossed: whether start..end crosses the plane through Wes that
 * holds the Wes-person line and the person's point raised to height 1. */
static int aside_crossed(const float hero[3],const float other[3],const float start[3],const float end[3],float scale)
{
    float d0[3],d1[3],n[3],dir[3],l0=0,l1=0;
    for(unsigned i=0;i<3;++i){d0[i]=hero[i]-other[i];d1[i]=hero[i]-(i==1?1.0f*scale:other[i]);l0+=d0[i]*d0[i];l1+=d1[i]*d1[i];}
    l0=sqrtf(l0);l1=sqrtf(l1);for(unsigned i=0;i<3;++i){d0[i]/=l0;d1[i]/=l1;dir[i]=end[i]-start[i];}
    n[0]=d0[1]*d1[2]-d0[2]*d1[1];n[1]=d0[2]*d1[0]-d0[0]*d1[2];n[2]=d0[0]*d1[1]-d0[1]*d1[0];
    float den=n[0]*dir[0]+n[1]*dir[1]+n[2]*dir[2];
    if(den==0)return 0; /* GScolsys2UtilGetCpPlaneLine: parallel */
    float t=(n[0]*(hero[0]-start[0])+n[1]*(hero[1]-start[1])+n[2]*(hero[2]-start[2]))/den;
    return !(t<0||t>1);
}
/* fieldBuildRing: eight spots 8 around center (x cos, z sin of i x 45
 * degrees), blocked when fn_8010F320 (player walls along center..spot,
 * radius 4) hits; here the field's wall sweep, which also refuses a spot
 * with no floor. Sorted by distance from origin (qsort with
 * _fnDistanceSortFunc); equal distances keep ring order. */
static void aside_ring(Impl *s,AsideSpot ring[8],const float center[3],const float origin[3])
{
    const float sc=s->map->scale;
    for(int i=0;i<8;++i){AsideSpot *e=ring+i;float angle=(float)i*.78539819f;
        e->index=i;e->pos[0]=center[0]+8.0f*sc*(float)cos(angle);e->pos[1]=center[1];e->pos[2]=center[2]+8.0f*sc*(float)sin(angle);
        C3FieldPlayer probe=*s->hero;probe.collision_count=0;
        memcpy(probe.position,e->pos,sizeof e->pos);memcpy(probe.proposed,e->pos,sizeof e->pos);
        e->blocked=!c3_field_resolve_position(s->map,&probe,center)||probe.collision_count;
        e->distance=aside_distance(e->pos,origin);
    }
    for(int i=1;i<8;++i)for(int j=i;j>0&&ring[j-1].distance>ring[j].distance;--j){AsideSpot k=ring[j];ring[j]=ring[j-1];ring[j-1]=k;}
}
/* The spot fn_801C7730 walks the follower to (keepFacing 1, fn_8018AACC);
 * W_ASIDE then waits as fieldWaitPartnerMove does. With no spot, or no
 * follower in the corridor, it returns at once. */
static int aside_start(Impl *s,RunTask *t,Person *other,char *error)
{
    const float sc=s->map->scale,gap=4.0f*sc;Person *f=&s->partner_actor;
    float follower[3],hero[3],person_at[3];
    memcpy(follower,f->public.pose.position,sizeof follower);memcpy(hero,s->hero->position,sizeof hero);
    memcpy(person_at,pose(s,other)->position,sizeof person_at);
    /* fn_8018F5E4 radius + 1, tested with 1 + that: the corridor's half width. */
    float width=1.0f*sc+(other->source?other->source->radius:s->map->radius)+1.0f*sc;
    if(!aside_blocked(hero,person_at,follower,width,sc)){
        log_line(s,"story_step_aside person=%lu follower=clear",(unsigned long)other->public.index);return 1;}
    AsideSpot ring[8];
    for(int pass=0;pass<2;++pass){
        /* First the ring around Wes, then the ring around the follower,
         * each nearest the follower first. */
        aside_ring(s,ring,pass?follower:hero,follower);
        for(int i=0;i<8;++i){const AsideSpot *e=ring+i;
            if(e->blocked||aside_blocked(hero,person_at,e->pos,gap,sc)||aside_blocked(follower,e->pos,hero,gap,sc)||
               aside_crossed(hero,person_at,follower,e->pos,sc))continue;
            s->aside.has_alt=0;
            /* A diagonal spot of the first ring that is not reached in
             * 3 seconds is retried at its axis neighbour (3,5 -> 4; 1,7 -> 0). */
            if(!pass&&(e->index&1)){
                int wanted=e->index==3||e->index==5?4:0;
                for(int k=0;k<8;++k)if(ring[k].index==wanted){
                    if(!ring[k].blocked&&!aside_blocked(hero,person_at,ring[k].pos,gap,sc)){
                        s->aside.has_alt=1;memcpy(s->aside.alt,ring[k].pos,sizeof s->aside.alt);}
                    break;}
            }
            log_line(s,"story_step_aside person=%lu ring=%s spot=%d x=%.3f z=%.3f",(unsigned long)other->public.index,
                     pass?"follower":"hero",e->index,(double)e->pos[0],(double)e->pos[2]);
            if(!lift_walk(s,f,e->pos,error))return 0;
            s->aside.stage=0;s->aside.timer=3.0f;t->wait=W_ASIDE;return 1;
        }
    }
    log_line(s,"story_step_aside person=%lu follower=no_spot",(unsigned long)other->public.index);return 1;
}
/* fieldWaitPartnerMove for W_ASIDE: up to 3 s per attempt. When neither
 * attempt arrives, fn_80187D48 turns her toward her own position, which
 * ends the walk where she stands. */
static int aside_wait(Impl *s,RunTask *t,float seconds,char *error)
{
    Person *f=&s->partner_actor;
    if(!f->public.model||(!f->moving&&!f->turning))return resume(s,t,0,error);
    s->aside.timer-=seconds;if(s->aside.timer>0)return 1;
    if(!s->aside.stage&&s->aside.has_alt){
        if(!lift_walk(s,f,s->aside.alt,error))return 0;
        s->aside.stage=1;s->aside.timer=3.0f;return 1;
    }
    f->moving=f->turning=0;
    if(!set_motion(s,f,f->source?f->source->motions[0]:0,0,1,error))return 0;
    log_line(s,"story_step_aside follower=stopped x=%.3f z=%.3f",(double)f->public.pose.position[0],(double)f->public.pose.position[2]);
    return resume(s,t,0,error);
}
static int run_native(Impl *s,RunTask *t,char *error)
{
    C3StoryCall *c=&t->call;uint32_t *a=c->args,value=0;Person *p;
    C3StoryDoor *object=NULL;uint32_t object_row=0;
    if(c->kind==C3_STORY_WAIT_FRAMES){t->wait=W_SECONDS;t->remaining=(float)a[0]/60.0f;return 1;}
    /* Opcode 23 is fn_800F16C0 (gs_vm.c): it formats a debug string into
     * the developer log and returns no value. Retail shows nothing, so the
     * call is logged by position only (Pyrite's windmill cutscene, town
     * function 30, starts with one). */
    if(c->kind==C3_STORY_PRINT){
        log_line(s,"story_print task=%u script=%08lx pc=%08lx argc=%lu",t->key,(unsigned long)c->script_id,
                 (unsigned long)c->pc,(unsigned long)c->argc);
        return resume(s,t,0,error);
    }
    if(c->kind!=C3_STORY_NATIVE)return fail(error,"Unknown original GSVM call kind %u",(unsigned)c->kind);
    if(!validate_native(s,c,error))return 0;
    if(c->native!=0x51&&c->native!=0x47&&c->native!=0x1f&&c->native!=0x3a)
        log_line(s,"story_native task=%u script=%08lx pc=%08lx id=%02lx argc=%lu a=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx",
            t->key,(unsigned long)c->script_id,(unsigned long)c->pc,(unsigned long)c->native,(unsigned long)c->argc,
            (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2],(unsigned long)a[3],(unsigned long)a[4],(unsigned long)a[5]);
    switch(c->native){
    case 0x00:
        /* lbl_802E1CF0[0] is NULL: fn_800F10E8 unwinds with result 0. */
        break;
    case 0x03:
        if(!start(s,a[0],a+2,4,&t->child,error))return 0;
        value=t->child;break;
    case 0x04: c3_story_stop(&s->vm,(uint16_t)a[0]);break;
    case 0x06:
        t->child=(uint16_t)a[0];t->child_finished=0;
        if(c3_story_active(&s->vm,t->child)){t->wait=W_TASK;return 1;}break;
    case 0x07:
        if(c->argc<3||a[1]!=1)return fail(error,"Only blocking original field messages are supported");
        return message_when_free(s,t,a[0],s->speaker,error);
    case 0xe0:
        /* winMsgOpenFieldWithSE(message, wait, 0, sound): the blocking field
         * message of native 07 with a sound effect that is not yet played. */
        if(c->argc<2||a[1]!=1)return fail(error,"Only blocking original field messages are supported");
        note_presentation(s,0xe0,"message_sound_effect_silent");
        return message_when_free(s,t,a[0],s->speaker,error);
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
    /* heroMoveInitEvent/heroMoveTermEvent (hero_move.c) set and clear the
     * party's event people flags; they do not nest. Pyrite cave function 21
     * (Miror B.) starts an event, battles and returns without 21, then main
     * wraps function 11 in 20/21: retail gives control back after that 21. */
    case 0x20: s->hero_event=1;s->hero_ready=0;break;
    case 0x21: s->hero_event=0;break;
    case 0x24:
        if(a[0]!=0&&a[0]!=3&&a[0]!=4)return fail(error,"Camera mode %lu is not prepared",(unsigned long)a[0]);
        value=(uint32_t)s->camera_mode;s->camera_mode=(int)a[0];
        s->view.camera_active=s->camera_mode!=0;break;
    case 0x25:
        if(!a[0]&&a[1]==100){s->camera_person=&s->hero_actor;break;}
        /* Another person (group, index): follow them; one that is not on
         * this floor keeps the current target, and the gap is recorded. */
        p=person(s,a[0],a[1]);
        if(p)s->camera_person=p;else note_presentation(s,0x25,"camera_target_person_missing_kept_current");
        break;
    case 0x26: for(unsigned i=0;i<3;++i)s->view.camera.target[i]=as_float(a[i])*s->map->scale;break;
    case 0x27:
        if(as_float(a[0])!=0||as_float(a[2])!=0)return fail(error,"Nonvertical follow-camera offset is not prepared");
        s->map->camera_target_height=as_float(a[1])*s->map->scale;break;
    case 0x28: for(unsigned i=0;i<3;++i)s->view.camera.eye[i]=as_float(a[i])*s->map->scale;break;
    case 0x29: {
        float x=as_float(a[0]),y=as_float(a[1]);
        if(as_float(a[2])!=0)return fail(error,"Original camera roll is not prepared");
        s->camera_rotation[0]=x;s->camera_rotation[1]=y;s->rotation_time=s->rotation_duration=0;
        aim_camera(s,x,y);break;
    }
    case 0x2a:{
        /* A floor with several event cameras plays the one named. */
        const C3AreaCamera *kept=s->camera_track;
        for(uint32_t i=0;i<s->camera_count;++i)
            if(s->cameras[i].frames&&s->cameras[i].resource==a[1]){s->camera_track=s->cameras+i;break;}
        if(a[0]!=s->area->group||!s->camera_track||!s->camera_track->frames||
           a[1]!=(s->camera_track->resource?s->camera_track->resource:0x00e51800u))
        {
            /* An event camera that was not converted for this floor: the
             * current camera stays and nothing plays (waits see no camera). */
            s->camera_track=kept;note_presentation(s,0x2a,"event_camera_missing_kept_current");break;
        }
        s->view.camera_seconds=(float)a[2]/30.0f;s->camera_playing=1;s->camera_rate=1;
        s->view.camera_active=1;s->camera_mode=4;break;}
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
        if(!p||s->camera_mode!=3){
            /* Outside the free-camera scene (or toward an absent person) the
             * current camera stays; the gap is recorded. */
            note_presentation(s,0x30,"camera_target_interpolation_kept_current");break;
        }
        s->camera_person=p;s->target_time=0;s->target_duration=as_float(a[3]);s->target_moving=1;
        memcpy(s->target_from,s->view.camera.target,sizeof s->target_from);
        memcpy(s->target_to,pose(s,p)->position,sizeof s->target_to);break;
    case 0x32:
        /* A person whose pack could not be prepared stays hidden (as a
         * person without a model does in retail) and the gap is recorded. */
        p=person(s,a[0],a[1]);if(p){p->want_visible=!!a[2];p->public.visible=a[2]&&p->public.model;
            if(a[2]&&!p->public.model)note_presentation(s,0x32,"actor_pack_missing_hidden");}break;
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
            /* Retail shows "???" (0fa3) until the hear flag is set. No script
             * bank sets those flags (the engine does, outside the script VM),
             * so every named person stayed "???" here: the port shows the
             * person's own name, and the gap is recorded. */
            if(!known&&msg)note_presentation(s,0x36,"speaker_name_shown_before_heard");
            if(msg&&!c3_story_message(&s->vm,msg,s->hero_name,"","",s->speaker,sizeof s->speaker,error))return 0;
            /* A Pokemon has no PeopleInfo name: its species names it. */
            if(!msg&&s->species_name){unsigned species=c3_field_story_model_species(p->source->resource);
                if(species&&!s->species_name(s->species_context,species,s->speaker,sizeof s->speaker))s->speaker[0]=0;}
            if(!p->talking){p->old_yaw=p->public.pose.yaw;p->talking=1;}
            if(p->source->talk_start==2)p->public.pose.yaw=atan2f(s->hero->position[0]-p->public.pose.position[0],s->hero->position[2]-p->public.pose.position[2]);
        }
        return message_when_free(s,t,a[2],s->speaker,error);
    case 0x37:close_message(s);restore_speakers(s);break;
    case 0x3a:
        if(a[1]==100){ /* the hero: keep the box for diagnostics (c3_field_story_hero_regions) */
            float box[4]={as_float(a[2]),as_float(a[3]),as_float(a[4]),as_float(a[5])};unsigned k=0;
            while(k<s->hero_region_count&&memcmp(s->hero_regions[k],box,sizeof box))++k;
            if(k==s->hero_region_count&&k<32){memcpy(s->hero_regions[k],box,sizeof box);++s->hero_region_count;}
        }
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
            else {
                /* A field pose holds yaw only. Eagun's house (floor 02C)
                 * tilts Wes by a few degrees of pitch and roll as he wakes
                 * there; the tilt is recorded as unrendered. */
                if(as_float(a[2])!=0||as_float(a[4])!=0)note_presentation(s,0x3c,"actor_pitch_roll_unrendered");
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
        if(parent->source&&parent->source->aux3==SCENE_MODEL_MARKER){
            /* fn_801845E4 on an event scene's model: GSmodelGetPart(model,
             * part) is retained as the row's sampled bone. Its world matrix
             * carries the full rotation (as native A6's root part does). */
            if(a[4]!=parent->source->aux2||!parent->public.model||!parent->source->part1_bone_plus_one||
               parent->source->part1_bone_plus_one>parent->public.model->bone_count||parent->parent||p==parent)
                return fail(error,"Original scene-model part %lu is not prepared",(unsigned long)a[4]);
            p->parent=parent;p->parent_bone_plus_one=parent->source->part1_bone_plus_one;p->parent_root=1;
            p->moving=p->turning=p->waypoint_active=0;
            memset(p->local_position,0,sizeof p->local_position);memset(p->local_rotation,0,sizeof p->local_rotation);
            log_line(s,"story_attach child=%lu,%lu parent=%08lx part=%lu bone=%lu",(unsigned long)a[0],(unsigned long)a[1],
                     (unsigned long)a[3],(unsigned long)a[4],(unsigned long)p->parent_bone_plus_one-1u);
            value=1;break;
        }
        if(!parent->source||parent->source->resource!=0x010b0400u||a[4]!=1||
           !parent->public.model||!parent->source->part1_bone_plus_one||
           parent->source->part1_bone_plus_one>parent->public.model->bone_count||parent->parent||p==parent)
        {
            /* Only the reviewed vehicle attachment is ported: any other
             * leaves the child where it stands, and the gap is recorded. */
            note_presentation(s,0x46,"attachment_unported_left_detached");break;
        }
        /* GSmodelAttachToGSpart(type7,updateOnce0,reset1). The reviewed
         * vehicle has an identity source root and one sampled part node. */
        p->parent=parent;p->parent_bone_plus_one=parent->source->part1_bone_plus_one;p->parent_root=0;
        p->moving=p->turning=p->waypoint_active=0;
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
    /* GSflagGet (fn_801906A0, native table lbl_802E1CF0 entry 0x52): the
     * flag's whole stored value. Phenac Stadium's receptionist (function 7)
     * reads flag 0x9A0 with it. */
    case 0x52:if(!flag(s,a[0],&value,error))return 0;break;
    /* fn_8018CA20 (people.c peopleSetShadowVisible): show or hide the
     * person's ground shadow only; the person itself keeps its visibility
     * (native 32 does that). The port draws no person shadows, so the call
     * is recorded as unrendered. Hiding the person here removed the Pyrite
     * windmill keeper (floor 017 init hides his shadow while he waits). */
    case 0x97:
        p=person(s,a[0],a[1]);if(p)note_presentation(s,0x97,"person_shadow_unrendered");break;
    /* fn_80186620: does the person touch the vertical wall over segment
     * (x0,z0)-(x1,z1) within its PeopleInfo radius? The script passes
     * group, index, the segment, a float the original ignores and an
     * optional push word (PowerPC passes ints and floats in separate
     * registers, hence the source prototype's order). Pushed people are
     * set back to just outside the radius. A person standing still has no
     * step to sweep, so the test is the end point alone. */
    case 0xaa:{
        p=person(s,a[0],a[1]);if(!p)break;
        C3FieldPlayer *q=pose(s,p);float sc=s->map->scale;
        float x0=as_float(a[2])*sc,z0=as_float(a[3])*sc,x1=as_float(a[4])*sc,z1=as_float(a[5])*sc;
        float reach=p==&s->hero_actor?s->map->radius:p->source->radius;if(reach<=0)reach=4.0f*sc;
        float lx=x1-x0,lz=z1-z0,length=hypotf(lx,lz);if(length<0.0001f*sc)break;
        lx/=length;lz/=length;
        float px=q->position[0],pz=q->position[2],along=(px-x0)*lx+(pz-z0)*lz;
        float cx=x0+lx*along,cz=z0+lz*along; /* projection on the line */
        float sx=along<0?x0:along>length?x1:cx,sz=along<0?z0:along>length?z1:cz;
        if(hypotf(px-sx,pz-sz)>reach)break;
        value=1;
        if(c->argc>7&&a[7]){float dx=px-cx,dz=pz-cz,d=hypotf(dx,dz);
            if(d>0){q->position[0]=cx+dx/d*(reach+0.0001f);q->position[2]=cz+dz/d*(reach+0.0001f);}}
    }break;
    /* fn_80117AE4: select the floor's field-camera texture entry a[0]. */
    case 0xab:tv_select(s,a[0]);value=1;break;
    /* mailMainSendByScrpt: deliver mail once (mailAddMailbox). The scene
     * starts the new-mail tone and envelope (mailMainStartSe) when
     * view.mail_received changes. */
    case 0xad:{
        int added=c3_game_state_mail_add(s->game,a[0]);
        if(added<0)return fail(error,"Original mail %lu cannot be delivered",(unsigned long)a[0]);
        if(added){++s->mail_sent;s->view.mail_received=s->mail_sent;
            log_line(s,"story_mail id=%lu status=delivered alert=%lu",(unsigned long)a[0],(unsigned long)s->mail_sent);}
    }break;
    /* scriptStoreTemochiPokemon copies the party's cries and personality
     * values into lbl_804670B4 for later presentation natives. */
    case 0xe1:{
        C3GameSummary party;if(!c3_game_state_summary(s->game,&party))return fail(error,"Native E1 needs the party");
        for(unsigned i=0;i<6;++i){s->temochi[i].species=i<party.party_count?party.party[i].species:0;
            s->temochi[i].personality=i<party.party_count?party.party[i].personality:0;}
    }break;
    /* scriptCheckTemochiPokemon: 1 when a stored member is missing or a
     * party member was not stored (the party changed since native E1). */
    case 0xe2:{
        C3GameSummary party;uint8_t used[6]={0};
        if(!c3_game_state_summary(s->game,&party))return fail(error,"Native E2 needs the party");
        value=0;
        for(unsigned i=0;i<6&&!value;++i){if(!s->temochi[i].species)continue;unsigned j;
            for(j=0;j<party.party_count&&j<6;++j)if(!used[j]&&party.party[j].species==s->temochi[i].species&&
                party.party[j].personality==s->temochi[i].personality)break;
            if(j>=party.party_count||j>=6)value=1;else used[j]=1;}
        for(unsigned j=0;j<party.party_count&&j<6&&!value;++j)if(!used[j])value=1;
    }break;
    /* scriptGetPokemonNum: valid party members. */
    case 0x7b:{C3GameSummary party;if(!c3_game_state_summary(s->game,&party))return fail(error,"Native 7B needs the party");
        value=party.party_count;}break;
    /* fn_801140C8 clears the floor state's pending-resume override
     * (usePendingMaterial); the port keeps no such override. */
    case 0xa2:break;
    /* GbaMisc_HasActiveContextState: no Game Boy Advance is connected. */
    case 0xd4:value=0;break;
    /* scriptIsMoveButtonPush: 1 while the D-pad (pad mask 0xF) is pushed
     * or the control stick is deflected past 2. The scene turns the stick
     * into hero motion, so a walking or running hero counts as the stick;
     * the D-pad is this tick's pressed directions. Eagun's house (floor
     * 02C) waits on it after his talk before telling Wes about the lab. */
    /* fn_801140DC: write the save's resume floor, previous floor and spawn
     * (game-data status 5, 7 and 8, which New Game sets to 2/1/1 and
     * fn_80113FE8 reads on Continue). Mt. Battle's reception (floor 04B)
     * points it back at itself when a challenge ends. */
    case 0xf6:
        if(!a[0]||a[0]>0xffffu||a[1]>0xffffu||a[2]>0xffu)return fail(error,"Native F6 resume point %lu/%lu/%lu is invalid",
            (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2]);
        if(!c3_game_state_set_floor(s->game,a[0],a[1],(uint8_t)a[2]))return fail(error,"Native F6 could not write the resume point");
        log_line(s,"story_resume_point floor=%03lx previous=%03lx spawn=%lu",(unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2]);
        break;
    case 0xc3:value=s->arrival_spawn;break; /* floorGetNextPosIndex: the entrance used */
    case 0xd9:{
        /* fn_801CADA8(kind): the hero receives a gift Pokemon (1..6; 3,
         * Duking's Plusle, at floor 014). The scene's service builds and
         * stores it; the script gets heroGetPokemon's result (party slot,
         * -1 PC box, -2 no room). */
        if(!s->gift){
            note_presentation(s,0xd9,"gift_pokemon_no_service");
            log_line(s,"story_gift_pokemon kind=%lu status=no_service",(unsigned long)a[0]);value=1;break;
        }
        uint16_t species=0;unsigned where=0;
        int got=s->gift(s->gift_context,a[0],&species,&where);
        if(got<-2){
            note_presentation(s,0xd9,"gift_pokemon_not_added");
            log_line(s,"story_gift_pokemon kind=%lu status=failed",(unsigned long)a[0]);got=-2;
        }else if(got==-2)log_line(s,"story_gift_pokemon kind=%lu result=-2 status=no_room",(unsigned long)a[0]);
        else log_line(s,"story_gift_pokemon kind=%lu species=%u result=%d where=%s slot=%u",(unsigned long)a[0],
                      (unsigned)species,got,where<6?"party":"box",where);
        value=(uint32_t)(int32_t)got;break;}
    /* fn_8018BDF4: the person's position into a script GSvec. */
    case 0xac:{
        p=person(s,a[0],a[1]);if(!p||!a[2])break;
        const float *at=pose(s,p)->position;
        for(unsigned i=0;i<3;++i){float v=at[i]/s->map->scale;uint32_t w;memcpy(&w,&v,4);
            if(!c3_story_write_value(&s->vm,a[2]+i,w))return fail(error,"Native AC position target %08lx is invalid",(unsigned long)a[2]);}
    }break;
    /* floorEventChangeTresure rewrites a treasure-table row; treasure
     * pickups are not ported, so the row change is recorded only. */
    case 0xcb:
        if(a[0]>=512){value=(uint32_t)-1;break;}
        treasure_session[a[0]].changed=1;treasure_session[a[0]].item=a[1]&0xffffu;treasure_session[a[0]].count=(uint8_t)a[2];
        for(unsigned i=0;i<s->treasure_count;++i)if(s->treasure[i].index==a[0]){s->treasure[i].item=a[1]&0xffffu;s->treasure[i].count=(uint8_t)a[2];}
        log_line(s,"story_treasure_change index=%lu item=%lu count=%lu",(unsigned long)a[0],(unsigned long)(a[1]&0xffffu),(unsigned long)(a[2]&0xffu));
        value=0;break;
    /* floorEventCtrlElevator / fn_801C9B6C: elevator animations and
     * riding the platform. The animation is not rendered and its duration
     * (percent of a second) is zero; the floor change that follows is the
     * script's own native. */
    case 0x61:note_presentation(s,c->native,"elevator_animation");value=0;break;
    case 0xc5:{int r=lift_native(s,t,a,&value);if(!r)return 0;if(r==2)return 1;break;}
    /* fn_80181478: lock a person for a conversation (motion 1) or release
     * it; 1 while locked. Floor people here are always active. */
    case 0xe7:
        p=person(s,a[0],a[1]);if(!p){value=0;break;}
        if(a[2]){if(!p->talk_lock){p->talk_lock=1;if(!set_motion(s,p,1,0,1,error))return 0;}value=1;}
        else{p->talk_lock=0;value=0;}break;
    /* heroMoveAddAutoEvent: the event the hero's movement starts next
     * (script, then four arguments). Recorded; hero_move does not run it yet. */
    case 0x22:memcpy(s->auto_event,a,sizeof s->auto_event);
        log_line(s,"story_auto_event script=%08lx args=%lu,%lu,%lu,%lu",(unsigned long)a[0],(unsigned long)a[1],
                 (unsigned long)a[2],(unsigned long)a[3],(unsigned long)a[4]);break;
    /* scriptHaveItem: heroItemCheckHaveItemDataId over the bag pockets. */
    case 0x78:{static const unsigned counts[]={20,43,16,64,46,3};uint32_t total=0;
        for(unsigned pocket=0;pocket<6;++pocket)for(unsigned i=0;i<counts[pocket];++i){C3GameItem item;
            if(c3_game_state_item(s->game,pocket,i,&item)&&item.id==a[0])total+=item.quantity;}
        value=total&0xffu;}break;
    /* scriptGetDarkPokemonNum / scriptGetDarkPointZeroPokemonNum: party
     * Shadow Pokemon (darkpokemonDataId at 0xD8 with dp >= 0), and those
     * whose heart gauge (dp at 0xDC) is empty. */
    /* preReliveDisplayHokoraParticle: the shrine's glow while a party
     * Shadow Pokemon's heart is open (a particle thread). Not drawn. */
    case 0xf1:log_line(s,"story_shrine_particle drawn=0");value=0;break;
    /* field_range_801CB180.c: held D-pad or either axis outside [-2,2]. */
    case 0x6f:value=(uint32_t)s->move_input;break;
    case 0x7c:case 0xf2:{C3GameSummary party;uint8_t record[0x138];
        if(!c3_game_state_summary(s->game,&party))return fail(error,"Native %02lx needs the party",(unsigned long)c->native);
        value=0;
        for(unsigned i=0;i<party.party_count&&i<6;++i){if(!c3_game_state_pokemon(s->game,i,record))continue;
            uint16_t dark=(uint16_t)(record[0xd8]<<8|record[0xd9]);
            int32_t dp=(int32_t)((uint32_t)record[0xdc]<<24|(uint32_t)record[0xdd]<<16|(uint32_t)record[0xde]<<8|record[0xdf]);
            if(dark&&dp>=0&&(c->native==0x7c||dp==0))++value;}
    }break;
    /* fn_80184470: detach a person from the part it rides. */
    case 0xa8:
        p=person(s,a[0],a[1]);if(p&&p->parent)p->parent=NULL;
        if(p&&(p==&s->hero_actor||p==&s->partner_actor))s->ride.on=0;
        break;
    /* floorEventSetTresureDisp: show or hide a treasure object; treasure
     * objects are not floor people here, so the change is recorded. */
    /* floorEventSetTresureDisp(table index, shown). */
    case 0xd6:
        for(unsigned i=0;i<s->treasure_count;++i)if(s->treasure[i].index==a[0])s->treasure_display[i]=(int8_t)(a[1]!=0);
        log_line(s,"story_treasure_display index=%lu visible=%lu",(unsigned long)a[0],(unsigned long)a[1]);break;
    /* fn_801D0BD0: the Poke Ball battle-intro animation (mode 0) and its
     * cleanup (modes 1-3). Not rendered. */
    case 0x53:{
        /* fn_801D0BD0(group,id,mode), the Pokemon Center service: every
         * mode heals the whole party first through fn_801D0AFC, which runs
         * pokemonAllKaihuku on each valid member (HP, PP and condition).
         * Mode 0 then plays the healing machine with the party's balls
         * beside person (group,id) (fn_801D0DB0); modes 1-3 fade and play
         * jingle 0x19 (fn_801D0C30). The switch has no default action. */
        if(a[2]>3)break;
        if(!s->restore)return fail(error,"Native 53 mode %lu needs the party service",(unsigned long)a[2]);
        int restored=s->restore(s->restore_context);
        if(restored<0)return fail(error,"Native 53 mode %lu party service failed",(unsigned long)a[2]);
        log_line(s,"story_party_restore native=53 mode=%lu members=%d",(unsigned long)a[2],restored);
        if(a[2]){note_presentation(s,0x53,"heal_fade_jingle");break;}
        /* Mode 0: the machine sequence holds the script until it ends. */
        uint32_t count=0;uint8_t record[0x138];
        for(uint32_t i=0;i<6;++i)if(c3_game_state_pokemon(s->game,i,record)&&(record[0]|record[1]))++count;
        object=map_object(s,c3_field_heal_machine(s->area->group),&object_row);
        if(!object||!count||count>=object->model->clip_count||object->model->bone_count<=C3_HEAL_BALL_SLOTS){
            note_presentation(s,0x53,"heal_machine_balls");break;}
        memset(&s->heal,0,sizeof s->heal);
        s->heal.count=count;s->heal.machine=object_row;s->heal.group=a[0];s->heal.id=a[1];
        for(uint32_t i=0;i<count;++i)if(i>=s->heal_model_count||s->heal_bound[i]!=party_ball(s,i)){
            note_presentation(s,0x53,"heal_ball_model_stale");break;}
        p=person(s,a[0],a[1]);
        if(p){
            /* Face machine part 14 at turn speed 2, remembering the yaw. */
            float at[3];heal_part(object->model,14,at);
            s->heal.person=1;s->heal.yaw=pose(s,p)->yaw;
            start_turn(s,p,atan2f(at[0]-pose(s,p)->position[0],at[2]-pose(s,p)->position[2]),2.0f);
        }else s->heal.stage=1;
        log_line(s,"story_heal_start resource=%08lx balls=%lu person=%d",
                 (unsigned long)s->door_rows[object_row].resource,(unsigned long)count,s->heal.person);
        note_presentation(s,0x53,"heal_sound_effects_silent");
        t->wait=W_HEAL;return 1;
        }break;
    /* fn_80188984 (peopleIsHeadTurning): head turns complete at once here. */
    case 0x9b:value=0;break;
    /* fn_801C8DD0 (menuSubOpenSelect): the selection menu returns the
     * chosen row. The menu is not ported; the first row is taken. */
    case 0xc7:note_presentation(s,0xc7,"select_menu_first_row");value=0;break;
    /* fn_801C9C9C clears the blocked-path state (heroMoveChkHinderClear). */
    case 0xc4:break;
    /* fn_80089C54: a GBA is linked. None is. */
    case 0xdc:value=0;break;
    /* fn_801C89F8: the GBA link conversation. Without a GBA it ends with
     * message 3B83 and returns 0. */
    case 0xc8:return message_when_free(s,t,0x3b83,s->speaker,error);
    /* fn_801141D8: floor texture override (pending material). Not rendered. */
    case 0x80:note_presentation(s,0x80,"floor_texture_override");break;
    /* scriptSetMemoFlag: memoDataSetMemoFlag(0) marks every memo seen. */
    case 0xbd:if(c3_game_state_memo_mark_seen(s->game)<0)return fail(error,"Strategy Memo is unavailable");break;
    /* fn_8001E00C: custom menu 0x43; the first row (0) is taken. */
    case 0x9e:note_presentation(s,0x9e,"custom_menu_43_first_row");value=0;break;
    /* scriptGetPokedoru: the hero's money. */
    case 0x86:{C3GameSummary hero;if(!c3_game_state_summary(s->game,&hero))return fail(error,"Native 86 needs the hero");
        value=hero.money;}break;
    /* scriptAddPokedoru: add (or, negative, take) money, then the total.
     * heroSetStatus keeps the save's 0..9,999,999 range. */
    case 0x85:{C3GameSummary hero;if(!c3_game_state_summary(s->game,&hero))return fail(error,"Native 85 needs the hero");
        int64_t money=(int64_t)hero.money+(int32_t)a[0];if(money<0)money=0;if(money>9999999)money=9999999;
        if(!c3_game_state_add_money(s->game,money-(int64_t)hero.money))return fail(error,"Money cannot be changed");
        log_line(s,"story_money delta=%ld total=%ld",(long)(int32_t)a[0],(long)money);value=(uint32_t)money;}break;
    /* The Premium pool, flag 0A9E (field_range_801CA7EC.c): B7
     * scriptAddPremium (adds, returns the new value), BB scriptGetPremium,
     * BC scriptSetPremium. The colosseum scripts pay it out with BB then 85
     * and clear it with BC(0); okanePool battles add to it. */
    case 0xb7:if(!flag(s,C3_PREMIUM_FLAG,&value,error)||!set_flag(s,C3_PREMIUM_FLAG,value+a[0],error))return 0;
        value+=a[0];break;
    case 0xbb:if(!flag(s,C3_PREMIUM_FLAG,&value,error))return 0;break;
    case 0xbc:if(!set_flag(s,C3_PREMIUM_FLAG,a[0],error))return 0;break;
    /* fn_80035D70 pushes menu floor 0x393 and yields until it returns.
     * That menu floor is not ported; it returns at once. */
    case 0xca:s->view.unported_menu=0xca;
        log_line(s,"story_menu_floor floor=393 status=unported_returned");break;
    /* fn_801C766C: record the resume floor and run the save menu
     * (fn_801D0748(4,2,0) == 4 is saved). The scene owns saving; here
     * the save menu is not opened, which the original reports as 0. */
    case 0xeb:s->view.unported_menu=0xeb;
        log_line(s,"story_save_menu status=unported_not_saved");value=0;break;
    /* fn_801C8688(item, item, item): the three DNA disc items (0x200-0x211,
     * each pair one species) must all name species of the 3x3 grid
     * lbl_8026F488 (gbaConvCheckIds; result 2 otherwise, no floor push).
     * Then fn_800FF730(0x387) pushes the disc puzzle floor and CC returns 1
     * when its result (fn_80087C64) is 0. The scene runs floor 0x387 when it
     * enables that menu floor; otherwise the puzzle is taken as solved and
     * recorded, for headless callers without the modal scene enabled. */
    case 0xcc:{
        static const uint16_t grid[9]={0xb9,0xc8,0xf4,0x11f,0x9c,0xf3,0x99,0xf5,0x9f};
        uint16_t species[3];int ok=1;
        for(unsigned i=0;i<3;++i){unsigned sp=disc_species(a[i]&0xffffu);int found=0;
            for(unsigned j=0;j<9;++j){found|=sp&&grid[j]==sp;}
            species[i]=(uint16_t)sp;ok&=found;}
        if(!ok){log_line(s,"story_dna_check discs=%lx,%lx,%lx result=2",(unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2]);
            value=0;break;}
        unsigned i;
        for(i=0;i<s->menu_floor_count&&s->menu_floors[i]!=0x387u;++i){}
        if(i<s->menu_floor_count){
            s->view.menu_floor=0x387;memcpy(s->view.dna_species,species,sizeof species);
            log_line(s,"story_menu_floor floor=387 status=waiting_scene discs=%lx,%lx,%lx species=%x,%x,%x",
                     (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2],species[0],species[1],species[2]);
            t->wait=W_MENU_FLOOR;return 1;
        }
        log_line(s,"story_menu_floor floor=387 status=unported_solved temporary=1 discs=%lx,%lx,%lx",
                 (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2]);
        value=1;break;}
    /* fn_801C8834(item): on floor 0x43 the disc's Pokemon turns as a
     * hologram (menu 0xD8, fn_800756C8); the disc becomes its used twin
     * (item + 1). Returns the species. */
    case 0xcd:{
        unsigned sp=disc_species(a[0]&0xffffu);value=sp;
        if(!sp)break;
        if(s->area->floor==0x43)note_presentation(s,0xcd,"dna_hologram_menu_d8");
        if((a[0]&1u)==0){ /* gsAddItem(item,-1); gsAddItem(item+1,1) */
            if(!s->item_change)return fail(error,"Native CD needs the signed item service, which this scene did not bind");
            if(s->item_change(s->item_change_context,a[0]&0xffffu,-1)<0||s->item_change(s->item_change_context,(a[0]&0xffffu)+1u,1)<0)
                return fail(error,"DNA disc %lu could not be exchanged",(unsigned long)a[0]);
        }
        log_line(s,"story_dna_disc item=%lu species=%u",(unsigned long)(a[0]&0xffffu),sp);
        break;}
    /* fn_801C8804: close the hologram menu if it is open. */
    case 0xcf:break;
    /* Light animations (fn_801C9DC4 set, fn_801C9E7C active, fn_801C9CDC
     * wait for the end): the floor's animated lights are not rendered, so
     * an animation has ended at once. */
    case 0xbe:case 0xbf:case 0xc1:note_presentation(s,c->native,"light_animation_unrendered");value=0;break;
    /* fn_801EE67C(row, value): record row of the common table lbl_80478F6C
     * (data+0x1439D8, 0x38-byte rows; the PDA's notes): its order slot in
     * the save, flag +0x0C set and flag +0x1C = value. The table is not
     * prepared here; the call is recorded (the PDA notes are the systems
     * lane's). */
    case 0xf3:log_line(s,"story_pda_record row=%lu value=%lu status=unported_table",(unsigned long)a[0],(unsigned long)a[1]);break;
    /* fn_800190D8: register an item whose use from the pause menu runs a
     * script (eight entries; a ninth is ignored, as in the original). */
    case 0x11:
        if(s->item_event_count<8){s->item_events[s->item_event_count].item=(uint16_t)a[0];
            s->item_events[s->item_event_count++].script=a[1];}
        log_line(s,"story_item_event item=%lu script=%08lx count=%u",(unsigned long)(a[0]&0xffffu),(unsigned long)a[1],s->item_event_count);
        break;
    /* fn_801C8628: put the pause-menu cursor on its Items row (menu 0x59
     * item 0x20C) and run the pause menu (fn_8000D710(1)), so the player
     * can use a key item, e.g. at the Shadow Pokemon Lab's key slot. Using
     * an item looks it up in the native 11 list (fn_80019070, which also
     * empties the list) and queues its script as an auto event. The pause
     * menu is the scene's own: with it enabled the script waits with
     * view.item_menu set; otherwise it closes at once (the cancel branch). */
    case 0xd0:
        t->program=0xd0;t->prog_state=0;t->prog_asking=0;t->prog_yes=0;
        return program_step(s,t,0,error);
    case 0x54:case 0x55:case 0x56:case 0x57:
        t->program=c->native;t->prog_state=0;t->prog_asking=0;
        return program_step(s,t,0,error);
    /* preReliveSetParameter(mode, index): modes 0 and 1 run preReliveMain
     * (the Relic Stone's purification), as a program of this task. */
    case 0xc2:
        if(a[0]!=0&&a[0]!=1){value=0;break;}
        t->program=0xc2;t->prog_state=0;t->prog_asking=0;t->relive_mode=a[0];t->relive_slot=a[1];t->stage=0;
        return program_step(s,t,0,error);
    /* floorSetFadeScript: the scripts run around the next floor fade. */
    case 0x90:s->fade_begin=a[0];s->fade_poll=a[1];
        log_line(s,"story_fade_scripts begin=%08lx poll=%08lx",(unsigned long)a[0],(unsigned long)a[1]);break;
    case 0x5c:value=s->area->floor;break;
    case 0x5d:value=s->area->group;break;
    case 0x5e:value=s->previous_floor;break;
    case 0x5f:case 0x7d:
        if(s->floor_supported?!s->floor_supported(a[0]):(a[0]!=1&&a[0]!=2)){snprintf(s->view.blocked,sizeof s->view.blocked,"Area %lu is not prepared in this build. Press A to stay here.",(unsigned long)a[0]);
            log_line(s,"story_floor_unprepared from=%lu to=%lu spawn=%lu",(unsigned long)s->area->floor,(unsigned long)a[0],(unsigned long)a[1]);
            t->wait=W_BATTLE;return 1;}
        s->pending_floor=a[0];s->pending_spawn=a[1];break;
    case 0x64:
        p=person(s,s->area->group,a[0]);if(p){p->want_visible=!!a[1];p->public.visible=a[1]&&p->public.model;
            if(a[1]&&!p->public.model)note_presentation(s,0x64,"actor_pack_missing_hidden");}break;
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
        if(a[0]==1&&s->name_entry_enabled){
            /* Mode 1 at its only reached site (Phenac function 6) names the
             * partner. The scene collects the name and reports it. */
            s->view.name_entry=a[0]+1u;
            log_line(s,"story_name_entry mode=%lu index=%lu status=waiting_scene",(unsigned long)a[0],(unsigned long)a[1]);
            t->wait=W_NAME;return 1;
        }
        log_line(s,"story_name_entry mode=%lu index=%lu status=default_name_kept menu_ported=0",
                 (unsigned long)a[0],(unsigned long)a[1]);
        value=1;break;
    case 0x62:{
        /* fn_800FF730(floor): push a menu floor (0x391 PC box, 0x38F bag,
         * 0x392 P*DA, 0x386 evolution ...) and yield until it returns. A
         * floor the scene shows waits with view.menu_floor set; any other
         * is not ported and returns at once, recorded. */
        unsigned i;
        for(i=0;i<s->menu_floor_count&&s->menu_floors[i]!=a[0];++i){}
        if(i<s->menu_floor_count){
            s->view.menu_floor=a[0];
            log_line(s,"story_menu_floor floor=%03lx status=waiting_scene",(unsigned long)a[0]);
            t->wait=W_MENU_FLOOR;return 1;
        }
        s->view.unported_menu=0x62;
        log_line(s,"story_menu_floor floor=%03lx status=unported_returned",(unsigned long)a[0]);
        break;}
    case 0x19:
        /* menuShopOpen (native table entry 0x19) stores the shop number and
         * links to menu floor 0x38F, which runs the shop and returns. That
         * floor is not ported; the scene shows the port's shop menu for the
         * same shop and stock (colosseum3ds/shop.h) and reports when it is
         * left. Without a scene the shop opens and closes at once. */
        if(s->shop_enabled){
            s->view.shop=a[0]+1u;
            log_line(s,"story_shop shop=%lu status=waiting_scene",(unsigned long)a[0]);
            t->wait=W_SHOP;return 1;
        }
        log_line(s,"story_shop shop=%lu status=closed menu_ported=0",(unsigned long)a[0]);
        break;
    case 0x23:
        /* fn_801C9F00: heroSetStatus(0,0x18,active) and the follower state. */
        if(!c3_game_state_set_partner(s->game,a[0]!=0))return fail(error,"The partner state cannot be written");
        if(a[0]&&!s->partner_actor.public.visible&&s->partner_actor.public.model)partner_place_behind(s);
        s->partner_actor.public.visible=a[0]!=0&&s->partner_actor.public.model;s->partner_actor.want_visible=a[0]!=0;
        log_line(s,"story_partner present=%d",a[0]!=0);
        break;
    case 0x31:
        /* cameraUpdate: recompute the field camera now. The scene derives
         * its camera from the current state on every frame. */
        break;
    case 0x60:{
        /* floorEventCtrlDoor(floor,index,mode): 0 open animated, 1 open
         * instant, 2 close animated, 3 close instant. */
        uint32_t position,mode=a[2]&0xffu,open=0,gate=0;const C3AreaDoor *row=NULL;C3StoryDoor *door=NULL;
        for(position=0;position<s->door_count;++position)if(s->door_rows[position].index==a[1]&&!(a[1]&C3_AREA_MAP_OBJECT_ROW)){row=s->door_rows+position;door=s->doors+position;break;}
        if(!row||mode>3){value=UINT32_MAX;break;}
        if(row->open_flag&&!flag(s,row->open_flag,&open,error))return 0;
        if(row->close_flag&&!flag(s,row->close_flag,&gate,error))return 0;
        if(mode==0){
            if(row->open_flag&&open){value=0;break;}       /* already open */
            if(row->close_flag&&!gate){value=1;break;}     /* locked */
        }
        if(mode==2&&row->open_flag&&!open){value=0;break;} /* already closed */
        if(row->open_flag&&!set_flag(s,row->open_flag,mode<2,error))return 0;
        if(row->collision_id>=0)c3_field_set_object_enabled(s->map,(uint32_t)row->collision_id,mode>=2);
        int animation=mode==0?row->open_anim:mode==2?row->close_anim:mode==1?row->open_end_anim:row->close_end_anim;
        int last_frame=0;
        if(mode&1u&&animation<0){animation=mode==1?row->open_anim:row->close_anim;last_frame=1;}
        if(!row->resource||!door->model||animation<0||(uint32_t)animation>=door->model->clip_count){value=UINT32_MAX;break;}
        door->clip=(uint32_t)animation;door->playing=!last_frame;
        door->seconds=last_frame?(float)(door->model->clips[animation].frame_count-1u)/door->model->fps:0;
        log_line(s,"story_door_control index=%lu mode=%lu clip=%d",(unsigned long)a[1],(unsigned long)mode,animation);
        if(mode==1||mode==3){value=0;break;}
        /* Modes 0 and 2 hold the hero in an event until the model stops. */
        note_presentation(s,c->native,"door_sound_backend_pending");
        ++s->event_depth;s->hero_ready=0;t->stage=position;t->wait=W_DOOR;return 1;}
    case 0x66:case 0x98:
        if(c->native==0x66||c->argc<3){a[1]=c->argc>1?a[1]:0;a[2]=0;if(c->native==0x66)a[1]=0;}
        s->view.battle_id=a[0];
        if(s->battle_loaded&&a[0]==10){
            size_t bytes=0;const uint8_t *body=c3_game_state_body(s->game,&bytes);
            if(!c3_battle_request(&s->battle_request,&s->battle_data,a,body,bytes,error))return 0;
            log_line(s,"story_battle_snapshot encounter=%lu player_party=%u status=waiting_backend save_unchanged=1",
                     (unsigned long)a[0],s->battle_request.player_party_count);
        }
        /* Marks the waiting request only: the field starts the battle at
         * once (no prompt in the original) and never shows this text. */
        snprintf(s->view.blocked,sizeof s->view.blocked,"Battle %lu is starting.",(unsigned long)a[0]);
        log_line(s,"story_battle encounter=%lu status=waiting_backend result_written=0",(unsigned long)a[0]);
        t->wait=W_BATTLE;return 1;
    case 0x67:
        /* fn_801EF634 (native table row 0x67): the result ID the last fight
         * left in the fight-floor global. Zero until a battle has finished. */
        value=s->fight_result;break;
    case 0x68:case 0x69:
        /* fn_80165A20 / fn_801659FC (sound, fade, volume): start a song in
         * the 0x406 / 0x407 slot; the floor scripts start area music here. */
        s->sound_id=a[0];
        if(s->audio){
            int r=s->audio(s->audio_context,c->native==0x68?C3_STORY_AUDIO_PLAY_406:C3_STORY_AUDIO_PLAY_407,a[0],a[1],a[2]);
            log_line(s,"story_audio native=%02lx sound=%lx fade=%lu volume=%lu started=%d",(unsigned long)c->native,
                     (unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[2],r);
        }else note_presentation(s,c->native,"field_audio_backend_pending");
        break;
    case 0x6a:case 0x6d:
        /* soundStop(sound, fade) / fn_80165718(volume, fade). */
        if(s->audio){
            s->audio(s->audio_context,c->native==0x6a?C3_STORY_AUDIO_STOP:C3_STORY_AUDIO_MASTER,a[0],a[1],0);
            log_line(s,"story_audio native=%02lx a0=%lx fade=%lu",(unsigned long)c->native,(unsigned long)a[0],(unsigned long)a[1]);
        }else note_presentation(s,c->native,"field_audio_backend_pending");
        break;
    case 0x8a:case 0x99:
        note_presentation(s,c->native,"field_audio_backend_pending");break;
    case 0x70:{
        /* fn_801CBA0C: peopleOpen of an original model, with signed-byte
         * serial ORed into the original transient person namespace. Packs
         * are reserved and streamed by the scene, never fabricated here. */
        uint32_t id=(uint32_t)(int32_t)(int8_t)s->dynamic_serial++|0x7ffe0000u;p=NULL;
        for(uint32_t i=0;i<s->area->actor_count;++i){Person *candidate=s->actors+i;
            if(candidate->source->aux3==DYNAMIC_MARKER&&!candidate->dynamic_active&&candidate->source->resource==a[0]){p=candidate;break;}}
        if(!p||!p->public.model)return fail(error,"Dynamic original model %08lx is not prepared",(unsigned long)a[0]);
        if(person(s,s->area->group,id))break; /* Source duplicate open returns zero. */
        p->dynamic_active=1;p->public.index=id;p->want_visible=1;p->public.visible=1;
        p->public.clip=0;p->public.loop=1;p->public.playing=1;p->public.pose.animation_seconds=0;
        memcpy(p->public.pose.position,p->source->position,sizeof p->source->position);p->public.pose.yaw=p->source->yaw;
        value=id;log_line(s,"story_dynamic_open resource=%08lx id=%08lx",(unsigned long)a[0],(unsigned long)id);
        }break;
    case 0xa6:{
        /* fn_801CB61C -> fn_801845E4: source child resource follows the
         * original animated carrier's root part0, retained as C3DY aux2. */
        p=person(s,s->area->group,a[0]);Person *parent=person(s,s->area->group,a[1]);
        if(!p||!parent)break;
        if(a[2]||!parent->source||parent->source->aux3!=DYNAMIC_MARKER||
           !parent->source->aux2||!parent->public.model||parent->parent||p==parent||
           parent->source->aux2>parent->public.model->bone_count)
            return fail(error,"Original root attachment is not prepared");
        p->parent=parent;p->parent_bone_plus_one=parent->source->aux2;p->parent_root=1;
        p->moving=p->turning=p->waypoint_active=0;
        memset(p->local_position,0,sizeof p->local_position);memset(p->local_rotation,0,sizeof p->local_rotation);
        value=1;break;
    }
    case 0x71:
        /* fn_801CB9D8 closes a transient person and releases its slot. The
         * prepared GPU pack stays resident for a later original allocation. */
        p=person(s,s->area->group,a[0]);
        if(p&&p->source&&p->source->aux3==DYNAMIC_MARKER){
            p->dynamic_active=0;p->want_visible=0;p->public.visible=0;p->public.playing=0;
            p->moving=p->turning=p->waypoint_active=0;p->parent=NULL;
            log_line(s,"story_dynamic_close id=%08lx",(unsigned long)a[0]);}
        break;
    case 0xdd:
        /* fn_801CB530 binds the source particle bank to a model. Generic
         * HSD particles have no renderer; preserve this visible limitation. */
        note_presentation(s,c->native,"particle_bank_attachment_unrendered");break;
    case 0x72:
        /* fn_801CB954: a resource that is a floor person changes that
         * person's visibility (as native 32); otherwise the map model's.
         * Map-model visibility is recorded; the floor renderer draws every
         * map group (hidden groups are a renderer follow-up). */
        p=person(s,s->area->group,a[0]);
        if(p){p->want_visible=!!a[1];p->public.visible=a[1]&&p->public.model;break;}
        if(!a[1])note_presentation(s,0x72,"map_resource_hide");
        log_line(s,"story_map_visibility resource=%lu visible=%lu",(unsigned long)a[0],(unsigned long)a[1]);
        break;
    case 0x76:
        value=c3_field_set_object_enabled(s->map,a[0],!!a[1])?0:2;break;
    case 0x7f:
        /* cameraSetFloorDefault(height, distance, rotY) (camera.c) stores all
         * three as given: there is no "keep" value. The shop passes rotY -1.0,
         * a real rotation of -1 radian that turns its follow camera to the
         * front-left three-quarter view; reading it as "unchanged" left the
         * camera square to the car's black cut-away front wall. */
        s->map->camera_height=as_float(a[0])*s->map->scale;
        s->map->camera_distance=as_float(a[1])*s->map->scale;
        s->map->camera_yaw=as_float(a[2]);
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
    case 0xdb:{
        /* scriptAddItem (field_range_801CA7EC.c): a direct bag mutation,
         * without native 77's jingle, message or PC-overflow handling.
         * The retail ABI narrows itemId and the magnitude to u16. */
        int32_t count=(int32_t)a[1];
        if(!count)return fail(error,"Native DB zero item count has no defined retail result");
        if(!s->item_change)return fail(error,"Native DB needs the signed item service, which this scene did not bind");
        uint32_t item=(uint16_t)a[0],amount=(uint16_t)(count<0?0u-a[1]:a[1]);
        int32_t signed_amount=count<0?-(int32_t)amount:(int32_t)amount;
        int result=s->item_change(s->item_change_context,item,signed_amount);
        value=(uint32_t)result;
        log_line(s,"story_item_direct item=%lu count=%ld result=%d",(unsigned long)item,(long)signed_amount,result);
        }break;
    case 0x77:
        /* scriptGetItem -> floorEventGetTresure(4,item,count): the item
         * jingle 0x3CA plays to its end first (a fixed wait here), then the
         * bag receives the item and the result message is shown. */
        if(!s->receive)return fail(error,"Native 77 needs the item service, which this scene did not bind");
        /* floorEventGetTresure plays jingle 0x3CA (fn_80165668(0x3CA,0,0xFF))
         * and waits for it; W_ITEM then waits for the window. */
        if(s->audio&&s->audio(s->audio_context,C3_STORY_AUDIO_JINGLE,0x3ca,0,0xff)>0){
            t->jingle=0x3ca;t->jingle_seconds=0;t->wait=W_ITEM;t->remaining=0;return 1;}
        note_presentation(s,c->native,"item_jingle_stream_missing_wait_1s");
        t->jingle=0;t->wait=W_ITEM;t->remaining=1.0f;return 1;
    case 0x84:
        /* msgctrlSetValue(variable,value). */
        if(a[0]>255)return fail(error,"Message variable %lu is out of range",(unsigned long)a[0]);
        s->message_values[a[0]]=a[1];break;
    case 0x73:case 0x9a:{
        /* fn_801CB834 / fn_8018B368: start a raw model animation on a person
         * (73 addresses it by index in the current group). 73 on a model
         * resource that is not a person animates a map object. */
        uint32_t shift=c->native==0x9a;
        p=c->native==0x9a?person(s,a[0],a[1]):person(s,s->area->group,a[0]);
        if((int32_t)a[shift+1]<0)break;
        if(p){if(!set_motion(s,p,a[shift+1],a[shift+2],a[shift+3],error))return 0;}
        else if(c->native==0x73&&(object=map_object(s,a[0],NULL))&&a[1]<object->model->clip_count){
            /* GSmodelSetAnimIndex/Frame, rate 0.5 (30 source frames a
             * second), loop or one-shot; the pack's clip is both tracks. */
            float end=(float)(object->model->clips[a[1]].frame_count-1u)/object->model->fps;
            object->clip=a[1];object->loop=a[3]!=0;object->playing=1;object->rate=1;
            object->seconds=fminf(end,fmaxf(0,(float)(int32_t)a[2]/30.0f));
            log_line(s,"story_object_motion resource=%08lx clip=%lu loop=%lu",(unsigned long)a[0],(unsigned long)a[1],(unsigned long)a[3]);}
        else if(c->native==0x73)note_presentation(s,c->native,"map_object_animation_unrendered");
        }break;
    case 0x74:
        /* fn_801CB7C4: stop a person's resource animation, else the raw
         * floor model: a bound map object holds its current pose. */
        p=person(s,s->area->group,a[0]);
        if(p){p->public.playing=0;p->pending_motion=0;}
        else if((object=map_object(s,a[0],NULL)))object->playing=0;
        else note_presentation(s,c->native,"map_object_animation_stop_unrendered");
        break;
    case 0x75:
        /* scriptWaitSyncMotion(resource,wait): a person waits like native 35;
         * a bound map object waits for its clip to end; any other model
         * resource reports finished. */
        p=person(s,s->area->group,a[0]);
        if(p&&p->public.playing&&pose(s,p)->animation_seconds<clip_seconds(&p->public)){
            if(!a[1])value=1;
            else if(!p->public.loop){t->call.args[1]=a[0];t->call.args[0]=s->area->group;t->wait=W_MOTION;return 1;}
        }else if(!p&&(object=map_object(s,a[0],&object_row))&&object->playing){
            /* GSmodelHasAnimationEnded: a looping clip never ends. */
            if(!a[1])value=1;
            else{t->stage=object_row;t->wait=W_OBJECT;return 1;}
        }break;
    case 0x8d:
        /* cameraMoveRotationXYZ: linear interpolation of the camera rotation. */
        if(as_float(a[3])!=0)return fail(error,"Original camera roll is not prepared");
        s->rotation_from[0]=s->camera_rotation[0];s->rotation_from[1]=s->camera_rotation[1];
        s->rotation_to[0]=as_float(a[1]);s->rotation_to[1]=as_float(a[2]);
        s->rotation_time=0;s->rotation_duration=as_float(a[4]);
        if(s->rotation_duration<=0){s->camera_rotation[0]=s->rotation_to[0];s->camera_rotation[1]=s->rotation_to[1];
            aim_camera(s,s->camera_rotation[0],s->camera_rotation[1]);}
        break;
    case 0xa0:
        /* cameraMoveEndCheck(wait): scripted camera moves still running? */
        if(s->camera_returning||s->target_moving||s->camera_move_time<s->camera_move_duration||s->rotation_time<s->rotation_duration){
            if(a[0]){t->wait=W_CAMERA_MOVE;return 1;}
            value=1;
        }break;
    case 0xb5:{
        /* fn_80188FA0: look at another person (41 is this with the hero). */
        Person *target=person(s,a[2],a[3]);p=person(s,a[0],a[1]);
        if(p&&target){p->looking=1;memcpy(p->look,pose(s,target)->position,sizeof p->look);
            note_presentation(s,c->native,"procedural_head_aim");}
        }break;
    case 0x7e:
        /* heroMoveSetNeckMode: procedural head tracking of a party member. */
        note_presentation(s,c->native,"head_tracking_unrendered");
        value=a[1]<=1&&(a[0]==0||(a[0]==1&&s->partner_actor.public.visible));break;
    case 0x82:
        /* fn_80165668 pauses the BGM, plays a jingle and blocks until it ends:
         * the wait follows the rendered song (0x3C9/0x3CA are one-shot
         * songs of the music pack). Without the stream the script waits a
         * fixed substitute time, recorded. */
        if(start_jingle(s,t,a[0],a[1],a[2]))return 1;
        note_presentation(s,c->native,"jingle_stream_missing_wait_2s");
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
    case 0xb2:
        /* fn_801821B8 (peopleFallAppear): the person is shown, flag 8 is
         * cleared, sound 0x49E plays and it drops from 40 source units above
         * its position, 1.3 units per 60 Hz frame (fn_800D3088), with motion
         * 7; on the ground motion 8 plays to its end, then motion 1. The
         * table row's third word is not read by the original. The sound is
         * recorded as unrendered. */
        p=person(s,a[0],a[1]);if(!p)break;
        p->want_visible=1;p->public.visible=p->public.model!=NULL;p->public.flags&=~8u;
        note_presentation(s,0xb2,"fall_sound_049e_unrendered");
        t->fall_ground=pose(s,p)->position[1];t->remaining=40.0f;
        pose(s,p)->position[1]=t->fall_ground+40.0f*s->map->scale;
        if(!set_motion(s,p,7,0,1,error))return 0;
        t->stage=0;t->wait=W_FALL;return 1;
    case 0xb1:{
        /* fn_8018A700: walk up to `distance` (tens of source units) from
         * another person, along the line between them; already inside that
         * distance the person stops. The walk is the 3D primitive. */
        Person *target=person(s,a[2],a[3]);p=person(s,a[0],a[1]);
        if(p&&target){
            float delta[3],reach=10.0f*as_float(a[4])*s->map->scale,length=0;
            for(unsigned i=0;i<3;++i){delta[i]=pose(s,p)->position[i]-pose(s,target)->position[i];length+=delta[i]*delta[i];}
            length=sqrtf(length);
            float ratio=length==0?1:reach/length;
            if(ratio>=1){p->moving=0;p->turning=0;}
            else{
                if(p->parent)return fail(error,"Attached actor walking is not prepared");
                for(unsigned i=0;i<3;++i)p->target[i]=pose(s,target)->position[i]+delta[i]*ratio;
                p->moving=1;p->waypoint_active=0;p->speed=1;p->public.flags|=8u;
                start_turn(s,p,atan2f(p->target[0]-pose(s,p)->position[0],p->target[2]-pose(s,p)->position[2]),1);
                if(!a[5])p->turning=0;
                if(!set_motion(s,p,p->source?p->source->motions[1]:2,0,1,error))return 0;
            }
        }
        }break;
    case 0xd8:{
        /* fn_801C852C: guarded by 08AE. Mode0 heals fully; mode1
         * clears only condition IDs 3 and4 (poison/toxic), without HP/PP. */
        uint32_t battle_mode=0;
        if(!flag(s,0x8ae,&battle_mode,error))return 0;
        if(battle_mode)break;
        if(a[0]>1)break; /* the original switch has no default action */
        C3FieldStoryRestore service=a[0]?s->clear_poison:s->restore;
        void *context=a[0]?s->poison_context:s->restore_context;
        if(!service)return fail(error,"Native D8 mode %lu needs the %s service",(unsigned long)a[0],a[0]?"poison":"party");
        int restored=service(context);
        if(restored<0)return fail(error,"Native D8 mode %lu party service failed",(unsigned long)a[0]);
        log_line(s,"story_party_restore mode=%lu members=%d",(unsigned long)a[0],restored);
        }break;
    case 0xe8:
        /* fn_801C7730 moves the follower out of the hero-to-NPC corridor
         * and always returns 0 (aside_start). Without a follower on the
         * field it returns at once, as the original does when member 1 has
         * no resource; so it does for a person that is not there. */
        p=person(s,a[0],a[1]);
        if(s->partner_actor.public.model&&s->partner_actor.public.visible&&p&&p!=&s->partner_actor&&p!=&s->hero_actor){
            if(!aside_start(s,t,p,error))return 0;
            if(t->wait!=W_NONE)return 1;
        }
        break;
    case 0xb4:
        /* floorGetNextFloorID: destination of the most recent link; on
         * arrival it still names the current floor. */
        value=s->pending_floor?s->pending_floor:s->area->floor;break;
    case 0x9d:
        /* fn_801CA4F8 loops while elapsed < duration: a negative duration
         * (common bank 05960006 computes -0.5) returns at once. */
        t->remaining=as_float(a[0]);if(!isfinite(t->remaining)||t->remaining>3600)return fail(error,"Invalid story wait duration");
        if(t->remaining<0)t->remaining=0;
        t->wait=W_SECONDS;return 1;
    case 0xb3:
        s->camera_mode=0;s->camera_playing=0;s->camera_person=&s->hero_actor;
        s->return_from=s->view.camera;c3_field_camera(s->map,s->hero,&s->return_to);
        s->return_duration=as_float(a[0]);s->return_time=0;s->camera_returning=1;s->view.camera_active=1;
        s->target_moving=0;s->camera_move_time=s->camera_move_duration;
        if(a[1]){t->wait=W_CAMERA_MOVE;return 1;}break;
    /* fn_801C9F5C: the news event of the television check (common bank
     * 0596 function 19), a program of this task (program_step 0xb6). */
    case 0xb6:
        ++s->event_depth;t->program=0xb6;t->prog_state=0;t->prog_asking=0;t->stage=0;
        return program_step(s,t,0,error);
    case 0xd1:
        if(!start(s,a[0],a+1,4,&t->child,error))return 0;
        t->child_finished=0;t->wait=W_TASK;return 1;
    case 0xd3:if(!tv_report(s,error))return 0;value=1;break;
    case 0xe4:value=s->sound_id;break;
    default:return fail(error,"Unported native %02lx in GSVM %08lx at %08lx",(unsigned long)c->native,(unsigned long)c->script_id,(unsigned long)c->pc);
    }
    return resume(s,t,value,error);
}

/* etctool.c menu programs as state machines. say() shows a message and
 * continues at `next`; ask() shows a message then the yes/no menu
 * (fn_8001E184: 0 is yes). A step that needs an unported menu (party
 * selection, Day Care withdrawal, the gift table) takes the branch the
 * original takes when the player cancels, and view.unported_menu records
 * the native. END finishes the native with 0. */
enum { END=-1 };
static int say(Impl *s,RunTask *t,uint32_t id,int next,char *error)
{t->prog_state=next;t->prog_asking=0;return message(s,t,id,s->speaker,error);}
static int ask(Impl *s,RunTask *t,uint32_t id,int yes,int no,char *error)
{t->prog_yes=yes;t->prog_no=no;t->prog_asking=1;return message(s,t,id,s->speaker,error);}
static void unported_menu(Impl *s,RunTask *t,const char *what)
{
    s->view.unported_menu=t->program;
    log_line(s,"story_menu_program native=%02lx step=%s status=unported_cancel_branch",(unsigned long)t->program,what);
}
static int near_surface(const Impl *s,uint32_t source_id,float reach)
{
    const C3FieldTriangle *sets[2]={s->area->transitions,s->area->interactions};
    uint32_t counts[2]={s->area->transition_count,s->area->interaction_count};
    for(unsigned k=0;k<2;++k)for(uint32_t i=0;i<counts[k];++i){const C3FieldTriangle *tr=sets[k]+i;
        if(tr->source_id!=source_id)continue;
        for(unsigned v=0;v<3;++v){float dx=tr->vertices[v*3]-s->hero->position[0],dz=tr->vertices[v*3+2]-s->hero->position[2];
            if(dx*dx+dz*dz<=reach*reach)return 1;}
        float cx=(tr->vertices[0]+tr->vertices[3]+tr->vertices[6])/3,cz=(tr->vertices[2]+tr->vertices[5]+tr->vertices[8])/3;
        float dx=cx-s->hero->position[0],dz=cz-s->hero->position[2];if(dx*dx+dz*dz<=reach*reach)return 1;
    }
    return 0;
}
static int program_step(Impl *s,RunTask *t,uint32_t answer,char *error)
{
    if(t->prog_asking==1){ /* message shown: open the yes/no menu */
        t->prog_asking=2;s->dialog_owner=t->key;s->view.dialogue=1;s->view.choice=1;s->view.choice_no=0;
        t->wait=W_CHOICE;t->remaining=.12f;return 1;
    }
    if(t->prog_asking==2){t->prog_state=answer==0?t->prog_yes:t->prog_no;t->prog_asking=0;}
    for(unsigned guard=0;guard<32;++guard){
        int st=t->prog_state;
        if(st==END&&t==&s->treasure_task){
            t->program=0;s->view.dialogue=0;s->dialog_owner=0;t->wait=W_NONE;s->view.message_id=0;s->view.message[0]=0;
            s->treasure_active=0;t->key=0;log_line(s,"story_treasure_end");return 1;
        }
        if(st==END){
            /* fn_801C9F5C ends with heroMoveTermEvent and returns 1. */
            uint32_t result=t->program==0xb6;
            if(result){close_message(s);if(s->event_depth)--s->event_depth;}
            t->program=0;s->view.dialogue=0;s->dialog_owner=0;t->wait=W_NONE;
            return c3_story_resume(&s->vm,t->key,result)||fail(error,"Cannot resume original story task %u",t->key);
        }
        switch(t->program){
        /* fn_801C9F5C (gs_range_801C766C.c), entered with heroMoveInitEvent.
         * t->stage: the special report (0 ADA, 1 ADB) or the closing message
         * mode (0x426E + mode). Each wait is 3 seconds (lbl_8047E11C); each
         * message is winMsgOpenFieldWithSE(id, 1, 0, 5) then
         * winMsgCloseField(1). */
        case 0xb6:
            if(st==2||st==3)close_message(s); /* after 439F/439D and 43A0/439E */
            switch(st){
            case 0:{
                unsigned i;uint32_t value;
                note_presentation(s,0xb6,"message_sound_effect_silent");
                /* fn_801EEAD0(0x43): Shadow data row 0x43's flag +0x12
                 * (common REL125 data+0x1439D8, 0x0890 = 0x084E + 0x43 - 1;
                 * tools/prepare_game_state.py checks it) gives mode 4 and
                 * sets no watched flag. */
                if(!flag(s,0x0890,&value,error))return 0;
                if(value)t->stage=4;
                else{
                    if(!tv_current(s,&i,error))return 0;
                    if(!set_flag(s,i<TV_REPORTS?tv_reports[i].watched:0x0A05,1,error))return 0;
                    if(i<2){ /* the special report: entry 12, then 15 (ADA) or 14 (ADB) */
                        t->stage=(int)i;tv_select(s,12);
                        t->prog_state=1;t->wait=W_SECONDS;t->remaining=3.0f;return 1;
                    }
                    t->stage=(int)(TV_REPORTS-i); /* 42C 4, AD3 3, AD0 2, ACF 1, none 0 */
                }
                if(!tv_report(s,error))return 0;
                t->prog_state=4;t->wait=W_SECONDS;t->remaining=3.0f;return 1;}
            case 1:t->prog_state=2;return message_when_free(s,t,t->stage?0x439D:0x439F,"",error);
            case 2:tv_select(s,t->stage?14:15);
                t->prog_state=3;return message_when_free(s,t,t->stage?0x439E:0x43A0,"",error);
            case 3:if(!tv_report(s,error))return 0;
                t->prog_state=END;continue;
            default:t->prog_state=END;return message_when_free(s,t,0x426E + (uint32_t)t->stage,"",error);
            }
        case 0xb2:{
            Person *fall=person(s,t->call.args[0],t->call.args[1]);
            if(st==0){t->prog_state=1;
                if(fall){if(!set_motion(s,fall,8,0,0,error))return 0;
                    if(fall->public.playing&&!fall->public.loop){t->wait=W_MOTION;return 1;}}
                continue;}
            if(fall&&!set_motion(s,fall,1,0,1,error))return 0;
            t->prog_state=END;continue;}
        case 0x7fff:{ /* floorEventCtrlTresure(mode 2) -> floorEventGetTresure */
            C3StoryTreasure *r=s->treasure+s->treasure_row;
            switch(st){
            case 0:
                if(r->taken_flag&&!set_flag(s,r->taken_flag,1,error))return 0;
                r->taken=1;
                if(r->kind==1)log_line(s,"story_presentation native=treasure status=unrendered reason=chest_open_motion");
                log_line(s,"story_treasure_take index=%lu type=%lu item=%lu count=%lu",(unsigned long)r->index,
                         (unsigned long)r->type,(unsigned long)r->item,(unsigned long)r->count);
                log_line(s,"story_presentation native=treasure status=unrendered reason=jingle_3ca");
                if(r->type==2){ /* money */
                    s->message_values[0x4b]=r->item;return say(s,t,0x3cb5,10,error);
                }
                if(r->type==1||r->type==4||r->type==3){
                    uint32_t count=r->type==3?1:r->count;
                    if(!count){t->prog_state=END;continue;}
                    if(!s->receive)return fail(error,"Treasure pickup needs the item service, which this scene did not bind");
                    s->message_values[0x2d]=r->item;s->message_values[0x2f]=count;s->item_name[0]=0;
                    s->treasure_result=s->receive(s->receive_context,r->item,count,s->item_name,sizeof s->item_name);
                    log_line(s,"story_item item=%lu count=%lu result=%d",(unsigned long)r->item,(unsigned long)count,s->treasure_result);
                    if(s->treasure_result<0){t->prog_state=END;continue;}
                    if(r->type==3)return say(s,t,0x3cbc,20,error);
                    if(r->type==1)return say(s,t,count==1?0x3cb4:0x3cb9,1,error);
                    t->prog_state=1;continue;
                }
                t->prog_state=END;continue;
            case 1: /* type 1/4: into the bag, or the PC when it is full */
                if(s->treasure_result>0)return say(s,t,r->count==1?0x3cba:0x3cbb,END,error);
                return say(s,t,r->count==1?0x3cb8:0x3cbd,END,error);
            case 10:{
                C3GameSummary hero;
                if(!c3_game_state_summary(s->game,&hero))return fail(error,"Game state is unavailable");
                int64_t money=(int64_t)hero.money+r->item;if(money>9999999)money=9999999;
                if(!c3_game_state_add_money(s->game,money-(int64_t)hero.money))return fail(error,"Money cannot be changed");
                return say(s,t,0x3cb7,END,error);}
            case 20: /* type 3: the e-Reader card follow-ups (fn_8001E184, GBA) are not shown */
                log_line(s,"story_presentation native=treasure status=unported reason=type3_gba_followup");
                return say(s,t,0x3cb6,END,error);
            }
            t->prog_state=END;continue;}
        case 0xd0: /* fn_8000D710(1): the pause menu */
            if(st==0){
                /* Opening the menu dispatches kind 4 for the event surfaces
                 * the hero is on (heroMoveCheckEvent, fn_80116D30(4, id)):
                 * the key slot's registers its key with native 11. Here a
                 * surface counts when one of its triangles is within 1.5
                 * units of the hero. prog_yes is the next link to try. */
                while((uint32_t)t->prog_yes<s->area->link_count){
                    const C3AreaLink *l=s->area->links+t->prog_yes++;
                    if(l->kind!=4||!near_surface(s,l->source_id,1.5f))continue;
                    log_line(s,"story_pause_menu_event source=%lu script=%08lx",(unsigned long)l->source_id,(unsigned long)l->script);
                    if(!start(s,l->script,l->args,4,&t->child,error))return 0;
                    t->child_finished=0;t->wait=W_TASK;return 1;
                }
                t->prog_state=1;
                if(s->item_menu_enabled){
                    s->view.item_menu=1;
                    log_line(s,"story_pause_menu row=items status=waiting_scene registered=%u",s->item_event_count);
                    t->wait=W_ITEM_MENU;return 1;
                }
                s->view.unported_menu=0xd0;s->item_event_count=0;
                log_line(s,"story_pause_menu row=items status=closed menu_ported=0");
            }
            t->prog_state=END;continue;
        case 0x54: /* fn_801E03D4: rename */
            switch(st){
            case 0:return ask(s,t,0x3b21,2,1,error);
            case 1:return say(s,t,0x3b22,END,error);
            case 2:unported_menu(s,t,"party_select");return say(s,t,0x3b23,1,error);
            default:break;
            }break;
        case 0x55:{ /* fn_801DFC30: Day Care */
            C3GameSummary party;
            switch(st){
            case 0:t->prog_state=c3_game_state_daycare_occupied(s->game)?9:1;continue;
            case 1:return ask(s,t,0x3b11,2,8,error);
            case 2:if(!c3_game_state_summary(s->game,&party))return fail(error,"Day Care needs the party");
                t->prog_state=party.party_count==1?3:5;continue;
            case 3:return say(s,t,0x3b13,END,error);
            case 4:return say(s,t,0x3b15,END,error);
            case 5:unported_menu(s,t,"party_select");return say(s,t,0x3b17,8,error);
            case 8:return say(s,t,0x3b1b,END,error);
            /* The level-up report 3B1D needs the Day Care Pokemon's record. */
            case 9:unported_menu(s,t,"daycare_levels");return say(s,t,0x3b1c,12,error);
            case 12:return ask(s,t,0x3b1e,13,4,error);
            case 13:unported_menu(s,t,"daycare_withdraw");t->prog_state=4;continue;
            default:break;
            }break;}
        case 0x56: /* fn_801DF790: Move Deleter */
            switch(st){
            case 0:return ask(s,t,0x3b28,2,12,error);
            case 2:unported_menu(s,t,"party_select");return say(s,t,0x3b29,12,error);
            case 12:return say(s,t,0x3b2d,END,error);
            default:break;
            }break;
        case 0x57:{ /* fn_801DF474: the item gift */
            uint32_t v=0;
            switch(st){
            case 0:
                if(!flag(s,0xd0,&v,error))return 0;
                if(!v){if(!set_flag(s,0xd0,1,error))return 0;t->prog_state=1;continue;}
                if(!flag(s,0xd1,&v,error))return 0;
                if(v>=500){if(!set_flag(s,0xd1,0,error))return 0;t->prog_state=1;continue;}
                if(!flag(s,0xafe,&v,error))return 0;
                if(v){if(!set_flag(s,0xafe,0,error))return 0;t->prog_state=3;continue;}
                t->prog_state=4;continue;
            case 1:return say(s,t,0x5571,2,error);
            case 2:unported_menu(s,t,"gift_table");t->prog_state=END;continue;
            case 3:return say(s,t,0x5573,END,error);
            case 4:return say(s,t,0x5574,END,error);
            default:break;
            }break;}
        case 0xc2:{ /* preReliveMain + reliveMain + reliveCeremonyAll */
            C3ReliveResult *r=&s->purified.result;
            switch(st){
            case 0:{C3ReliveCounts counts;
                if(!c3_relive_counts(s->game,&counts))return fail(error,"Purification needs the party");
                int action=c3_relive_action((int)t->relive_mode,&counts);
                log_line(s,"story_relive mode=%lu action=%d remaining=%u ready=%u item=%d",(unsigned long)t->relive_mode,
                         action,counts.remaining,counts.ready,counts.has_item);
                if(action==C3_RELIVE_PICK){t->prog_state=1;continue;}
                if(action==C3_RELIVE_PURIFY){t->prog_state=4;continue;}
                t->prog_state=END;continue;}
            case 1:return say(s,t,0x3b0f,2,error);
            case 2: /* winMsgCloseField, then menuPokemonOpen(7) in the scene */
                close_message(s);s->view.relive_pick=1;t->prog_state=3;t->wait=W_RELIVE;return 1;
            case 3:
                s->view.relive_pick=0;
                if(answer>=6){log_line(s,"story_relive pick=cancel");t->prog_state=END;continue;}
                t->relive_slot=answer;t->relive_mode=0;t->prog_state=4;continue;
            case 4:{ /* reliveMain on floor 0x385: the ceremony, then the purification */
                if(!s->purify)return fail(error,"Native C2 needs the purification service");
                memset(&s->purified,0,sizeof s->purified);
                int done=s->purify(s->purify_context,t->relive_slot,t->relive_mode==1,&s->purified);
                log_line(s,"story_relive slot=%lu forced=%lu purified=%d species=%u shadow=%u pool_exp=%lu levels=%u learned=%u skipped=%u"
                         " ceremony_scene=unported",(unsigned long)t->relive_slot,(unsigned long)t->relive_mode,done,r->species,r->shadow_id,
                         (unsigned long)r->pool_exp,r->exp.level_count,r->exp.learned_count,r->exp.skipped_count);
                if(done<0)return fail(error,"Purification of party slot %lu failed",(unsigned long)t->relive_slot);
                t->stage=0;t->prog_state=done?5:END;continue;}
            case 5:
                if(!r->move0){t->prog_state=6;continue;}
                return say(s,t,0x3b10,6,error);
            case 6:s->message_values[0x2f]=r->pool_exp;return say(s,t,0x3b0b,7,error);
            case 7: /* each level reached: 44CE with the level in 2F */
                if(t->stage>=r->exp.level_count){t->prog_state=8;continue;}
                s->message_values[0x2f]=r->exp.levels[t->stage++];return say(s,t,0x44ce,7,error);
            case 8:return say(s,t,0x3b0c,9,error);
            case 9:return ask(s,t,0x3b0d,10,END,error);
            case 10:unported_menu(s,t,"nickname_entry");t->prog_state=END;continue;
            default:break;
            }break;}
        default:break;
        }
        return fail(error,"Menu program %02lx reached unported state %d",(unsigned long)t->program,st);
    }
    return fail(error,"Menu program %02lx did not settle",(unsigned long)t->program);
}

static int wait_task(Impl *s,RunTask *t,float seconds,uint16_t *pressed,char *error)
{
    Person *p;
    switch(t->wait){
    case W_NONE:return 1;
    case W_SECONDS:
        t->remaining-=seconds;if(t->remaining>0)return 1;
        return resume(s,t,0,error);
    case W_JINGLE:
        if(jingle_playing(s,t,seconds))return 1;
        return resume(s,t,0,error);
    case W_TASK:
        if(t->child_finished)return resume(s,t,t->value,error);
        if(c3_story_active(&s->vm,t->child))return 1;
        return resume(s,t,c3_story_result(&s->vm,t->child),error);
    case W_FALL:
        p=person(s,t->call.args[0],t->call.args[1]);
        if(!p)return resume(s,t,0,error);
        if(!t->stage){
            t->remaining-=1.3f*60.0f*seconds;if(t->remaining<0)t->remaining=0;
            pose(s,p)->position[1]=t->fall_ground+t->remaining*s->map->scale;
            if(t->remaining>0)return 1;
            if(!set_motion(s,p,8,0,0,error))return 0;
            t->stage=1;return 1;
        }
        if(p->public.playing&&!p->public.loop&&pose(s,p)->animation_seconds<clip_seconds(&p->public))return 1;
        if(!set_motion(s,p,1,0,1,error))return 0;
        return resume(s,t,0,error);
    case W_MOTION:
        p=person(s,t->call.args[0],t->call.args[1]);
        if(p&&p->public.playing&&!p->public.loop&&pose(s,p)->animation_seconds<clip_seconds(&p->public))return 1;
        return resume(s,t,0,error);
    case W_MOVE:
        p=person(s,t->call.args[0],t->call.args[1]);if(p&&(p->moving||p->turning))return 1;
        return resume(s,t,0,error);
    case W_ASIDE:return aside_wait(s,t,seconds,error);
    case W_CAMERA:
        if(s->view.camera_active&&s->camera_track&&s->camera_track->frames&&
           s->view.camera_seconds<(float)(s->camera_track->frame_count-1u)/s->camera_track->fps)return 1;
        return resume(s,t,0,error);
    case W_CAMERA_MOVE:
        if(s->camera_returning||s->target_moving||s->camera_move_time<s->camera_move_duration||
           s->rotation_time<s->rotation_duration)return 1;
        return resume(s,t,0,error);
    case W_FADE:
        if(s->fade_elapsed<s->fade_duration)return 1;
        return resume(s,t,0,error);
    case W_DIALOG:
        if(s->dialog_owner&&s->dialog_owner!=t->key)return 1;
        return message(s,t,t->pending_message,t->pending_speaker,error);
    case W_MESSAGE:
        t->remaining-=seconds;if(t->remaining>0||!(*pressed&(PAD_A|PAD_B)))return 1;
        *pressed&=~(PAD_A|PAD_B);
        if(s->full_message[s->page_end]=='\f'){
            s->page_start=s->page_end+1;page(s);t->remaining=.12f;return 1;
        }
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
    case W_BATTLE:case W_NAME:case W_SHOP:case W_MENU_FLOOR:case W_RELIVE:case W_ITEM_MENU:return 1;
    case W_DOOR:
        if(t->stage<s->door_count&&s->doors[t->stage].playing)return 1;
        if(s->event_depth)--s->event_depth;
        return resume(s,t,0,error);
    case W_OBJECT:
        if(t->stage<s->door_count&&s->doors[t->stage].playing)return 1;
        return resume(s,t,0,error);
    case W_LIFT:return lift_step(s,t,seconds,error);
    case W_HEAL:{
        /* fn_801D0DB0: turn to the machine, one ball every half second into
         * parts 9.., clip <balls> at rate 1.0 with jingle 0x19 played to its
         * end (fn_801653CC), the clip's end, 0.7 s, the balls removed, 0.7 s,
         * then turn back. The machine keeps the clip's last frame. */
        C3StoryDoor *machine=s->doors+s->heal.machine;
        Person *nurse=s->heal.person?person(s,s->heal.group,s->heal.id):NULL;
        switch(s->heal.stage){
        case 0:if(nurse&&nurse->turning)return 1;s->heal.stage=1;/* fall through */
        case 1:{uint32_t i=s->heal.ball;
            heal_part(machine->model,9u+i,s->heal.at[i]);s->heal.shown[i]=1;s->heal.timer=0;s->heal.stage=2;
            log_line(s,"story_heal_ball slot=%lu ball=%lu",(unsigned long)i,(unsigned long)party_ball(s,i));
            return 1;}
        case 2:
            s->heal.timer+=seconds;if(s->heal.timer<.5f)return 1;
            if(++s->heal.ball<s->heal.count){s->heal.stage=1;return 1;}
            machine->clip=s->heal.count;machine->seconds=0;machine->loop=0;machine->playing=1;machine->rate=2;
            log_line(s,"story_heal_machine clip=%lu",(unsigned long)s->heal.count);
            if(s->audio&&s->audio(s->audio_context,C3_STORY_AUDIO_JINGLE,0x19,0,0xff)>0){
                t->jingle=0x19;t->jingle_seconds=0;log_line(s,"story_jingle sound=019 status=playing");s->heal.stage=3;return 1;}
            note_presentation(s,0x53,"heal_jingle_stream_missing");s->heal.stage=4;return 1;
        case 3:if(jingle_playing(s,t,seconds))return 1;s->heal.stage=4;/* fall through */
        case 4:if(machine->playing)return 1;s->heal.timer=0;s->heal.stage=5;return 1;
        case 5:
            s->heal.timer+=seconds;if(s->heal.timer<.7f)return 1;
            memset(s->heal.shown,0,sizeof s->heal.shown);s->heal.timer=0;s->heal.stage=6;
            log_line(s,"story_heal_balls_removed");return 1;
        case 6:
            s->heal.timer+=seconds;if(s->heal.timer<.7f)return 1;
            s->heal.stage=7;if(nurse)start_turn(s,nurse,s->heal.yaw,2.0f);return 1;
        default:
            if(nurse&&nurse->turning)return 1;
            log_line(s,"story_heal_done balls=%lu",(unsigned long)s->heal.count);
            return resume(s,t,0,error);
        }}
    case W_ITEM:{
        /* Wait for the shared window before adding the item, so a retry
         * never adds it twice. */
        if(t->jingle&&jingle_playing(s,t,seconds))return 1;
        if(s->dialog_owner&&s->dialog_owner!=t->key)return 1;
        t->remaining-=seconds;if(t->remaining>0)return 1;
        uint32_t item=t->call.args[0];int32_t count=(int32_t)t->call.args[1];
        if(count<=0)return resume(s,t,0,error); /* the original returns without a message */
        s->message_values[0x2d]=item;s->message_values[0x2f]=(uint32_t)count;s->item_name[0]=0;
        int result=s->receive(s->receive_context,item,(uint32_t)count,s->item_name,sizeof s->item_name);
        log_line(s,"story_item item=%lu count=%ld result=%d",(unsigned long)item,(long)count,result);
        if(result<0)return resume(s,t,(uint32_t)result,error);
        /* A positive result is the amount that did not fit the bag; the
         * original then stores it in the PC and shows 3CBA/3CBB. */
        if(result>0)return fail(error,"Item %lu did not fit the bag; the PC storage path is not ported",(unsigned long)item);
        if(!message(s,t,count==1?0x3cb8u:0x3cbdu,"",error))return 0;
        return 1;}
    }
    return fail(error,"Invalid native story wait state");
}

void c3_field_story_close(C3FieldStory *story)
{
    if(!story||!story->impl)return;
    Impl *s=story->impl;c3_story_free(&s->vm);free(s);story->impl=NULL;
}
void c3_field_story_describe_waits(const C3FieldStory *story,char *out,size_t capacity)
{
    static const char *const names[]={"none","seconds","task","motion","move","camera","aside","camera_move",
        "fade","message","choice","battle","name","item","door","shop","dialog","relive"};
    const Impl *s=story?story->impl:NULL;size_t used=0;
    if(!out||!capacity)return;
    out[0]=0;if(!s)return;
    {int n=snprintf(out,capacity,"depth=%d ready=%d event=%u/%d talk=%u ",s->event_depth+s->hero_event,s->hero_ready,
        (unsigned)s->event_key,s->event_key?c3_story_active(&((Impl*)s)->vm,s->event_key):0,(unsigned)s->talk_key);
     if(n>0)used=(size_t)n<capacity?(size_t)n:capacity-1;}
    for(unsigned i=0;i<C3_STORY_TASKS&&used+1<capacity;++i){
        const RunTask *t=s->tasks+i;
        if(!t->key||t->wait==W_NONE)continue;
        int n=snprintf(out+used,capacity-used,"%s%u:%s:%08lx@%lx:%02lx(%lx,%lx)",used?" ":"",(unsigned)t->key,
            (unsigned)t->wait<sizeof names/sizeof names[0]?names[t->wait]:"?",(unsigned long)t->call.script_id,
            (unsigned long)t->call.pc,(unsigned long)t->call.native,(unsigned long)t->call.args[0],(unsigned long)t->call.args[1]);
        if(n<0)break;
        used+=(size_t)n<capacity-used?(size_t)n:capacity-used-1;
    }
}
int c3_field_story_attach_actor(C3FieldStory *story,uint32_t area_index,C3Title *model,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||area_index>=s->area->actor_count)return fail(error,"Invalid streamed actor");
    Person *p=s->actors+area_index;
    if(p->public.model||!model)return 1;
    p->public.model=model;
    if(p->public.clip>=model->clip_count)p->public.clip=0;
    p->public.visible=p->want_visible;
    if(p->pending_motion){
        p->pending_motion=0;
        if(!set_motion(s,p,p->pending_clip,p->pending_frame,p->pending_loop,error))return 0;
    }
    return 1;
}
unsigned c3_field_story_model_species(uint32_t resource)
{
    /* Field Pokemon models (global FSYS IDs, named after their species in
     * the archives' file tables) -> internal species index. Every index was
     * checked against the original species names in pda.c3pd. */
    static const struct {uint32_t resource;uint16_t species;} models[]={
        {0x010e0400u,280},{0x0c530400u,382},{0x0ff80400u,316},{0x011b0400u,287},
        {0x10ba0400u,295},{0x10d20400u,250},{0x01240400u,277},{0x11060400u,306},
        {0x0c460400u,129},{0x10870400u,335},{0x0f890400u,183},{0x0c4c0400u,43},
        {0x012b0400u,25},{0x01030400u,385},{0x06f90400u,353},{0x0c520400u,39},
        {0x012e0400u,297},{0x0ffa0400u,202},{0x01310400u,304},{0x0c510400u,288},
    };
    for(unsigned i=0;i<sizeof models/sizeof models[0];++i)if(models[i].resource==resource)return models[i].species;
    return 0;
}
void c3_field_story_set_species_namer(C3FieldStory *story,
    int (*name)(void *context,unsigned species,char *out,size_t capacity),void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->species_name=name;s->species_context=context;}
}
void c3_field_story_set_random(C3FieldStory *story,uint16_t (*next16)(void *context),void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->random16=next16;s->random_context=context;}
}
void c3_field_story_enable_name_entry(C3FieldStory *story,int enabled)
{
    Impl *s=story?story->impl:NULL;if(s)s->name_entry_enabled=enabled!=0;
}
int c3_field_story_name_result(C3FieldStory *story,const char *name,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid name-entry result");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_NAME)continue;
        if(!c3_game_state_set_partner_name(s->game,name))return fail(error,"The partner name is not valid");
        s->view.name_entry=0;
        log_line(s,"story_name_entry status=named length=%lu",(unsigned long)strlen(name));
        return resume(s,t,1,error);
    }
    return fail(error,"No name entry is waiting");
}
void c3_field_story_enable_shop(C3FieldStory *story,int enabled)
{
    Impl *s=story?story->impl:NULL;if(s)s->shop_enabled=enabled!=0;
}
void c3_field_story_enable_menu_floor(C3FieldStory *story,uint32_t floor,int enabled)
{
    Impl *s=story?story->impl:NULL;unsigned i;if(!s)return;
    for(i=0;i<s->menu_floor_count&&s->menu_floors[i]!=floor;++i){}
    if(enabled&&i==s->menu_floor_count&&i<8)s->menu_floors[s->menu_floor_count++]=floor;
    if(!enabled&&i<s->menu_floor_count)s->menu_floors[i]=s->menu_floors[--s->menu_floor_count];
}
int c3_field_story_dna_result(C3FieldStory *story,int result,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||result<0||result>2)return fail(error,"Invalid DNA puzzle result");
    if(s->view.menu_floor!=0x387u)return fail(error,"No DNA puzzle is waiting");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_MENU_FLOOR)continue;
        log_line(s,"story_menu_floor floor=387 status=returned result=%d native_cc=%d",result,result==0);
        s->view.menu_floor=0;memset(s->view.dna_species,0,sizeof s->view.dna_species);
        return resume(s,t,result==0,error);
    }
    return fail(error,"No DNA puzzle is waiting");
}
int c3_field_story_menu_floor_done(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid menu floor result");
    if(s->view.menu_floor==0x387u)return fail(error,"The DNA puzzle returns through c3_field_story_dna_result");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_MENU_FLOOR)continue;
        log_line(s,"story_menu_floor floor=%03lx status=returned",(unsigned long)s->view.menu_floor);
        s->view.menu_floor=0;
        return resume(s,t,0,error);
    }
    return fail(error,"No menu floor is waiting");
}
/* A common/floor message as plain text, with value control 32/36 (the
 * Pokemon a menu names) given and the rest from the field source. */
typedef struct { Impl *s; const char *name; } MenuText;
static int menu_text_source(void *context,uint8_t control,C3StoryValue *v)
{
    MenuText *m=context;
    if((control==0x32||control==0x36)&&m->name){v->text=m->name;return 1;}
    return message_source(m->s,control,v);
}
int c3_field_story_text(C3FieldStory *story,uint32_t id,const char *name,char *out,size_t capacity)
{
    Impl *s=story?story->impl:NULL;char error[C3_STORY_ERROR];MenuText m={s,name};
    if(!s||!out||!capacity)return 0;
    C3GameSummary names;
    if(c3_game_state_summary(s->game,&names))snprintf(s->partner_name,sizeof s->partner_name,"%s",names.partner);
    if(!c3_story_render(&s->vm,id,menu_text_source,&m,0,out,capacity,error))return 0;
    for(char *c=out;*c;++c)if(*c=='\n'||*c=='\f')*c=' ';
    return 1;
}
static uint32_t le32_at(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float lef32_at(const uint8_t *p){uint32_t v=le32_at(p);float f;memcpy(&f,&v,4);return f;}
int c3_field_story_load_treasure(C3FieldStory *story,const char *path,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!path)return fail(error,"Invalid treasure pack request");
    FILE *f=fopen(path,"rb");
    if(!f)return 1; /* a floor without treasure has no pack */
    uint8_t header[16],row[32];
    if(fread(header,1,16,f)!=16||memcmp(header,"C3R1",4)||le32_at(header+4)!=1||le32_at(header+8)!=s->area->floor||le32_at(header+12)>32){
        fclose(f);return fail(error,"Invalid treasure pack %s",path);
    }
    unsigned n=le32_at(header+12);s->treasure_count=0;
    for(unsigned i=0;i<n;++i){
        if(fread(row,1,32,f)!=32){fclose(f);return fail(error,"Truncated treasure pack %s",path);}
        C3StoryTreasure *r=s->treasure+i;memset(r,0,sizeof *r);
        r->index=(uint32_t)(row[0]|row[1]<<8);r->type=(row[2]>>2)&7u;r->kind=(row[2]>>5)&7u;r->count=row[3];
        int16_t rotation=(int16_t)(row[6]|row[7]<<8);
        r->taken_flag=(uint32_t)(row[8]|row[9]<<8);r->hide_flag=(uint32_t)(row[10]|row[11]<<8);r->item=le32_at(row+12);
        /* Rows hold source units; the hero and map are in field units. */
        for(unsigned k=0;k<3;++k)r->position[k]=lef32_at(row+16+4*k)*s->map->scale;
        /* A treasure is a person (floorEventInitTresure -> peopleOpen), so it
         * stands on the ground found from its row position (fn_8018E9B4). */
        {float ground;if(c3_field_ground(s->map,r->position,&ground))r->position[1]=ground;}
        r->yaw=(float)rotation*PI/180.0f;
        if(r->index<512&&treasure_session[r->index].changed){r->item=treasure_session[r->index].item;r->count=treasure_session[r->index].count;}
        s->treasure_display[i]=-1;
    }
    fclose(f);s->treasure_count=n;
    log_line(s,"story_treasure_rows floor=%lu count=%u",(unsigned long)s->area->floor,n);
    return 1;
}
unsigned c3_field_story_hero_regions(const C3FieldStory *story,float out[][4],unsigned max)
{
    const Impl *s=story?story->impl:NULL;unsigned n=0;
    if(!s)return 0;
    for(;n<s->hero_region_count&&n<max;++n)for(unsigned k=0;k<4;++k)out[n][k]=s->hero_regions[n][k]*s->map->scale;
    return n;
}
unsigned c3_field_story_treasure_count(const C3FieldStory *story)
{const Impl *s=story?story->impl:NULL;return s?s->treasure_count:0;}
const C3StoryTreasure *c3_field_story_treasure(const C3FieldStory *story,unsigned index)
{const Impl *s=story?story->impl:NULL;return s&&index<s->treasure_count?s->treasure+index:NULL;}
void c3_field_story_set_item_take_service(C3FieldStory *story,C3FieldStoryTake take,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->take=take;s->take_context=context;}
}
void c3_field_story_enable_item_menu(C3FieldStory *story,int enabled)
{
    Impl *s=story?story->impl:NULL;if(s)s->item_menu_enabled=enabled!=0;
}
unsigned c3_field_story_item_menu_items(const C3FieldStory *story,uint32_t items[8])
{
    const Impl *s=story?story->impl:NULL;unsigned n=0;
    if(!s)return 0;
    for(;n<s->item_event_count&&n<8;++n)items[n]=s->item_events[n].item;
    return n;
}
int c3_field_story_item_menu_result(C3FieldStory *story,uint32_t item,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid item menu result");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_ITEM_MENU)continue;
        uint32_t script=0;
        if(item)for(unsigned j=0;j<s->item_event_count;++j)if(s->item_events[j].item==(uint16_t)item){script=s->item_events[j].script;break;}
        s->item_event_count=0;s->view.item_menu=0;
        if(script){s->pending_item_script=script;s->pending_item=item;}
        log_line(s,"story_pause_menu row=items item=%lu script=%08lx status=%s",(unsigned long)item,(unsigned long)script,
                 script?"used":item?"no_event":"cancelled");
        return resume(s,t,0,error);
    }
    return fail(error,"No item menu is waiting");
}
int c3_field_story_pause_menu_open(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid pause menu");
    if(!s->entered)return 1;
    /* fn_8000D710: fn_80116D30(4, id) for each surface heroMoveCheckEvent
     * reports, then the menu. A registering script (a flag test and native
     * 11) runs to its end here; one that waits finishes on later ticks. */
    for(uint32_t i=0;i<s->area->link_count;++i){const C3AreaLink *l=s->area->links+i;uint16_t key;
        if(l->kind!=4||!near_surface(s,l->source_id,1.5f))continue;
        log_line(s,"story_pause_menu_event source=%lu script=%08lx menu=player",(unsigned long)l->source_id,(unsigned long)l->script);
        if(!start(s,l->script,l->args,4,&key,error))return 0;
        RunTask *t=NULL;for(unsigned j=0;j<C3_STORY_TASKS&&!t;++j)if(s->tasks[j].key==key)t=s->tasks+j;
        for(unsigned calls=0;t&&calls<128;++calls){
            C3StoryStatus status=c3_story_step(&s->vm,t->key,1024,&t->call,error);
            if(status==C3_STORY_FAILED)return 0;
            if(status==C3_STORY_FINISHED){log_line(s,"story_end key=%u result=%lu",t->key,(unsigned long)c3_story_result(&s->vm,t->key));break;}
            if(status==C3_STORY_RUNNING)break;
            if(!run_native(s,t,error))return 0;
            if(t->wait!=W_NONE)break;
        }
    }
    log_line(s,"story_pause_menu open registered=%u",s->item_event_count);
    return 1;
}
int c3_field_story_pause_menu_use(C3FieldStory *story,uint32_t item)
{
    Impl *s=story?story->impl:NULL;uint32_t script=0;
    if(!s||!item)return 0;
    /* fn_80021A9C finds the item in the native 11 list; fn_8000D710 then
     * queues its script with heroMoveAddAutoEvent (fn_80019070). */
    for(unsigned j=0;j<s->item_event_count&&!script;++j)if(s->item_events[j].item==(uint16_t)item)script=s->item_events[j].script;
    log_line(s,"story_pause_menu item=%lu script=%08lx status=%s menu=player",(unsigned long)item,(unsigned long)script,script?"used":"not_now");
    if(!script)return 0;
    s->pending_item_script=script;s->pending_item=item;s->item_event_count=0;
    return 1;
}
void c3_field_story_pause_menu_close(C3FieldStory *story)
{
    Impl *s=story?story->impl:NULL;if(s)s->item_event_count=0;
}
int c3_field_story_shop_done(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid shop result");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_SHOP)continue;
        log_line(s,"story_shop shop=%lu status=closed",(unsigned long)(s->view.shop-1u));
        s->view.shop=0;
        return resume(s,t,0,error);
    }
    return fail(error,"No shop is waiting");
}
void c3_field_story_set_audio_service(C3FieldStory *story,C3FieldStoryAudio audio,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->audio=audio;s->audio_context=context;}
}
void c3_field_story_set_purify_service(C3FieldStory *story,C3FieldStoryPurify purify,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->purify=purify;s->purify_context=context;}
}
int c3_field_story_relive_pick(C3FieldStory *story,int slot,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"No field story");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_RELIVE)continue;
        return resume(s,t,slot<0||slot>5?UINT32_MAX:(uint32_t)slot,error);
    }
    return fail(error,"No purification is waiting for a party choice");
}
void c3_field_story_set_item_change_service(C3FieldStory *story,C3FieldStoryItemChange change,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->item_change=change;s->item_change_context=context;}
}
void c3_field_story_set_item_service(C3FieldStory *story,C3FieldStoryReceive receive,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->receive=receive;s->receive_context=context;}
}
void c3_field_story_set_poison_service(C3FieldStory *story,C3FieldStoryRestore clear,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->clear_poison=clear;s->poison_context=context;}
}
void c3_field_story_set_party_service(C3FieldStory *story,C3FieldStoryRestore restore,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->restore=restore;s->restore_context=context;}
}
void c3_field_story_set_gift_service(C3FieldStory *story,C3FieldStoryGift gift,void *context)
{
    Impl *s=story?story->impl:NULL;if(s){s->gift=gift;s->gift_context=context;}
}
int c3_field_story_bind_doors(C3FieldStory *story,const C3AreaDoor *rows,uint32_t count,
                              C3Title *const *models,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||s->entered||count>C3_AREA_MAX_DOORS||(count&&(!rows||!models)))return fail(error,"Invalid door binding");
    memset(s->doors,0,sizeof s->doors);s->door_rows=rows;s->door_count=count;
    for(uint32_t i=0;i<count;++i){
        C3StoryDoor *door=s->doors+i;uint32_t open=0;door->model=models[i];door->rate=1;
        /* A door whose open flag is already set stands open, with or without
         * a model: its collision is off; a model shows the end of its open
         * animation, otherwise its first frame. */
        if(rows[i].open_flag&&!flag(s,rows[i].open_flag,&open,error))return 0;
        if(rows[i].collision_id>=0)c3_field_set_object_enabled(s->map,(uint32_t)rows[i].collision_id,!open);
        if(!door->model)continue;
        if(door->model->fps<=0||!door->model->clip_count)
            return fail(error,"Door %lu model has no rest frame",(unsigned long)rows[i].index);
        /* Elevator rows may have travel animations only, or no animations.
         * floorEvent elevator control starts openAnim only when >=0. */
        const int8_t animations[]={rows[i].open_anim,rows[i].close_anim,rows[i].open_end_anim,
            rows[i].close_end_anim,rows[i].alternate_a,rows[i].alternate_b};
        for(unsigned j=0;j<sizeof animations/sizeof animations[0];++j)
            if(animations[j]>=0&&(uint32_t)animations[j]>=door->model->clip_count)
                return fail(error,"Door %lu model lacks animation %d",(unsigned long)rows[i].index,animations[j]);
        door->clip=rows[i].open_anim>=0?(uint32_t)rows[i].open_anim:0;
        door->seconds=open&&rows[i].open_anim>=0?
            (float)(door->model->clips[door->clip].frame_count-1u)/door->model->fps:0;
        /* floorOpenObject starts every map model on clip 0, looping. */
        if(rows[i].index&C3_AREA_MAP_OBJECT_ROW)door->playing=door->loop=1;

    }
    return 1;
}
int c3_field_story_bind_heal_balls(C3FieldStory *story,C3Title *const *models,const uint8_t *balls,
                                   uint32_t count,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||count>C3_HEAL_BALL_SLOTS||(count&&(!models||!balls)))return fail(error,"Invalid heal ball binding");
    for(uint32_t i=0;i<count;++i)if(!models[i]||balls[i]>=13)return fail(error,"Invalid heal ball binding");
    memset(s->heal_models,0,sizeof s->heal_models);
    for(uint32_t i=0;i<count;++i){s->heal_models[i]=models[i];s->heal_bound[i]=balls[i];}
    s->heal_model_count=count;return 1;
}
const C3StoryDoor *c3_field_story_door(const C3FieldStory *story,uint32_t position)
{
    const Impl *s=story?story->impl:NULL;
    return s&&position<s->door_count?s->doors+position:NULL;
}
void c3_field_story_bind_cameras(C3FieldStory *story,const C3AreaCamera *cameras,uint32_t count)
{Impl *s=story?story->impl:NULL;if(s){s->cameras=cameras;s->camera_count=cameras?count:0;}}
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
        s->fight_result=result;s->view.blocked[0]=0;s->view.battle_id=0;s->view.battle_refused=0;
        s->fade_from=0;s->fade_to=1;s->fade_elapsed=0;s->fade_duration=.5f;s->view.fade=0;
        log_line(s,"story_battle_result encounter=%lu result=%lu native98_resumed=1",
                 (unsigned long)t->call.args[0],(unsigned long)result);
        return resume(s,t,result,error);
    }
    return fail(error,"No original battle request is waiting for a result");
}
int c3_field_story_battle_refuse(C3FieldStory *story,const char *reason,char *error)
{
    Impl *s=story?story->impl:NULL;uint32_t encounter;
    if(!s||!c3_field_story_battle_pending(story,&encounter))return fail(error,"No original battle request is waiting");
    s->view.battle_refused=encounter;
    snprintf(s->view.blocked,sizeof s->view.blocked,"Battle %lu cannot be played in this build (%.80s). START: quit.",
             (unsigned long)encounter,reason&&reason[0]?reason:"no reason given");
    log_line(s,"story_battle_refused encounter=%lu reason=\"%s\" result_written=0 native98_waiting=1",
             (unsigned long)encounter,reason?reason:"");
    return 1;
}
/* A floor link (native 5F/7D) to a floor this build cannot load waits with
 * view.blocked set. Refusing it resumes the script as if the link had been
 * registered and stays on this floor: the common door script (05960004 is
 * native 7D, then return) ends and the hero gets control back. */
int c3_field_story_floor_refused(const C3FieldStory *story,uint32_t *floor)
{
    const Impl *s=story?story->impl:NULL;
    if(!s)return 0;
    for(unsigned i=0;i<C3_STORY_TASKS;++i){const RunTask *t=s->tasks+i;
        if(t->key&&t->wait==W_BATTLE&&(t->call.native==0x5f||t->call.native==0x7d)){if(floor)*floor=t->call.args[0];return 1;}
    }
    return 0;
}
int c3_field_story_stay_on_floor(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s)return fail(error,"Invalid field story");
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_BATTLE||(t->call.native!=0x5f&&t->call.native!=0x7d))continue;
        s->view.blocked[0]=0;
        log_line(s,"story_floor_refused from=%lu to=%lu stayed=1",(unsigned long)s->area->floor,(unsigned long)t->call.args[0]);
        return resume(s,t,0,error);
    }
    return fail(error,"No floor link is waiting");
}
/* A lost battle whose encounter has the zenmetu flag: fn_8020DAD0 halves the
 * money (the scene does that), sets flag 0E05, restores the party through the
 * bound party service and sends the hero to the temporary whiteout point
 * (fn_80113FE8, set by native A1) or else to the last save's floor, instead
 * of resuming the script. The floor is left through the normal departure.
 * With neither point known the script resumes with result 3. */
int c3_field_story_battle_whiteout(C3FieldStory *story,char *error)
{
    Impl *s=story?story->impl:NULL;
    uint32_t floor=0,previous=0;uint8_t spawn=0;const char *source="temporary";
    if(!s||!c3_field_story_battle_pending(story,NULL))return fail(error,"No original battle request is waiting for a result");
    /* fn_8020DAD0 after the money: flag 0E05, fn_801D0AFC(1) (every valid
     * member pokemonAllKaihuku), fn_80113FE8, fade script 05960008. */
    if(!set_flag(s,0x0e05,1,error))return 0;
    if(s->restore){
        int restored=s->restore(s->restore_context);
        if(restored<0)return fail(error,"The party could not be restored after the loss");
        log_line(s,"story_party_restore mode=whiteout members=%d",restored);
    }
    /* fn_80113FE8: the point native A1 left, else the last card save's
     * floor/previous floor/entrance (gamedatasaveGetStatus 5/7/8). */
    if(s->whiteout_valid){floor=s->whiteout_floor;previous=s->whiteout_previous;spawn=(uint8_t)s->whiteout_spawn;}
    else if(c3_game_state_save_point(s->game,&floor,&previous,&spawn))source="save";
    s->whiteout_valid=0;
    if(!floor){
        log_line(s,"story_battle_whiteout return_point=missing resumed_with=3");
        return c3_field_story_battle_result(story,3,error);
    }
    s->fight_result=3;s->view.blocked[0]=0;s->view.battle_id=0;s->view.battle_refused=0;
    /* The waiting request ends here: the floor is left, not resumed. */
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||t->wait!=W_BATTLE||(t->call.native!=0x98&&t->call.native!=0x66))continue;
        c3_story_stop(&s->vm,t->key);t->key=0;t->wait=W_NONE;}
    s->pending_floor=floor;s->pending_spawn=spawn;
    log_line(s,"story_battle_whiteout floor=%lu previous=%lu spawn=%lu source=%s flag=0e05 fade_script=05960008",
             (unsigned long)floor,(unsigned long)previous,(unsigned long)spawn,source);
    return 1;
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
    s->camera_track=camera;s->trace=trace;s->previous_floor=summary.previous_floor;s->arrival_spawn=summary.spawn;
    s->view.fade=1;s->view.camera=map->camera;s->camera_rate=1;
    s->view.tv_entry=0; /* fn_80117E58 selects entry 0 when the floor starts */
    snprintf(s->hero_name,sizeof s->hero_name,"%s",summary.name);
    s->hero_actor.public.model=hero_model;s->hero_actor.public.index=100;
    s->hero_actor.public.visible=1;s->hero_actor.public.loop=1;s->hero_actor.public.playing=1;
    s->hero_actor.public.clip=map->idle_clip;s->hero_actor.speed=1;
    for(uint32_t i=0;i<area->actor_count;++i){
        Person *p=s->actors+i;const C3AreaActor *a=area->actors+i;p->source=a;
        p->public.model=actor_models[i];p->public.index=a->index;
        memcpy(p->public.pose.position,a->position,sizeof a->position);p->public.pose.yaw=a->yaw;
        p->want_visible=a->aux3==DYNAMIC_MARKER?0:(a->flags>>31)&1;p->public.visible=p->want_visible;p->public.clip=a->motions[0];
        p->public.flags=0x0cu|(a->talk_start==1?0x10u:a->talk_start==2?0x20u:0u)|(a->talk_end==1?0x40u:0u);
        p->public.loop=1;p->public.playing=1;p->speed=1;
        /* A scene model rests on its first frame until native 73 plays it. */
        if(a->aux3==SCENE_MODEL_MARKER){p->public.loop=0;p->public.playing=0;p->public.flags=0;}
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
    for(uint32_t i=0;i<s->area->actor_count;++i){Person *p=s->actors+i;
        if(p->public.visible&&p->source->move_script){
            uint32_t args[4]={s->area->group,p->public.index,0,0};
            if(!start(s,p->source->move_script,args,4,&p->thread_key,error))return 0;
        }
    }
    /* movie.c fn_80035F64 installs this entry override after floorLink(2,1).
     * The card checkpoint uses previous1, so a fresh Continue does not replay it. */
    if(s->area->floor==2&&s->previous_floor==0x39f){
        /* Staffroll finish fn_80035F34 leaves the screen black. */
        s->view.fade=0;s->fade_from=s->fade_to=0;s->fade_elapsed=s->fade_duration=0;
        uint16_t key;
        if(!start(s,0x05960008u,NULL,0,&key,error))return 0;
        log_line(s,"story_entry_fade script=05960008 previous=039f");
    }
    return start_floor_script(s,s->area->main_script,&s->main_key,error);
}
const C3FieldStoryView *c3_field_story_view(const C3FieldStory *story)
{const Impl *s=story?story->impl:NULL;return s?&s->view:NULL;}
C3StoryActor *c3_field_story_person(C3FieldStory *story,uint32_t group,uint32_t index)
{Impl *s=story?story->impl:NULL;Person *p=s?person(s,group,index):NULL;return p?&p->public:NULL;}
C3StoryActor *c3_field_story_actor(C3FieldStory *story,uint32_t index)
{Impl *s=story?story->impl:NULL;Person *p=s?person(s,s->area->group,index):NULL;
 /* The renderer addresses streamed rows by prepared index, even after the
  * original native assigns a transient person ID. Script lookup stays closed
  * until allocation, while this scene-facing accessor retains the row. */
 if(!p&&s)for(uint32_t i=0;i<s->area->actor_count;++i)
     if(s->actors[i].source->aux3==DYNAMIC_MARKER&&s->actors[i].source->index==index){p=s->actors+i;break;}
 return p?&p->public:NULL;}

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
    for(uint32_t i=0;i<=s->area->actor_count+1u;++i){Person *p=person_at(s,i);
        if(p==&s->partner_actor&&!p->public.model)continue;
        C3FieldPlayer *q=pose(s,p);
        if(p->public.playing&&(p!=&s->hero_actor||!s->view.control))q->animation_seconds+=elapsed;
        if(!advance_turn(s,p,elapsed,error))return 0;
        if(!p->moving)continue;
        float walk,run;
        if(p->source){
            uint32_t clip=p->source->motions[1];
            /* A person still streaming in (no model yet) or a pack without
             * the walk clip: one walk cycle per second until it arrives. */
            float frames=30.0f;
            if(p->public.model&&clip<p->public.model->clip_count&&p->public.model->clips[clip].frame_count>=2)
                frames=(float)(p->public.model->clips[clip].frame_count-1u)*30.0f/p->public.model->fps;
            else if(p->public.model)note_presentation(s,0x49,"actor_walk_clip_missing_default_rate");
            walk=p->source->walk_distance/frames;run=walk;
            if(p->speed>1){
                clip=p->source->motions[2];
                frames=30.0f;
                if(p->public.model&&clip<p->public.model->clip_count&&p->public.model->clips[clip].frame_count>=2)
                    frames=(float)(p->public.model->clips[clip].frame_count-1u)*30.0f/p->public.model->fps;
                else if(p->public.model)note_presentation(s,0x49,"actor_run_clip_missing_default_rate");
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
/* Rui behind Wes (hero_move.c heroMoveMain: fn_8012DE94 / fn_8012D7F0 for
 * the follower, then moveLeader's record) while the player has control and
 * no script is moving her. Pack clips 0/1/2 are her idle, walk and run. */
static void partner_place_behind(Impl *s)
{
    Person *p=&s->partner_actor;float sc=s->map->scale,ground;
    float spot[3]={s->hero->position[0]-sinf(s->hero->yaw)*12.0f*sc,s->hero->position[1],
                   s->hero->position[2]-cosf(s->hero->yaw)*12.0f*sc};
    if(!c3_field_ground(s->map,spot,&ground)){memcpy(spot,s->hero->position,sizeof spot);ground=spot[1];}
    spot[1]=ground;memcpy(p->public.pose.position,spot,sizeof spot);memcpy(p->public.pose.proposed,spot,sizeof spot);
    p->public.pose.yaw=s->hero->yaw;c3_follow_reset(&s->follow);
}
static float clip_length(const C3Title *model,uint32_t clip)
{
    if(!model||clip>=model->clip_count||model->clips[clip].frame_count<2u||!(model->fps>0))return 0;
    return (float)(model->clips[clip].frame_count-1u)/model->fps;
}
/* fn_8018805C for the follower: a new target each frame keeps the sub-tick
 * remainder of the turn already running instead of restarting it. */
static void follow_turn(Impl *s,Person *p,float yaw,float speed)
{
    if(!p->turning){start_turn(s,p,yaw,speed);return;}
    p->turn_target=yaw+6.2831855f*(int32_t)(pose(s,p)->yaw/6.2831855f);p->turn_speed=speed;
}
static int sweep_humans(Impl *s,const Person *self,const float before[3],float pos[3]);
static int push_out_of_humans(Impl *s,const Person *self,float pos[3]);
static void follow_partner(Impl *s,float seconds)
{
    Person *p=&s->partner_actor;float sc=s->map->scale;
    if(!p->public.model||!s->view.control)return;
    if(p->public.visible&&!p->moving&&!p->parent){
        C3FieldPlayer *q=&p->public.pose;C3FollowStep step;float before[3];
        float dt=fminf(seconds,.25f),frames=dt*60.0f;
        memcpy(before,q->position,sizeof before);
        c3_follow_advance(&s->follow,s->hero->position,q->position,12.0f*sc,sc,dt,&step);
        q->position[0]+=step.move[0];q->position[2]+=step.move[1];
        /* fn_8012CA84 moves her with fn_801885C4 -> fn_8018E9B4 under her
         * people flags 0x701: human cylinders (0x400, Wes's included), then
         * walls. She stops at Wes's cylinder instead of walking into him. */
        (void)sweep_humans(s,p,before,q->position);
        memcpy(q->proposed,q->position,sizeof q->proposed);
        if(!c3_field_resolve_position(s->map,q,before))memcpy(q->position,before,sizeof before);
        /* fn_8012CA84: the turn rate and animation amount come from the
         * travel this frame, in original units per 60 Hz frame (at most 1).
         * Facing turns at the people turn rate, never snaps. */
        float moved=hypotf(step.move[0],step.move[1]),amount=0;
        if(moved>0&&frames>0&&sc>0){
            float speed=fminf(moved/sc/frames,1.0f),dx=q->position[0]-before[0],dz=q->position[2]-before[2];
            float travel=hypotf(dx,dz)/moved*speed; /* the travel vector scaled to speed */
            amount=c3_follow_amount(travel,s->map->walk_distance/s->map->walk_frames/sc,
                                    s->map->run_distance/s->map->run_frames/sc);
            if(dx*step.facing[0]+dz*step.facing[1]<0)amount=-amount;
            if(step.facing[0]!=0||step.facing[1]!=0)follow_turn(s,p,atan2f(step.facing[0],step.facing[1]),speed);
        }else{
            float target,to_leader=atan2f(s->hero->position[0]-q->position[0],s->hero->position[2]-q->position[2]);
            if(c3_follow_idle_turn(q->yaw,to_leader,75.0f*.017453292f,&target)){follow_turn(s,p,target,1.0f);amount=1.0f;}
            else p->turning=0;
        }
        /* fn_8012C660: the dominant clip of the blend, entered at the phase
         * the previous clip had reached (no restart from frame 0). */
        uint32_t clip=c3_follow_choose(&s->follow,amount,p->public.clip,dt);
        if(clip>=p->public.model->clip_count)clip=0;
        if(clip!=p->public.clip){
            q->animation_seconds=c3_follow_phase(q->animation_seconds,clip_length(p->public.model,p->public.clip),
                                                 clip_length(p->public.model,clip));
            p->public.clip=clip;
        }
        p->public.loop=1;p->public.playing=1;
        if(c3_follow_far(&s->follow,s->hero->position,q->position,sc,dt)){
            float record[3],ground;
            /* fn_8012DE94 takes the newest log point where she would hit
             * nothing; a point inside another person's cylinder is skipped. */
            for(unsigned i=0;c3_follow_record_at(&s->follow,i,record);++i){float probe[3];
                if(!c3_field_ground(s->map,record,&ground))continue;
                record[1]=ground;memcpy(probe,record,sizeof probe);
                if(push_out_of_humans(s,p,probe))continue;
                memcpy(q->position,record,sizeof record);break;}
            c3_follow_caught_up(&s->follow);
            log_line(s,"story_partner_catch_up x=%.3f z=%.3f",(double)q->position[0],(double)q->position[2]);
        }
    }
    c3_follow_record(&s->follow,s->hero->position,sc);
}
int c3_field_story_attach_partner(C3FieldStory *story,C3Title *model,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!model)return fail(error,"Invalid partner model");
    if(model->clip_count<3)return fail(error,"The partner pack lacks its idle, walk and run motions");
    Person *p=&s->partner_actor;
    memset(&s->partner_source,0,sizeof s->partner_source);
    s->partner_source.index=101;s->partner_source.motions[0]=0;s->partner_source.motions[1]=1;s->partner_source.motions[2]=2;
    s->partner_source.walk_distance=s->map->walk_distance;s->partner_source.run_distance=s->map->run_distance;
    s->partner_source.radius=s->map->radius;
    p->source=&s->partner_source;p->public.model=model;p->public.index=101;
    p->public.clip=0;p->public.loop=1;p->public.playing=1;p->speed=1;
    p->public.visible=c3_game_state_partner(s->game);p->want_visible=p->public.visible;
    partner_place_behind(s);
    log_line(s,"story_partner_model visible=%d x=%.3f z=%.3f",p->public.visible,
             (double)p->public.pose.position[0],(double)p->public.pose.position[2]);
    return 1;
}
void c3_field_story_set_move_input(C3FieldStory *story,uint16_t held,int stick_x,int stick_y)
{
    Impl *s=story?story->impl:NULL;
    if(s)s->move_input=(held&PAD_DIRECTIONS)||stick_x>2||stick_x< -2||stick_y>2||stick_y< -2;
}

int c3_field_story_tick(C3FieldStory *story,float seconds,uint16_t pressed,char *error)
{
    Impl *s=story?story->impl:NULL;
    if(!s||!isfinite(seconds)||seconds<0)return fail(error,"Invalid field story tick");
    s->tick_pressed=pressed;
    /* Animation, camera, waits and fades share active elapsed time. Only
     * collision-sensitive translation retains a bounded movement step. */
    if(!advance_people(s,seconds,error))return 0;
    follow_partner(s,seconds);
    for(uint32_t i=0;i<s->door_count;++i){C3StoryDoor *door=s->doors+i;
        if(!door->playing||!door->model)continue;
        float end=(float)(door->model->clips[door->clip].frame_count-1u)/door->model->fps;
        door->seconds+=seconds*door->rate;
        if(door->seconds>=end&&door->loop)door->seconds=end>0?fmodf(door->seconds,end):0;
        else if(door->seconds>=end){door->seconds=end;door->playing=0;}
    }
    ride_update(s);
    if(s->camera_playing&&s->view.camera_active)s->view.camera_seconds+=seconds*s->camera_rate;
    int rotating=s->rotation_time<s->rotation_duration;
    if(rotating){
        s->rotation_time=fminf(s->rotation_duration,s->rotation_time+seconds);
        float f=s->rotation_time/s->rotation_duration;
        for(unsigned i=0;i<2;++i)s->camera_rotation[i]=s->rotation_from[i]+(s->rotation_to[i]-s->rotation_from[i])*f;
        aim_camera(s,s->camera_rotation[0],s->camera_rotation[1]);
    }
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
        if(!start_floor_script(s,script,&s->setup_key,error))return 0;
    }
    for(unsigned i=0;i<C3_STORY_TASKS;++i){RunTask *t=s->tasks+i;
        if(!t->key||!c3_story_active(&s->vm,t->key))continue;
        if(t->key==s->talk_held)continue;
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
        s->talk_key=0;s->talk_held=0;close_message(s);restore_speakers(s);
        /* heroMoveInitEvent (native 20) without its heroMoveTermEvent:
         * an event branch may return early (Phenac fn 27 when flag 9a2 is
         * set). The hero move lock belongs to the event, so it ends with it. */
        if(!s->event_key&&s->event_depth>s->event_depth_base)s->event_depth=s->event_depth_base;
        if(!s->event_key)s->hero_event=0;
    }
    if(s->event_key&&!c3_story_active(&s->vm,s->event_key)){
        s->event_key=0;
        if(!s->talk_key&&s->event_depth>s->event_depth_base)s->event_depth=s->event_depth_base;
        if(!s->talk_key)s->hero_event=0;
    }
    if(s->pending_floor&&!s->departure_started){
        /* The original scene runs its post callback on departure. Suspend
         * other floor tasks before it so no more input or event can race it. */
        for(unsigned i=0;i<C3_STORY_TASKS;++i)if(s->tasks[i].key)c3_story_stop(&s->vm,s->tasks[i].key);
        s->talk_held=0;
        if(!start(s,0x05960009u,NULL,0,&s->exit_fade_key,error))return 0;
        if(!start_floor_script(s,s->area->post_script,&s->post_key,error))return 0;
        s->departure_started=1;
    }
    if(s->pending_floor&&s->departure_started&&!c3_story_active(&s->vm,s->post_key)&&
       !c3_story_active(&s->vm,s->exit_fade_key)&&s->fade_elapsed>=s->fade_duration&&s->view.fade<=.0001f){
        s->view.next_floor=s->pending_floor;s->view.next_spawn=s->pending_spawn;
        log_line(s,"story_floor_link from=%lu to=%lu spawn=%lu post_complete=1",(unsigned long)s->area->floor,
                 (unsigned long)s->view.next_floor,(unsigned long)s->view.next_spawn);
    }
    if(s->treasure_active&&!wait_task(s,&s->treasure_task,seconds,&pressed,error))return 0;
    for(unsigned i=0;i<s->treasure_count;++i){C3StoryTreasure *r=s->treasure+i;uint32_t v=0;
        if(r->taken_flag){c3_game_state_get_flag(s->game,r->taken_flag,&v);r->taken=v!=0;}
        uint32_t shown=1;if(r->hide_flag)c3_game_state_get_flag(s->game,r->hide_flag,&shown);
        r->visible=s->treasure_display[i]>=0?s->treasure_display[i]:(r->kind==1||!r->taken)&&shown;
    }
    /* heroMoveAddAutoEvent from the pause menu: the used item's script
     * runs once the event that opened the menu has ended. */
    if(s->pending_item_script&&!s->talk_key&&!s->event_key&&!s->pending_floor&&!s->view.dialogue){
        uint32_t script=s->pending_item_script,args[4]={s->pending_item,0,0,0};s->pending_item_script=0;
        log_line(s,"story_item_use script=%08lx item=%lu",(unsigned long)script,(unsigned long)args[0]);
        if(!start(s,script,args,4,&s->event_key,error))return 0;
    }
    s->view.control=s->entered&&s->hero_ready&&!s->event_depth&&!s->hero_event&&!s->talk_key&&!s->event_key&&
        !s->view.dialogue&&!s->pending_floor&&!s->view.blocked[0]&&!s->treasure_active;
    /* hero_move.c fn_8013024C main step: once the hero is free, a script
     * queued by heroMoveAddAutoEvent (native 22) starts with its four
     * arguments before any touch event. The queue is hero work, not floor
     * work, so a door that queues it and then links (common function 12 of
     * the Realgam dome) runs it on the destination floor. */
    if(s->view.control&&s->auto_event[0]){
        uint32_t script=s->auto_event[0],args[4]={s->auto_event[1],s->auto_event[2],s->auto_event[3],s->auto_event[4]};
        memset(s->auto_event,0,sizeof s->auto_event);
        log_line(s,"story_auto_event_start script=%08lx args=%lu,%lu,%lu,%lu",(unsigned long)script,(unsigned long)args[0],
                 (unsigned long)args[1],(unsigned long)args[2],(unsigned long)args[3]);
        s->view.control=0;
        if(!start(s,script,args,4,&s->event_key,error))return 0;
    }
    return 1;
}

int c3_field_story_pending_auto_event(const C3FieldStory *story,uint32_t out[5])
{
    const Impl *s=story?story->impl:NULL;
    if(!s||!s->auto_event[0])return 0;
    memcpy(out,s->auto_event,sizeof s->auto_event);return 1;
}

void c3_field_story_set_auto_event(C3FieldStory *story,const uint32_t in[5])
{
    Impl *s=story?story->impl:NULL;
    if(!s||!in)return;
    memcpy(s->auto_event,in,sizeof s->auto_event);
    log_line(s,"story_auto_event_carried script=%08lx",(unsigned long)in[0]);
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
    /* hero_move.c: the chat search (fn_8018CD08) also finds the 0x7FFF
     * treasure people; one not yet taken runs floorEventCtrlTresure(2). */
    for(unsigned i=0;i<s->treasure_count;++i){C3StoryTreasure *r=s->treasure+i;
        if(!r->visible||r->taken)continue;
        float dx=r->position[0]-s->hero->position[0],dz=r->position[2]-s->hero->position[2];
        float distance=hypotf(dx,dz),angle=angle_delta(atan2f(dx,dz)-s->hero->yaw);
        if(distance>12*s->map->scale+2*s->map->radius||fabsf(angle)>40.0f*PI/180.0f||
           fabsf(r->position[1]-s->hero->position[1])>2.0f)continue;
        memset(&s->treasure_task,0,sizeof s->treasure_task);
        s->treasure_task.key=0xffff;s->treasure_task.program=0x7fff;s->treasure_row=i;s->treasure_active=1;
        s->hero->yaw=atan2f(dx,dz);s->hero->motion=0;s->hero->animation_seconds=0;
        s->view.control=0;
        log_line(s,"story_treasure index=%lu",(unsigned long)r->index);
        return program_step(s,&s->treasure_task,0,error);
    }
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
        /* updateChat (fn_8012C0B4) suspends the person's own thread around
         * the talk: fn_80183730 before fn_800F7434, fn_80183688 after. A
         * trainer's sight loop (Pyrite 2F function 10: a9, then the talk
         * function) cannot fight the same battle while the talk runs. */
        s->talk_held=c3_story_active(&s->vm,best->thread_key)?best->thread_key:0;
        if(s->talk_held)log_line(s,"story_talk_hold task=%u",s->talk_held);
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
/* hero_move.c dispatches the touched event surfaces as kind 2 through
 * floor_character.c fn_80116D30 before the crossed ones (kind 1). The
 * common door script 05960005 needs native 60 and the door table, which
 * are not ported: those links are skipped and recorded once. */
int c3_field_story_touch(C3FieldStory *story,uint32_t source_id,char *error)
{
    Impl *s=story?story->impl:NULL;if(!s||!s->view.control)return 0;
    const C3AreaLink *link=c3_area_link(s->area,2,source_id);
    if(!link)return 0;
    /* Without bound doors the common door script would only return -1. */
    if(link->script==0x05960005u&&!s->door_count){note_presentation(s,0x60,"door_table_not_bound");return 0;}
    log_line(s,"story_touch source=%lu script=%08lx",(unsigned long)source_id,(unsigned long)link->script);
    s->view.control=0;
    return start(s,link->script,link->args,4,&s->event_key,error)?1:-1;
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
/* A human's cylinder radius, or a negative value when its cylinder is not
 * enabled. Wes (0,100) and the partner (0,101) are registered by hero_move.c
 * (fn_8018CB5C); PeopleInfo rows 1 and 2 are collision type 1, radius 4.
 * Floor people need collision type 1. A cylinder follows its person's
 * visibility (fn_8018C1E8 -> fn_8018FB2C -> GScolsys2HumanEnable). */
static float human_radius(Impl *s,const Person *p)
{
    if(p==&s->hero_actor)return s->map->radius;
    if(!p->public.visible||!p->public.model||!p->source)return -1;
    if(p!=&s->partner_actor&&p->source->collision_type!=1)return -1;
    return p->source->radius;
}
/* fn_8010FAF4 for mover self at pos: up to ten passes, each pushing pos out
 * of the first other enabled human cylinder it is inside
 * (checkCollisionHeadToHead: height 17, radius sum, 0.0001 margin, +X when
 * the centres coincide). Only self's own cylinder is skipped, so Wes and
 * Rui block each other as well as the floor's people. 1 when pushed. */
static int push_out_of_humans(Impl *s,const Person *self,float pos[3])
{
    const float height=17.0f*s->map->scale,own=human_radius(s,self);int pushed=0;
    for(unsigned pass=0;pass<10;++pass){
        const float *at=NULL;float dx=0,dz=0,distance=0,radius=0;
        for(uint32_t i=0;i<=s->area->actor_count+1u&&!at;++i){
            Person *p=person_at(s,i);float other=p==self?-1:human_radius(s,p);
            if(other<0)continue;
            const float *o=pose(s,p)->position;
            if(o[1]>=pos[1]+height||o[1]+height<=pos[1])continue;
            dx=pos[0]-o[0];dz=pos[2]-o[2];distance=sqrtf(dx*dx+dz*dz);radius=other+own;
            if(distance<radius)at=o;
        }
        if(!at)break;
        if(distance<=0)distance=dx=1.0f;
        float k=(.0001f+radius)/distance;
        pos[0]=dx*k+at[0];pos[2]=dz*k+at[2];pushed=1;
    }
    return pushed;
}
/* GScolsys2HumanCollision for self's step from before to pos: the step is
 * tested in slices of self's radius, and the first slice end inside another
 * human is pushed out and ends the step there (written to pos). 1 on hit. */
static int sweep_humans(Impl *s,const Person *self,const float before[3],float pos[3])
{
    float delta[3];for(unsigned i=0;i<3;++i)delta[i]=pos[i]-before[i];
    float distance=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]),own=human_radius(s,self);
    float step=distance>0&&own>0?fminf(1.0f,own/distance):0;
    for(float t=0;t<1.0f;t+=step){
        float next=step>0?fminf(1.0f,t+step):1.0f,slice[3];
        for(unsigned i=0;i<3;++i)slice[i]=before[i]+delta[i]*next;
        if(push_out_of_humans(s,self,slice)){memcpy(pos,slice,sizeof slice);return 1;}
        if(step<=0)break;
    }
    return 0;
}
/* The hero's step against the human cylinders. Testing only the end of a
 * console-rate step (often longer than a cylinder) let Wes pass through
 * people, and the partner was never an obstacle. */
void c3_field_story_collide(C3FieldStory *story,const float before[3])
{
    Impl *s=story?story->impl:NULL;if(!s||!before)return;
    float moved_from[3],pos[3],height;memcpy(moved_from,s->hero->position,sizeof moved_from);
    memcpy(pos,s->hero->position,sizeof pos);
    if(sweep_humans(s,&s->hero_actor,before,pos)){
        if(c3_field_ground(s->map,pos,&height)){pos[1]=height;memcpy(s->hero->position,pos,sizeof pos);}
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
    const float *m=parent->public.model->pose.matrices[p->parent_bone_plus_one-1u];
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
    for(unsigned i=0;i<3;++i)if(!isfinite(angles[i])||(!p->parent_root&&fabsf(angles[i])>.25f))
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
    /* Passengers only: free actors are placed inside c3_title_animate. */
    float euler[3],scale[3],r[9];
    memcpy(euler,p->world_rotation,sizeof euler);memcpy(scale,p->world_scale,sizeof scale);
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
    for(uint32_t i=0;i<=s->area->actor_count+1u;++i){Person *p=person_at(s,i);
        C3Title *model=p->public.model;if(!model)continue;
        uint32_t clip=p->public.clip;
        if(p==&s->hero_actor&&s->view.control)clip=s->hero->motion==2?s->map->run_clip:s->hero->motion==1?s->map->walk_clip:s->map->idle_clip;
        if(clip>=model->clip_count)return fail(error,"Actor clip is out of range");
        model->clips[clip].loop=(uint32_t)p->public.loop;
        /* A free actor's rigid yaw placement is folded into its bones, so an
         * unchanged sample keeps its staged vertices and a hidden actor is not
         * skinned. Passengers wait for their vehicle's sample: legacy path. */
        if(!p->parent){
            const C3FieldPlayer *q=pose(s,p);
            c3_title_place_yaw(model,q->position,cosf(q->yaw),sinf(q->yaw),1.0f,1);
            model->retained=1;model->hidden=(uint8_t)!p->public.visible;
        }else{model->retained=0;model->hidden=0;model->placement.enabled=0;}
        if(!c3_title_animate(model,clip,pose(s,p)->animation_seconds))return fail(error,"Original actor animation sample failed");
    }
    /* All parent samples are current before resolving a passenger. Original
     * actor-table order places passengers before the vehicle. */
    /* Poke Balls of the heal: GSmodelSetPosition at their part, scale 1.5. */
    for(uint32_t i=0;i<s->heal_model_count;++i){C3Title *ball=s->heal_models[i];
        c3_title_place_yaw(ball,s->heal.at[i],1.0f,0.0f,1.5f,1);
        ball->retained=1;ball->hidden=(uint8_t)!s->heal.shown[i];
        if(!c3_title_animate(ball,0,0))return fail(error,"Original Poke Ball sample failed");
    }
    for(uint32_t i=0;i<=s->area->actor_count+1u;++i)
        if(!attachment_pose(s,person_at(s,i),error))return 0;
    for(uint32_t i=0;i<=s->area->actor_count+1u;++i){Person *p=person_at(s,i);
        C3Title *model=p->public.model;if(!model||!p->parent)continue;
        transform(model,pose(s,p),p);
        if(!p->public.visible)for(uint32_t b=0;b<model->batch_count;++b)model->pose.materials[b].visible=0;
    }
    return 1;
}
