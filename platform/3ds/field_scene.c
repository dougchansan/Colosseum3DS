#include "field_scene.h"
#include "bottom_screen.h"
#include "game_ui_views.h"
#include "ingame_menu.h"
#include "pack_cache.h"
#include "pack_loader.h"
#include "battle_general_scene.h"
#include "battle_general_3d.h"
#include "colosseum3ds/battle_story.h"
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
#include "colosseum3ds/orbit_camera.h"
#include "colosseum3ds/floor_profile.h"
#include "colosseum3ds/inventory.h"
#include "colosseum3ds/name_entry.h"
#include "world_map_scene.h"
#include "colosseum3ds/area_lights.h"
#include "colosseum3ds/title.h"
#include "colosseum3ds/shop.h"

#include <3ds.h>
#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "music_player.h"

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
/* A whole prepared file from the boot directory (at most 1 MiB), or NULL. */
static uint8_t *read_boot_file(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");long length=0;uint8_t *data=NULL;
    if(file&&!fseek(file,0,SEEK_END)&&(length=ftell(file))>0&&length<=1024*1024&&!fseek(file,0,SEEK_SET)&&
       (data=malloc((size_t)length))&&fread(data,1,(size_t)length,file)!=(size_t)length){free(data);data=NULL;}
    if(file)fclose(file);
    if(data)*size=(size_t)length;
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
/* Script music natives 68/69/6A/6D -> the streamed MusyX player. */
static int story_audio(void *context,unsigned op,uint32_t a,uint32_t b,uint32_t c)
{
    (void)context;
    switch(op){
    case C3_STORY_AUDIO_PLAY_406:return c3_music_play(C3_MUSIC_SLOT_406,a,c,b);
    case C3_STORY_AUDIO_PLAY_407:return c3_music_play(C3_MUSIC_SLOT_407,a,c,b);
    case C3_STORY_AUDIO_STOP:c3_music_stop(a,b);return 1;
    case C3_STORY_AUDIO_MASTER:c3_music_master(a,b);return 1;
    }
    return 0;
}

/* Party service for script native D8. */
static int restore_party(void *context)
{
    C3PdaAssets assets={0};uint8_t *data=open_pda(&assets);
    int restored=data?c3_inventory_restore_party(context,&assets):-1;
    free(data);return restored;
}
/* Destinations the original doorway script may request from a field. */
/* The in-game menu stays loaded while the hero walks: its hub is the
 * bottom screen, and X / START or a tap open it at once. Returns whether
 * the original P*DA art loaded (0 keeps the plain-panel hub). */
static int hub_menu_ready(C3IngameMenu *menu,const C3GameState *state)
{
    static int original;
    if(!menu->active){c3_ingame_init(menu,state);original=c3_ingame_load(menu,BOOT_ROOT "pda.c3pd");}
    return original;
}
/* Original species names from the P*DA data the bottom-screen hub keeps
 * loaded (none while it is not loaded). */
static int field_species_name(void *context,unsigned species,char *out,size_t capacity)
{
    C3IngameMenu *menu=context;C3PdaSpecies entry;
    if(!menu||!menu->active||!menu->assets.data||!c3_pda_species(&menu->assets,species,&entry))return 0;
    return c3_pda_text(&menu->assets,entry.name,"","",out,capacity);
}
/* Top-screen progress while the pack jobs run (main thread). */
static void load_progress(size_t done,size_t total)
{
    c3_n3ds_top_loading(total?(float)done/(float)total:1.0f);
}
static int field_renderer_init(C3Title *storage,const C3Title *const *models,uint32_t count,
                               uint64_t actors,char *error,size_t capacity);
typedef struct {
    C3PackJob *jobs; C3PackStream *stream;
    uint32_t first,count,area_index[C3_AREA_MAX_ACTORS];
    int attached; uint64_t start_us;
} NpcStream;
/* Bind streamed people once every pack has arrived (wait: block for them):
 * pose each on its idle clip, attach it to the story, and restart the
 * renderer with the full model list (drained first, as after a battle). */
static int npc_stream_attach(NpcStream *npc,int wait,C3Title *storage,const C3Title **models,C3Title **actor_models,
                             char paths[][160],C3FieldStory *story,uint32_t count,uint64_t skin,uint32_t tv_index,
                             int tv_enabled,unsigned frames,FILE *trace,char *error,size_t capacity)
{
    if(npc->attached)return 1;
    if(!wait&&c3_pack_stream_done(npc->stream)<npc->count)return 1;
    c3_pack_stream_finish(npc->stream);npc->stream=NULL;
    char story_error[C3_STORY_ERROR];
    for(uint32_t j=0;j<npc->count;++j){
        C3PackJob *job=npc->jobs+j;uint32_t slot=npc->first+j,i=npc->area_index[j];
        if(trace)fprintf(trace,"field_pack_load file=%s bytes=%lu us=%llu ok=%d streamed=1\n",job->path,
                         (unsigned long)job->title->file_bytes,(unsigned long long)job->microseconds,job->ok);
        if(!job->ok){snprintf(error,capacity,"%s",job->error);return 0;}
        snprintf(paths[slot],sizeof paths[slot],"%s",job->path);
        models[slot]=storage+slot;actor_models[i]=storage+slot;
        if(!c3_title_animate(storage+slot,0,0)){snprintf(error,capacity,"Original NPC idle motion is missing.");return 0;}
        if(!c3_field_story_attach_actor(story,i,storage+slot,story_error)){snprintf(error,capacity,"%s",story_error);return 0;}
    }
    free(npc->jobs);npc->jobs=NULL;
    if(c3_render_recover(trace,"people-stream",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){snprintf(error,capacity,"Renderer recovery requested an exit.");return 0;}
    if(field_renderer_init(storage,models,count,skin,error,capacity))return 0;
    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,tv_enabled,error,capacity))return 0;
    npc->attached=1;
    if(trace){fprintf(trace,"field_people_stream count=%lu us=%llu\n",(unsigned long)npc->count,
                      (unsigned long long)(c3_n3ds_time_us()-npc->start_us));fflush(trace);}
    return 1;
}
/* A story battle other than encounter 10, on the general battle engine:
 * the pack is loaded only for the battle (Phenac's heap is nearly full).
 * autoplay: both sides use the engine's policy (campaign build).
 * Returns 1 played and applied, 0 not playable here (the caller skips it),
 * 2 the application must exit, -1 error (in error). */
static void release_field_models(C3Title *storage,uint32_t count);
static int restore_field_models(C3Title *storage,uint32_t count,char paths[][160],C3FieldStory *story,
                                float elapsed,char *error,size_t capacity);
/* What a 3D story battle needs from the field to free its models first and
 * restore them afterwards (the encounter-10 path does the same inline). */
typedef struct {
    C3Title *storage; const C3Title **models; C3Title **actor_models; char (*paths)[160];
    C3FieldStory *story; NpcStream *npc; C3IngameMenu *ingame;
    uint32_t count, tv_index; uint64_t skin; int tv_enabled; unsigned frames; float elapsed;
} FieldBattleHost;
static int field_battle_release(FieldBattleHost *h,FILE *trace,char *error,size_t capacity)
{
    c3_n3ds_log_memory(trace,"field_battle_before_drain");
    if(c3_render_recover(trace,"field-to-battle",h->frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)return -1;
    /* A battle start waits for the streamed people, as encounter 10 does. */
    if(!h->npc->attached&&!npc_stream_attach(h->npc,1,h->storage,h->models,h->actor_models,h->paths,h->story,h->count,
                                             h->skin,h->tv_index,h->tv_enabled,h->frames,trace,error,capacity))return 0;
    release_field_models(h->storage,h->count);
    if(h->ingame->active)c3_ingame_close(h->ingame);
    c3_pack_cache_clear(); /* the battle needs the linear memory */
    c3_n3ds_log_memory(trace,"field_battle_payloads_released");
    return 1;
}
static int field_battle_restore(FieldBattleHost *h,int exiting,FILE *trace,char *error,size_t capacity)
{
    if(c3_render_recover(trace,"battle-to-field",0,exiting?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)return -1;
    c3_n3ds_log_memory(trace,"field_battle_before_restore");
    if(!restore_field_models(h->storage,h->count,h->paths,h->story,h->elapsed,error,capacity))return 0;
    if(field_renderer_init(h->storage,h->models,h->count,h->skin,error,capacity))return 0;
    if(h->tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(h->tv_index,1,1,h->tv_enabled,error,capacity))return 0;
    c3_n3ds_log_memory(trace,"field_battle_restored");
    return 1;
}
/* host: the field to release for a 3D battle (NULL: bottom screen only). */
static int play_story_battle(C3FieldStory *story,C3GameState *state,uint32_t encounter,int autoplay,
                             FieldBattleHost *host,FILE *trace,char *error,size_t capacity)
{
    C3BattlePack pack;memset(&pack,0,sizeof pack);
    char problem[C3_GB_ERROR_SIZE]="";C3BattleEncounterInfo info;
    if(!c3_battle_pack_load(&pack,C3_BATTLE_PACK_PATH,problem)){
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu status=no_pack reason=%s\n",(unsigned long)encounter,problem);fflush(trace);}
        return 0;
    }
    if(!c3_battle_pack_info(&pack,encounter,&info)||info.reason){
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu status=refused reason=%s\n",(unsigned long)encounter,
                          c3_battle_pack_reason(info.reason));fflush(trace);}
        c3_battle_pack_free(&pack);return 0;
    }
    uint32_t seed=(uint32_t)c3_game_state_random16(state)<<16|c3_game_state_random16(state);
    C3GeneralRewards rewards;memset(&rewards,0,sizeof rewards);int result=-1;
    if(autoplay){
        unsigned turns=0;
        result=c3_battle_story_autoplay(story,state,&pack,encounter,seed,NULL,&rewards,&turns,problem)?1:-1;
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu trainer=%s mode=autoplay turns=%u result=%d code=%u\n",
                          (unsigned long)encounter,info.trainer,turns,result,(unsigned)rewards.result_code);fflush(trace);}
    }else{
        uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE];
        C3GeneralBattle *battle=malloc(sizeof *battle);int ok=battle!=NULL;
        for(unsigned i=0;ok&&i<C3_GB_PARTY;++i)ok=c3_game_state_pokemon(state,i,party[i]);
        if(ok)ok=c3_general_battle_begin(battle,&pack,encounter,party,seed,problem);
        else if(!problem[0])snprintf(problem,sizeof problem,"The party could not be read for the battle.");
        if(ok){
            int scene=-1;char why[192]="";
            if(host&&c3_battle_general_3d_ready(battle,trace,why,sizeof why)){
                /* On its original stage: the field's models make room first. */
                int released=field_battle_release(host,trace,problem,sizeof problem);
                if(released<0){free(battle);c3_battle_pack_free(&pack);return 2;}
                if(!released){free(battle);c3_battle_pack_free(&pack);
                    snprintf(error,capacity,"%s",problem[0]?problem:"The field could not make room for the battle.");return -1;}
                scene=c3_battle_general_3d_run(&pack,battle,trace);
                int restored=field_battle_restore(host,scene==1,trace,problem,sizeof problem);
                if(restored<0)scene=1;
                else if(!restored){free(battle);c3_battle_pack_free(&pack);
                    snprintf(error,capacity,"%s",problem[0]?problem:"The field could not be restored after the battle.");return -1;}
                /* A failed 3D stage leaves the battle intact: finish it below. */
                if(scene<0&&trace){fprintf(trace,"field_battle_3d_fallback encounter=%lu turn=%lu\n",(unsigned long)encounter,(unsigned long)battle->turn);fflush(trace);}
            }else if(trace&&host){fprintf(trace,"field_battle_3d_unavailable encounter=%lu reason=%s\n",(unsigned long)encounter,why);fflush(trace);}
            if(scene<0)scene=c3_battle_general_scene_run(&pack,battle,trace);
            if(scene==1)result=2;
            else if(scene<0){if(!problem[0])snprintf(problem,sizeof problem,"The battle engine failed (see the trace).");}
            else result=c3_battle_story_apply(story,state,battle,&pack,NULL,&rewards,problem)?1:-1;
        }
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu trainer=%s mode=played result=%d code=%u prize=%lu\n",
                          (unsigned long)encounter,info.trainer,result,(unsigned)rewards.result_code,(unsigned long)rewards.prize);fflush(trace);}
        free(battle);
    }
    c3_battle_pack_free(&pack);
    if(result<0)snprintf(error,capacity,"%s",problem[0]?problem:"The story battle failed.");
    return result;
}
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
/* Third-person orbit / free camera (orbit_camera.h); kept across floors. */
static C3OrbitCamera orbit={.zoom=1.0f};
#ifdef C3_CAMPAIGN
/* Campaign build: an autopilot plays the story unattended. When the hero
 * has control it starts the next event of the floor that was not yet tried
 * at the current story-flag state (NPC talks first, then touch events, then
 * doors; when all were tried, the least used door). Dialogue is advanced,
 * choices take their default, name entry keeps a default name, shops close,
 * encounter 10 is played in the battle scene with automatic A presses and
 * other encounters are reported as wins until their battles are ported.
 * Every step is logged to campaign/campaign-log.txt; the first arrival on a
 * floor is captured and saved to the campaign's own save. */
#define CAMPAIGN_HOURS 9.0
#define CAMPAIGN_STUCK_SECONDS 150.0f
#define CAMPAIGN_TRIED 32768u
static struct {
    FILE *log; uint64_t start_us; unsigned steps, floors_seen;
    uint64_t tried[CAMPAIGN_TRIED]; unsigned tried_count;
    uint16_t visits[0x400], uses[0x400][64];
    uint8_t seen[0x400];
    unsigned talk_shots, talk_shot_frame, talk_shot_id, bottom_shot_frame;
} campaign;
static uint32_t campaign_flags(C3GameState *state,uint32_t *set)
{
    uint32_t hash=2166136261u,value;*set=0;
    for(uint32_t id=1;id<3593;++id)if(c3_game_state_get_flag(state,id,&value)&&value){
        ++*set;hash=(hash^id)*16777619u;hash=(hash^value)*16777619u;}
    return hash;
}
extern uint32_t c3_campaign_launch;
static uint32_t campaign_pick(uint32_t floor)
{
    uint32_t h=c3_campaign_launch*0x9E3779B9u^campaign.steps*0x85EBCA6Bu^floor*0xC2B2AE35u;
    h^=h>>16;h*=0x7FEB352Du;h^=h>>15;h*=0x846CA68Bu;h^=h>>16;
    return h;
}
static int campaign_try(uint64_t key)
{
    for(unsigned i=0;i<campaign.tried_count;++i)if(campaign.tried[i]==key)return 0;
    if(campaign.tried_count<CAMPAIGN_TRIED)campaign.tried[campaign.tried_count++]=key;
    return 1;
}
#define CAMPAIGN_TRIED_PATH "sdmc:/3ds/Colosseum3DS/campaign/tried.bin"
#define CAMPAIGN_LAUNCH_PATH "sdmc:/3ds/Colosseum3DS/campaign/launches.bin"
/* Launch count, kept on the SD card: emulated time repeats from boot to
 * boot, so choices that must differ between relaunches mix this in. */
uint32_t c3_campaign_launch;
/* A well-mixed choice from the launch count, the step and the floor (a
 * plain linear mix kept the same parity on alternating floors). */
static uint32_t campaign_pick(uint32_t floor);
static void campaign_count_launch(void)
{
    FILE *f=fopen(CAMPAIGN_LAUNCH_PATH,"rb");
    if(f){if(fread(&c3_campaign_launch,sizeof c3_campaign_launch,1,f)!=1)c3_campaign_launch=0;fclose(f);}
    ++c3_campaign_launch;
    f=fopen(CAMPAIGN_LAUNCH_PATH,"wb");
    if(f){fwrite(&c3_campaign_launch,sizeof c3_campaign_launch,1,f);fclose(f);}
}
static void campaign_load_tried(void)
{
    FILE *f=fopen(CAMPAIGN_TRIED_PATH,"rb");
    if(f){campaign.tried_count=(unsigned)fread(campaign.tried,sizeof campaign.tried[0],CAMPAIGN_TRIED,f);fclose(f);}
}
static void campaign_store_tried(void)
{
    FILE *f=fopen(CAMPAIGN_TRIED_PATH,"wb");
    if(f){fwrite(campaign.tried,sizeof campaign.tried[0],campaign.tried_count,f);fclose(f);}
}
static void campaign_line(const char *format,...)
{
    if(!campaign.log){campaign.log=fopen("sdmc:/3ds/Colosseum3DS/campaign/campaign-log.txt","a");
        if(campaign.log)fprintf(campaign.log,"campaign_begin build=%s\n",C3_BUILD_ID);}
    if(!campaign.log)return;
    va_list args;va_start(args,format);
    fprintf(campaign.log,"t=%.0f ",(double)(c3_n3ds_time_us()-campaign.start_us)/1e6);
    vfprintf(campaign.log,format,args);va_end(args);fputc('\n',campaign.log);fflush(campaign.log);
}
#endif
#ifdef C3_WARP_TOUR
/* Warp tour (tour build only): each floor runs for a fixed time with
 * dialogue advanced by A, battles reported as wins and script floor
 * changes recorded instead of followed; then the top screen is captured. */
#define TOUR_SECONDS 12.0f
static struct { uint32_t next_floor, frames, draws, control, message; float seconds; int captured; char shot[96]; } tour;
#endif

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
    /* The pack cache shares linear memory with the renderer: if the scene
     * cannot start, empty it and try once more; once it runs, keep a margin
     * free for buffers allocated later (HUD, particles, audio). */
    if(c3_stage_renderer_init(models,count,error,capacity)){
        c3_pack_cache_clear();
        if(c3_stage_renderer_init(models,count,error,capacity))return 1;
    }
    c3_pack_cache_trim(6u*1024u*1024u);
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
    C3AreaCamera event_cameras[7]={{0}};uint32_t event_camera_count=0;
    C3Title *storage=NULL;const C3Title *models[C3_STAGE_MAX_MODELS]={0};
    C3Title *actor_models[C3_AREA_MAX_ACTORS]={0};
    C3AreaDoors doors={0};C3Title *door_models[C3_AREA_MAX_DOORS]={0};
    char model_paths[C3_STAGE_MAX_MODELS][160]={{0}};
    C3TitleCamera camera;C3TitleRenderStats stats={0};C3N3dsInput input={0};
    uint32_t opened=0,count=0,arrival_done=0,opening_done=0,arrival_count=0;
    unsigned wes_index=0,arrival_index=0,tv_index=UINT32_MAX,selection=0,frames=0;
    /* Load timing for the trace: area start, each pack, first control. */
    uint64_t load_start_us=c3_n3ds_time_us(),pack_us=0;int load_reported=0;
    /* People stream in after the room: their packs load on the second core
     * while the scene runs, and join the renderer when all have arrived. */
    NpcStream npc={0};uint64_t skin_core=0;
    c3_title_perf_clock(c3_n3ds_time_us);
    int result=-1,yes=0,dirty=1,arrival_end_sampled=0,entered=0,lights_loaded=0;
    FieldPhase phase=FIELD_SETUP;PausePage page=PAUSE_CLOSED;
    float elapsed=0,phase_seconds=0,arrival_seconds=0;
    uint64_t previous=0,next_console=0,next_trace=0,skin_actors=0;
    C3UiPager ui_pager={0};
    C3NameEntry name_entry;int naming=0;
    C3ShopMenu shop_menu;C3ShopTable shop_table={0};C3PdaAssets shop_assets={0};
    uint8_t *shop_pda=NULL,*shop_pack=NULL;size_t shop_pack_size=0;int shopping=0;
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
    if(trace){fprintf(trace,"field_load_stage name=%s us=%llu\n","field_area_files",(unsigned long long)(c3_n3ds_time_us()-load_start_us));}
    {   /* Event surfaces may sit on CCD objects past the walk/wall rows
         * (the mayor's-house exit is on object 3 of three): cover them. */
        uint32_t objects=0;
        for(uint32_t i=0;i<area.transition_count;++i)if(area.transitions[i].object+1u>objects)objects=area.transitions[i].object+1u;
        for(uint32_t i=0;i<area.interaction_count;++i)if(area.interactions[i].object+1u>objects)objects=area.interactions[i].object+1u;
        if(!c3_field_cover_objects(&map,objects)){snprintf(error,capacity,"The area's event surfaces exceed the collision object bound.");goto done;}
    }
    if(map.floor!=summary.floor||area.floor!=map.floor||map.actor_bone!=UINT32_MAX||map.actor_first_vertex!=0u){
        snprintf(error,capacity,"The field and interaction packs disagree.");goto done;
    }
    if(!c3_game_state_get_flag(state,0x08D0,&opening_done)||!c3_game_state_get_flag(state,0x0AF8,&arrival_done)){
        snprintf(error,capacity,"The original field progress flags are unavailable.");goto done;
    }
    if(!c3_field_spawn(&map,summary.spawn,&player)){
        /* A checkpoint whose entrance this floor's table lacks (a door that
         * leads to an entrance the prepared pack does not list): enter by
         * the first entrance and record it, rather than refusing the save. */
        if(trace){fprintf(trace,"field_spawn_fallback floor=%lu spawn=%u used=0\n",(unsigned long)map.floor,summary.spawn);fflush(trace);}
        if(!c3_field_spawn(&map,0,&player)){snprintf(error,capacity,"The saved area entrance is unavailable.");goto done;}
    }
    arrival_count=(profile->features&C3_FLOOR_ARRIVAL)&&opening_done&&!arrival_done?map.arrival_pack_count:0;
    count=map.map_pack_count+1u+arrival_count;wes_index=map.map_pack_count;arrival_index=wes_index+1u;
    /* The floor's door table (native 60) and the object pack of each door
     * that has one; those rest on their first frame until a script plays a
     * clip. A door without a pack changes only its open flag and collision,
     * as the original does for a door without a model. */
    snprintf(path,sizeof path,BOOT_ROOT "%s.c3d",prefix);
    if(exists(path)&&!c3_area_doors_load(&doors,path,error,capacity))goto done;
    for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0])++count;
    if(profile->features&C3_FLOOR_TV)tv_index=count++;
    npc.first=count;
    for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0]){++count;++npc.count;}
    if(count>C3_STAGE_MAX_MODELS){snprintf(error,capacity,"The area exceeds the model bound.");goto done;}
    storage=calloc(count,sizeof(*storage));
    if(!storage){snprintf(error,capacity,"Field model allocation failed.");goto done;}
    c3_bottom_screen_notice(profile->label,"Loading the area...","Please wait...");
    unsigned load_total=map.map_pack_count+1u+arrival_count;
    for(uint32_t i=0;i<doors.count;++i)load_total+=doors.rows[i].file[0]!=0;
    c3_n3ds_top_loading(0.0f);
    /* Every pack is a job with a fixed slot (the same order as before):
     * the main core and the New 3DS's second core load them in parallel,
     * then the slots are bound and posed in order on this thread. */
    C3PackJob *jobs=calloc(load_total?load_total:1u,sizeof *jobs);
    if(!jobs){snprintf(error,capacity,"Not enough memory for the load list.");goto done;}
    {
        unsigned job=0,slot=0;
        for(uint32_t i=0;i<map.map_pack_count+1u+arrival_count;++i,++job,++slot){
            if(i<map.map_pack_count)snprintf(jobs[job].path,sizeof jobs[job].path,BOOT_ROOT "%s_map%lu.c3t",prefix,(unsigned long)i);
            else if(i==wes_index)snprintf(jobs[job].path,sizeof jobs[job].path,BOOT_ROOT "%s_wes.c3t",prefix);
            else snprintf(jobs[job].path,sizeof jobs[job].path,BOOT_ROOT "%s_arrival%lu.c3t",prefix,(unsigned long)(i-arrival_index));
            jobs[job].title=storage+slot;
        }
        for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0]){
            snprintf(jobs[job].path,sizeof jobs[job].path,BOOT_ROOT "%s",doors.rows[i].file);jobs[job++].title=storage+slot++;}
    }
    if(npc.count){
        npc.jobs=calloc(npc.count,sizeof *npc.jobs);
        if(!npc.jobs){snprintf(error,capacity,"Not enough memory for the people list.");free(jobs);goto done;}
        unsigned job=0;uint32_t slot=npc.first;
        for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0]){
            snprintf(npc.jobs[job].path,sizeof npc.jobs[job].path,BOOT_ROOT "%s",area.actors[i].file);
            npc.jobs[job].title=storage+slot;npc.area_index[job]=i;++job;++slot;}
    }
    pack_us=c3_n3ds_time_us();
    size_t failed=c3_pack_load_all(jobs,load_total,load_progress);
    if(trace){fprintf(trace,"field_pack_jobs count=%u us=%llu\n",load_total,(unsigned long long)(c3_n3ds_time_us()-pack_us));}
    for(unsigned j=0;j<load_total;++j){
        if(trace)fprintf(trace,"field_pack_load file=%s bytes=%lu us=%llu ok=%d\n",jobs[j].path,
                         (unsigned long)jobs[j].title->file_bytes,(unsigned long long)jobs[j].microseconds,jobs[j].ok);
    }
    opened=load_total; /* every room slot; `done` closes them, loaded or not */
    if(failed){snprintf(error,capacity,"%s",jobs[failed-1].error);free(jobs);goto done;}
    {
        unsigned job=0;
        for(uint32_t i=0;i<map.map_pack_count+1u+arrival_count;++i,++job){
            snprintf(model_paths[i],sizeof model_paths[i],"%s",jobs[job].path);models[i]=storage+i;
            if(!c3_title_animate(storage+i,0,0)){snprintf(error,capacity,"Initial field animation failed.");free(jobs);goto done;}
        }
        uint32_t slot=map.map_pack_count+1u+arrival_count;
        for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0]){
            C3Title *model=storage+slot;snprintf(model_paths[slot],sizeof model_paths[slot],"%s",jobs[job++].path);
            models[slot++]=model;door_models[i]=model;
            int rest=doors.rows[i].open_anim>=0?doors.rows[i].open_anim:0;
            if(!c3_title_animate(model,(uint32_t)rest,0)){snprintf(error,capacity,"Original door clip is missing.");free(jobs);goto done;}
        }
    }
    free(jobs);
#ifdef C3_CAMPAIGN
    {static int loading_shot;if(!loading_shot){loading_shot=1;(void)c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/campaign/loading.bmp");}}
#endif
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
    /* Reviewed floors name their event camera; a surveyed floor loads one
     * when tools/prepare_tour.py prepared it. */
    if(trace){fprintf(trace,"field_load_stage name=%s us=%llu\n","packs",(unsigned long long)(c3_n3ds_time_us()-load_start_us));}
    snprintf(path,sizeof path,BOOT_ROOT "%s_camera.c3c",prefix);
    if((profile->features&C3_FLOOR_ROOM_CAMERA)||exists(path)){
        if(!c3_area_camera_load(&room_camera,path,error,capacity))goto done;
    }
    /* Further event cameras (prefix_camera1.c3c ...) of a surveyed floor. */
    for(unsigned i=1;i<=7;++i){
        snprintf(path,sizeof path,BOOT_ROOT "%s_camera%u.c3c",prefix,i);
        if(!exists(path))break;
        if(!c3_area_camera_load(event_cameras+event_camera_count,path,error,capacity))goto done;
        ++event_camera_count;
    }
    if(profile->features&C3_FLOOR_LIGHTS){
        snprintf(path,sizeof path,BOOT_ROOT "%s.c3l",prefix);
        if(!c3_area_lights_load(&lights,path,error,capacity))goto done;
        lights_loaded=1;
    }
    if(opened!=npc.first){snprintf(error,capacity,"Area model identity mismatch.");goto done;}
    if(trace){
        size_t cpu_models=0;
        for(uint32_t i=0;i<count;++i)cpu_models+=storage[i].resident_bytes;
        fprintf(trace,"field_models floor=%lu models=%lu cpu_resident_bytes=%lu\n",
                (unsigned long)map.floor,(unsigned long)count,(unsigned long)cpu_models);
        fflush(trace);
    }
    skin_actors=UINT64_C(1)<<wes_index;
    for(uint32_t i=arrival_index+arrival_count;i<count&&i<64u;++i)if(i!=tv_index)skin_actors|=UINT64_C(1)<<i;
    skin_core=skin_actors&(npc.first>=64u?~UINT64_C(0):(UINT64_C(1)<<npc.first)-1u);
    if(trace){fprintf(trace,"field_load_stage name=%s us=%llu\n","cameras_lights",(unsigned long long)(c3_n3ds_time_us()-load_start_us));}
    /* The room first; people join the renderer when their packs arrive. */
    if(field_renderer_init(storage,models,npc.first,skin_core,error,capacity))goto done;
    if(trace){fprintf(trace,"field_load_stage name=%s us=%llu\n","renderer",(unsigned long long)(c3_n3ds_time_us()-load_start_us));}
    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,true,error,capacity))goto done;
    snprintf(path,sizeof path,"%s_ready",prefix);
    c3_n3ds_log_memory(trace,path);
    snprintf(path,sizeof path,BOOT_ROOT "%s",profile->story);
    if(!c3_field_story_load(&story,path,state,&map,&area,&player,wes,actor_models,&room_camera,trace,story_error)||
       !c3_field_story_bind_battle(&story,BOOT_ROOT "willie.c3b",story_error))goto story_failed;
    if(trace){fprintf(trace,"field_load_stage name=%s us=%llu\n","story",(unsigned long long)(c3_n3ds_time_us()-load_start_us));}
    if(npc.count){npc.stream=c3_pack_stream_start(npc.jobs,npc.count);npc.start_us=c3_n3ds_time_us();
        if(!npc.stream){snprintf(error,capacity,"The people loader could not start.");goto done;}}
    else npc.attached=1;
    if(doors.count){
        uint32_t with_model=0;for(uint32_t i=0;i<doors.count;++i)with_model+=door_models[i]!=NULL;
        if(!c3_field_story_bind_doors(&story,doors.rows,doors.count,door_models,story_error))goto story_failed;
        if(trace){fprintf(trace,"field_doors floor=%lu rows=%lu models=%lu\n",(unsigned long)map.floor,(unsigned long)doors.count,(unsigned long)with_model);fflush(trace);}
    }
    c3_field_story_set_floor_support(&story,floor_reachable);
    c3_field_story_bind_cameras(&story,event_cameras,event_camera_count);
    c3_field_story_enable_name_entry(&story,1);
    c3_field_story_set_random(&story,c3_game_state_random16,state);
    c3_field_story_set_species_namer(&story,field_species_name,ingame);
    c3_field_story_enable_shop(&story,1);
    c3_field_story_set_item_service(&story,receive_item,state);
    c3_field_story_set_party_service(&story,restore_party,state);
    c3_field_story_set_audio_service(&story,story_audio,NULL);
    if(!c3_field_story_prepare(&story,story_error))goto story_failed;
    view=c3_field_story_view(&story);
    if(trace){fprintf(trace,"field_begin floor=%lu previous=%lu spawn=%u new=%d maps=%lu npcs=%lu models=%lu arrival=%d duration=%.3f\n",
        (unsigned long)map.floor,(unsigned long)summary.previous_floor,summary.spawn,new_game,(unsigned long)map.map_pack_count,
        (unsigned long)area.actor_count,(unsigned long)count,arrival!=NULL,(double)arrival_seconds);fflush(trace);}
    previous=c3_n3ds_active_time_us();result=1;
    while(c3_n3ds_poll(&input)){
        if(!npc.attached&&!npc_stream_attach(&npc,0,storage,models,actor_models,model_paths,&story,count,skin_actors,
                                             tv_index,view?view->tv_enabled:1,frames,trace,error,capacity)){result=-1;break;}
#ifdef C3_CAMPAIGN
        /* Review aid: the top screen half a second after the people joined. */
        {static unsigned people_frame;static int people_done;
         if(npc.count&&npc.attached&&!people_done&&!people_frame)people_frame=frames+4u;
         if(people_frame&&frames>=people_frame){char shot[112];people_done=1;people_frame=0;
             snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/campaign/people_%s.bmp",prefix);(void)c3_n3ds_capture_top(shot);}}
#endif
        uint64_t now=c3_n3ds_active_time_us();float delta=(float)(now-previous)/1000000.0f;
        previous=now;elapsed+=delta;phase_seconds+=delta;
        c3_game_state_add_play_time(state,delta);
#ifdef C3_WARP_TOUR
        if(page==PAUSE_CLOSED){
            if((view->dialogue||view->choice)&&frames%20==0)input.pressed|=C3_PAD_A;
            if(view->shop&&!shopping&&!c3_field_story_shop_done(&story,story_error))goto story_failed;
            if(view->blocked[0]&&view->battle_id&&!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
        }
        if(elapsed>=TOUR_SECONDS&&!tour.captured)tour.captured=-1; /* capture after this frame */
#endif
#ifdef C3_CAMPAIGN
        {
            static float idle,busy;
            if(!campaign.start_us)campaign.start_us=c3_n3ds_time_us();
            if((double)(c3_n3ds_time_us()-campaign.start_us)/3.6e9>=CAMPAIGN_HOURS){
                uint32_t set;campaign_flags(state,&set);
                campaign_line("campaign_end hours=%.1f steps=%u floors=%u flags=%lu",CAMPAIGN_HOURS,campaign.steps,campaign.floors_seen,(unsigned long)set);
                result=1;break;
            }
            if(page==PAUSE_CLOSED){
                if((view->dialogue||view->choice)&&frames%20==0)input.pressed|=C3_PAD_A;
                if(view->shop&&!shopping&&!c3_field_story_shop_done(&story,story_error))goto story_failed;
                if(view->blocked[0]&&view->battle_id==10&&phase==FIELD_CONTROL){
                    campaign_line("floor=%03lx battle=10 mode=scene",(unsigned long)map.floor);
                    c3_n3ds_autopress(1,1.6f);input.pressed|=C3_PAD_A;
                }else if(view->blocked[0]&&view->battle_id){
                    int played=play_story_battle(&story,state,view->battle_id,1,NULL,trace,story_error,sizeof story_error);
                    if(played<0)goto story_failed;
                    campaign_line("floor=%03lx battle=%lu mode=%s",(unsigned long)map.floor,(unsigned long)view->battle_id,
                                  played==1?"engine_autoplay":"reported_win");
                    if(played!=1&&!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
                }else if(view->blocked[0]){
                    /* An unprepared destination: reload this floor (the
                     * flags the script already wrote are kept). */
                    /* Save and exit: the host watcher restarts the campaign
                     * with fresh memory (an in-process reload of a full floor
                     * can fragment the heap). The flags the script wrote stay. */
                    int saved=c3_game_state_set_floor(state,map.floor,summary.previous_floor,0)&&
                              save_game(state,save_path,trace,message,sizeof message);
                    campaign_store_tried();
                    campaign_line("floor=%03lx blocked reason=\"%s\" action=restart saved=%d",(unsigned long)map.floor,view->blocked,saved);
                    result=1;break;
                }
            }
            if(campaign.bottom_shot_frame&&frames>=campaign.bottom_shot_frame&&page==PAUSE_CLOSED&&view->control){
                char shot[112];
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/campaign/%s_bottom.bmp",prefix);
                campaign_line("floor=%03lx bottom_shot=%d",(unsigned long)map.floor,c3_n3ds_capture_bottom(shot)==0);
                campaign.bottom_shot_frame=0;
            }
            if(campaign.talk_shot_frame&&frames>=campaign.talk_shot_frame){
                char shot[112];
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/campaign/talk_%03lx_%02u_L%lu.bmp",(unsigned long)map.floor,
                         campaign.talk_shot_id,(unsigned long)c3_campaign_launch);
                campaign_line("floor=%03lx talk_shot=%d id=%u",(unsigned long)map.floor,c3_n3ds_capture_top(shot)==0,campaign.talk_shot_id);
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/campaign/talk_%03lx_%02u_L%lu_bottom.bmp",(unsigned long)map.floor,
                         campaign.talk_shot_id,(unsigned long)c3_campaign_launch);
                (void)c3_n3ds_capture_bottom(shot);
                campaign.talk_shot_frame=0;++campaign.talk_shots;
            }
            if(phase==FIELD_CONTROL&&page==PAUSE_CLOSED){
                if(!campaign.seen[map.floor&0x3ffu]){
                    char shot[96];uint32_t set;campaign_flags(state,&set);
                    campaign.seen[map.floor&0x3ffu]=1;++campaign.floors_seen;
                    snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/campaign/%s.bmp",prefix);
                    int captured=frames>2&&c3_n3ds_capture_top(shot)==0;
                    campaign.bottom_shot_frame=frames+30;
                    int saved=save_game(state,save_path,trace,message,sizeof message);
                    campaign_store_tried();
                    campaign_line("floor=%03lx first_arrival flags=%lu shot=%d saved=%d",(unsigned long)map.floor,(unsigned long)set,captured,saved);
                }
                if(view->control&&!view->dialogue&&!view->blocked[0]){busy=0;idle+=delta;}
                else{idle=0;busy+=delta;}
                if(busy>CAMPAIGN_STUCK_SECONDS){
                    int saved=c3_game_state_set_floor(state,map.floor,summary.previous_floor,0)&&
                              save_game(state,save_path,trace,message,sizeof message);
                    campaign_store_tried();
                    campaign_line("floor=%03lx stuck seconds=%.0f message=%05lx action=restart saved=%d",(unsigned long)map.floor,(double)busy,(unsigned long)view->message_id,saved);
                    busy=0;result=1;break;
                }
                if(idle>0.75f&&npc.attached){
                    /* Choose the next event: untried at this flag state first. */
                    uint32_t set,digest=campaign_flags(state,&set);
                    int kind=-1;uint32_t id=0,best_uses=UINT32_MAX,slot=0;
                    for(int pass=0;pass<3&&kind<0;++pass){
                        if(pass==0)for(uint32_t i=0;i<area.actor_count&&kind<0;++i){
                            C3StoryActor *actor=c3_field_story_actor(&story,area.actors[i].index);
                            if(!area.actors[i].talk_script||!actor||!actor->visible)continue;
                            uint64_t key=((uint64_t)map.floor<<48)|((uint64_t)3<<44)|((uint64_t)area.actors[i].index<<32)|digest;
                            if(campaign_try(key)){kind=3;id=area.actors[i].index;}
                        }
                        else for(uint32_t i=0;i<area.link_count&&kind<0;++i){
                            const C3AreaLink *link=area.links+i;
                            if(link->kind!=(pass==1?2u:1u))continue;
                            uint64_t key=((uint64_t)map.floor<<48)|((uint64_t)link->kind<<44)|((uint64_t)link->source_id<<32)|digest;
                            if(campaign_try(key)){kind=(int)link->kind;id=link->source_id;}
                        }
                    }
                    /* Every event tried: a random door, so relaunches (which
                     * reset the in-memory use counts) do not replay one route. */
                    if(kind<0){uint32_t doors=0;for(uint32_t i=0;i<area.link_count;++i)doors+=area.links[i].kind==1u;
                        if(doors){uint32_t pick=campaign_pick(map.floor)%doors;
                            for(uint32_t i=0;i<area.link_count;++i)if(area.links[i].kind==1u&&!pick--){kind=1;id=area.links[i].source_id;break;}}}
                    /* No door (an exit made of touch events such as stairs):
                     * a random touch event; with none, back to the floor we
                     * came from through a save and relaunch. */
                    if(kind<0){uint32_t touches=0;for(uint32_t i=0;i<area.link_count;++i)touches+=area.links[i].kind==2u;
                        if(touches){uint32_t pick=campaign_pick(map.floor)%touches;
                            for(uint32_t i=0;i<area.link_count;++i)if(area.links[i].kind==2u&&!pick--){kind=2;id=area.links[i].source_id;break;}}}
                    if(kind<0&&summary.previous_floor&&summary.previous_floor!=map.floor){
                        int saved=c3_game_state_set_floor(state,summary.previous_floor,map.floor,0)&&
                                  save_game(state,save_path,trace,message,sizeof message);
                        campaign_store_tried();
                        campaign_line("floor=%03lx no_event action=return_to=%03lx saved=%d",(unsigned long)map.floor,(unsigned long)summary.previous_floor,saved);
                        result=1;break;
                    }
                    (void)best_uses;
                    if(kind==1)for(uint32_t i=0;i<area.link_count&&i<64;++i)if(area.links[i].kind==1u&&area.links[i].source_id==id){slot=i;break;}
                    if(kind==1&&slot<64)++campaign.uses[map.floor&0x3ffu][slot];
                    ++campaign.steps;idle=0;
                    campaign_line("floor=%03lx step=%u action=%s id=%lu flags=%lu",(unsigned long)map.floor,campaign.steps,
                        kind==3?"talk":kind==2?"touch":kind==1?"door":"none",(unsigned long)id,(unsigned long)set);
                    if(kind==3){
                        C3StoryActor *actor=c3_field_story_actor(&story,id);
                        const C3AreaActor *source=NULL;
                        for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].index==id){source=area.actors+i;break;}
                        /* Stand where a player could: on ground, with no wall
                         * between the person and the spot and no other person
                         * on it. Front first, then around (45 degree steps). */
                        float yaw=actor->pose.yaw,reach=map.radius+(source?source->radius:4.0f*map.scale)+9.0f*map.scale;
                        float spot[3]={0},best[3];int found=0;
                        best[0]=actor->pose.position[0]+sinf(yaw)*reach;best[1]=actor->pose.position[1];
                        best[2]=actor->pose.position[2]+cosf(yaw)*reach;
                        /* Pass 0: a clear line to the person. Passes 1-2: across
                         * a counter or table, a spot clear of walls all around,
                         * at this reach and then at the edge of talk range. */
                        float limit=12.0f*map.scale+map.radius+(source?source->radius:4.0f*map.scale)-0.5f*map.scale;
                        for(int k=0;k<24&&!found;++k){
                            static const int order[8]={0,1,-1,2,-2,3,-3,4};
                            int pass=k/8;float r=pass==2?limit:reach;
                            float angle=yaw+(float)order[k%8]*0.785398163f;
                            C3FieldPlayer probe=player;probe.collision_count=0;
                            probe.position[0]=actor->pose.position[0]+sinf(angle)*r;
                            probe.position[1]=actor->pose.position[1];
                            probe.position[2]=actor->pose.position[2]+cosf(angle)*r;
                            memcpy(spot,probe.position,sizeof spot);
                            if(pass==0){
                                if(!c3_field_resolve_position(&map,&probe,actor->pose.position)||probe.collision_count)continue;
                            }else{
                                float ground;if(!c3_field_ground(&map,spot,&ground))continue;
                                spot[1]=probe.position[1]=ground;int blocked=0;
                                for(int side=0;side<4&&!blocked;++side){
                                    float from[3]={spot[0]+(side==0?3.0f:side==1?-3.0f:0)*map.scale,ground,
                                                   spot[2]+(side==2?3.0f:side==3?-3.0f:0)*map.scale};
                                    C3FieldPlayer edge=probe;edge.collision_count=0;memcpy(edge.position,spot,sizeof spot);
                                    blocked=!c3_field_resolve_position(&map,&edge,from)||edge.collision_count;
                                }
                                if(blocked)continue;
                            }
                            if(fabsf(probe.position[1]-actor->pose.position[1])>6.0f*map.scale)continue;
                            int crowded=0;
                            for(uint32_t j=0;j<area.actor_count&&!crowded;++j){
                                if(area.actors[j].index==id)continue;
                                C3StoryActor *other=c3_field_story_actor(&story,area.actors[j].index);
                                if(!other||!other->visible)continue;
                                float dx=other->pose.position[0]-spot[0],dz=other->pose.position[2]-spot[2];
                                crowded=hypotf(dx,dz)<map.radius+area.actors[j].radius;
                            }
                            if(crowded)continue;
                            memcpy(best,probe.position,sizeof best);found=1;
                        }
                        memcpy(player.position,best,sizeof best);
                        player.yaw=atan2f(actor->pose.position[0]-best[0],actor->pose.position[2]-best[2]);
                        memcpy(player.proposed,player.position,sizeof player.position);
                        if(!found)campaign_line("floor=%03lx talk=%lu spot=fallback",(unsigned long)map.floor,(unsigned long)id);
                        /* Review aid: the top screen a moment after the warp,
                         * for the first talks of each launch. */
                        if(campaign.talk_shots<30){campaign.talk_shot_frame=frames+12;campaign.talk_shot_id=id;}
                        if(!c3_field_story_interact(&story,story_error))goto story_failed;
                    }else if(kind==2){if(c3_field_story_touch(&story,id,story_error)<0)goto story_failed;}
                    else if(kind==1){if(!c3_field_story_transition(&story,id,story_error))goto story_failed;}
                    input.pressed=0;input.touch_pressed=false;dirty=1;
                }
            }
        }
#endif
        int story_consumed_input=view->dialogue||!view->control;
        /* Script native 16: the partner is named on the bottom-screen
         * keyboard while the event waits. The keyboard owns all input. */
#ifdef C3_CAMPAIGN
        if(view->name_entry&&!naming&&page==PAUSE_CLOSED){
            if(!c3_field_story_name_result(&story,"RUI",story_error))goto story_failed;
            campaign_line("floor=%03lx name_entry default=RUI",(unsigned long)map.floor);
        }
#endif
        if(view->name_entry&&!naming&&page==PAUSE_CLOSED&&c3_game_state_summary(state,&summary)){
            /* An unnamed partner holds the original placeholder (message
             * 0xFA2, "N/A"; docs/game-state.md): start empty, with no
             * fallback, rather than offering the placeholder as her name. */
            c3_name_entry_init(&name_entry,strcmp(summary.partner,"N/A")?summary.partner:NULL);naming=1;dirty=1;
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
        /* Script native 19 (menuShopOpen): the port's shop menu for the
         * original stock while the event waits. It owns all input. A shop
         * that cannot open (missing packs, nothing for sale) closes at once. */
        if(view->shop&&!shopping&&page==PAUSE_CLOSED){
            unsigned shop_number=view->shop-1u;
            shop_pda=open_pda(&shop_assets);
            shop_pack=read_boot_file(BOOT_ROOT "shops.c3h",&shop_pack_size);
            if(shop_pda&&shop_pack&&c3_shop_table_open(&shop_table,shop_pack,shop_pack_size)&&
               c3_shop_menu_open(&shop_menu,&shop_table,&shop_assets,shop_number)){shopping=1;dirty=1;}
            else{free(shop_pda);free(shop_pack);shop_pda=shop_pack=NULL;
                if(!c3_field_story_shop_done(&story,story_error))goto story_failed;}
            if(trace){fprintf(trace,"shop_open shop=%u items=%u opened=%d\n",shop_number,shopping?shop_menu.count:0u,shopping);fflush(trace);}
        }
        if(shopping){
            uint32_t money_before=summary.money;
            C3ShopEvent shop_event=c3_shop_menu_update(&shop_menu,state,&shop_assets,input.pressed);
            if(shop_event!=C3_SHOP_EVENT_NONE)dirty=1;
            if(trace&&shop_menu.stage==C3_SHOP_MENU_MESSAGE&&shop_event==C3_SHOP_EVENT_CHANGED&&c3_game_state_summary(state,&summary)){
                fprintf(trace,"shop_result item=%u quantity=%u money=%lu->%lu message=%s\n",shop_menu.items[shop_menu.cursor],
                        shop_menu.quantity,(unsigned long)money_before,(unsigned long)summary.money,shop_menu.message);fflush(trace);}
            if(shop_event==C3_SHOP_EVENT_CLOSE){
                free(shop_pda);free(shop_pack);shop_pda=shop_pack=NULL;shopping=0;
                if(!c3_field_story_shop_done(&story,story_error))goto story_failed;
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
#ifdef C3_WARP_TOUR
                /* The last drawn frame is still in the framebuffer. */
                tour.next_floor=view->next_floor;
                snprintf(tour.shot,sizeof tour.shot,"sdmc:/3ds/Colosseum3DS/tour/%s.bmp",prefix);
                tour.captured=frames>2&&c3_n3ds_capture_top(tour.shot)==0?1:2;
                tour.frames=frames;tour.draws=stats.draw_calls;tour.seconds=elapsed;
                tour.control=view->control;tour.message=view->message_id;
                result=3;break;
#endif
                if(view->next_spawn>255||!c3_game_state_set_floor(state,view->next_floor,map.floor,(uint8_t)view->next_spawn)){
                    snprintf(error,capacity,"Original doorway checkpoint is invalid.");result=-1;break;
                }
#ifdef C3_CAMPAIGN
                /* Successive floor loads fragment the heap (a full exterior
                 * needs almost all of it): after one in-process load the
                 * campaign saves at the doorway and the watcher relaunches. */
                /* campaign/cache-test on the SD card: allow more in-process
                 * floor changes, so round trips can hit the pack cache. */
                static int cache_test=-1;
                if(cache_test<0)cache_test=exists("sdmc:/3ds/Colosseum3DS/campaign/cache-test");
                {static unsigned loads;if(view->next_floor!=C3_FLOOR_WORLD_MAP_ID&&++loads>=(cache_test?8u:2u)){
                    int saved=save_game(state,save_path,trace,message,sizeof message);
                    campaign_store_tried();
                    campaign_line("floor=%03lx door_to=%03lx action=restart_for_memory saved=%d",(unsigned long)map.floor,(unsigned long)view->next_floor,saved);
                    result=1;break;}}
#endif
                result=2;break;
            }
        }
        float fade=view->fade;
        if(phase==FIELD_SETUP&&view->prepared){phase=arrival?FIELD_ARRIVAL:FIELD_FADE_IN;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_load_stage name=prepared frames=%u us=%llu\n",frames,(unsigned long long)(c3_n3ds_time_us()-load_start_us));}}
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
            if(!load_reported&&trace){c3_pack_cache_trace(trace,"control");load_reported=1;
                fprintf(trace,"field_load_to_control floor=%lu us=%llu\n",(unsigned long)map.floor,
                        (unsigned long long)(c3_n3ds_time_us()-load_start_us));fflush(trace);}
        }
        if(phase==FIELD_CONTROL){
            if(page==PAUSE_CLOSED){
                if(view->blocked[0]&&(input.pressed&(C3_PAD_START|C3_PAD_X))){result=0;break;}
                if(view->blocked[0]&&view->battle_id&&view->battle_id!=10&&(input.pressed&C3_PAD_A)){
                    /* A story battle always runs on the
                     * general battle engine. Only an encounter the engine
                     * refuses is reported as the hero's win (result ID 2),
                     * with nothing written. */
                    uint32_t skipped=view->battle_id;
                    {
                        FieldBattleHost host={storage,models,actor_models,model_paths,&story,&npc,ingame,
                                              count,tv_index,skin_actors,view?view->tv_enabled:1,frames,elapsed};
                        int played=play_story_battle(&story,state,skipped,0,&host,trace,story_error,sizeof story_error);
                        if(played<0)goto story_failed;
                        if(played==2){result=1;break;}
                        /* Battle time must not advance the paused field scripts. */
                        previous=c3_n3ds_active_time_us();
                        if(played==1){dirty=1;next_console=0;continue;}
                    }
                    if(!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
                    if(trace){fprintf(trace,"field_battle_skipped encounter=%lu result_reported=2 temporary=1 party_writeback=0\n",
                        (unsigned long)skipped);fflush(trace);}
                    dirty=1;continue;
                }
                if(view->blocked[0]&&view->battle_id==10&&(input.pressed&C3_PAD_A)){
                    c3_n3ds_log_memory(trace,"field_battle_before_drain");
                    if(c3_render_recover(trace,"field-to-battle",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT){result=1;break;}
                    if(!npc.attached&&!npc_stream_attach(&npc,1,storage,models,actor_models,model_paths,&story,count,skin_actors,
                                                         tv_index,view?view->tv_enabled:1,frames,trace,error,capacity)){result=-1;break;}
                    release_field_models(storage,count);
                    /* The bottom-screen hub keeps the P*DA art loaded; free it
                     * for the battle (it reloads on the next field frame). */
                    if(ingame->active)c3_ingame_close(ingame);
                    c3_pack_cache_clear(); /* the battle needs the linear memory */
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
#ifdef C3_CAMPAIGN
                    c3_n3ds_autopress(0,0);
                    campaign_line("floor=%03lx battle=10 returned preview=%d outcome=%d reported=%d",(unsigned long)map.floor,preview_result,battle_outcome,reported);
#endif
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
                    if(c3_orbit_free_chord(input.held,input.pressed)){
                        /* Hold ZL/ZR (or R) and press Y: detached free flight from the current view. */
                        if(orbit.free)orbit.free=0;
                        else{if(first_person.enabled)c3_first_person_toggle(&first_person,&player,map.floor);
                             c3_orbit_free_enter(&orbit,&camera);}
                        input.pressed&=(uint16_t)~(C3_PAD_Y|C3_PAD_Z|C3_PAD_R);
                        if(trace){fprintf(trace,"free_camera enabled=%d\n",orbit.free);fflush(trace);}
                    }
                    if(c3_orbit_recenter_chord(input.held,input.pressed)){
                        /* ZL/ZR (or R) + Down: leave free flight, authored view again. */
                        c3_orbit_reset(&orbit);
                        input.pressed&=(uint16_t)~(C3_PAD_DOWN|C3_PAD_Z|C3_PAD_R);
                        if(trace){fprintf(trace,"camera_recenter\n");fflush(trace);}
                    }
                    if(first_person.enabled){
                        c3_first_person_look(&first_person,input.cstick_x,input.cstick_y,delta);
                        c3_first_person_touch(&first_person,input.touch_held,input.touch_x,input.touch_y);
                    }else if(orbit.free){
                        c3_orbit_free_update(&orbit,input.stick_x,input.stick_y,input.cstick_x,input.cstick_y,
                                             input.held,map.scale,delta);
                    }else (void)c3_orbit_input(&orbit,input.cstick_x,input.cstick_y,input.held,delta);
                    /* Home screen: a tapped button opens the in-game menu at
                     * that page (SAVE at its confirmation). In first person
                     * the stylus looks around instead. */
                    int home=!first_person.enabled&&input.touch_pressed?c3_ingame_hub_hit(input.touch_x,input.touch_y):-1;
                    if(home==7)home=-1; /* RESUME: already walking */
                    if(home>=0||(input.pressed&(C3_PAD_START|C3_PAD_X))){
                        int original=hub_menu_ready(ingame,state);
                        if(trace){fprintf(trace,"ingame_open original_assets=%d hub_item=%d\n",original,home);fflush(trace);}
                        ingame->idle=0;ingame->page=C3_GAME_HUB;ingame->selection=ingame->hub_selection=home>=0?(unsigned)home:0u;
                        page=PAUSE_MAIN;selection=0;dirty=1;
                        if(home==5){page=PAUSE_SAVE;yes=0;}
                        else if(home==6){page=PAUSE_LEAVE;yes=0;}
                        else if(home>=0){
                            C3N3dsInput open={0};open.pressed=C3_PAD_A;
                            (void)c3_ingame_update(ingame,state,&open,0);
                        }
                        input.pressed=0;input.touch_pressed=false;
                    }
                    else if(input.pressed&C3_PAD_A){if(!c3_field_story_interact(&story,story_error))goto story_failed;dirty=1;}
                    else{
                        float before[3];memcpy(before,player.position,sizeof before);
                        float move_yaw=first_person.enabled?c3_first_person_move_yaw(&first_person):
                                       orbit.active?c3_orbit_move_yaw(&camera):map.camera_yaw;
                        /* D-pad Up/Down zoom the view; free flight moves the camera, not the hero. */
                        uint16_t walk_buttons=(uint16_t)(input.held&~(C3_PAD_UP|C3_PAD_DOWN));
                        int walk_x=orbit.free?0:input.stick_x,walk_y=orbit.free?0:input.stick_y;
                        /* Hold L to run: the circle pad's direction at full reach,
                         * which is hard to hold by thumb. */
                        if((input.held&C3_PAD_L)&&!orbit.free){
                            float reach=hypotf((float)walk_x,(float)walk_y);
                            if(reach>15.0f){walk_x=(int)lroundf((float)walk_x*127.0f/reach);walk_y=(int)lroundf((float)walk_y*127.0f/reach);}
                        }
                        if(orbit.free)walk_buttons=0;
                        if(!c3_field_move(&map,&player,walk_x,walk_y,walk_buttons,move_yaw,delta)){
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
                else if(event.kind==C3_INGAME_RESUME){c3_pda_audio_close();pda_audio_ready=0;page=PAUSE_CLOSED;dirty=1;}
                else if(event.kind==C3_INGAME_SAVE){page=PAUSE_SAVE;yes=0;dirty=1;}
                else if(event.kind==C3_INGAME_LEAVE){page=PAUSE_LEAVE;yes=0;dirty=1;}
                else if(event.kind==C3_INGAME_OPTIONS){c3_movie_player_set_mono(ingame->option_mono);c3_music_set_mono(ingame->option_mono);dirty=1;}
                else if(event.kind==C3_INGAME_READ_MAIL){
                    int ready=!event.mail.script||c3_field_story_menu_event(&story,event.mail.script,story_error);
                    if(ready&&!c3_game_state_mail_read(state,&event.mail.save,1)){
                        if(event.mail.script)c3_field_story_cancel_menu_event(&story);
                        ready=0;snprintf(story_error,sizeof story_error,"The mail read changes could not be applied.");
                    }
                    if(!ready)c3_ingame_error(ingame,story_error);
                    else if(event.mail.script){c3_pda_audio_close();pda_audio_ready=0;page=PAUSE_CLOSED;}
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
            }else{c3_field_camera(&map,&player,&camera);c3_orbit_apply(&orbit,&map,&player,&camera);}
            if(phase==FIELD_FADE_IN)fade=fminf(fade,phase_seconds/(arrival?1.0f:.5f));
        }
        if(!c3_game_state_summary(state,&summary)){snprintf(error,capacity,"Game state became unavailable.");result=-1;break;}
        if(dirty||input.pressed||now>=next_console){
            C3BottomUi *bottom=c3_bottom_screen_begin();
            if(naming)c3_name_entry_draw(&name_entry,bottom,"PARTNER'S NAME");
            else if(shopping)c3_shop_menu_draw(&shop_menu,state,bottom,profile->label);
            else if(page==PAUSE_MAIN&&ingame->active)c3_ingame_draw_to(ingame,state,bottom);
            else if(page==PAUSE_CLOSED&&(phase==FIELD_CONTROL||phase==FIELD_FADE_IN)&&!view->dialogue&&!view->blocked[0]&&
                    hub_menu_ready(ingame,state)>=0){
                /* The P*DA in-game menu hub is the field bottom screen, also
                 * while an event runs without a message (taps open it only
                 * when the hero has control). */
                ingame->idle=1;ingame->page=C3_GAME_HUB;c3_ingame_draw_to(ingame,state,bottom);
            }
            else c3_field_ui_draw_to(bottom,&summary,phase,page,selection,yes,message,view,&ui_pager);
            c3_bottom_screen_present();dirty=0;next_console=now+UINT64_C(100000);
        }
        if(pda_preview){
            pda_seconds+=delta;
            c3_pda_preview_control(pda_preview,abs(input.cstick_x)>20?input.cstick_x/128.f:0,
                                   abs(input.cstick_y)>20?input.cstick_y/128.f:0,delta);
        }
        int draw_result=pda_preview?c3_pda_preview_draw(pda_preview,pda_seconds,&stats,error,capacity):
            c3_stage_renderer_draw(models,npc.attached?count:npc.first,&camera,fade,&stats,error,capacity);
        if(draw_result==C3_RENDER_BUSY){
            C3RenderRecoveryResult recovery=c3_render_recover(trace,"field",frames,C3_RECOVER_WAIT);
            if(recovery==C3_RECOVERY_EXIT){result=1;break;}
            if(recovery==C3_RECOVERY_RESUME_PAUSED){previous=c3_n3ds_active_time_us();dirty=1;next_console=0;}
            continue;
        }
        if(draw_result){result=-1;break;}
        ++frames;
#ifdef C3_ROUNDTRIP_PROBE
        /* Leave for the world map once the people have joined. */
        if(npc.attached&&frames>=90u){
            if(!c3_game_state_set_floor(state,C3_FLOOR_WORLD_MAP_ID,map.floor,0)){result=-1;break;}
            result=2;break;
        }
#endif
#ifdef C3_WARP_TOUR
        if(tour.captured==-1&&frames>2){
            snprintf(tour.shot,sizeof tour.shot,"sdmc:/3ds/Colosseum3DS/tour/%s.bmp",prefix);
            tour.captured=c3_n3ds_capture_top(tour.shot)==0?1:2;
            tour.frames=frames;tour.draws=stats.draw_calls;tour.seconds=elapsed;
            tour.control=view->control;tour.message=view->message_id;
            result=3;break;
        }
#endif
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
#ifdef C3_CAMPAIGN
    /* The event that failed is already in the tried set: keep it there, so
     * the restarted campaign (from the last save) moves on to other events. */
    campaign_store_tried();
    campaign_line("floor=%03lx script_error=\"%s\" action=restart",(unsigned long)map.floor,story_error);
#endif
done:
    /* Every slot, room and people: streamed people attach mid-scene without
     * moving `opened`, and calloc'd slots that never loaded close as no-ops.
     * Closing only `opened` leaked every person pack on each area exit. */
    if(npc.stream){c3_pack_stream_finish(npc.stream);npc.stream=NULL;}
    if(storage)opened=count;
    free(npc.jobs);npc.jobs=NULL;
    c3_pda_audio_close();
    c3_ingame_close(ingame);
    free(ingame);
    free(shop_pda);free(shop_pack);
    if(result<0)c3_n3ds_log_memory(trace,"field_failed");
    if(c3_render_recover(trace,"field",frames,result==1?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)result=1;
    c3_pda_preview_close(&pda_preview);
    c3_field_story_close(&story);
    for(uint32_t i=0;i<opened;++i)c3_title_close(storage+i);
    free(storage);c3_area_lights_close(&lights);c3_area_camera_close(&room_camera);
    for(uint32_t i=0;i<event_camera_count;++i)c3_area_camera_close(event_cameras+i);
    c3_area_close(&area);c3_field_close(&map);
    if(trace){fprintf(trace,"field_end result=%d error=%s\n",result,result<0?error:"");fflush(trace);}
    return result;
}

#ifdef C3_WARP_TOUR
/* Visit every floor listed (hex, one per line) in boot/tour.txt from the
 * loaded save, entrance 0, and log one line per floor to tour/tour-log.txt.
 * The save is never written: run_area saves only from the in-game menu,
 * which the tour does not open. */
int c3_field_scene_tour(C3GameState *state,FILE *trace,char *error,size_t capacity)
{
    FILE *list=fopen(BOOT_ROOT "tour.txt","r"),*log;char line[32];unsigned visited=0,ok=0;
    mkdir("sdmc:/3ds/Colosseum3DS/tour",0777);
    log=fopen("sdmc:/3ds/Colosseum3DS/tour/tour-log.txt","w");
    if(!list||!log){if(list)fclose(list);if(log)fclose(log);snprintf(error,capacity,"tour.txt or the tour log is unavailable.");return -1;}
    fprintf(log,"tour_begin build=%s seconds=%.1f\n",C3_BUILD_ID,(double)TOUR_SECONDS);fflush(log);
    while(fgets(line,sizeof line,list)){
        unsigned long floor=strtoul(line,NULL,16);
        if(!floor||floor>0xffff)continue;
        memset(&tour,0,sizeof tour);error[0]=0;
        int result;
        if(!c3_field_scene_supports((uint32_t)floor)){result=-2;snprintf(error,capacity,"packs missing");}
        else if(!c3_game_state_set_floor(state,(uint32_t)floor,0,0)){result=-1;snprintf(error,capacity,"floor checkpoint rejected");}
        else result=run_area(state,0,"sdmc:/3ds/Colosseum3DS/tour/unused.gci",trace,error,capacity);
        ++visited;if(result==3&&tour.captured==1)++ok;
        fprintf(log,"floor=%03lx result=%d frames=%lu seconds=%.1f fps=%.1f draws=%lu control=%lu message=%05lx next=%03lx shot=%s error=%s\n",
                floor,result,(unsigned long)tour.frames,(double)tour.seconds,
                tour.seconds>0?(double)tour.frames/(double)tour.seconds:0.0,(unsigned long)tour.draws,
                (unsigned long)tour.control,(unsigned long)tour.message,(unsigned long)tour.next_floor,
                tour.captured==1?tour.shot:"-",result==3?"":error);
        fflush(log);
        if(result==1)break; /* the player asked to exit */
    }
    fprintf(log,"tour_end visited=%u captured=%u\n",visited,ok);
    fclose(log);fclose(list);
    return 0;
}
#endif

#ifdef C3_ROUNDTRIP_PROBE
int c3_field_scene_roundtrip_probe(C3GameState *state,FILE *trace,char *error,size_t capacity)
{
    enum{AREAS=7};
    char stage[64];int result=0;
    c3_n3ds_log_memory(trace,"roundtrip_baseline");
    /* Start in Phenac; the world map then alternates between the two
     * destinations the save has unlocked. */
    if(!c3_game_state_set_floor(state,3,C3_FLOOR_WORLD_MAP_ID,0)){snprintf(error,capacity,"floor rejected");return -1;}
    for(unsigned i=0;i<AREAS;++i){
        C3GameSummary summary;uint32_t floor=c3_game_state_summary(state,&summary)?summary.floor:0;
        result=run_area(state,0,"sdmc:/3ds/Colosseum3DS/probe-unused.gci",trace,error,capacity);
        snprintf(stage,sizeof stage,"roundtrip_%u_field_%03lx_end",i,(unsigned long)floor);
        c3_n3ds_log_memory(trace,stage);
        if(result!=2||i+1==AREAS)break;
        result=c3_world_map_scene_run(state,trace,error,capacity);
        snprintf(stage,sizeof stage,"roundtrip_%u_worldmap_end",i);
        c3_n3ds_log_memory(trace,stage);
        if(result!=2)break;
    }
    if(trace){fprintf(trace,"roundtrip_end result=%d error=%s\n",result,result<0?error:"");fflush(trace);}
    return result<0?-1:0;
}
#endif

int c3_field_scene_run(C3GameState *state,int new_game,const char *save_path,
                       FILE *trace,char *error,size_t capacity)
{
#ifdef C3_CAMPAIGN
    /* Before any floor allocation: the log's buffers and the tried set must
     * not sit between floor allocations. */
    campaign.start_us=c3_n3ds_time_us();campaign_load_tried();campaign_count_launch();
    campaign_line("campaign_start new_game=%d tried=%u launch=%lu",new_game,campaign.tried_count,(unsigned long)c3_campaign_launch);
#endif
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
