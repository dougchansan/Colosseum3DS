#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/inventory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opt-in original-pack route probe. One imported game persists across every
 * floor. The private recipe declares placements and inputs, never flag writes.
 * Every battle is explicitly reported as a synthetic win. No disk save writes.
 * usage: test_under_chapter BOOT SAVE RECIPE REPORT TRACE */
typedef struct {
    C3FieldStory story; C3FieldMap map; C3Area area; C3FieldPlayer hero;
    C3Title wes, actors[C3_AREA_MAX_ACTORS];
    C3Title *models[C3_AREA_MAX_ACTORS]; C3AreaCamera camera, cameras[8]; unsigned camera_count;
    C3AreaDoors doors; C3Title door_models[C3_AREA_MAX_DOORS]; C3Title *door_ptrs[C3_AREA_MAX_DOORS];
} Scene;
static C3GameState game;
static C3PdaAssets assets;
static Scene *scene;
static FILE *report, *trace;
static const char *boot;
static char error[C3_STORY_ERROR];
static unsigned battles;
static int supported(uint32_t floor) { return floor > 0 && floor < 0x10000; }
static void path(char out[1024], const char *name) { snprintf(out,1024,"%s/%s",boot,name); }
static int receive(void *ctx,uint32_t item,uint32_t count,char *name,size_t size)
{
    (void)ctx; C3InventoryItem info;
    if (!c3_inventory_item(&assets,item,&info)) return -1;
    if (!c3_pda_text(&assets,info.name,"WES","RUI",name,size)) snprintf(name,size,"Item %lu",(unsigned long)item);
    int result=c3_inventory_receive(&game,&assets,item,count);
    fprintf(report,"chapter_item item=%lu count=%lu result=%d real_bag=1\n",(unsigned long)item,(unsigned long)count,result);
    return result;
}
static int change(void *ctx,uint32_t item,int32_t count)
{
    (void)ctx;
    int result=count<0?c3_inventory_remove(&game,&assets,item,(unsigned)(-(int64_t)count)):c3_inventory_receive(&game,&assets,item,(unsigned)count);
    fprintf(report,"chapter_item_change item=%lu count=%ld result=%d real_bag=1\n",(unsigned long)item,(long)count,result);return result;
}
static int clear_poison(void *ctx) { (void)ctx; return c3_inventory_clear_party_poison(&game); }
static int restore(void *ctx) { (void)ctx; return c3_inventory_restore_party(&game,&assets); }
static int talk(unsigned id,unsigned placement)
{
    C3StoryActor *actor=c3_field_story_actor(&scene->story,id);
    if(!actor||!actor->visible){snprintf(error,sizeof error,"talk actor is absent");return 0;}
    uint32_t script=0;
    for(unsigned i=0;i<scene->area.actor_count;++i)if(scene->area.actors[i].index==id)script=scene->area.actors[i].talk_script;
    float yaw=actor->pose.yaw+(float)placement*0.785398163f;
    scene->hero=actor->pose;scene->hero.position[0]+=sinf(yaw)*0.35f;scene->hero.position[2]+=cosf(yaw)*0.35f;
    scene->hero.yaw=yaw+3.14159265f;memcpy(scene->hero.proposed,scene->hero.position,sizeof scene->hero.position);
    if(fflush(trace))return 0;
    long start=ftell(trace);
    if(!c3_field_story_interact(&scene->story,error)||fflush(trace))return 0;
    long end=ftell(trace);char expected[100],line[1024];int matched=0;
    snprintf(expected,sizeof expected,"story_talk actor=%u script=%08lx",id,(unsigned long)script);
    if(start<0||end<start||fseek(trace,start,SEEK_SET))return 0;
    while(ftell(trace)<end&&fgets(line,sizeof line,trace))if(strstr(line,expected))matched=1;
    if(fseek(trace,end,SEEK_SET))return 0;
    if(!matched)snprintf(error,sizeof error,"intended actor/script was not selected (adjust recipe placement)");
    fprintf(report,"chapter_talk actor=%u script=%08lx matched=%d\n",id,(unsigned long)script,matched);
    return matched;
}
static void close_scene(void)
{
    if (!scene) return;
    c3_field_story_close(&scene->story); c3_field_close(&scene->map); c3_area_close(&scene->area);
    c3_area_camera_close(&scene->camera); c3_title_close(&scene->wes);
    for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(scene->actors+i);
    for(unsigned i=0;i<C3_AREA_MAX_DOORS;++i)c3_title_close(scene->door_models+i);
    for(unsigned i=0;i<scene->camera_count;++i)c3_area_camera_close(scene->cameras+i);
    free(scene);scene=NULL;
}
static int ticks(unsigned frames,unsigned pressed)
{
    for(unsigned frame=0;frame<frames;++frame) {
        const C3FieldStoryView *v=c3_field_story_view(&scene->story);
        uint32_t encounter;
        if(c3_field_story_battle_pending(&scene->story,&encounter)) {
            fprintf(report,"chapter_battle encounter=%lu result=2 synthetic=1\n",(unsigned long)encounter); ++battles;
            if(!c3_field_story_battle_result(&scene->story,2,error))return 0;
        }
        uint16_t input=(uint16_t)pressed;
        if((v->dialogue||v->choice)&&frame%15==0)input|=0x100;
        if(v->name_entry&&!c3_field_story_name_result(&scene->story,"WES",error))return 0;
        if(v->shop&&!c3_field_story_shop_done(&scene->story,error))return 0;
        if(!c3_field_story_tick(&scene->story,1.0f/60.0f,input,error)||!c3_field_story_sample(&scene->story,error))return 0;
        if(c3_field_story_view(&scene->story)->next_floor)break;
    }
    const C3FieldStoryView *v=c3_field_story_view(&scene->story);
    fprintf(report,"chapter_state floor=%03lx control=%d dialogue=%d choice=%d next=%03lx blocked=%s\n",(unsigned long)scene->area.floor,v->control,!!v->dialogue,!!v->choice,(unsigned long)v->next_floor,v->blocked);
    return 1;
}
static int load_scene(unsigned floor,unsigned previous,unsigned spawn)
{
    char file[1024],name[80]; close_scene();scene=calloc(1,sizeof *scene);
    if(!scene){snprintf(error,sizeof error,"allocation failed");return 0;}
    if(!c3_game_state_set_floor(&game,floor,previous,(uint8_t)spawn))return 0;
    snprintf(name,sizeof name,"f%03x.c3f",floor);path(file,name);
    if(!c3_field_load(&scene->map,file,error,sizeof error))return 0;
    snprintf(name,sizeof name,"f%03x.c3a",floor);path(file,name);
    if(!c3_area_load(&scene->area,file,error,sizeof error))return 0;
    snprintf(name,sizeof name,"f%03x_wes.c3t",floor);path(file,name);
    if(!c3_title_load(&scene->wes,file,error,sizeof error))return 0;
    for(unsigned i=0;i<scene->area.actor_count;++i) {
        if(!scene->area.actors[i].file[0])continue;
        path(file,scene->area.actors[i].file);
        if(!c3_title_load(scene->actors+i,file,error,sizeof error))return 0;
        scene->models[i]=scene->actors+i;
    }
    snprintf(name,sizeof name,"f%03x_camera.c3c",floor);path(file,name);FILE *f=fopen(file,"rb");
    if(f){fclose(f);if(!c3_area_camera_load(&scene->camera,file,error,sizeof error))return 0;}
    if(!c3_field_spawn(&scene->map,(uint8_t)spawn,&scene->hero)){snprintf(error,sizeof error,"entrance unavailable");return 0;}
    snprintf(name,sizeof name,"f%03x.c3d",floor);path(file,name);f=fopen(file,"rb");
    if(f){fclose(f);if(!c3_area_doors_load(&scene->doors,file,error,sizeof error))return 0;}
    for(unsigned i=0;i<scene->doors.count;++i){if(!scene->doors.rows[i].file[0])continue;path(file,scene->doors.rows[i].file);if(!c3_title_load(scene->door_models+i,file,error,sizeof error))return 0;scene->door_ptrs[i]=scene->door_models+i;}
    for(unsigned i=1;i<=8;++i){snprintf(name,sizeof name,"f%03x_camera%u.c3c",floor,i);path(file,name);f=fopen(file,"rb");if(!f)break;fclose(f);if(!c3_area_camera_load(scene->cameras+scene->camera_count,file,error,sizeof error))return 0;++scene->camera_count;}
    snprintf(name,sizeof name,"f%03x.c3s",floor);path(file,name);
    if(!c3_field_story_load(&scene->story,file,&game,&scene->map,&scene->area,&scene->hero,&scene->wes,scene->models,&scene->camera,trace,error))return 0;
    if(!c3_field_story_bind_doors(&scene->story,scene->doors.rows,scene->doors.count,scene->door_ptrs,error))return 0;
    c3_field_story_bind_cameras(&scene->story,scene->cameras,scene->camera_count);
    c3_field_story_set_floor_support(&scene->story,supported);
    c3_field_story_set_item_service(&scene->story,receive,NULL);
    c3_field_story_set_item_change_service(&scene->story,change,NULL);
    c3_field_story_set_party_service(&scene->story,restore,NULL);
    c3_field_story_set_poison_service(&scene->story,clear_poison,NULL);
    c3_field_story_enable_name_entry(&scene->story,1);
    if(!c3_field_story_prepare(&scene->story,error))return 0;
    for(unsigned i=0;i<3600&&!c3_field_story_view(&scene->story)->prepared;++i)
        if(!c3_field_story_tick(&scene->story,1.0f/60.0f,0,error))return 0;
    if(!c3_field_story_view(&scene->story)->prepared){snprintf(error,sizeof error,"preparation timed out");return 0;}
    return c3_field_story_enter(&scene->story,error)&&ticks(120,0);
}
int main(int argc,char **argv)
{
    if(argc!=6){puts("under-chapter: private integration skipped (needs BOOT SAVE RECIPE REPORT TRACE)");return 0;}
    boot=argv[1];report=fopen(argv[4],"w");trace=fopen(argv[5],"w+");FILE *recipe=fopen(argv[3],"r");
    if(!report||!trace||!recipe)return 2;
    fprintf(report,"chapter_shortcuts placements=recipe dialogue_auto=1 choice=first battle=synthetic_win persistent_game=1 flag_grants=0 save_write=0\n");
    void *data=NULL;
    char file[1024];path(file,"newgame.c3g");
    if(!c3_game_state_load(&game,argv[2],file,error))goto fail;
    C3GameSummary summary;if(!c3_game_state_summary(&game,&summary))goto fail;
    fprintf(report,"chapter_save floor=%03lx previous=%03lx name=%s seconds=%lu party=%u\n",(unsigned long)summary.floor,(unsigned long)summary.previous_floor,summary.name,(unsigned long)summary.play_seconds,summary.party_count);
    path(file,"pda.c3pd");FILE *pda=fopen(file,"rb");if(!pda){snprintf(error,sizeof error,"private PDA pack missing");goto fail;}
    fseek(pda,0,SEEK_END);long size=ftell(pda);rewind(pda);data=size>0?malloc((size_t)size):NULL;
    if(!data||fread(data,1,(size_t)size,pda)!=(size_t)size){fclose(pda);goto fail;}fclose(pda);
    if(!c3_pda_assets_open(&assets,data,(size_t)size))goto fail;
    char line[256];unsigned line_no=0;
    while(fgets(line,sizeof line,recipe)) {
        ++line_no;if(line[0]=='#'||line[0]=='\n')continue;
        fprintf(report,"chapter_step line=%u command=%s",line_no,line);fflush(report);
        uint32_t before[0x1000]={0};for(unsigned i=0;i<0x1000;++i)c3_game_state_get_flag(&game,i,before+i);
        unsigned a,b,c;float x,y,z;int ok=0;
        if(sscanf(line,"floor %x %x %x",&a,&b,&c)==3)ok=load_scene(a,b,c);
        else if(!scene){snprintf(error,sizeof error,"recipe needs floor first");goto fail;}
        else if(sscanf(line,"ticks %u %x",&a,&b)==2)ok=ticks(a,b);
        else if(sscanf(line,"move %x",&a)==1){c3_field_story_set_move_input(&scene->story,(uint16_t)a,0,0);ok=1;}
        else if(sscanf(line,"pos %f %f %f",&x,&y,&z)==3){scene->hero.position[0]=x*scene->map.scale;scene->hero.position[1]=y*scene->map.scale;scene->hero.position[2]=z*scene->map.scale;memcpy(scene->hero.proposed,scene->hero.position,sizeof scene->hero.position);ok=1;}
        else if(sscanf(line,"talk %u %u",&a,&b)==2)ok=talk(a,b);
        else if(!strncmp(line,"interact",8)){ok=c3_field_story_interact(&scene->story,error);if(!ok&&!error[0])snprintf(error,sizeof error,"no original interactable at recipe position");}
        else if(sscanf(line,"item %x %u",&a,&b)==2){unsigned total=0;for(unsigned pocket=0;pocket<6;++pocket)for(unsigned slot=0;slot<c3_inventory_capacity(pocket);++slot){unsigned item,count;if(c3_inventory_slot(&game,pocket,slot,&item,&count)&&item==a)total+=count;}ok=total==b;fprintf(report,"chapter_bag item=%04x expected=%u actual=%u\n",a,b,total);if(!ok)snprintf(error,sizeof error,"original key item was not acquired");}
        else if(sscanf(line,"door %u",&a)==1)ok=c3_field_story_transition(&scene->story,a,error);
        else if(sscanf(line,"touch %u",&a)==1)ok=c3_field_story_touch(&scene->story,a,error)>0;
        else if(sscanf(line,"flag %x %u",&a,&b)==2){uint32_t value=0;ok=c3_game_state_get_flag(&game,a,&value)&&value==b;fprintf(report,"chapter_flag id=%04x expected=%u actual=%lu\n",a,b,(unsigned long)value);if(!ok)snprintf(error,sizeof error,"original flag did not reach expected value");}
        else if(sscanf(line,"next %x",&a)==1){unsigned value=c3_field_story_view(&scene->story)->next_floor;ok=value==a;fprintf(report,"chapter_next expected=%03x actual=%03x\n",a,value);if(!ok)snprintf(error,sizeof error,"original connector did not reach expected floor");}
        else snprintf(error,sizeof error,"unknown recipe command");
        for(unsigned i=0;i<0x1000;++i){uint32_t after;if(c3_game_state_get_flag(&game,i,&after)&&after!=before[i])fprintf(report,"chapter_changed id=%04x before=%lu after=%lu\n",i,(unsigned long)before[i],(unsigned long)after);}
        if(!ok)goto fail;
    }
    fprintf(report,"chapter_summary status=ok synthetic_battles=%u\n",battles);close_scene();c3_game_state_free(&game);free(data);fclose(recipe);fclose(trace);fclose(report);return 0;
fail:
    fprintf(report,"chapter_summary status=blocked synthetic_battles=%u reason=%s\n",battles,error);close_scene();c3_game_state_free(&game);free(data);fclose(recipe);fclose(trace);fclose(report);return 1;
}
