#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/floor_profile.h"

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
static int restore(void *c){(void)c;return 6;}
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
            fprintf(trace,"phenac_event_battle encounter=%lu reported=2\n",(unsigned long)encounter);
            if(!c3_field_story_battle_result(&s->story,2,error))return 0;
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
    battle_count=0;link_floor=0;error[0]=0;
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
    talk_sweep();
    printf("phenac-events private integration: %u events, %u messages (%u with non-ASCII), %u checks passed\n",
           (unsigned)(sizeof EVENTS/sizeof EVENTS[0]),messages,odd_glyphs,checks);
    return 0;
}
