#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/floor_profile.h"
#include "route_battles.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional PRIVATE-DATA story test for Phenac City across the whole game.
 *
 * Each row starts from a player's chapter save (read only), moves the hero
 * to one Phenac floor and entrance, sets or clears the story flags that put
 * the game at one point of the story, and runs the original floor scripts
 * with the prepared packs: the arrival, then optionally one conversation
 * (the person whose talk script is named) or one touch surface. The row
 * passes when the original scripts set the flags the row expects, request
 * exactly the battles it expects (each reported as the hero's win, the
 * documented shortcut) and hand control back without a script failure.
 *
 * The flag edits are the only thing the test decides; every expected flag
 * is written by the original scripts during the run. Flags that later
 * events would set are cleared so the row sees the game as it was then.
 *
 * usage: test_phenac_events <newgame dir> <phenac pack dir> <save dir> [trace]
 *        test_phenac_events ... --explore <save> <floor> <spawn> [set=..] [clear=..]
 * Without arguments it checks nothing private. */
static unsigned checks;
static char error[C3_STORY_ERROR];
static FILE *trace;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)
#define FLAGS 3593u

typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, actors[C3_AREA_MAX_ACTORS];
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
} Scene;

enum { ARRIVAL, TALK, TOUCH };
typedef struct Event {
    const char *name, *save;
    uint16_t floor, spawn;
    uint16_t set[16], clear[24];
    int action; uint32_t arg;   /* TALK: talk script id; TOUCH: surface */
    uint16_t expect[12];        /* flags the original scripts must set */
    uint16_t battles[8];        /* encounters requested, in order */
    int stand; float at[2];     /* stand the hero at (x, z) after entering */
} Event;

static const char *newgame, *root, *saves;
static int any_floor(uint32_t floor){return floor>=1&&floor<0x400;}
static unsigned gifts;
static int receive(void *c,uint32_t item,uint32_t count,char *name,size_t capacity)
{(void)c;(void)item;(void)count;++gifts;snprintf(name,capacity,"ITEM");return 0;}
static C3GameState *current_game;
static uint32_t current_floor;
static unsigned restore_calls;
static int restore(void *c){(void)c;++restore_calls;return route_battles_enabled()?route_battles_heal(current_game):6;}
static void path(char out[1024],const char *dir,const char *name)
{CHECK(snprintf(out,1024,"%s/%s",dir,name)>0&&strlen(out)<1023);}
static int present(const char *dir,const char *name)
{char file[1024];FILE *f;path(file,dir,name);f=fopen(file,"rb");if(f)fclose(f);return f!=NULL;}

static int open_floor(Scene *s,C3GameState *game,uint32_t floor,uint32_t spawn)
{
    const C3FloorProfile *profile=c3_floor_profile(floor);char file[1024],name[96];unsigned frame;
    CHECK(profile);
    snprintf(name,sizeof name,"%s.c3f",profile->prefix);path(file,root,name);
    if(!c3_field_load(&s->map,file,error,sizeof error))return 0;
    snprintf(name,sizeof name,"%s.c3a",profile->prefix);path(file,root,name);
    if(!c3_area_load(&s->area,file,error,sizeof error))return 0;
    snprintf(name,sizeof name,"%s_wes.c3t",profile->prefix);path(file,root,name);
    if(!c3_title_load(&s->wes,file,error,sizeof error))return 0;
    for(uint32_t i=0;i<s->area.actor_count;++i){
        if(!s->area.actors[i].file[0])continue;
        path(file,root,s->area.actors[i].file);
        if(!c3_title_load(s->actors+i,file,error,sizeof error))return 0;
        s->models[i]=s->actors+i;
    }
    snprintf(name,sizeof name,"%s_camera.c3c",profile->prefix);
    if(present(root,name)){path(file,root,name);if(!c3_area_camera_load(&s->camera,file,error,sizeof error))return 0;}
    if(!c3_field_spawn(&s->map,spawn,&s->hero)){snprintf(error,sizeof error,"entrance %lu is unavailable",(unsigned long)spawn);return 0;}
    path(file,root,profile->story);
    if(!c3_field_story_load(&s->story,file,game,&s->map,&s->area,&s->hero,&s->wes,s->models,&s->camera,trace,error))return 0;
    c3_field_story_set_floor_support(&s->story,any_floor);
    c3_field_story_enable_name_entry(&s->story,1);
    c3_field_story_set_item_service(&s->story,receive,NULL);
    c3_field_story_set_party_service(&s->story,restore,NULL);
    c3_field_story_set_random(&s->story,c3_game_state_random16,game);
    if(!c3_field_story_prepare(&s->story,error))return 0;
    for(frame=0;frame<3600&&!c3_field_story_view(&s->story)->prepared;++frame)
        if(!c3_field_story_tick(&s->story,1.0f/60.0f,0,error))return 0;
    if(!c3_field_story_view(&s->story)->prepared){snprintf(error,sizeof error,"preparation did not finish");return 0;}
    return c3_field_story_enter(&s->story,error);
}
static void close_floor(Scene *s)
{
    c3_field_story_close(&s->story);c3_field_close(&s->map);c3_area_close(&s->area);c3_area_camera_close(&s->camera);
    c3_title_close(&s->wes);for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(s->actors+i);
    memset(s,0,sizeof *s);
}

/* Tick until the hero has control with no dialogue (after at least 30
 * frames), a floor link is requested or the budget is spent. Battles are
 * reported as the hero's win and recorded. 1 on control or link. */
static unsigned battle_log[32],battle_count,link_floor,messages,odd_glyphs;
/* Count the messages shown and list the non-ASCII characters they use. */
static void inspect_message(const C3FieldStoryView *v)
{
    static uint32_t last;
    if(!v->dialogue||!v->message_id||v->message_id==last)return;
    last=v->message_id;++messages;
    for(const unsigned char *c=(const unsigned char *)v->message;*c;++c)if(*c>=0x80){
        unsigned code=*c<0xe0?(c[0]&31u)<<6|(c[1]&63u):(c[0]&15u)<<12|(c[1]&63u)<<6|(c[2]&63u);
        ++odd_glyphs;fprintf(trace,"phenac_event_glyph message=%05lx code=U+%04X\n",(unsigned long)v->message_id,code);break;}
}
static int run(Scene *s,unsigned budget)
{
    for(unsigned frame=0;frame<budget;++frame){
        const C3FieldStoryView *v=c3_field_story_view(&s->story);
        uint32_t encounter;
        if(v->next_floor){link_floor=v->next_floor;return 1;}
        inspect_message(v);
        if(frame>30&&v->control&&!v->dialogue)return 1;
        if(c3_field_story_battle_pending(&s->story,&encounter)){
            if(battle_count<32)battle_log[battle_count++]=encounter;
            /* Real battles with C3_ROUTE_BATTLES (tests/route_battles.h). */
            if(!route_battle_play(&s->story,current_game,current_floor,2,trace,"phenac_event_battle",error,sizeof error))return 0;
            if(c3_field_story_view(&s->story)->next_floor)continue;
        }
        if(v->shop&&!c3_field_story_shop_done(&s->story,error))return 0;
        if(v->name_entry&&!c3_field_story_name_result(&s->story,"RUI",error))return 0;
        if(c3_field_story_floor_refused(&s->story,NULL)){snprintf(error,sizeof error,"%s",v->blocked);return 0;}
        uint16_t pressed=(v->dialogue||v->choice)&&frame%15==0?0x100:0;
        if(!c3_field_story_tick(&s->story,1.0f/60.0f,pressed,error)||!c3_field_story_sample(&s->story,error))return 0;
    }
    snprintf(error,sizeof error,"no control after %u frames",budget);return 0;
}
static int talk(Scene *s,uint32_t script)
{
    for(uint32_t i=0;i<s->area.actor_count;++i){
        if(s->area.actors[i].talk_script!=script)continue;
        C3StoryActor *a=c3_field_story_actor(&s->story,s->area.actors[i].index);
        if(!a||!a->visible)continue;
        float yaw=a->pose.yaw,reach=.6f;
        s->hero=a->pose;s->hero.position[0]+=sinf(yaw)*reach;s->hero.position[2]+=cosf(yaw)*reach;
        s->hero.yaw=yaw+3.14159265f;memcpy(s->hero.proposed,s->hero.position,sizeof s->hero.position);
        fprintf(trace,"phenac_event_talk actor=%lu script=%08lx\n",(unsigned long)s->area.actors[i].index,(unsigned long)script);
        return c3_field_story_interact(&s->story,error);
    }
    snprintf(error,sizeof error,"no visible person has talk script %08lx",(unsigned long)script);return 0;
}
static void snapshot(const C3GameState *g,uint8_t out[FLAGS])
{for(uint32_t id=1;id<FLAGS;++id){uint32_t v=0;out[id]=c3_game_state_get_flag(g,id,&v)&&v;}}

static void load_state(C3GameState *game,const char *save,uint32_t floor,uint32_t spawn)
{
    char file[1024],gate[1024],gerror[C3_GAME_ERROR_SIZE]={0};C3GameSummary summary;
    path(file,saves,save);path(gate,newgame,"newgame.c3g");
    if(!c3_game_state_load(game,file,gate,gerror)){snprintf(error,sizeof error,"%s",gerror);CHECK(0);}
    CHECK(c3_game_state_summary(game,&summary));
    CHECK(c3_game_state_set_floor(game,floor,spawn==0&&floor==3?0x38e:summary.floor,spawn));
}

/* Run one row: arrival, then the action. Returns flags changed through out. */
static int play(const Event *e,uint8_t before[FLAGS],uint8_t after[FLAGS],C3GameState *game)
{
    Scene *s=calloc(1,sizeof *s);int ok=0;CHECK(s);
    battle_count=0;link_floor=0;error[0]=0;current_game=game;current_floor=e->floor;
    load_state(game,e->save,e->floor,e->spawn);
    for(unsigned i=0;i<16&&e->set[i];++i)CHECK(c3_game_state_set_flag(game,e->set[i],1));
    for(unsigned i=0;i<24&&e->clear[i];++i)CHECK(c3_game_state_set_flag(game,e->clear[i],0));
    snapshot(game,before);
    fprintf(trace,"phenac_event_begin name=%s save=%s floor=%03x spawn=%u\n",e->name,e->save,e->floor,e->spawn);
    if(!open_floor(s,game,e->floor,e->spawn))goto done;
    if(e->stand){float ground;s->hero.position[0]=e->at[0];s->hero.position[2]=e->at[1];
        if(c3_field_ground(&s->map,s->hero.position,&ground))s->hero.position[1]=ground;
        memcpy(s->hero.proposed,s->hero.position,sizeof s->hero.position);}
    if(!run(s,30000))goto done;
    if(e->action==TALK&&!link_floor){if(!talk(s,e->arg)||!run(s,30000))goto done;}
    if(e->action==TOUCH&&!link_floor){int started=c3_field_story_touch(&s->story,e->arg,error);
        if(started<=0){if(!started)snprintf(error,sizeof error,"touch %lu started nothing",(unsigned long)e->arg);goto done;}
        if(!run(s,30000))goto done;}
    ok=1;
done:
    snapshot(game,after);
    close_floor(s);free(s);return ok;
}
static void report(const char *name,const uint8_t before[FLAGS],const uint8_t after[FLAGS])
{
    fprintf(trace,"phenac_event_end name=%s link=%03x battles=",name,link_floor);
    for(unsigned i=0;i<battle_count;++i)fprintf(trace,"%s%x",i?",":"",battle_log[i]);
    fprintf(trace," set=");
    for(uint32_t id=1,n=0;id<FLAGS;++id)if(after[id]!=before[id])fprintf(trace,"%s%s%03lx",n++?",":"",after[id]?"":"-",(unsigned long)id);
    fprintf(trace,"\n");fflush(trace);
}

static int explore(int argc,char **argv)
{
    static Event e;static uint8_t before[FLAGS],after[FLAGS];
    C3GameState game={0};unsigned n=0,m=0;
    e.name="explore";e.save=argv[0];e.floor=(uint16_t)strtoul(argv[1],0,16);e.spawn=(uint16_t)strtoul(argv[2],0,16);
    for(int i=3;i<argc;++i){
        const char *p=argv[i];int setting=!strncmp(p,"set=",4);
        if(sscanf(p,"at=%f,%f",&e.at[0],&e.at[1])==2){e.stand=1;continue;}
        if(!setting&&strncmp(p,"clear=",6))continue;
        for(p=strchr(p,'=')+1;*p;){char *end;unsigned long v=strtoul(p,&end,16);if(end==p)break;
            if(setting&&n<16)e.set[n++]=(uint16_t)v;else if(!setting&&m<24)e.clear[m++]=(uint16_t)v;p=*end?end+1:end;}
    }
    /* The arrival, then every person and every touch surface in turn. */
    int ok=play(&e,before,after,&game);report("arrival",before,after);
    printf("arrival: %s %s\n",ok?"ok":"FAILED",ok?"":error);
    c3_game_state_free(&game);
    {Scene probe={0};C3GameState g={0};load_state(&g,e.save,e.floor,e.spawn);
     CHECK(open_floor(&probe,&g,e.floor,e.spawn));
     C3Area area=probe.area;uint32_t scripts[C3_AREA_MAX_ACTORS],touches[64];unsigned ns=0,nt=0;
     for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].talk_script)scripts[ns++]=area.actors[i].talk_script;
     for(uint32_t i=0;i<area.link_count&&nt<64;++i)if(area.links[i].kind==2)touches[nt++]=area.links[i].source_id;
     close_floor(&probe);c3_game_state_free(&g);
     for(unsigned i=0;i<ns;++i){e.action=TALK;e.arg=scripts[i];ok=play(&e,before,after,&game);
         char label[32];snprintf(label,sizeof label,"talk_%08lx",(unsigned long)scripts[i]);report(label,before,after);
         printf("%s: %s %s\n",label,ok?"ok":"FAILED",ok?"":error);c3_game_state_free(&game);}
     for(unsigned i=0;i<nt;++i){e.action=TOUCH;e.arg=touches[i];ok=play(&e,before,after,&game);
         char label[32];snprintf(label,sizeof label,"touch_%lu",(unsigned long)touches[i]);report(label,before,after);
         printf("%s: %s %s\n",label,ok?"ok":"FAILED",ok?"":error);c3_game_state_free(&game);}}
    return 0;
}

/* Saves (private/lanes/saves/INDEX.md). EARLY sits in the Phenac Pokemon
 * Center just after Rui joined (0x3E5 set, the mayor not yet met).
 * MID_PYRITE has finished Pyrite (0x413) and met the late mayor (0x478)
 * but not the Pre Gym (0x3F7). LATE has every Phenac main-story flag;
 * SIDE lacks the three late town trainers' flags 0x84F/0x850. */
#define EARLY "pokemon-colosseum.35188.gci"
#define MID_PYRITE "pokemon-colosseum.15310.gci"
#define LATE "pokemon-colosseum.27046.gci"
#define SIDE "pokemon-colosseum.5007.gci"
/* The first visit writes, in story order: 0x9A4 (mayor), 0x3E6 (stadium),
 * 0x3E7/0xB10 (grunts), 0x99E (Mart); the Outskirt Stand shop then writes
 * 0x3E8 (Poke Balls), and the second visit 0x3E9 and 0x3EA/0x9B1-0x9B4. */
#define VISIT1_AHEAD 0x9a4,0x3e6,0x3e7,0xb10,0x99e,0x3e8,0x3e9,0x3ea,0x9b1,0x9b2,0x9b3,0x9b4
#define PREGYM_AHEAD 0x3f1,0x3f2,0x3f3,0x3f4,0x3f5,0x3f6,0x3f7,0x48b,0x995,0x994,0x99c,0x41d,0x41e,0x41f,0x48c,0xddd
static const Event EVENTS[]={
    /* First visit (main story, after the partner event). */
    {.name="mayor_welcome",.save=EARLY,.floor=5,.spawn=0,.clear={VISIT1_AHEAD},.action=ARRIVAL,.expect={0x9a4}},
    {.name="stadium_receptionist",.save=EARLY,.floor=0xd,.spawn=0,.set={0x9a4},.clear={0x3e6,0x3e7,0xb10,0x99e,0x3e8,0x3e9,0x3ea},
     .action=TALK,.arg=0x01000007,.expect={0x3e6}},
    {.name="grunts_confront_wes",.save=EARLY,.floor=3,.spawn=7,.set={0x9a4,0x3e6},.clear={0x3e7,0xb10,0x99e,0x3e8,0x3e9,0x3ea},
     .action=ARRIVAL,.expect={0x3e7,0xb10},.battles={26}},
    {.name="mart_shopper",.save=EARLY,.floor=9,.spawn=0,.set={0x9a4,0x3e6,0x3e7,0xb10},.clear={0x99e,0x3e8,0x3e9,0x3ea},
     .action=TALK,.arg=0x01000008,.expect={0x99e}},
    {.name="mart2f_shopper",.save=EARLY,.floor=0xa,.spawn=0,.set={0x9a4,0x3e6,0x3e7,0xb10},.clear={0x99e,0x3e8,0x3e9,0x3ea},
     .action=TALK,.arg=0x01000006,.expect={0x99e}},
    /* Second visit: back from the Outskirt Stand with Poke Balls. */
    {.name="back_with_poke_balls",.save=EARLY,.floor=3,.spawn=0,.set={0x9a4,0x3e6,0x3e7,0xb10,0x99e,0x3e8},.clear={0x3e9,0x3ea},
     .action=ARRIVAL,.expect={0x3e9}},
    {.name="mayor_house_ambush",.save=EARLY,.floor=5,.spawn=0,.set={0x9a4,0x3e6,0x3e7,0xb10,0x99e,0x3e8,0x3e9},
     .clear={0x3ea,0x9b1,0x9b2,0x9b3,0x9b4},.action=ARRIVAL,.expect={0x3ea,0x9b1,0x9b2,0x9b3,0x9b4},.battles={15,17}},
    {.name="town_trainer_3ee",.save=EARLY,.floor=3,.spawn=0,.set={0x9a4,0x3e6,0x3e7,0xb10,0x99e,0x3e8,0x3e9},.clear={0x3ee},
     .action=TALK,.arg=0x0100000c,.expect={0x3ee},.battles={20}},
    {.name="mayor_2f_after_pyrite",.save=EARLY,.floor=6,.spawn=0,
     .set={0x9a4,0x3e6,0x3e7,0xb10,0x99e,0x3e8,0x3e9,0x3ea,0x9b1,0x9b2,0x9b3,0x9b4,0x3f9},.action=TALK,.arg=0x01000005},
    /* The Pre Gym after Pyrite: the challenge, its four trainers, Justy. */
    {.name="pregym_challenge",.save=MID_PYRITE,.floor=0xb,.spawn=0,.clear={PREGYM_AHEAD},.action=TALK,.arg=0x0100000c,
     .expect={0x3f1,0x995}},
    {.name="pregym_trainers",.save=MID_PYRITE,.floor=0xb,.spawn=0,.set={0x3f1},
     .clear={0x3f2,0x3f3,0x3f4,0x3f5,0x3f6,0x3f7,0x48b,0x995,0x994,0x99c,0x41d,0x41e,0x41f,0x48c,0xddd},
     .action=ARRIVAL,.stand=1,.at={-1.5f,0},.expect={0x3f2,0x3f3,0x3f4,0x3f5,0x3f6,0x3f7,0x48b,0x995},.battles={22,23,24,25}},
    {.name="pregym_justy",.save=MID_PYRITE,.floor=0xb,.spawn=0,
     .set={0x3f1,0x3f2,0x3f3,0x3f4,0x3f5,0x3f6,0x3f7,0x48b,0x995,0x41d},.clear={0x41e,0x41f,0x48c,0xddd},
     .action=ARRIVAL,.stand=1,.at={-1.5f,0},.expect={0x41e,0x41f,0x48c,0xddd},.battles={36}},
    /* Later visits. */
    {.name="mayor_late_first",.save=MID_PYRITE,.floor=5,.spawn=0,.clear={0x47c,0x47d,0x47e,0x47f},.action=TALK,.arg=0x01000014,
     .expect={0x47c,0x47d},.battles={16}},
    {.name="mayor_late_second",.save=MID_PYRITE,.floor=5,.spawn=0,.clear={0x47e,0x47f},.action=TALK,.arg=0x01000015,
     .expect={0x47e,0x47f},.battles={19}},
    {.name="town_late_scene",.save=LATE,.floor=3,.spawn=0,.clear={0xa59},.action=ARRIVAL,.expect={0xa59}},
    {.name="town_late_trainer_1",.save=SIDE,.floor=3,.spawn=0,.set={0x84f,0x850,0x851},.clear={0x3ec,0x9d2,0x9d3,0x9d4},
     .action=TALK,.arg=0x01000016,.expect={0x3ec,0x9d2},.battles={0x214}},
    {.name="town_late_trainer_2",.save=SIDE,.floor=3,.spawn=0,.set={0x84f,0x850,0x851},.clear={0x3ec,0x9d2,0x9d3,0x9d4},
     .action=TALK,.arg=0x01000017,.expect={0x3ec,0x9d3},.battles={0x215}},
    {.name="town_late_trainer_3",.save=SIDE,.floor=3,.spawn=0,.set={0x84f,0x850,0x851},.clear={0x3ec,0x9d2,0x9d3,0x9d4},
     .action=TALK,.arg=0x01000018,.expect={0x3ec,0x9d4},.battles={0x216}},
    {.name="stadium_late_receptionist",.save=LATE,.floor=0xd,.spawn=0,.clear={0x9b7},.action=TALK,.arg=0x01000007,.expect={0x9b7}},
};

/* Every person on every Phenac floor, at three points of the story: each
 * conversation must run to its end and give control back. Known gaps are
 * listed with their owner and still counted. */
static const struct { uint16_t floor; uint32_t script; const char *why; } TALK_GAPS[]={
    /* Card room (not reached by any door or script on this disc): native
     * C7 menuSubOpenSelect takes the first row ("read a card"), native CA
     * returns from the unported card menu floor 0x393 and the dealer asks
     * again. Select menus are the systems lane's (claims ledger). */
    {0x80,0x01000008,"select menu C7 takes the first row; card menu 0x393 unported"},
};
/* The Pokemon Center nurse (PC 1F person 3, talk script 01000008) answers
 * YES with native 53, fn_801D0BD0, which heals the party in every mode. */
static void nurse_heals(void)
{
    static uint8_t before[FLAGS],after[FLAGS];
    Event e={.name="nurse",.save=EARLY,.floor=4,.spawn=0,.action=TALK,.arg=0x01000008};
    C3GameState game={0};restore_calls=0;
    int ok=play(&e,before,after,&game);c3_game_state_free(&game);
    if(!ok)fprintf(stderr,"nurse: %s\n",error);
    CHECK(ok&&restore_calls==1);
    printf("phenac-events nurse: party restored once\n");
}
/* Rui's event, main function 6 of 013E0E00. No chapter save sits before it,
 * so the row starts a fresh game with the eleven flags the Outskirt scripts
 * have written by the world-map link (as tests/test_phenac_story.c does),
 * plays the arrival (function 5, battle 14 reported as a win) and attaches
 * Rui's pack at load as the console does. The hero talks to the sack
 * (actor 0, talk script 01000021). Native 3B places Wes at (0, 323.75),
 * the boy (actor 7) at (-8.1, 317), the woman (actor 5) at (9, 312) and
 * Rui (0,101) on the sack, (0, 313): original tenths, times the map scale.
 * No one stands inside another's cylinder (PeopleInfo radii). Wes then
 * runs straight at Rui at console tick lengths, one stalled 0.25 s tick
 * first: she is a human cylinder too (hero_move.c registers (0,101) with
 * fn_8018CB5C; PeopleInfo row 2 is collision type 1, radius 4), and the
 * step is swept in hero-radius slices as GScolsys2HumanCollision does,
 * so he never passes through her or the boy. */
static float gap(const C3FieldPlayer *a,const C3FieldPlayer *b)
{return hypotf(a->position[0]-b->position[0],a->position[2]-b->position[2]);}
static void partner_event(void)
{
    if(!present(root,"phenac_partner.c3t")){printf("phenac-events partner: skipped (no phenac_partner.c3t)\n");return;}
    static const uint16_t outskirt[]={0x8d0,0xaf8,0x3da,0x3db,0xa05,0xace,0x3dc,0x3dd,0x3de,0xa5a,0x3df};
    Scene *s=calloc(1,sizeof *s);C3GameState game={0};static C3Title rui_model;char file[1024];uint32_t v=0;
    CHECK(s);error[0]=0;link_floor=0;battle_count=0;current_game=&game;current_floor=3;
    path(file,newgame,"newgame.c3g");CHECK(c3_game_state_new(&game,"WES",0x12345678,file,error));
    for(size_t i=0;i<sizeof outskirt/sizeof outskirt[0];++i)CHECK(c3_game_state_set_flag(&game,outskirt[i],1));
    CHECK(c3_game_state_set_floor(&game,3,C3_FLOOR_WORLD_MAP_ID,0));
    CHECK(open_floor(s,&game,3,0));
    path(file,root,"phenac_partner.c3t");CHECK(c3_title_load(&rui_model,file,error,sizeof error));
    CHECK(c3_field_story_attach_partner(&s->story,&rui_model,error));
    CHECK(run(s,60000)&&!link_floor&&battle_count==1&&battle_log[0]==14);
    CHECK(c3_game_state_get_flag(&game,0x3e1,&v)&&v&&!c3_game_state_partner(&game));battle_count=0;
    C3StoryActor *sack=c3_field_story_actor(&s->story,0),*rui=c3_field_story_person(&s->story,0,101);
    C3StoryActor *boy=c3_field_story_actor(&s->story,7),*woman=c3_field_story_actor(&s->story,5);
    CHECK(sack&&sack->visible&&rui&&!rui->visible&&boy&&boy->visible&&woman&&woman->visible);
    /* Talk to the sack from just north of it, facing it. */
    s->hero.position[0]=sack->pose.position[0];s->hero.position[2]=sack->pose.position[2]+1.0f;s->hero.yaw=3.1415927f;
    memcpy(s->hero.proposed,s->hero.position,sizeof s->hero.position);
    CHECK(c3_field_story_interact(&s->story,error));
    CHECK(run(s,30000)&&!link_floor&&battle_count==0);
    CHECK(c3_game_state_get_flag(&game,0x3e5,&v)&&v&&c3_game_state_partner(&game)&&rui->visible);
    const float sc=s->map.scale,r=s->map.radius;
    const struct { const C3FieldPlayer *pose; float x, z, radius; const char *name; } at[]={
        {&s->hero,0.0f,323.75f,r,"wes"},{&rui->pose,0.0f,313.0f,r,"rui"},
        {&boy->pose,-8.1f,317.0f,s->area.actors[7].radius,"boy"},{&woman->pose,9.0f,312.0f,s->area.actors[5].radius,"woman"}};
    for(unsigned i=0;i<4;++i){
        fprintf(trace,"phenac_partner_event %s x=%.3f z=%.3f expected=%.3f,%.3f\n",at[i].name,(double)at[i].pose->position[0],
                (double)at[i].pose->position[2],(double)(at[i].x*sc),(double)(at[i].z*sc));
        CHECK(fabsf(at[i].pose->position[0]-at[i].x*sc)<1e-3f&&fabsf(at[i].pose->position[2]-at[i].z*sc)<1e-3f);
        for(unsigned j=0;j<i;++j)CHECK(gap(at[i].pose,at[j].pose)>=at[i].radius+at[j].radius);
    }
    CHECK(fabsf(s->hero.yaw-3.1415927f)<1e-3f); /* native 3C: Wes faces the sack */
    /* Wes runs south (yaw pi) into Rui; she stays a cylinder ahead of him. */
    const float dts[]={.25f,1.0f/17.0f,1.0f/18.0f,1.0f/15.5f};float closest=1e9f,wes_start=s->hero.position[2];unsigned blocked=0;
    for(unsigned tick=0;tick<40;++tick){
        float dt=tick?dts[1+tick%3]:dts[0],before[3];memcpy(before,s->hero.position,sizeof before);
        unsigned count=s->hero.collision_count;
        CHECK(c3_field_story_view(&s->story)->control);
        CHECK(c3_field_move(&s->map,&s->hero,0,127,0,0.0f,dt));
        c3_field_story_collide(&s->story,before);
        blocked+=s->hero.collision_count!=count;
        float d=gap(&s->hero,&rui->pose);closest=fminf(closest,d);
        fprintf(trace,"phenac_partner_push tick=%u dt=%.4f wes_z=%.3f rui=%.3f,%.3f gap=%.3f boy_gap=%.3f\n",tick,(double)dt,
                (double)s->hero.position[2],(double)rui->pose.position[0],(double)rui->pose.position[2],(double)d,(double)gap(&s->hero,&boy->pose));
        CHECK(d>=2*r-1e-4f&&s->hero.position[2]>rui->pose.position[2]);
        CHECK(gap(&s->hero,&boy->pose)>=r+s->area.actors[7].radius-1e-4f);
        CHECK(c3_field_story_tick(&s->story,dt,0,error)&&c3_field_story_sample(&s->story,error));
    }
    fprintf(trace,"phenac_partner_push_end wes_moved=%.3f closest=%.3f blocked_ticks=%u\n",(double)(wes_start-s->hero.position[2]),(double)closest,blocked);
    CHECK(blocked>0&&wes_start-s->hero.position[2]>0.5f);
    close_floor(s);free(s);c3_title_close(&rui_model);c3_game_state_free(&game);
    printf("phenac-events partner: script positions held, Wes stays outside Rui's cylinder (closest %.3f)\n",(double)closest);
}
/* The console stop of 2026-11-21 (run-deploy6 boot-trace-ring): with Rui
 * following, Wes ran up the north ramp onto the Pre-Gym ring (y 2.0) and
 * north-west to (-4.8654, -3.7359), the inside corner of the ring's railing
 * (CCD object 7: x -5.27 and z -4.14 plus Wes's radius). West, north-west
 * and north press into that railing; south and east leave. Rui is a human
 * cylinder for Wes and Wes for her (her steps run fn_8018E9B4 under people
 * flags 0x701), so at no tick does either stand inside the other. Then Rui
 * is put inside Wes's cylinder south, east or south-east of him and held
 * still: a step that does not run into her or the railing still leaves the
 * corner, so he is never pinned between them. */
static void ring_step(Scene *s,float yaw,float dt)
{
    float before[3],run=s->map.run_distance/s->map.run_frames*dt*60.0f;memcpy(before,s->hero.position,sizeof before);
    s->hero.yaw=yaw;s->hero.motion=2;
    s->hero.position[0]+=sinf(yaw)*run;s->hero.position[2]+=cosf(yaw)*run;
    memcpy(s->hero.proposed,s->hero.position,sizeof s->hero.proposed);
    CHECK(c3_field_resolve_position(&s->map,&s->hero,before));
    c3_field_story_collide(&s->story,before);
}
static void ring_corner(void)
{
    if(!present(root,"phenac_partner.c3t")){printf("phenac-events ring corner: skipped (no phenac_partner.c3t)\n");return;}
    Scene *s=calloc(1,sizeof *s);C3GameState game={0};static C3Title rui_model;char file[1024];float g;
    CHECK(s);error[0]=0;link_floor=0;battle_count=0;current_game=&game;current_floor=3;
    load_state(&game,EARLY,3,0);CHECK(c3_game_state_partner(&game));
    CHECK(open_floor(s,&game,3,0));
    CHECK(run(s,30000)&&!link_floor);
    s->hero.position[0]=-0.1827f;s->hero.position[2]=10.8055f;s->hero.position[1]=0;
    CHECK(c3_field_ground(&s->map,s->hero.position,&g));s->hero.position[1]=g;s->hero.yaw=3.1415927f;
    memcpy(s->hero.proposed,s->hero.position,sizeof s->hero.position);
    path(file,root,"phenac_partner.c3t");CHECK(c3_title_load(&rui_model,file,error,sizeof error));
    CHECK(c3_field_story_attach_partner(&s->story,&rui_model,error));
    C3StoryActor *rui=c3_field_story_person(&s->story,0,101);CHECK(rui&&rui->visible);
    const float dts[]={1.0f/17.0f,1.0f/18.0f,1.0f/15.5f},r=s->map.radius,corner[2]={-4.8654f,-3.7359f};
    unsigned n=0;float closest=1e9f;
    #define RING_TICK(yaw_) do { float dt_=dts[n++%3]; ring_step(s,(yaw_),dt_); \
        CHECK(c3_field_story_tick(&s->story,dt_,0,error)&&c3_field_story_sample(&s->story,error)&&c3_field_story_view(&s->story)->control); \
        float gap_=gap(&s->hero,&rui->pose);closest=fminf(closest,gap_); \
        fprintf(trace,"phenac_ring tick=%u wes=%.4f,%.4f,%.4f rui=%.4f,%.4f,%.4f gap=%.3f\n",n,(double)s->hero.position[0],(double)s->hero.position[1], \
                (double)s->hero.position[2],(double)rui->pose.position[0],(double)rui->pose.position[1],(double)rui->pose.position[2],(double)gap_); \
        CHECK(gap_>=2*r-1e-3f); } while (0)
    /* The logged route: up the ramp, then west and north-west along the ring. */
    static const float way[][2]={{-0.1827f,0.6088f},{-0.2496f,0.4820f},{-1.1608f,-0.8969f},{-4.0366f,-1.4295f},{-4.1090f,-1.5666f},{-6.0f,-5.5f}};
    for(unsigned w=0;w<sizeof way/sizeof way[0];++w)for(unsigned guard=0;guard<200;++guard){
        float from[3];memcpy(from,s->hero.position,sizeof from);
        float dx=way[w][0]-s->hero.position[0],dz=way[w][1]-s->hero.position[2];if(hypotf(dx,dz)<.15f)break;
        RING_TICK(atan2f(dx,dz));
        if(hypotf(s->hero.position[0]-from[0],s->hero.position[2]-from[2])<1e-4f)break;
    }
    fprintf(trace,"phenac_ring_corner wes=%.4f,%.4f,%.4f\n",(double)s->hero.position[0],(double)s->hero.position[1],(double)s->hero.position[2]);
    CHECK(fabsf(s->hero.position[0]-corner[0])<.01f&&fabsf(s->hero.position[2]-corner[1])<.01f&&fabsf(s->hero.position[1]-2.0f)<.05f);
    s->hero.motion=0;for(unsigned i=0;i<20;++i)RING_TICK(s->hero.yaw);
    /* The railing holds him: west, north-west, north (the console's sticks). */
    static const float blocked[]={4.712389f,3.627f,3.1415927f};
    for(unsigned b=0;b<3;++b){for(unsigned i=0;i<17;++i)RING_TICK(blocked[b]);
        CHECK(fabsf(s->hero.position[0]-corner[0])<.01f&&fabsf(s->hero.position[2]-corner[1])<.01f);}
    /* South and east leave the corner. */
    for(unsigned i=0;i<8;++i)RING_TICK(0.0f);
    CHECK(s->hero.position[2]-corner[1]>.3f);
    /* Rui inside his cylinder (0.3 south or east of him) and held still (no
     * story tick, so she cannot step aside): every step away from her still
     * leaves the corner. */
    static const struct { float rx, rz, yaw; } pin[]={
        {0,.3f,1.5707964f},{0,.3f,2.356194f},{0,.3f,1.9f},{.3f,0,0.0f},{.3f,0,-.6f},{.2f,.2f,2.356194f}};
    for(unsigned a=0;a<sizeof pin/sizeof pin[0];++a){
        s->hero.position[0]=corner[0];s->hero.position[2]=corner[1];s->hero.position[1]=2.0003f;
        CHECK(c3_field_ground(&s->map,s->hero.position,&g));s->hero.position[1]=g;
        rui->pose.position[0]=corner[0]+pin[a].rx;rui->pose.position[2]=corner[1]+pin[a].rz;rui->pose.position[1]=g;
        float start[3];memcpy(start,s->hero.position,sizeof start);
        for(unsigned i=0;i<8;++i)ring_step(s,pin[a].yaw,dts[i%3]);
        float moved=hypotf(s->hero.position[0]-start[0],s->hero.position[2]-start[2]);
        fprintf(trace,"phenac_ring_unpin rui=+%.1f,+%.1f yaw=%.3f moved=%.3f gap=%.3f\n",(double)pin[a].rx,(double)pin[a].rz,(double)pin[a].yaw,
                (double)moved,(double)gap(&s->hero,&rui->pose));
        CHECK(moved>.3f);
    }
    #undef RING_TICK
    close_floor(s);free(s);c3_title_close(&rui_model);c3_game_state_free(&game);
    printf("phenac-events ring corner: railing corner held, never inside Rui (closest %.3f), unpinned from an overlap\n",(double)closest);
}
static void talk_sweep(void)
{
    static const char *const sweep_saves[]={EARLY,MID_PYRITE,LATE};
    static const uint16_t floors[]={3,4,5,6,7,8,9,10,11,12,13,0x77,0x80};
    static uint8_t before[FLAGS],after[FLAGS];
    unsigned talks=0,absent=0,gaps=0;
    for(unsigned si=0;si<3;++si)for(unsigned fi=0;fi<sizeof floors/sizeof floors[0];++fi){
        Scene probe={0};C3GameState g={0};uint32_t scripts[C3_AREA_MAX_ACTORS],indices[C3_AREA_MAX_ACTORS];unsigned n=0;
        const C3FloorProfile *profile=c3_floor_profile(floors[fi]);char name[96];
        snprintf(name,sizeof name,"%s.c3a",profile->prefix);if(!present(root,name))continue;
        load_state(&g,sweep_saves[si],floors[fi],0);
        CHECK(open_floor(&probe,&g,floors[fi],0));
        for(uint32_t i=0;i<probe.area.actor_count;++i)if(probe.area.actors[i].talk_script){
            C3StoryActor *a=c3_field_story_actor(&probe.story,probe.area.actors[i].index);
            if(a&&a->visible){scripts[n]=probe.area.actors[i].talk_script;indices[n++]=probe.area.actors[i].index;}else ++absent;}
        close_floor(&probe);c3_game_state_free(&g);
        for(unsigned i=0;i<n;++i){
            Event e={.name="talk",.save=sweep_saves[si],.floor=floors[fi],.spawn=0,.action=TALK,.arg=scripts[i]};
            C3GameState game={0};int ok=play(&e,before,after,&game),gap=0;c3_game_state_free(&game);
            for(size_t k=0;k<sizeof TALK_GAPS/sizeof TALK_GAPS[0];++k)gap|=TALK_GAPS[k].floor==floors[fi]&&TALK_GAPS[k].script==scripts[i];
            fprintf(trace,"phenac_talk save=%s floor=%03x actor=%lu script=%08lx status=%s%s%s\n",sweep_saves[si],floors[fi],
                    (unsigned long)indices[i],(unsigned long)scripts[i],ok?"ok":gap?"known_gap":"failed",ok?"":" reason=",ok?"":error);
            if(!ok&&!gap)fprintf(stderr,"talk floor %03x script %08lx (%s): %s\n",floors[fi],(unsigned long)scripts[i],sweep_saves[si],error);
            CHECK(ok||gap);
            ++talks;gaps+=!ok;
        }
    }
    printf("phenac-events talks: %u conversations ended, %u known gap(s), %u people absent at those points\n",talks-gaps,gaps,absent);
}

int main(int argc,char **argv)
{
    if(argc<4){printf("phenac-events: private integration skipped (needs <newgame dir> <phenac dir> <save dir>)\n");return 0;}
    newgame=argv[1];root=argv[2];saves=argv[3];
    if(argc>4&&!strcmp(argv[4],"--explore")){trace=stdout;return explore(argc-5,argv+5);}
    trace=argc>4?fopen(argv[4],"w"):tmpfile();CHECK(trace);
    CHECK(route_battles_open(error,sizeof error));
    static uint8_t before[FLAGS],after[FLAGS];
    for(size_t i=0;i<sizeof EVENTS/sizeof EVENTS[0];++i){
        const Event *e=EVENTS+i;C3GameState game={0};
        int ok=play(e,before,after,&game);report(e->name,before,after);
        if(!ok)fprintf(stderr,"event %s: %s\n",e->name,error);
        CHECK(ok);
        for(unsigned j=0;j<12&&e->expect[j];++j){
            if(!after[e->expect[j]]||before[e->expect[j]])fprintf(stderr,"event %s: flag %03x not written\n",e->name,e->expect[j]);
            CHECK(after[e->expect[j]]&&!before[e->expect[j]]);
        }
        unsigned expected=0;while(expected<8&&e->battles[expected])++expected;
        if(battle_count!=expected)fprintf(stderr,"event %s: %u battles, expected %u\n",e->name,battle_count,expected);
        CHECK(battle_count==expected);
        for(unsigned j=0;j<expected;++j)CHECK(battle_log[j]==e->battles[j]);
        printf("event %-24s ok\n",e->name);
        c3_game_state_free(&game);
    }
    /* Non-ASCII characters (e.g. the Pre Gym's U+2605 star) are listed in
     * the trace; the original font's glyphs draw them (bottom_ui extended
     * font). An unknown message control fails its script and the row. */
    CHECK(messages>0);
    nurse_heals();
    partner_event();
    ring_corner();
    talk_sweep();
    {unsigned played=route_battles_played(),missing=route_battles_close(trace);
     if(played)printf("phenac-events real battles: %u played\n",played);
     CHECK(!missing);}
    printf("phenac-events private integration: %u events, %u messages (%u with non-ASCII), %u checks passed\n",
           (unsigned)(sizeof EVENTS/sizeof EVENTS[0]),messages,odd_glyphs,checks);
    return 0;
}
