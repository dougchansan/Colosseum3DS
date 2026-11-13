#include "field_scene.h"
#include "bottom_screen.h"
#include "game_ui_views.h"
#include "ingame_menu.h"
#include "pda_preview.h"
#include "pda_audio.h"
#include "movie_player.h"
#include "battle_preview_scene.h"
#include "colosseum3ds/battle_preview.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "stage_renderer.h"
#include "colosseum3ds/field.h"
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/first_person.h"
#include "colosseum3ds/floor_profile.h"
#include "colosseum3ds/inventory.h"
#include "colosseum3ds/name_entry.h"
#include "world_map_scene.h"
#include "colosseum3ds/area_lights.h"
#include "colosseum3ds/title.h"

#include <3ds.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BOOT_ROOT "sdmc:/3ds/Colosseum3DS/boot/"
#define SAVE_DIRECTORY "sdmc:/3ds/Colosseum3DS/saves"

static int exists(const char *path)
{
    struct stat entry;
    return stat(path,&entry)==0 && S_ISREG(entry.st_mode);
}

/* A walkable floor is supported when it has a profile and its core packs are
 * on the SD card. The world map is a separate menu-class scene. */
int c3_field_scene_supports(uint32_t floor)
{
    const C3FloorProfile *profile=c3_floor_profile(floor);
    static const char *const core[]={"%s.c3f","%s.c3a","%s_wes.c3t","%s_map0.c3t"};
    char name[96],path[200];
    if(!profile||(profile->features&C3_FLOOR_WORLD_MAP))return 0;
    for(unsigned i=0;i<sizeof core/sizeof core[0];++i){
        snprintf(name,sizeof name,core[i],profile->prefix);
        snprintf(path,sizeof path,BOOT_ROOT "%s",name);if(!exists(path))return 0;
    }
    snprintf(path,sizeof path,BOOT_ROOT "%s",profile->story);
    return exists(path);
}
/* The PDA pack holds the item and move tables and their names. Script
 * services read it for one request and release it again. */
static uint8_t *open_pda(C3PdaAssets *assets)
{
    FILE *file=fopen(BOOT_ROOT "pda.c3pd","rb");long length=0;uint8_t *data=NULL;
    if(file&&!fseek(file,0,SEEK_END)&&(length=ftell(file))>=64&&length<=8*1024*1024&&!fseek(file,0,SEEK_SET)&&
       (data=malloc((size_t)length))&&(fread(data,1,(size_t)length,file)!=(size_t)length||
       !c3_pda_assets_open(assets,data,(size_t)length))){free(data);data=NULL;}
    if(file)fclose(file);
    return data;
}
/* Item service for script native 77. */
static int receive_item(void *context,uint32_t item,uint32_t count,char *name,size_t capacity)
{
    C3GameState *state=context;C3GameSummary summary;C3PdaAssets assets={0};C3InventoryItem info;
    uint8_t *data=open_pda(&assets);int result=-1;
    if(data&&c3_inventory_item(&assets,item,&info)&&c3_game_state_summary(state,&summary)){
        result=c3_inventory_receive(state,&assets,item,count);
        if(!c3_pda_text(&assets,info.name,summary.name,summary.partner,name,capacity))
            snprintf(name,capacity,"ITEM %lu",(unsigned long)item);
    }
    free(data);return result;
}
/* Party service for script native D8. */
static int restore_party(void *context)
{
    C3PdaAssets assets={0};uint8_t *data=open_pda(&assets);
    int restored=data?c3_inventory_restore_party(context,&assets):-1;
    free(data);return restored;
}
/* Destinations the original doorway script may request from a field. */
static int floor_reachable(uint32_t floor)
{
    return floor==C3_FLOOR_WORLD_MAP_ID?c3_world_map_scene_available():c3_field_scene_supports(floor);
}

int c3_field_scene_available(void)
{
    static const char *const required[]={"outskirt.c3f","outskirt.c3a","outskirt.c3s",
        "outskirt_wes.c3t","outskirt_npc0.c3t","outskirt_npc1.c3t","outskirt_npc2.c3t","outskirt_npc3.c3t",
        "shop.c3f","shop.c3a","outskirt_shop.c3s","shop_wes.c3t",
        "shop_camera.c3c","shop.c3l","shop_tv.c3t","willie.c3b"};
    char path[160];
    for(unsigned i=0;i<sizeof required/sizeof required[0];++i){
        snprintf(path,sizeof path,BOOT_ROOT "%s",required[i]);if(!exists(path))return 0;
    }
    for(unsigned i=0;i<4;++i){snprintf(path,sizeof path,BOOT_ROOT "outskirt_map%u.c3t",i);if(!exists(path))return 0;}
    for(unsigned i=0;i<2;++i){snprintf(path,sizeof path,BOOT_ROOT "outskirt_arrival%u.c3t",i);if(!exists(path))return 0;}
    for(unsigned i=0;i<2;++i){snprintf(path,sizeof path,BOOT_ROOT "shop_map%u.c3t",i);if(!exists(path))return 0;}
    for(unsigned i=0;i<5;++i){snprintf(path,sizeof path,BOOT_ROOT "shop_npc%u.c3t",i);if(!exists(path))return 0;}
    return 1;
}

/* Optional first-person view; kept across floor loads like the recomp mod. */
static C3FirstPerson first_person;

static void hide(C3Title *model)
{
    for(uint32_t i=0;i<model->batch_count;++i)model->pose.materials[i].visible=0;
}

/* Story actors retain the C3Title addresses, never their owned payloads.
 * Only call while the VM is paused and the stage renderer is shut down. */
static void release_field_models(C3Title *storage,uint32_t count)
{
    for(uint32_t i=0;i<count;++i)c3_title_close(storage+i);
}

static int restore_field_models(C3Title *storage,uint32_t count,
                                char paths[][160],C3FieldStory *story,
                                float elapsed,
                                char *error,size_t capacity)
{
    for(uint32_t i=0;i<count;++i){
        if(!c3_title_load(storage+i,paths[i],error,capacity))return 0;
        if(!c3_title_animate(storage+i,0,elapsed)){
            snprintf(error,capacity,"Restored field animation failed.");return 0;
        }
    }
    /* Resample the preserved NPC/hero clocks, loops, visibility, attachments,
     * and transforms. Do not tick/prepare/enter or reload the story VM. */
    char story_error[C3_STORY_ERROR]={0};
    if(!c3_field_story_sample(story,story_error)){
        snprintf(error,capacity,"%s",story_error);return 0;
    }
    return 1;
}


static int save_game(C3GameState *state,const char *path,FILE *trace,char *message,size_t capacity)
{
    char error[C3_GAME_ERROR_SIZE]={0};C3GameSummary summary;
    if(mkdir(SAVE_DIRECTORY,0777)!=0 && errno!=EEXIST) {
        snprintf(message,capacity,"The save directory could not be created.");return 0;
    }
    int ok=c3_game_state_save(state,path,error);
    if(ok && c3_game_state_summary(state,&summary)) {
        const uint8_t *body=c3_game_state_body(state,NULL);
        snprintf(message,capacity,"%s saved the game.\nSave number %lu.\n\nContinue starts at the area entrance.",
                 summary.name,(unsigned long)summary.saved_count);
        if(trace)fprintf(trace,"game_save status=ok name=%s count=%lu floor=%lu previous=%lu spawn=%u time=%lu time_bits=%02x%02x%02x%02x\n",
                         summary.name,(unsigned long)summary.saved_count,(unsigned long)summary.floor,
                         (unsigned long)summary.previous_floor,summary.spawn,(unsigned long)summary.play_seconds,
                         body[0x20],body[0x21],body[0x22],body[0x23]);
    } else {
        snprintf(message,capacity,"The game could not be saved.\n%s",error);
        if(trace)fprintf(trace,"game_save status=failed reason=%s\n",error);
    }
    if(trace)fflush(trace);
    return ok;
}

/* Every stage renderer init of the field. Wes and the area's NPCs (bit per
 * model in `actors`) get a GPU skin stream; their supported batches are then
 * skinned and lit by the GPU on every frame field_story retains them. Map
 * packs are static and staged once; the TV and arrival parts stay on the CPU.
 * models[i] is storage+i for every field model. */
static int field_renderer_init(C3Title *storage,const C3Title *const *models,uint32_t count,
                               uint64_t actors,char *error,size_t capacity)
{
#ifndef C3_NO_GPU_SKIN
    c3_stage_renderer_request_gpu_skin(actors);
#else
    (void)actors;
#endif
    if(c3_stage_renderer_init(models,count,error,capacity))return 1;
#ifndef C3_NO_GPU_SKIN
    for(uint32_t i=0;i<count;++i)storage[i].gpu_batches=c3_stage_renderer_gpu_skin_mask(i);
#else
    (void)storage;
#endif
    return 0;
}

/* Return 2 only after a real source doorway and its post callback finish. */
static int run_area(C3GameState *state,int new_game,const char *save_path,
                    FILE *trace,char *error,size_t capacity)
{
    C3FieldMap map={0};C3Area area={0};C3FieldPlayer player={0};C3GameSummary summary;
    C3FieldStory story={0};C3AreaLights lights={0};C3AreaCamera room_camera={0};
    C3Title *storage=NULL;const C3Title *models[C3_STAGE_MAX_MODELS]={0};
    C3Title *actor_models[C3_AREA_MAX_ACTORS]={0};
    C3AreaDoors doors={0};C3Title *door_models[C3_AREA_MAX_DOORS]={0};
    char model_paths[C3_STAGE_MAX_MODELS][160]={{0}};
    C3TitleCamera camera;C3TitleRenderStats stats={0};C3N3dsInput input={0};
    uint32_t opened=0,count=0,arrival_done=0,opening_done=0,arrival_count=0;
    unsigned wes_index=0,arrival_index=0,tv_index=UINT32_MAX,selection=0,frames=0;
    int result=-1,yes=0,dirty=1,arrival_end_sampled=0,entered=0,lights_loaded=0;
    FieldPhase phase=FIELD_SETUP;PausePage page=PAUSE_CLOSED;
    float elapsed=0,phase_seconds=0,arrival_seconds=0;
    uint64_t previous=0,next_console=0,next_trace=0,skin_actors=0;
    C3UiPager ui_pager={0};
    C3NameEntry name_entry;int naming=0;
    C3IngameMenu *ingame=calloc(1,sizeof *ingame);
    C3PdaPreview *pda_preview=NULL;
    float pda_seconds=0;
    int pda_audio_ready=0;
    char path[160],message[256]={0},story_error[C3_STORY_ERROR]={0};
    const C3FieldStoryView *view=NULL;
    if(!ingame){snprintf(error,capacity,"In-game menu allocation failed.");return -1;}
    if(!state||!save_path||!c3_game_state_summary(state,&summary)||!c3_field_scene_supports(summary.floor)){
        snprintf(error,capacity,"The saved location is not available in this build.");free(ingame);return -1;
    }
    const C3FloorProfile *profile=c3_floor_profile(summary.floor);
    const char *prefix=profile->prefix;
    snprintf(path,sizeof path,BOOT_ROOT "%s.c3f",prefix);
    if(!c3_field_load(&map,path,error,capacity))goto done;
    snprintf(path,sizeof path,BOOT_ROOT "%s.c3a",prefix);
    if(!c3_area_load(&area,path,error,capacity))goto done;
    if(map.floor!=summary.floor||area.floor!=map.floor||map.actor_bone!=UINT32_MAX||map.actor_first_vertex!=0u){
        snprintf(error,capacity,"The field and interaction packs disagree.");goto done;
    }
    if(!c3_game_state_get_flag(state,0x08D0,&opening_done)||!c3_game_state_get_flag(state,0x0AF8,&arrival_done)){
        snprintf(error,capacity,"The original field progress flags are unavailable.");goto done;
    }
    if(!c3_field_spawn(&map,summary.spawn,&player)){
        snprintf(error,capacity,"The saved area entrance is unavailable.");goto done;
    }
    arrival_count=(profile->features&C3_FLOOR_ARRIVAL)&&opening_done&&!arrival_done?map.arrival_pack_count:0;
    count=map.map_pack_count+1u+arrival_count;wes_index=map.map_pack_count;arrival_index=wes_index+1u;
    for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0])++count;
    /* The floor's door table (native 60) and the object pack of each door
     * that has one; those rest on their first frame until a script plays a
     * clip. A door without a pack changes only its open flag and collision,
     * as the original does for a door without a model. */
    snprintf(path,sizeof path,BOOT_ROOT "%s.c3d",prefix);
    if(exists(path)&&!c3_area_doors_load(&doors,path,error,capacity))goto done;
    for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0])++count;
    if(profile->features&C3_FLOOR_TV)tv_index=count++;
    if(count>C3_STAGE_MAX_MODELS){snprintf(error,capacity,"The area exceeds the model bound.");goto done;}
    storage=calloc(count,sizeof(*storage));
    if(!storage){snprintf(error,capacity,"Field model allocation failed.");goto done;}
    c3_bottom_screen_notice(profile->label,"Loading the area...","Please wait...");
    for(uint32_t i=0;i<map.map_pack_count+1u+arrival_count;++i){
        if(i<map.map_pack_count)snprintf(path,sizeof path,BOOT_ROOT "%s_map%lu.c3t",prefix,(unsigned long)i);
        else if(i==wes_index)snprintf(path,sizeof path,BOOT_ROOT "%s_wes.c3t",prefix);
        else snprintf(path,sizeof path,BOOT_ROOT "%s_arrival%lu.c3t",prefix,(unsigned long)(i-arrival_index));
        if(!c3_title_load(storage+i,path,error,capacity))goto done;
        snprintf(model_paths[i],sizeof model_paths[i],"%s",path);
        ++opened;models[i]=storage+i;
        if(!c3_title_animate(storage+i,0,0)){snprintf(error,capacity,"Initial field animation failed.");goto done;}
    }
    for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0]){
        snprintf(path,sizeof path,BOOT_ROOT "%s",area.actors[i].file);
        C3Title *model=storage+opened;
        if(!c3_title_load(model,path,error,capacity))goto done;
        snprintf(model_paths[opened],sizeof model_paths[opened],"%s",path);
        models[opened++]=model;actor_models[i]=model;
        if(!c3_title_animate(model,area.actors[i].motions[0],0)){snprintf(error,capacity,"Original NPC idle motion is missing.");goto done;}
    }
    for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0]){
        snprintf(path,sizeof path,BOOT_ROOT "%s",doors.rows[i].file);
        C3Title *model=storage+opened;
        if(!c3_title_load(model,path,error,capacity))goto done;
        snprintf(model_paths[opened],sizeof model_paths[opened],"%s",path);
        models[opened++]=model;door_models[i]=model;
        int rest=doors.rows[i].open_anim>=0?doors.rows[i].open_anim:0;
        if(!c3_title_animate(model,(uint32_t)rest,0)){snprintf(error,capacity,"Original door clip is missing.");goto done;}
    }
    C3Title *wes=storage+wes_index,*arrival=arrival_count?storage+arrival_index:NULL;
    if(wes->vertex_count!=map.actor_vertex_count||wes->clip_count<4u){
        snprintf(error,capacity,"The prepared Wes motions have an unsupported layout.");goto done;
    }
    if(arrival){
        if(arrival->clip_count!=1u||arrival->clips[0].loop||arrival->clips[0].frame_count<2u){
            snprintf(error,capacity,"The arrival motion has an unsupported layout.");goto done;
        }
        arrival_seconds=(float)(arrival->clips[0].frame_count-1u)/arrival->fps;
        for(uint32_t i=1;i<arrival_count;++i){C3Title *part=storage+arrival_index+i;
            if(part->clip_count!=1u||part->clips[0].loop||part->clips[0].frame_count!=arrival->clips[0].frame_count||part->fps!=arrival->fps){
                snprintf(error,capacity,"The arrival model timelines disagree.");goto done;
            }
        }
    }
    if(profile->features&C3_FLOOR_TV){
        snprintf(path,sizeof path,BOOT_ROOT "%s_tv.c3t",prefix);
        if(!c3_title_load(storage+tv_index,path,error,capacity))goto done;
        snprintf(model_paths[tv_index],sizeof model_paths[tv_index],"%s",path);
        models[opened++]=storage+tv_index;
        if(!c3_title_animate(storage+tv_index,0,0))goto done;
    }
    if(profile->features&C3_FLOOR_ROOM_CAMERA){
        snprintf(path,sizeof path,BOOT_ROOT "%s_camera.c3c",prefix);
        if(!c3_area_camera_load(&room_camera,path,error,capacity))goto done;
    }
    if(profile->features&C3_FLOOR_LIGHTS){
        snprintf(path,sizeof path,BOOT_ROOT "%s.c3l",prefix);
        if(!c3_area_lights_load(&lights,path,error,capacity))goto done;
        lights_loaded=1;
    }
    if(opened!=count){snprintf(error,capacity,"Area model identity mismatch.");goto done;}
    if(trace){
        size_t cpu_models=0;
        for(uint32_t i=0;i<count;++i)cpu_models+=storage[i].resident_bytes;
        fprintf(trace,"field_models floor=%lu models=%lu cpu_resident_bytes=%lu\n",
                (unsigned long)map.floor,(unsigned long)count,(unsigned long)cpu_models);
        fflush(trace);
    }
    skin_actors=UINT64_C(1)<<wes_index;
    for(uint32_t i=arrival_index+arrival_count;i<count&&i<64u;++i)if(i!=tv_index)skin_actors|=UINT64_C(1)<<i;
    if(field_renderer_init(storage,models,count,skin_actors,error,capacity))goto done;
    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,true,error,capacity))goto done;
    snprintf(path,sizeof path,"%s_ready",prefix);
    c3_n3ds_log_memory(trace,path);
    snprintf(path,sizeof path,BOOT_ROOT "%s",profile->story);
    if(!c3_field_story_load(&story,path,state,&map,&area,&player,wes,actor_models,&room_camera,trace,story_error)||
       !c3_field_story_bind_battle(&story,BOOT_ROOT "willie.c3b",story_error))goto story_failed;
    if(doors.count){
        uint32_t with_model=0;for(uint32_t i=0;i<doors.count;++i)with_model+=door_models[i]!=NULL;
        if(!c3_field_story_bind_doors(&story,doors.rows,doors.count,door_models,story_error))goto story_failed;
        if(trace){fprintf(trace,"field_doors floor=%lu rows=%lu models=%lu\n",(unsigned long)map.floor,(unsigned long)doors.count,(unsigned long)with_model);fflush(trace);}
    }
    c3_field_story_set_floor_support(&story,floor_reachable);
    c3_field_story_enable_name_entry(&story,1);
    c3_field_story_set_item_service(&story,receive_item,state);
    c3_field_story_set_party_service(&story,restore_party,state);
    if(!c3_field_story_prepare(&story,story_error))goto story_failed;
    view=c3_field_story_view(&story);
    if(trace){fprintf(trace,"field_begin floor=%lu previous=%lu spawn=%u new=%d maps=%lu npcs=%lu models=%lu arrival=%d duration=%.3f\n",
        (unsigned long)map.floor,(unsigned long)summary.previous_floor,summary.spawn,new_game,(unsigned long)map.map_pack_count,
        (unsigned long)area.actor_count,(unsigned long)count,arrival!=NULL,(double)arrival_seconds);fflush(trace);}
    previous=c3_n3ds_active_time_us();result=1;
    while(c3_n3ds_poll(&input)){
        uint64_t now=c3_n3ds_active_time_us();float delta=(float)(now-previous)/1000000.0f;
        previous=now;elapsed+=delta;phase_seconds+=delta;
        c3_game_state_add_play_time(state,delta);
        int story_consumed_input=view->dialogue||!view->control;
        /* Script native 16: the partner is named on the bottom-screen
         * keyboard while the event waits. The keyboard owns all input. */
        if(view->name_entry&&!naming&&page==PAUSE_CLOSED&&c3_game_state_summary(state,&summary)){
            c3_name_entry_init(&name_entry,summary.partner);naming=1;dirty=1;
            if(trace){fprintf(trace,"name_entry_open mode=%lu\n",(unsigned long)(view->name_entry-1u));fflush(trace);}
        }
        if(naming){
            C3NameEvent typed=c3_name_entry_update(&name_entry,input.pressed,input.touch_held,input.touch_pressed,
                                                   input.touch_released,input.touch_x,input.touch_y);
            if(typed!=C3_NAME_NONE)dirty=1;
            if(typed==C3_NAME_DONE){
                if(!c3_field_story_name_result(&story,name_entry.text,story_error))goto story_failed;
                if(trace){fprintf(trace,"name_entry_done length=%u\n",name_entry.length);fflush(trace);}
                naming=0;
            }
            input.pressed=0;input.touch_pressed=false;
        }
        /* Stylus on the bottom screen: a tap advances the open message like
         * A. On a YES/NO choice a tap on either answer selects and confirms
         * it (the choice box is drawn at x 180..310, YES above y 189). */
        if(input.touch_pressed&&page==PAUSE_CLOSED&&(view->dialogue||view->blocked[0])){
            if(!view->choice)input.pressed|=C3_PAD_A;
            else if(input.touch_x>=180&&input.touch_x<310&&input.touch_y>=165&&input.touch_y<215){
                int want_no=input.touch_y>=189;
                if(want_no!=view->choice_no)input.pressed|=C3_PAD_UP;
                input.pressed|=C3_PAD_A;
            }
        }
        /* Page locally before the VM sees A/B; do not acknowledge text that
         * has not been displayed. Reset on message and pause-page identity. */
        c3_ui_pager_sync(&ui_pager,c3_field_ui_identity(page,view));
        if(c3_ui_pager_advance(&ui_pager,&input.pressed))dirty=1;
        else if(input.pressed&(C3_PAD_A|C3_PAD_B))ui_pager.offset=0;

        if(page==PAUSE_CLOSED){
            if(!c3_field_story_tick(&story,delta,phase==FIELD_CONTROL?input.pressed:0,story_error))goto story_failed;
            if(view->next_floor){
                if(view->next_spawn>255||!c3_game_state_set_floor(state,view->next_floor,map.floor,(uint8_t)view->next_spawn)){
                    snprintf(error,capacity,"Original doorway checkpoint is invalid.");result=-1;break;
                }
                result=2;break;
            }
        }
        float fade=view->fade;
        if(phase==FIELD_SETUP&&view->prepared){phase=arrival?FIELD_ARRIVAL:FIELD_FADE_IN;phase_seconds=0;dirty=1;}
        if(phase==FIELD_ARRIVAL&&phase_seconds>=arrival_seconds){
            phase=FIELD_ARRIVAL_OUT;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_arrival camera_end=1 actors_closed=1\n");fflush(trace);}
        }
        if(phase==FIELD_ARRIVAL_OUT&&phase_seconds>=1){
            if(!c3_game_state_set_flag(state,0x0AF8,1)){snprintf(error,capacity,"The arrival completion flag could not be set.");result=-1;break;}
            arrival_done=1;phase=FIELD_FADE_IN;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_arrival_complete flag=0af8 camera_mode=0 target=0:100\n");fflush(trace);}
        }
        if(phase==FIELD_FADE_IN&&!entered){
            if(!c3_field_story_enter(&story,story_error))goto story_failed;
            entered=1;
        }
        if(phase==FIELD_FADE_IN&&phase_seconds>=(arrival?1.0f:.5f)){
            phase=FIELD_CONTROL;phase_seconds=0;dirty=1;
        }
        if(phase==FIELD_CONTROL){
            if(page==PAUSE_CLOSED){
                if(view->blocked[0]&&(input.pressed&(C3_PAD_START|C3_PAD_X))){result=0;break;}
                if(view->blocked[0]&&view->battle_id&&view->battle_id!=10&&(input.pressed&C3_PAD_A)){
                    /* Temporary shortcut: only encounter 10 has a battle
                     * scene. Any other requested encounter is reported as
                     * the hero's win (result ID 2) so the original script
                     * can continue; nothing is written to the party. */
                    uint32_t skipped=view->battle_id;
                    if(!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
                    if(trace){fprintf(trace,"field_battle_skipped encounter=%lu result_reported=2 temporary=1 party_writeback=0\n",
                        (unsigned long)skipped);fflush(trace);}
                    dirty=1;continue;
                }
                if(view->blocked[0]&&view->battle_id==10&&(input.pressed&C3_PAD_A)){
                    c3_n3ds_log_memory(trace,"field_battle_before_drain");
                    if(c3_render_recover(trace,"field-to-battle",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    release_field_models(storage,count);
                    c3_n3ds_log_memory(trace,"field_battle_payloads_released");
                    int battle_outcome=0;
                    int preview_result=c3_battle_preview_from_game_state(state,trace,&battle_outcome);
                    /* Defensively drain even a failed/partial battle initialization.
                     * Never reload CPU packs while battle GPU storage is live. */
                    if(c3_render_recover(trace,"battle-to-field",0,
                        preview_result>0?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    c3_n3ds_log_memory(trace,"field_battle_before_restore");
                    if(!restore_field_models(storage,count,model_paths,&story,
                        elapsed,error,capacity)){result=-1;break;}
                    if(field_renderer_init(storage,models,count,skin_actors,error,capacity)){result=-1;break;}
                    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,
                        view->tv_enabled,error,capacity)){result=-1;break;}
                    c3_n3ds_log_memory(trace,"field_battle_restored");
                    /* A hero win is reported as the original result ID 2, the
                     * value the field script compares native 67 against. Any
                     * other outcome leaves native 98 waiting (retry). */
                    int reported=0;
                    if(preview_result==0&&battle_outcome==C3_PREVIEW_PLAYER_WON){
                        if(!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
                        reported=1;
                    }
                    if(trace){fprintf(trace,"field_battle_return preview=%d outcome=%d result_reported=%d pending=%lu vm_preserved=1 models=%lu\n",
                        preview_result,battle_outcome,reported,(unsigned long)view->battle_id,(unsigned long)count);fflush(trace);}
                    /* Modal preview time must not advance paused field scripts. */
                    previous=c3_n3ds_active_time_us();dirty=1;next_console=0;
                    continue;
                }
                if(view->control&&!story_consumed_input){
                    if(c3_first_person_chord(input.held,input.pressed)){
                        /* Hold ZL/ZR (or R on a 3DS without them) and press X. */
                        c3_first_person_toggle(&first_person,&player,map.floor);
                        input.pressed&=(uint16_t)~(C3_PAD_X|C3_PAD_Z|C3_PAD_R);
                        if(trace){fprintf(trace,"first_person enabled=%d yaw=%.4f\n",first_person.enabled,(double)first_person.yaw);fflush(trace);}
                    }
                    if(first_person.enabled){
                        c3_first_person_look(&first_person,input.cstick_x,input.cstick_y,delta);
                        c3_first_person_touch(&first_person,input.touch_held,input.touch_x,input.touch_y);
                    }
                    if(input.pressed&(C3_PAD_START|C3_PAD_X)){
                        c3_ingame_init(ingame,state);
                        int original=c3_ingame_load(ingame,BOOT_ROOT "pda.c3pd");
                        if(trace){fprintf(trace,"ingame_open original_assets=%d\n",original);fflush(trace);}
                        page=PAUSE_MAIN;selection=0;dirty=1;
                    }
                    else if(input.pressed&C3_PAD_A){if(!c3_field_story_interact(&story,story_error))goto story_failed;dirty=1;}
                    else{
                        float before[3];memcpy(before,player.position,sizeof before);
                        float move_yaw=first_person.enabled?c3_first_person_move_yaw(&first_person):map.camera_yaw;
                        if(!c3_field_move(&map,&player,input.stick_x,input.stick_y,input.held,move_yaw,delta)){
                            snprintf(error,capacity,"Field movement failed.");result=-1;break;
                        }
                        c3_field_story_collide(&story,before);
                        float from[3]={before[0],before[1]+8.5f*map.scale,before[2]};
                        float to[3]={player.position[0],player.position[1]+8.5f*map.scale,player.position[2]};
                        /* Touched surfaces (kind 2) run before crossed ones
                         * (kind 1), as in hero_move.c. A started touch
                         * script takes the frame; the doorway follows once
                         * it has finished and the player keeps moving. */
                        uint16_t touched[4];int touch_started=0;
                        /* people.c queries touches with the step before wall
                         * correction (player.proposed), so a fence's own
                         * touch surface is reached; doorways keep `to`. */
                        float touch_to[3]={player.proposed[0],player.proposed[1]+8.5f*map.scale,player.proposed[2]};
                        unsigned touch_count=c3_area_touch_events(&area,&map,from,touch_to,map.radius,touched);
                        for(unsigned i=0;i<touch_count&&!touch_started;++i){
                            touch_started=c3_field_story_touch(&story,touched[i],story_error);
                            if(touch_started<0)goto story_failed;
                        }
                        const C3FieldTriangle *door=touch_started?NULL:c3_area_transition_active(&area,&map,from,to,map.radius);
                        if(door&&!c3_field_story_transition(&story,door->source_id,story_error))goto story_failed;
                    }
                }
            }else if(page==PAUSE_MAIN){
                C3IngamePage old_page=ingame->page;
                if(pda_preview&&(input.pressed&C3_PAD_B)){
                    if(c3_render_recover(trace,"pda-to-field",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    if(!c3_pda_preview_close(&pda_preview)){snprintf(error,capacity,"PDA renderer still owns its model.");result=-1;break;}
                    ingame->preview=0;ingame->dirty=1;input.pressed&=(uint16_t)~C3_PAD_B;
                    if(field_renderer_init(storage,models,count,skin_actors,error,capacity)||
                       (tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,view->tv_enabled,error,capacity))){result=-1;break;}
                    previous=c3_n3ds_active_time_us();
                }
                C3IngameEvent event=c3_ingame_update(ingame,state,&input,delta);
                if(pda_preview&&(old_page!=ingame->page||event.kind==C3_INGAME_PREVIEW)){
                    if(c3_render_recover(trace,"pda-to-field",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    if(!c3_pda_preview_close(&pda_preview)){snprintf(error,capacity,"PDA renderer still owns its model.");result=-1;break;}
                    ingame->preview=0;ingame->dirty=1;
                    if(field_renderer_init(storage,models,count,skin_actors,error,capacity)||
                       (tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,view->tv_enabled,error,capacity))){result=-1;break;}
                    if(event.kind==C3_INGAME_PREVIEW)event.kind=C3_INGAME_NONE;
                    previous=c3_n3ds_active_time_us();
                }
                if(ingame->dirty)dirty=1;
                if(trace&&(old_page!=ingame->page||event.kind)){
                    fprintf(trace,"ingame_page page=%u event=%u input=%04x\n",(unsigned)ingame->page,(unsigned)event.kind,input.pressed);fflush(trace);
                }
                if(event.kind==C3_INGAME_PREVIEW){
                    uint16_t species;uint32_t pid,trainer;char preview_error[192];
                    if(!c3_ingame_preview_identity(ingame,state,&species,&pid,&trainer))c3_ingame_error(ingame,"A model is not available for this saved entry yet.");
                    else{
                        if(c3_render_recover(trace,"field-to-pda",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                        if(c3_pda_preview_open(&pda_preview,"sdmc:/3ds/Colosseum3DS/pda/models",species,pid,trainer,preview_error,sizeof preview_error)){
                            ingame->preview=1;pda_seconds=0;
                            if(trace){fprintf(trace,"pda_preview species=%u pid=%08lx trainer=%08lx vm_preserved=1\n",species,(unsigned long)pid,(unsigned long)trainer);fflush(trace);}
                        }else{
                            if(pda_preview){
                                if(c3_render_recover(trace,"pda-failed",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                                if(!c3_pda_preview_close(&pda_preview)){result=-1;break;}
                            }
                            if(field_renderer_init(storage,models,count,skin_actors,error,capacity)||
                               (tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,view->tv_enabled,error,capacity))){result=-1;break;}
                            c3_ingame_error(ingame,preview_error);
                        }
                        previous=c3_n3ds_active_time_us();
                    }
                    dirty=1;
                }
                else if(event.kind==C3_INGAME_PLAY_TONE){char audio_error[192];
                    if(!pda_audio_ready)pda_audio_ready=c3_pda_audio_open("sdmc:/3ds/Colosseum3DS",audio_error,sizeof audio_error);
                    if(!pda_audio_ready||!c3_pda_audio_preview(event.tone,audio_error,sizeof audio_error))c3_ingame_error(ingame,audio_error);
                    c3_pda_audio_trace(trace,"preview");dirty=1;
                }
                else if(event.kind==C3_INGAME_STOP_TONE){c3_pda_audio_stop();dirty=1;}
                else if(event.kind==C3_INGAME_RESUME){c3_pda_audio_close();pda_audio_ready=0;c3_ingame_close(ingame);page=PAUSE_CLOSED;dirty=1;}
                else if(event.kind==C3_INGAME_SAVE){page=PAUSE_SAVE;yes=0;dirty=1;}
                else if(event.kind==C3_INGAME_LEAVE){page=PAUSE_LEAVE;yes=0;dirty=1;}
                else if(event.kind==C3_INGAME_OPTIONS){c3_movie_player_set_mono(ingame->option_mono);dirty=1;}
                else if(event.kind==C3_INGAME_READ_MAIL){
                    int ready=!event.mail.script||c3_field_story_menu_event(&story,event.mail.script,story_error);
                    if(ready&&!c3_game_state_mail_read(state,&event.mail.save,1)){
                        if(event.mail.script)c3_field_story_cancel_menu_event(&story);
                        ready=0;snprintf(story_error,sizeof story_error,"The mail read changes could not be applied.");
                    }
                    if(!ready)c3_ingame_error(ingame,story_error);
                    else if(event.mail.script){c3_pda_audio_close();pda_audio_ready=0;c3_ingame_close(ingame);page=PAUSE_CLOSED;}
                    if(trace){fprintf(trace,"ingame_mail id=%u committed=%d callback=%08lx disk_write=0\n",event.mail.save.id,ready,(unsigned long)event.mail.script);fflush(trace);}
                    dirty=1;
                }
            }else if(page==PAUSE_SAVE||page==PAUSE_LEAVE){
                if(input.pressed&(C3_PAD_UP|C3_PAD_DOWN|C3_PAD_LEFT|C3_PAD_RIGHT)){yes=!yes;dirty=1;}
                if(input.pressed&C3_PAD_B){page=PAUSE_MAIN;dirty=1;}
                else if(input.pressed&C3_PAD_A){
                    if(!yes){page=PAUSE_MAIN;dirty=1;}
                    else if(page==PAUSE_LEAVE){result=0;break;}
                    else{save_game(state,save_path,trace,message,sizeof message);page=PAUSE_MESSAGE;dirty=1;}
                }
            }else if(input.pressed&C3_PAD_B||(page==PAUSE_MESSAGE&&input.pressed&C3_PAD_A)){page=PAUSE_MAIN;dirty=1;}
        }
        int sampled=1;
        /* Nothing rewrites a map pack's sampled vertices, so only batches whose
         * bones, UVs or lights changed are skinned and staged again. The flag
         * is set here because a battle return reloads the packs. */
        for(uint32_t i=0;i<map.map_pack_count;++i){storage[i].retained=1;sampled&=c3_title_animate(storage+i,0,elapsed);}
        /* Actor placement is the story sample's time outside c3_title_animate. */
        uint64_t place_clock=c3_n3ds_time_us(),place_animate=c3_title_perf_animate_us();
        if(!c3_field_story_sample(&story,story_error))goto story_failed;
        c3_stage_renderer_perf_place(c3_n3ds_time_us()-place_clock-(c3_title_perf_animate_us()-place_animate));
        /* Doors hold the clip and time native 60 left them at; retained, so
         * a door at rest is not resampled. */
        for(uint32_t i=0;i<doors.count;++i)if(door_models[i]){
            const C3StoryDoor *door=c3_field_story_door(&story,i);
            door_models[i]->retained=1;
            sampled&=c3_title_animate(door_models[i],door->clip,door->seconds);
        }
        if(phase==FIELD_ARRIVAL){for(uint32_t i=0;i<arrival_count;++i)sampled&=c3_title_animate(storage+arrival_index+i,0,phase_seconds);}
        else if(phase==FIELD_ARRIVAL_OUT&&!arrival_end_sampled){
            sampled&=c3_title_animate(arrival,0,fmaxf(0,arrival_seconds-.1f/30.0f));arrival_end_sampled=1;
        }
        if(tv_index!=UINT32_MAX){
            sampled&=c3_title_animate(storage+tv_index,0,elapsed);
            if(!pda_preview&&!c3_stage_renderer_set_tv(tv_index,1,1,view->tv_enabled,error,capacity)){result=-1;break;}
        }
        if(lights_loaded)for(uint32_t i=0;i<count;++i)if(i!=tv_index)sampled&=c3_area_lights_apply(&lights,elapsed,storage+i);
        if(!sampled){snprintf(error,capacity,"An original area animation or light sample failed.");result=-1;break;}
        if(phase==FIELD_SETUP){fade=0;c3_field_camera(&map,&player,&camera);}
        else if(phase==FIELD_ARRIVAL||phase==FIELD_ARRIVAL_OUT){
            camera=arrival->pose.camera;hide(wes);
            for(uint32_t i=0;i<area.actor_count;++i)if(actor_models[i])hide(actor_models[i]);
            if(phase==FIELD_ARRIVAL_OUT){for(uint32_t i=0;i<arrival_count;++i)hide(storage+arrival_index+i);fade=1-phase_seconds;}
            else fade=fminf(1,phase_seconds);
        }else{
            for(uint32_t i=0;i<arrival_count;++i)hide(storage+arrival_index+i);
            if(view->camera_active)camera=view->camera;
            else if(first_person.enabled){
                /* Scripted cameras above suspend the view; arrivals start it level. */
                c3_first_person_arrive(&first_person,&player,map.floor);
                c3_first_person_camera(&first_person,&map,&player,&camera);hide(wes);
            }else c3_field_camera(&map,&player,&camera);
            if(phase==FIELD_FADE_IN)fade=fminf(fade,phase_seconds/(arrival?1.0f:.5f));
        }
        if(!c3_game_state_summary(state,&summary)){snprintf(error,capacity,"Game state became unavailable.");result=-1;break;}
        if(dirty||input.pressed||now>=next_console){
            C3BottomUi *bottom=c3_bottom_screen_begin();
            if(naming)c3_name_entry_draw(&name_entry,bottom,"PARTNER'S NAME");
            else if(page==PAUSE_MAIN&&ingame->active)c3_ingame_draw_to(ingame,state,bottom);
            else c3_field_ui_draw_to(bottom,&summary,phase,page,selection,yes,message,view,&ui_pager);
            c3_bottom_screen_present();dirty=0;next_console=now+UINT64_C(100000);
        }
        if(pda_preview){
            pda_seconds+=delta;
            c3_pda_preview_control(pda_preview,abs(input.cstick_x)>20?input.cstick_x/128.f:0,
                                   abs(input.cstick_y)>20?input.cstick_y/128.f:0,delta);
        }
        int draw_result=pda_preview?c3_pda_preview_draw(pda_preview,pda_seconds,&stats,error,capacity):
            c3_stage_renderer_draw(models,count,&camera,fade,&stats,error,capacity);
        if(draw_result==C3_RENDER_BUSY){
            C3RenderRecoveryResult recovery=c3_render_recover(trace,"field",frames,C3_RECOVER_WAIT);
            if(recovery==C3_RECOVERY_EXIT){result=1;break;}
            if(recovery==C3_RECOVERY_RESUME_PAUSED){previous=c3_n3ds_active_time_us();dirty=1;next_console=0;}
            continue;
        }
        if(draw_result){result=-1;break;}
        ++frames;
        if(trace&&(now>=next_trace||input.pressed)){
            C3TitleRenderSnapshot snapshot;c3_stage_renderer_snapshot(&snapshot);
            char perf[1024];c3_stage_renderer_perf_format(perf,sizeof perf);
            fprintf(trace,"field_tick floor=%lu phase=%u menu=%u control=%d message=%05lx input=%04x stick=%d,%d xyz=%.4f,%.4f,%.4f yaw=%.4f motion=%lu sample=%.2f hits=%lu time=%lu draws=%lu command_words=%lu gpu_bytes=%lu"
                " vertices=%lu vertices_prepared=%lu cached_models=%lu gpu_previous_ms=%.3f%s\n",
                (unsigned long)map.floor,(unsigned)phase,(unsigned)page,view->control,(unsigned long)view->message_id,input.pressed,input.stick_x,input.stick_y,
                (double)player.position[0],(double)player.position[1],(double)player.position[2],(double)player.yaw,(unsigned long)player.motion,
                (double)wes->active_frame,(unsigned long)player.collision_count,(unsigned long)summary.play_seconds,
                (unsigned long)stats.draw_calls,(unsigned long)snapshot.last_command_words,(unsigned long)stats.gpu_buffer_bytes,
                (unsigned long)stats.vertices,(unsigned long)stats.vertices_prepared,(unsigned long)stats.cached_models,
                (double)stats.gpu_previous_ms,perf);
            /* A queued cue alone does not prove DSP playback progressed. */
            if(pda_audio_ready&&ingame->active&&ingame->page==C3_GAME_TONES)c3_pda_audio_trace(trace,"pda_menu_tick");
            fflush(trace);next_trace=now+UINT64_C(1000000);
        }
    }
    goto done;
story_failed:
    snprintf(error,capacity,"%s",story_error);result=-1;
done:
    c3_pda_audio_close();
    c3_ingame_close(ingame);
    free(ingame);
    if(result<0)c3_n3ds_log_memory(trace,"field_failed");
    if(c3_render_recover(trace,"field",frames,result==1?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)result=1;
    c3_pda_preview_close(&pda_preview);
    c3_field_story_close(&story);
    for(uint32_t i=0;i<opened;++i)c3_title_close(storage+i);
    free(storage);c3_area_lights_close(&lights);c3_area_camera_close(&room_camera);c3_area_close(&area);c3_field_close(&map);
    if(trace){fprintf(trace,"field_end result=%d error=%s\n",result,result<0?error:"");fflush(trace);}
    return result;
}

int c3_field_scene_run(C3GameState *state,int new_game,const char *save_path,
                       FILE *trace,char *error,size_t capacity)
{
    if(new_game&&!c3_game_state_set_floor(state,1,0x03a0,0)){
        snprintf(error,capacity,"The original first-area checkpoint could not be prepared.");return -1;
    }
    int result;
    do{
        C3GameSummary summary;
        /* Floor 0x38E is the menu-class world map, not a walkable field. */
        if(c3_game_state_summary(state,&summary)&&summary.floor==C3_FLOOR_WORLD_MAP_ID)
            result=c3_world_map_scene_run(state,trace,error,capacity);
        else result=run_area(state,new_game,save_path,trace,error,capacity);
        new_game=0;
    }while(result==2);
    return result;
}
