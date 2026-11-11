#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/floor_profile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional PRIVATE-DATA integration test for Phenac City (floor 3). It runs
 * the original field bank 013E0E00 through the production adapter with the
 * privately prepared packs. It renders nothing, plays no battle and reads no
 * completed save: every run creates a fresh WES game and states which
 * Outskirt flags it presets. Without arguments it only checks the profile. */
static unsigned checks;
static char error[C3_STORY_ERROR];
static FILE *trace;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)

typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, actors[C3_AREA_MAX_ACTORS];
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
} Scene;

static void path(char out[1024],const char *root,const char *name)
{ CHECK(snprintf(out,1024,"%s/%s",root,name)>0&&strlen(out)<1023); }
static uint32_t flag(C3GameState *game,uint32_t id)
{ uint32_t value=0;CHECK(c3_game_state_get_flag(game,id,&value));return value; }
static int logged(const char *text)
{
    fpos_t position;char line[1024];int found=0;
    CHECK(fflush(trace)==0&&fgetpos(trace,&position)==0);rewind(trace);
    while(fgets(line,sizeof line,trace))if(strstr(line,text)){found=1;break;}
    CHECK(!ferror(trace)&&fsetpos(trace,&position)==0);return found;
}
static int reachable(uint32_t floor){return floor>=1&&floor<=3;}
static unsigned gifts;static uint32_t gift_item,gift_count;
static int receive(void *context,uint32_t item,uint32_t count,char *name,size_t capacity)
{
    (void)context;++gifts;gift_item=item;gift_count=count;snprintf(name,capacity,"TEST ITEM");return 0;
}
static void tick(Scene *s,unsigned frame)
{
    const C3FieldStoryView *v=c3_field_story_view(&s->story);
    uint16_t pressed=v->dialogue&&frame%15==0?0x100:0;
    CHECK(c3_field_story_tick(&s->story,1.0f/60.0f,pressed,error));
    CHECK(c3_field_story_sample(&s->story,error));
}

static int present(const char *root,const char *name)
{
    char file[1024];FILE *f;path(file,root,name);f=fopen(file,"rb");if(f)fclose(f);return f!=NULL;
}
/* Diagnostic pass over another prepared floor: load its packs, run the
 * original load/init/pre scripts, enter and tick ten seconds. A failure is
 * recorded with its reason (typically an unported native) and does not stop
 * the run; it shows what the floor still needs. Returns 1 when the floor
 * reached control or kept running without an error. */
static int smoke(C3GameState *game,const char *root,uint32_t floor,uint32_t previous)
{
    const C3FloorProfile *profile=c3_floor_profile(floor);Scene *s=calloc(1,sizeof *s);char file[1024],name[96];
    int ok=0;unsigned frame=0;
    CHECK(profile&&s);
    snprintf(name,sizeof name,"%s.c3f",profile->prefix);
    if(!present(root,name)){free(s);return -1;}
    error[0]=0;
    CHECK(c3_game_state_set_floor(game,floor,previous,0));
    path(file,root,name);
    if(!c3_field_load(&s->map,file,error,sizeof error))goto done;
    snprintf(name,sizeof name,"%s.c3a",profile->prefix);path(file,root,name);
    if(!c3_area_load(&s->area,file,error,sizeof error))goto done;
    snprintf(name,sizeof name,"%s_wes.c3t",profile->prefix);path(file,root,name);
    if(!c3_title_load(&s->wes,file,error,sizeof error))goto done;
    for(uint32_t i=0;i<s->area.actor_count;++i){
        if(!s->area.actors[i].file[0])continue;
        path(file,root,s->area.actors[i].file);
        if(!c3_title_load(s->actors+i,file,error,sizeof error))goto done;
        s->models[i]=s->actors+i;
    }
    snprintf(name,sizeof name,"%s_camera.c3c",profile->prefix);
    if(present(root,name)){path(file,root,name);if(!c3_area_camera_load(&s->camera,file,error,sizeof error))goto done;}
    if(!c3_field_spawn(&s->map,0,&s->hero)){snprintf(error,sizeof error,"entrance 0 is unavailable");goto done;}
    path(file,root,profile->story);
    if(!c3_field_story_load(&s->story,file,game,&s->map,&s->area,&s->hero,&s->wes,s->models,&s->camera,trace,error))goto done;
    c3_field_story_set_floor_support(&s->story,reachable);
    c3_field_story_enable_name_entry(&s->story,1);
    c3_field_story_set_item_service(&s->story,receive,NULL);
    if(!c3_field_story_prepare(&s->story,error))goto done;
    for(frame=0;frame<3600&&!c3_field_story_view(&s->story)->prepared;++frame)
        if(!c3_field_story_tick(&s->story,1.0f/60.0f,0,error))goto done;
    if(!c3_field_story_view(&s->story)->prepared){snprintf(error,sizeof error,"preparation did not finish");goto done;}
    if(!c3_field_story_enter(&s->story,error))goto done;
    for(frame=0;frame<600;++frame){
        const C3FieldStoryView *v=c3_field_story_view(&s->story);
        uint16_t pressed=v->dialogue&&frame%15==0?0x100:0;
        if(c3_field_story_battle_pending(&s->story,NULL)&&!c3_field_story_battle_result(&s->story,2,error))goto done;
        if(!c3_field_story_tick(&s->story,1.0f/60.0f,pressed,error)||!c3_field_story_sample(&s->story,error))goto done;
    }
    ok=1;
done:
    fprintf(trace,"floor_smoke floor=%02lx prefix=%s status=%s actors=%lu transitions=%lu interactions=%lu control=%d frames=%u reason=%s\n",
            (unsigned long)floor,profile->prefix,ok?"ok":"failed",(unsigned long)s->area.actor_count,
            (unsigned long)s->area.transition_count,(unsigned long)s->area.interaction_count,
            ok?c3_field_story_view(&s->story)->control:0,frame,ok?"":error);
    printf("floor %02lx %-18s %s %s\n",(unsigned long)floor,profile->prefix,ok?"ok    ":"FAILED",ok?"":error);
    c3_field_story_close(&s->story);c3_field_close(&s->map);c3_area_close(&s->area);c3_area_camera_close(&s->camera);
    c3_title_close(&s->wes);for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(s->actors+i);
    free(s);error[0]=0;return ok;
}

int main(int argc,char **argv)
{
    const C3FloorProfile *profile=c3_floor_profile(3);
    CHECK(profile&&!strcmp(profile->prefix,"phenac")&&!strcmp(profile->story,"phenac.c3s")&&profile->features==C3_FLOOR_ROOM_CAMERA);
    CHECK(c3_floor_profile(C3_FLOOR_WORLD_MAP_ID)&&(c3_floor_profile(C3_FLOOR_WORLD_MAP_ID)->features&C3_FLOOR_WORLD_MAP));
    CHECK(!c3_floor_profile(0)&&!c3_floor_profile(0x7fff));
    /* Every Phenac floor of the lifecycle table has exactly one profile with
     * its own prefix and story pack. */
    {static const uint16_t floors[]={3,4,5,6,7,8,9,10,11,12,13,0x77};
     for(unsigned i=0;i<sizeof floors/sizeof floors[0];++i){
         const C3FloorProfile *a=c3_floor_profile(floors[i]);
         CHECK(a&&a->floor==floors[i]&&!strncmp(a->prefix,"phenac",6)&&strstr(a->story,a->prefix)==a->story&&a->label[0]);
         for(unsigned j=0;j<i;++j)CHECK(strcmp(a->prefix,c3_floor_profile(floors[j])->prefix));
     }}
    if(argc<3){printf("phenac-story: %u profile checks passed; private integration skipped (needs <newgame dir> <phenac dir>)\n",checks);return 0;}
    trace=argc>3?fopen(argv[3],"w+"):tmpfile();CHECK(trace!=NULL);
    char file[1024];path(file,argv[1],"newgame.c3g");
    C3GameState game={0};C3GameSummary summary;Scene *s=calloc(1,sizeof *s);CHECK(s);
    CHECK(c3_game_state_new(&game,"WES",0x12345678,file,error));
    /* Audit checkpoint: the flags the original Outskirt scripts have written
     * by the time the world map links to Phenac (tests/test_field_story.c
     * observes each of them being written by those scripts). */
    static const uint16_t preset[]={0x8d0,0xaf8,0x3da,0x3db,0xa05,0xace,0x3dc,0x3dd,0x3de,0xa5a,0x3df};
    for(unsigned i=0;i<sizeof preset/sizeof preset[0];++i)CHECK(c3_game_state_set_flag(&game,preset[i],1));
    CHECK(c3_game_state_set_floor(&game,3,C3_FLOOR_WORLD_MAP_ID,0));
    fprintf(trace,"phenac_checkpoint fresh_game=1 outskirt_flags_preset=%u floor=3 previous=038e spawn=0\n",
            (unsigned)(sizeof preset/sizeof preset[0]));

    path(file,argv[2],"phenac.c3f");CHECK(c3_field_load(&s->map,file,error,sizeof error));
    path(file,argv[2],"phenac.c3a");CHECK(c3_area_load(&s->area,file,error,sizeof error));
    CHECK(s->map.floor==3&&s->area.floor==3&&s->area.group==0x1f);
    path(file,argv[2],"phenac_wes.c3t");CHECK(c3_title_load(&s->wes,file,error,sizeof error));
    unsigned models=0;
    for(uint32_t i=0;i<s->area.actor_count;++i){
        if(!s->area.actors[i].file[0])continue;
        path(file,argv[2],s->area.actors[i].file);
        CHECK(c3_title_load(s->actors+i,file,error,sizeof error));s->models[i]=s->actors+i;++models;
    }
    fprintf(trace,"phenac_packs actors=%lu models=%u transitions=%lu interactions=%lu links=%lu\n",
            (unsigned long)s->area.actor_count,models,(unsigned long)s->area.transition_count,
            (unsigned long)s->area.interaction_count,(unsigned long)s->area.link_count);
    path(file,argv[2],"phenac_camera.c3c");CHECK(c3_area_camera_load(&s->camera,file,error,sizeof error));
    CHECK(s->camera.resource==0x0c101800u);
    CHECK(c3_field_spawn(&s->map,0,&s->hero));
    path(file,argv[2],"phenac.c3s");
    CHECK(c3_field_story_load(&s->story,file,&game,&s->map,&s->area,&s->hero,&s->wes,s->models,&s->camera,trace,error));
    c3_field_story_set_floor_support(&s->story,reachable);
    c3_field_story_enable_name_entry(&s->story,1);
    c3_field_story_set_item_service(&s->story,receive,NULL);
    CHECK(c3_field_story_prepare(&s->story,error));
    unsigned frame;
    for(frame=0;frame<3600&&!c3_field_story_view(&s->story)->prepared;++frame)
        CHECK(c3_field_story_tick(&s->story,1.0f/60.0f,0,error));
    CHECK(c3_field_story_view(&s->story)->prepared);
    fprintf(trace,"phenac_prepared frames=%u\n",frame);
    CHECK(c3_field_story_enter(&s->story,error));

    /* First arrival: main 01000007 sees 03E0 clear and runs function 5, the
     * original arrival event, up to battle native 98 with encounter 14. */
    uint32_t encounter=0;
    for(frame=0;frame<60000&&!c3_field_story_battle_pending(&s->story,&encounter);++frame)tick(s,frame);
    CHECK(frame<60000&&encounter==14&&flag(&game,0x3e2)==1&&flag(&game,0x3e3)==0&&flag(&game,0x3e1)==0);
    fprintf(trace,"phenac_arrival_battle_request encounter=14 frames=%u flag03E2=1\n",frame);
    /* The reported result resumes the same event; it ends by setting 03E1. */
    CHECK(c3_field_story_battle_result(&s->story,2,error));
    for(frame=0;frame<60000&&!flag(&game,0x3e1);++frame)tick(s,frame);
    CHECK(frame<60000&&flag(&game,0x3e3)==1&&flag(&game,0x3e1)==1);
    fprintf(trace,"phenac_arrival_event_complete frames=%u flags03E3_03E1=1\n",frame);
    for(frame=0;frame<600&&!c3_field_story_view(&s->story)->control;++frame)tick(s,frame);
    CHECK(c3_field_story_view(&s->story)->control&&!c3_field_story_battle_pending(&s->story,NULL));
    CHECK(c3_game_state_summary(&game,&summary)&&summary.floor==3);
    /* Rui's event (function 6) starts once the hero has talked to the sack,
     * actor 0 (talk script 01000021), from inside the original rectangle
     * x -8..10, z 314..340: that script sets bank global 7 and main then
     * calls function 6. The fixture places the hero; it does not walk. */
    {C3StoryActor *sack=c3_field_story_actor(&s->story,0);CHECK(sack&&sack->visible);
     fprintf(trace,"phenac_sack x=%.3f z=%.3f hero x=%.3f z=%.3f\n",(double)sack->pose.position[0],(double)sack->pose.position[2],
             (double)s->hero.position[0],(double)s->hero.position[2]);
     s->hero.position[0]=sack->pose.position[0];s->hero.position[2]=sack->pose.position[2]+1.0f;
     CHECK(s->hero.position[0]>=-.8f&&s->hero.position[0]<=1.0f&&s->hero.position[2]>=31.4f&&s->hero.position[2]<=34.0f);
     s->hero.yaw=3.1415927f;
     CHECK(!c3_game_state_partner(&game)&&flag(&game,0x3e4)==0&&flag(&game,0x3e5)==0);
     CHECK(c3_field_story_interact(&s->story,error));CHECK(logged("story_talk actor=0 script=01000021"));}
    /* The scene collects the partner's name when native 16 waits for it. */
    {int named=0,spoken=0;
     for(frame=0;frame<60000&&!flag(&game,0x3e5);++frame){
         if(c3_field_story_view(&s->story)->name_entry){
             CHECK(c3_field_story_view(&s->story)->name_entry==2&&!named);
             CHECK(!c3_field_story_name_result(&s->story,"",error)&&!c3_field_story_name_result(&s->story,"ELEVENCHARS",error));
             CHECK(c3_field_story_view(&s->story)->name_entry==2);
             CHECK(c3_field_story_name_result(&s->story,"Rui Two",error));named=1;
             CHECK(!c3_field_story_view(&s->story)->name_entry&&!c3_field_story_name_result(&s->story,"AGAIN",error));
         }
         /* Later messages print the new name through control 2C. */
         if(named&&strstr(c3_field_story_view(&s->story)->message,"Rui Two"))spoken=1;
         tick(s,frame);
     }
     CHECK(named&&spoken);}
    CHECK(frame<60000&&c3_game_state_partner(&game)&&flag(&game,0x3e5)==1);
    CHECK(c3_game_state_summary(&game,&summary)&&!strcmp(summary.partner,"Rui Two"));
    CHECK(logged("story_partner present=1")&&logged("story_name_entry mode=1 index=0 status=waiting_scene"));
    for(frame=0;frame<6000&&!c3_field_story_view(&s->story)->control;++frame)tick(s,frame);
    CHECK(c3_field_story_view(&s->story)->control&&flag(&game,0xacf)==1&&flag(&game,0x3e4)==1);
    fprintf(trace,"phenac_partner_event_complete partner=1 flags03E5_0ACF=1 control=1\n");
    /* The optional trainer, actor 15: main's function 40 tests native A9
     * (hero within 30 units and 15 degrees of the actor's facing), shows the
     * emote, walks the actor up with B1 and calls function 39: a YES/NO
     * choice, battle native 66 with encounter 21, flag 03EF on a win and the
     * gift of item 0x149 through native 77. */
    {C3StoryActor *trainer=c3_field_story_actor(&s->story,15);CHECK(trainer&&trainer->visible&&flag(&game,0x3ef)==0);
     s->hero.position[0]=trainer->pose.position[0]+2.0f*sinf(trainer->pose.yaw);
     s->hero.position[2]=trainer->pose.position[2]+2.0f*cosf(trainer->pose.yaw);
     s->hero.position[1]=trainer->pose.position[1];
     encounter=0;gifts=0;
     for(frame=0;frame<60000&&!c3_field_story_battle_pending(&s->story,&encounter);++frame)tick(s,frame);
     CHECK(frame<60000&&encounter==21&&logged("id=b1 argc=6")&&logged("id=e8 argc=2"));
     float dx=trainer->pose.position[0]-s->hero.position[0],dz=trainer->pose.position[2]-s->hero.position[2];
     CHECK(fabsf(sqrtf(dx*dx+dz*dz)-1.0f)<.05f); /* B1 distance 1.0 = ten source units */
     CHECK(c3_field_story_battle_result(&s->story,2,error));
     int shown=0;
     for(frame=0;frame<60000&&!(flag(&game,0x3ef)&&gifts&&c3_field_story_view(&s->story)->control);++frame){
         const C3FieldStoryView *v=c3_field_story_view(&s->story);
         if(v->message_id==0x3cb8&&strstr(v->message,"TEST ITEM")&&strstr(v->message,"WES"))shown=1;
         tick(s,frame);
     }
     CHECK(frame<60000&&gifts==1&&gift_item==0x149&&gift_count==1&&shown&&flag(&game,0x3ef)==1);
     CHECK(logged("story_item item=329 count=1 result=0"));
     fprintf(trace,"phenac_trainer15_complete encounter=21 item=0149 flag03EF=1\n");}

    c3_field_story_close(&s->story);c3_field_close(&s->map);c3_area_close(&s->area);
    c3_area_camera_close(&s->camera);
    c3_title_close(&s->wes);for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(s->actors+i);
    free(s);
    /* Report, floor by floor, how far each prepared interior gets from the
     * state reached above. Missing packs are skipped. */
    {static const uint16_t interiors[]={4,5,6,7,8,9,10,11,12,13,0x77};unsigned passed=0,failed=0,absent=0;
     for(unsigned i=0;i<sizeof interiors/sizeof interiors[0];++i){
         int result=smoke(&game,argv[2],interiors[i],3);
         if(result>0)++passed;else if(result<0)++absent;else ++failed;
     }
     fprintf(trace,"floor_smoke_summary passed=%u failed=%u absent=%u\n",passed,failed,absent);
     printf("interior floors: %u ok, %u failed, %u without packs\n",passed,failed,absent);}
    c3_game_state_free(&game);
    fprintf(trace,"phenac_complete checks=%u\n",checks);
    if(argc<=3){char buffer[4096];size_t size;rewind(trace);
        while((size=fread(buffer,1,sizeof buffer,trace)))CHECK(fwrite(buffer,1,size,stdout)==size);}
    CHECK(fclose(trace)==0);
    printf("phenac-story private integration: %u checks passed\n",checks);return 0;
}
