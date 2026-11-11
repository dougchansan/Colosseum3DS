#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/floor_profile.h"

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
static void tick(Scene *s,unsigned frame)
{
    const C3FieldStoryView *v=c3_field_story_view(&s->story);
    uint16_t pressed=v->dialogue&&frame%15==0?0x100:0;
    CHECK(c3_field_story_tick(&s->story,1.0f/60.0f,pressed,error));
    CHECK(c3_field_story_sample(&s->story,error));
}

int main(int argc,char **argv)
{
    const C3FloorProfile *profile=c3_floor_profile(3);
    CHECK(profile&&!strcmp(profile->prefix,"phenac")&&!strcmp(profile->story,"phenac.c3s")&&profile->features==C3_FLOOR_ROOM_CAMERA);
    CHECK(c3_floor_profile(C3_FLOOR_WORLD_MAP_ID)&&(c3_floor_profile(C3_FLOOR_WORLD_MAP_ID)->features&C3_FLOOR_WORLD_MAP));
    CHECK(!c3_floor_profile(0)&&!c3_floor_profile(0x7fff));
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
    (void)logged;

    c3_field_story_close(&s->story);c3_field_close(&s->map);c3_area_close(&s->area);
    c3_area_camera_close(&s->camera);
    c3_title_close(&s->wes);for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(s->actors+i);
    free(s);c3_game_state_free(&game);
    fprintf(trace,"phenac_complete checks=%u\n",checks);
    if(argc<=3){char buffer[4096];size_t size;rewind(trace);
        while((size=fread(buffer,1,sizeof buffer,trace)))CHECK(fwrite(buffer,1,size,stdout)==size);}
    CHECK(fclose(trace)==0);
    printf("phenac-story private integration: %u checks passed\n",checks);return 0;
}
