#include "field_scene.h"
#include "bag_menu.h"
#include "battle_preview_scene.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "stage_renderer.h"
#include "colosseum3ds/field.h"
#include "colosseum3ds/field_story.h"
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

typedef enum { FIELD_SETUP, FIELD_ARRIVAL, FIELD_ARRIVAL_OUT, FIELD_FADE_IN, FIELD_CONTROL } FieldPhase;
typedef enum { PAUSE_CLOSED, PAUSE_MAIN, PAUSE_PARTY, PAUSE_SAVE, PAUSE_LEAVE, PAUSE_MESSAGE, PAUSE_BAG } PausePage;

static int exists(const char *path)
{
    struct stat entry;
    return stat(path,&entry)==0 && S_ISREG(entry.st_mode);
}

int c3_field_scene_supports(uint32_t floor) { return floor==1u || floor==2u; }

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

static const char *pokemon_name(uint16_t species)
{
    switch(species) {case 197:return "UMBREON";case 196:return "ESPEON";default:return "POKEMON";}
}

static void draw_console(const C3GameSummary *summary,FieldPhase phase,PausePage page,
                         unsigned selection,int yes,const char *message,const C3FieldStoryView *story)
{
    consoleClear();
    printf("OUTSKIRT STAND%s\n\n%s  |  $%lu\nTime %lu:%02lu:%02lu\n\n",summary->floor==2?" - INSIDE":"",summary->name,
           (unsigned long)summary->money,(unsigned long)(summary->play_seconds/3600u),
           (unsigned long)(summary->play_seconds/60u%60u),(unsigned long)(summary->play_seconds%60u));
    if(phase==FIELD_SETUP){printf("Loading the original area scripts...\n");return;}
    if(phase==FIELD_ARRIVAL || phase==FIELD_ARRIVAL_OUT) {
        printf("Arriving at Outskirt Stand...\n");return;
    }
    if(page==PAUSE_CLOSED && story && story->blocked[0]){
        printf("%s\n\n",story->blocked);
        if(story->battle_id==10)printf("A: nonpersistent combat preview\n");
        printf("X / START: main menu\n");return;
    }
    if(page==PAUSE_CLOSED && story && story->dialogue){
        printf("%s\n\n",story->message);
        if(story->choice)printf("%s YES\n%s NO\n\nA: confirm   B: no\n",story->choice_no?" ":">",story->choice_no?">":" ");
        else printf("A / B: continue\n");
        return;
    }
    if(page==PAUSE_CLOSED) {
        printf("Circle Pad / D-pad: move\nA: talk / inspect   X / START: menu\n\n");
        printf("%s Lv.%u   HP %u/%u\n%s Lv.%u   HP %u/%u\n\n",
               pokemon_name(summary->party[0].species),summary->party[0].level,
               summary->party[0].hp,summary->party[0].max_hp,
               pokemon_name(summary->party[1].species),summary->party[1].level,
               summary->party[1].hp,summary->party[1].max_hp);
        if(story && !story->control)printf("An original story event is running.\n");
    } else if(page==PAUSE_MAIN) {
        static const char *const choices[]={"RESUME","POKEMON","SAVE","MAIN MENU","BAG"};
        for(unsigned i=0;i<5;++i)printf("%s %s\n",i==selection?">":" ",choices[i]);
        printf("\nA: choose   B: resume\n");
    } else if(page==PAUSE_PARTY) {
        printf("POKEMON\n\n");
        for(unsigned i=0;i<6;++i)if(summary->party[i].species)
            printf("%s Lv.%u  HP %u/%u\n",pokemon_name(summary->party[i].species),
                   summary->party[i].level,summary->party[i].hp,summary->party[i].max_hp);
        printf("\nB: back\n");
    } else if(page==PAUSE_SAVE) {
        printf("Save your game here?\n\nContinue resumes at this area's\nentrance. Your party and progress\nwill be kept.\n\n");
        printf("%s YES\n%s NO\n\nA: confirm   B: cancel\n",yes?">":" ",yes?" ":">");
    } else if(page==PAUSE_LEAVE) {
        printf("Return to the main menu?\nUnsaved progress will be lost.\n\n");
        printf("%s YES\n%s NO\n\nA: confirm   B: cancel\n",yes?">":" ",yes?" ":">");
    } else {
        printf("%s\n\nA / B: back\n",message);
    }
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

/* Return 2 only after a real source doorway and its post callback finish. */
static int run_area(C3GameState *state,int new_game,const char *save_path,
                    FILE *trace,char *error,size_t capacity)
{
    C3FieldMap map={0};C3Area area={0};C3FieldPlayer player={0};C3GameSummary summary;
    C3ItemCatalog items={0};C3BagMenu bag={0};
    C3FieldStory story={0};C3AreaLights lights={0};C3AreaCamera room_camera={0};
    C3Title *storage=NULL;const C3Title *models[C3_STAGE_MAX_MODELS]={0};
    C3Title *actor_models[C3_AREA_MAX_ACTORS]={0};
    char model_paths[C3_STAGE_MAX_MODELS][160]={{0}};
    C3TitleCamera camera;C3TitleRenderStats stats={0};C3N3dsInput input={0};
    uint32_t opened=0,count=0,arrival_done=0,opening_done=0,arrival_count=0;
    unsigned wes_index=0,arrival_index=0,tv_index=UINT32_MAX,selection=0,frames=0;
    int result=-1,yes=0,dirty=1,arrival_end_sampled=0,entered=0;
    FieldPhase phase=FIELD_SETUP;PausePage page=PAUSE_CLOSED;
    float elapsed=0,phase_seconds=0,arrival_seconds=0;
    uint64_t previous=0,next_console=0,next_trace=0;
    char path[160],message[256]={0},story_error[C3_STORY_ERROR]={0};
    const C3FieldStoryView *view=NULL;
    if(!state||!save_path||!c3_game_state_summary(state,&summary)||!c3_field_scene_supports(summary.floor)){
        snprintf(error,capacity,"The saved location is not available in this build.");return -1;
    }
    const char *prefix=summary.floor==1?"outskirt":"shop";
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
        snprintf(error,capacity,"The saved Outskirt Stand entrance is unavailable.");goto done;
    }
    arrival_count=map.floor==1&&opening_done&&!arrival_done?map.arrival_pack_count:0;
    count=map.map_pack_count+1u+arrival_count;wes_index=map.map_pack_count;arrival_index=wes_index+1u;
    for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0])++count;
    if(map.floor==2)tv_index=count++;
    if(count>C3_STAGE_MAX_MODELS){snprintf(error,capacity,"The area exceeds the model bound.");goto done;}
    storage=calloc(count,sizeof(*storage));
    if(!storage){snprintf(error,capacity,"Field model allocation failed.");goto done;}
    consoleClear();printf("OUTSKIRT STAND\n\nLoading the original area...\n");
    for(uint32_t i=0;i<map.map_pack_count+1u+arrival_count;++i){
        if(i<map.map_pack_count)snprintf(path,sizeof path,BOOT_ROOT "%s_map%lu.c3t",prefix,(unsigned long)i);
        else if(i==wes_index)snprintf(path,sizeof path,BOOT_ROOT "%s_wes.c3t",prefix);
        else snprintf(path,sizeof path,BOOT_ROOT "outskirt_arrival%lu.c3t",(unsigned long)(i-arrival_index));
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
    C3Title *wes=storage+wes_index,*arrival=arrival_count?storage+arrival_index:NULL;
    if(wes->vertex_count!=map.actor_vertex_count||wes->clip_count!=4u){
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
    if(map.floor==2){
        if(!c3_title_load(storage+tv_index,BOOT_ROOT "shop_tv.c3t",error,capacity))goto done;
        snprintf(model_paths[tv_index],sizeof model_paths[tv_index],BOOT_ROOT "shop_tv.c3t");
        models[opened++]=storage+tv_index;
        if(!c3_title_animate(storage+tv_index,0,0)||
           !c3_area_camera_load(&room_camera,BOOT_ROOT "shop_camera.c3c",error,capacity)||
           !c3_area_lights_load(&lights,BOOT_ROOT "shop.c3l",error,capacity))goto done;
    }
    if(opened!=count){snprintf(error,capacity,"Area model identity mismatch.");goto done;}
    if(trace){
        size_t cpu_models=0;
        for(uint32_t i=0;i<count;++i)cpu_models+=storage[i].resident_bytes;
        fprintf(trace,"field_models floor=%lu models=%lu cpu_resident_bytes=%lu\n",
                (unsigned long)map.floor,(unsigned long)count,(unsigned long)cpu_models);
        fflush(trace);
    }
    if(c3_stage_renderer_init(models,count,error,capacity))goto done;
    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,true,error,capacity))goto done;
    c3_n3ds_log_memory(trace, map.floor==1?"outskirt_ready":"shop_ready");
    const char *story_path=map.floor==1?BOOT_ROOT "outskirt.c3s":BOOT_ROOT "outskirt_shop.c3s";
    if(!c3_field_story_load(&story,story_path,state,&map,&area,&player,wes,actor_models,&room_camera,trace,story_error)||
       !c3_field_story_bind_battle(&story,BOOT_ROOT "willie.c3b",story_error)||
       !c3_field_story_prepare(&story,story_error))goto story_failed;
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
                if(view->blocked[0]&&view->battle_id==10&&(input.pressed&C3_PAD_A)){
                    c3_n3ds_log_memory(trace,"field_battle_before_drain");
                    if(c3_render_recover(trace,"field-to-battle",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    release_field_models(storage,count);
                    c3_n3ds_log_memory(trace,"field_battle_payloads_released");
                    int preview_result=c3_battle_preview_from_game_state(state,trace);
                    /* Defensively drain even a failed/partial battle initialization.
                     * Never reload CPU packs while battle GPU storage is live. */
                    if(c3_render_recover(trace,"battle-to-field",0,
                        preview_result>0?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    c3_n3ds_log_memory(trace,"field_battle_before_restore");
                    if(!restore_field_models(storage,count,model_paths,&story,
                        elapsed,error,capacity)){result=-1;break;}
                    if(c3_stage_renderer_init(models,count,error,capacity)){result=-1;break;}
                    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,
                        view->tv_enabled,error,capacity)){result=-1;break;}
                    c3_n3ds_log_memory(trace,"field_battle_restored");
                    if(trace){fprintf(trace,"field_battle_return preview=%d pending=%lu vm_preserved=1 models=%lu\n",
                        preview_result,(unsigned long)view->battle_id,(unsigned long)count);fflush(trace);}
                    /* Modal preview time must not advance paused field scripts. */
                    previous=c3_n3ds_active_time_us();dirty=1;next_console=0;
                    continue;
                }
                if(view->control&&!story_consumed_input){
                    if(input.pressed&(C3_PAD_START|C3_PAD_X)){page=PAUSE_MAIN;selection=0;dirty=1;}
                    else if(input.pressed&C3_PAD_A){if(!c3_field_story_interact(&story,story_error))goto story_failed;dirty=1;}
                    else{
                        float before[3];memcpy(before,player.position,sizeof before);
                        if(!c3_field_move(&map,&player,input.stick_x,input.stick_y,input.held,map.camera_yaw,delta)){
                            snprintf(error,capacity,"Field movement failed.");result=-1;break;
                        }
                        c3_field_story_collide(&story,before);
                        float from[3]={before[0],before[1]+8.5f*map.scale,before[2]};
                        float to[3]={player.position[0],player.position[1]+8.5f*map.scale,player.position[2]};
                        const C3FieldTriangle *door=c3_area_transition_active(&area,&map,from,to,map.radius);
                        if(door&&!c3_field_story_transition(&story,door->source_id,story_error))goto story_failed;
                    }
                }
            }else if(page==PAUSE_MAIN){
                if(input.pressed&C3_PAD_UP){selection=(selection+4u)%5u;dirty=1;}
                if(input.pressed&C3_PAD_DOWN){selection=(selection+1u)%5u;dirty=1;}
                if(input.pressed&(C3_PAD_B|C3_PAD_START|C3_PAD_X)){page=PAUSE_CLOSED;dirty=1;}
                else if(input.pressed&C3_PAD_A){
                    static const PausePage choices[]={PAUSE_CLOSED,PAUSE_PARTY,PAUSE_SAVE,PAUSE_LEAVE,PAUSE_BAG};
                    page=choices[selection];yes=0;dirty=1;
                    if(page==PAUSE_BAG){
                        if(!items.impl&&!c3_item_catalog_load(&items,BOOT_ROOT "items.c3it",message))page=PAUSE_MESSAGE;
                        else c3_bag_menu_init(&bag,state,&items);
                    }
                }
            }else if(page==PAUSE_BAG){
                C3ItemUseResult used={0};
                C3BagEvent event=c3_bag_menu_step(&bag,state,&items,input.pressed,&used);
                if(event==C3_BAG_EVENT_CLOSED){page=PAUSE_MAIN;dirty=1;}
                if(event==C3_BAG_EVENT_USED&&trace){
                    fprintf(trace,"field_item_used id=%u party=%u quantity=%u->%u hp=%u->%u condition=%u->%u disk_writes=0\n",
                        used.item,used.party_slot,used.quantity_before,used.quantity_after,
                        used.hp_before,used.hp_after,used.condition_before,used.condition_after);
                    fflush(trace);
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
        for(uint32_t i=0;i<map.map_pack_count;++i)sampled&=c3_title_animate(storage+i,0,elapsed);
        if(!c3_field_story_sample(&story,story_error))goto story_failed;
        if(phase==FIELD_ARRIVAL){for(uint32_t i=0;i<arrival_count;++i)sampled&=c3_title_animate(storage+arrival_index+i,0,phase_seconds);}
        else if(phase==FIELD_ARRIVAL_OUT&&!arrival_end_sampled){
            sampled&=c3_title_animate(arrival,0,fmaxf(0,arrival_seconds-.1f/30.0f));arrival_end_sampled=1;
        }
        if(tv_index!=UINT32_MAX){
            sampled&=c3_title_animate(storage+tv_index,0,elapsed);
            if(!c3_stage_renderer_set_tv(tv_index,1,1,view->tv_enabled,error,capacity)){result=-1;break;}
            for(uint32_t i=0;i<count;++i)if(i!=tv_index)sampled&=c3_area_lights_apply(&lights,elapsed,storage+i);
        }
        if(!sampled){snprintf(error,capacity,"An original area animation or light sample failed.");result=-1;break;}
        if(phase==FIELD_SETUP){fade=0;c3_field_camera(&map,&player,&camera);}
        else if(phase==FIELD_ARRIVAL||phase==FIELD_ARRIVAL_OUT){
            camera=arrival->pose.camera;hide(wes);
            for(uint32_t i=0;i<area.actor_count;++i)if(actor_models[i])hide(actor_models[i]);
            if(phase==FIELD_ARRIVAL_OUT){for(uint32_t i=0;i<arrival_count;++i)hide(storage+arrival_index+i);fade=1-phase_seconds;}
            else fade=fminf(1,phase_seconds);
        }else{
            for(uint32_t i=0;i<arrival_count;++i)hide(storage+arrival_index+i);
            if(view->camera_active)camera=view->camera;else c3_field_camera(&map,&player,&camera);
            if(phase==FIELD_FADE_IN)fade=fminf(fade,phase_seconds/(arrival?1.0f:.5f));
        }
        if(!c3_game_state_summary(state,&summary)){snprintf(error,capacity,"Game state became unavailable.");result=-1;break;}
        if(dirty||input.pressed||now>=next_console){
            if(page==PAUSE_BAG)c3_bag_menu_draw(&bag,&items);
            else draw_console(&summary,phase,page,selection,yes,message,view);
            dirty=0;next_console=now+UINT64_C(100000);
        }
        int draw_result=c3_stage_renderer_draw(models,count,&camera,fade,&stats,error,capacity);
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
            fprintf(trace,"field_tick floor=%lu phase=%u menu=%u control=%d message=%05lx input=%04x stick=%d,%d xyz=%.4f,%.4f,%.4f yaw=%.4f motion=%lu sample=%.2f hits=%lu time=%lu draws=%lu command_words=%lu gpu_bytes=%lu\n",
                (unsigned long)map.floor,(unsigned)phase,(unsigned)page,view->control,(unsigned long)view->message_id,input.pressed,input.stick_x,input.stick_y,
                (double)player.position[0],(double)player.position[1],(double)player.position[2],(double)player.yaw,(unsigned long)player.motion,
                (double)wes->active_frame,(unsigned long)player.collision_count,(unsigned long)summary.play_seconds,
                (unsigned long)stats.draw_calls,(unsigned long)snapshot.last_command_words,(unsigned long)stats.gpu_buffer_bytes);
            fflush(trace);next_trace=now+UINT64_C(1000000);
        }
    }
    goto done;
story_failed:
    snprintf(error,capacity,"%s",story_error);result=-1;
done:
    c3_item_catalog_free(&items);
    if(result<0)c3_n3ds_log_memory(trace,"field_failed");
    if(c3_render_recover(trace,"field",frames,result==1?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)result=1;
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
    do{result=run_area(state,new_game,save_path,trace,error,capacity);new_game=0;}while(result==2);
    return result;
}
