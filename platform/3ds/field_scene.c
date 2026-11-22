#include "field_scene.h"
#include "bottom_screen.h"
#include "game_ui_views.h"
#include "ingame_menu.h"
#include "pack_cache.h"
#include "boot_index.h"
#include "area_prefetch.h"
#include "colosseum3ds/world_map.h"
#include "debug_overlay.h"
#include "pack_loader.h"
#include "battle_general_scene.h"
#include "battle_general_3d.h"
#include "colosseum3ds/battle_story.h"
#include "pda_preview.h"
#include "pda_audio.h"
#include "pda_sound.h"
#include "movie_player.h"
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
#include "colosseum3ds/pc_menu.h"
#include "colosseum3ds/mail_alert.h"
#include "world_map_scene.h"
#include "ending_scene.h"
#include "dna_key_scene.h"
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
    if(floor==0x39f)return c3_ending_scene_available();
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
        if(!c3_pda_text_utf8(&assets,info.name,summary.name,summary.partner,name,capacity))
            snprintf(name,capacity,"ITEM %lu",(unsigned long)item);
    }
    free(data);return result;
}
/* scriptAddItem bypasses the bag menu's key-item toss guard. */
static int change_item(void *context,uint32_t item,int32_t count)
{
    C3PdaAssets assets={0};uint8_t *data=open_pda(&assets);int result=-1;
    if(data)result=count<0?c3_inventory_remove(context,&assets,item,(unsigned)-count):
        c3_inventory_receive(context,&assets,item,(unsigned)count);
    free(data);return result;
}
/* Names the message controls print (msgctrlItem/Item2, msgctrlPokemonID)
 * from the PDA pack's item and species tables. Moves and trainer classes
 * are not in that pack yet; such a message fails with the control named. */
static int story_namer(void *context,unsigned kind,uint32_t id,C3StoryValue *value)
{
    static char text[64];
    C3GameState *state=context;C3GameSummary summary;C3PdaAssets assets={0};C3InventoryItem item;C3PdaSpecies species;
    uint8_t *data=open_pda(&assets);int ok=0;
    if(data&&c3_game_state_summary(state,&summary)){
        if(kind==C3_STORY_NAME_ITEM&&c3_inventory_item(&assets,id,&item))
            ok=c3_pda_text_utf8(&assets,item.name,summary.name,summary.partner,text,sizeof text);
        else if(kind==C3_STORY_NAME_SPECIES&&c3_pda_species(&assets,id,&species))
            ok=c3_pda_text_utf8(&assets,species.name,"","",text,sizeof text);
    }
    free(data);
    if(ok)value->text=text;
    return ok;
}
/* The PC menu's texts: original messages through the field story. */
static int pc_text(void *context,uint32_t id,const char *name,char *out,size_t capacity)
{return c3_field_story_text(context,id,name,out,capacity);}
/* Script music natives 68/69/6A/6D -> the streamed MusyX player. */
static int story_audio(void *context,unsigned op,uint32_t a,uint32_t b,uint32_t c)
{
    /* The last sound native 69 put in slot 0x407, put back after a jingle. */
    static uint32_t slot407_sound,slot407_volume;
    (void)context;
    switch(op){
    case C3_STORY_AUDIO_PLAY_406:return c3_music_play(C3_MUSIC_SLOT_406,a,c,b);
    case C3_STORY_AUDIO_PLAY_407:slot407_sound=a;slot407_volume=c;return c3_music_play(C3_MUSIC_SLOT_407,a,c,b);
    /* fn_80165668: the area music pauses while the one-shot jingle plays
     * (the player has two song slots; the jingle borrows 0x407). */
    case C3_STORY_AUDIO_JINGLE:
        c3_music_pause_slot(C3_MUSIC_SLOT_406,1);
        if(c3_music_play(C3_MUSIC_SLOT_407,a,c,b))return 1;
        c3_music_pause_slot(C3_MUSIC_SLOT_406,0);return 0;
    case C3_STORY_AUDIO_STATUS:return c3_music_status(a);
    case C3_STORY_AUDIO_JINGLE_END:
        c3_music_stop_slot(C3_MUSIC_SLOT_407,0);
        if(slot407_sound)c3_music_play(C3_MUSIC_SLOT_407,slot407_sound,slot407_volume,0);
        c3_music_pause_slot(C3_MUSIC_SLOT_406,0);return 1;
    case C3_STORY_AUDIO_STOP:c3_music_stop(a,b);return 1;
    case C3_STORY_AUDIO_MASTER:c3_music_master(a,b);return 1;
    }
    return 0;
}

/* Party service for script native D8. */
static int clear_party_poison(void *context)
{
    return c3_inventory_clear_party_poison(context);
}
static int restore_party(void *context)
{
    C3PdaAssets assets={0};uint8_t *data=open_pda(&assets);
    int restored=data?c3_inventory_restore_party(context,&assets):-1;
    free(data);return restored;
}
/* A party record's nickname (PokemonBios +0x2E, big-endian UTF-16, 11
 * characters), for the ASCII font. */
static void record_nickname(const uint8_t *r,char *out,size_t capacity)
{
    c3_game_text_ascii(r+0x2e,11,out,capacity);
}
/* Native C2 (preReliveMain -> reliveCeremonyAll): purify party `slot` with
 * the original rules (src/relive_purify.c) in the game state; the next save
 * writes it. The battle pack supplies growth, stats and level-up moves. */
static FILE *purify_trace;
static int purify_party(void *context,unsigned slot,int forced,C3FieldStoryPurified *out)
{
    C3GameState *state=context;C3BattlePack pack;char problem[C3_GB_ERROR_SIZE]="";
    uint8_t r[C3_GB_POKEMON_SIZE];
    memset(&pack,0,sizeof pack);
    if(!c3_game_state_pokemon(state,slot,r))return -1;
    record_nickname(r,out->nickname,sizeof out->nickname);
    if(!c3_battle_pack_load(&pack,C3_BATTLE_PACK_PATH,problem)){
        if(purify_trace){fprintf(purify_trace,"relive_purify slot=%u status=no_pack reason=%s\n",slot,problem);fflush(purify_trace);}
        return -1;
    }
    int done=c3_relive_purify(state,slot,forced,&pack,&out->result);
    if(done&&out->result.move0)c3_battle_pack_move_name(&pack,out->result.move0,out->move_name);
    if(done&&c3_game_state_pokemon(state,slot,r)&&purify_trace){
        fprintf(purify_trace,"relive_purify slot=%u name=%s species=%u level=%u exp=%lu dp=%ld friendship=%u->%u move0=%s pool_exp=%lu ribbon=%u\n",
                slot,out->nickname,(unsigned)out->result.species,(unsigned)r[0x60],
                (unsigned long)((uint32_t)r[0x5c]<<24|(uint32_t)r[0x5d]<<16|(uint32_t)r[0x5e]<<8|r[0x5f]),
                (long)(int32_t)((uint32_t)r[0xdc]<<24|(uint32_t)r[0xdd]<<16|(uint32_t)r[0xde]<<8|r[0xdf]),
                (unsigned)out->result.friendship_before,(unsigned)out->result.friendship_after,out->move_name,
                (unsigned long)out->result.pool_exp,(unsigned)r[0xc6]);
        fflush(purify_trace);
    }
    c3_battle_pack_free(&pack);
    return done?1:0;
}
/* menuPokemonOpen(7) for native C2: the party on the bottom screen; only a
 * Shadow Pokemon whose heart is open can be chosen. */
typedef struct { int open; unsigned cursor, count; char names[6][24]; uint8_t level[6], pickable[6]; } RelivePick;
static void relive_pick_open(RelivePick *p,const C3GameState *state)
{
    C3GameSummary summary;memset(p,0,sizeof *p);p->open=1;p->cursor=6;
    if(!c3_game_state_summary(state,&summary))return;
    for(unsigned i=0;i<summary.party_count&&i<6;++i){
        uint8_t r[C3_GB_POKEMON_SIZE];
        if(!c3_game_state_pokemon(state,i,r)||!(r[0]|r[1]))break;
        record_nickname(r,p->names[i],sizeof p->names[i]);
        p->level[i]=r[0x60];p->pickable[i]=(uint8_t)c3_relive_pickable(r);
        if(p->pickable[i]&&p->cursor==6)p->cursor=i;
        p->count=i+1;
    }
    if(p->cursor==6)p->cursor=0;
}
/* Returns the chosen slot, -1 to cancel, -2 while still choosing. */
static int relive_pick_update(RelivePick *p,uint32_t pressed)
{
    if(pressed&C3_PAD_B)return -1;
    if(!p->count)return pressed&C3_PAD_A?-1:-2;
    if(pressed&C3_PAD_UP)p->cursor=(p->cursor+p->count-1)%p->count;
    if(pressed&C3_PAD_DOWN)p->cursor=(p->cursor+1)%p->count;
    if((pressed&C3_PAD_A)&&p->pickable[p->cursor])return (int)p->cursor;
    return -2;
}
static void relive_pick_draw(const RelivePick *p,C3BottomUi *ui)
{
    c3_ui_header(ui,"RELIC STONE","Purify which Pokemon?");
    for(unsigned i=0;i<p->count;++i){
        char level[16];snprintf(level,sizeof level,"Lv%u",(unsigned)p->level[i]);
        c3_ui_row_value(ui,44+(int)i*24,p->names[i],p->pickable[i]?level:"---",i==p->cursor,!p->pickable[i]);
    }
    c3_ui_footer(ui,"A: purify   B: cancel");
}
/* Native D0 (fn_801C8628) opens the pause menu on Items to use a key item.
 * The port's own list shows the items the scripts registered (native 11)
 * that the bag holds; A uses one, B closes (the original cancel). */
typedef struct { int open; unsigned cursor, count; uint32_t items[8]; char names[8][32]; } ItemPick;
static void item_pick_open(ItemPick *p,const C3FieldStory *story,const C3GameState *state)
{
    uint32_t registered[8];unsigned n=c3_field_story_item_menu_items(story,registered);
    C3PdaAssets assets={0};uint8_t *data=open_pda(&assets);C3GameSummary summary;
    memset(p,0,sizeof *p);p->open=1;
    int have_summary=c3_game_state_summary(state,&summary);
    for(unsigned i=0;i<n&&p->count<8;++i){
        int held=0;
        for(unsigned pocket=0;pocket<6&&!held;++pocket)
            for(unsigned slot=0;slot<c3_inventory_capacity(pocket);++slot){unsigned item,count;
                if(c3_inventory_slot(state,pocket,slot,&item,&count)&&item==registered[i]&&count){held=1;break;}}
        if(!held)continue;
        C3InventoryItem info;char *name=p->names[p->count];
        if(!data||!have_summary||!c3_inventory_item(&assets,registered[i],&info)||
           !c3_pda_text(&assets,info.name,summary.name,summary.partner,name,sizeof p->names[0]))
            snprintf(name,sizeof p->names[0],"ITEM %lu",(unsigned long)registered[i]);
        p->items[p->count++]=registered[i];
    }
    free(data);
}
/* Returns the chosen item, 0 to close, UINT32_MAX while still choosing. */
static uint32_t item_pick_update(ItemPick *p,uint32_t pressed)
{
    if(pressed&C3_PAD_B)return 0;
    if(!p->count)return pressed&C3_PAD_A?0:UINT32_MAX;
    if(pressed&C3_PAD_UP)p->cursor=(p->cursor+p->count-1)%p->count;
    if(pressed&C3_PAD_DOWN)p->cursor=(p->cursor+1)%p->count;
    if(pressed&C3_PAD_A)return p->items[p->cursor];
    return UINT32_MAX;
}
static void item_pick_draw(const ItemPick *p,C3BottomUi *ui)
{
    c3_ui_header(ui,"ITEMS","Use which item?");
    if(!p->count)c3_ui_row_value(ui,44,"No item to use here","",1,1);
    for(unsigned i=0;i<p->count;++i)c3_ui_row_value(ui,44+(int)i*24,p->names[i],"",i==p->cursor,0);
    c3_ui_footer(ui,"A: use   B: close");
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
/* One area-load stage: time since the load started and the boot/ lookups
 * that reached the SD card meanwhile (boot_index.h); frames<0 omits frames. */
static void trace_stage(FILE *trace,const char *name,int frames,uint64_t start_us,const C3BootFsStats *base)
{
    if(!trace)return;
    C3BootFsStats now;c3_boot_fs_stats(&now);
    fprintf(trace,"field_load_stage name=%s",name);
    if(frames>=0)fprintf(trace," frames=%d",frames);
    fprintf(trace," us=%llu fs_lookups=%u fs_us=%llu fs_answered=%u\n",(unsigned long long)(c3_n3ds_time_us()-start_us),
            now.lookups-base->lookups,(unsigned long long)(now.lookup_us-base->lookup_us),now.answered-base->answered);
}
/* Top-screen progress while the pack jobs run (main thread). */
static void load_progress(size_t done,size_t total)
{
    c3_n3ds_top_loading(total?(float)done/(float)total:1.0f);
}
static int field_renderer_init(C3Title *storage,const C3Title *const *models,uint32_t count,
                               uint64_t actors,char *error,size_t capacity);
static uint32_t field_map_packs; /* the leading map packs of the current floor */
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
                      (unsigned long long)(c3_n3ds_time_us()-npc->start_us));
        fprintf(trace,"field_people_visible frame=%u people=%lu streamed=1\n",frames,(unsigned long)npc->count);fflush(trace);}
    return 1;
}
/* A story battle (every encounter, Willie's 10 included) on the general
 * battle engine: the pack is loaded only for the battle (Phenac's heap is
 * nearly full).
 * autoplay: both sides use the engine's policy (campaign build).
 * Returns 1 played and applied, 0 not playable here (the reason is in
 * error; the caller reports it, never a result), 2 the application must
 * exit, -1 error (in error). */
static void release_field_models(C3Title *storage,uint32_t count);
static int restore_field_models(C3Title *storage,uint32_t count,char paths[][160],C3FieldStory *story,
                                float elapsed,char *error,size_t capacity);
/* What a 3D story battle needs from the field to free its models first and
 * restore them afterwards. */
typedef struct {
    C3Title *storage; const C3Title **models; C3Title **actor_models; char (*paths)[160];
    C3FieldStory *story; NpcStream *npc; C3IngameMenu *ingame;
    uint32_t count, tv_index; uint64_t skin; int tv_enabled; unsigned frames; float elapsed;
} FieldBattleHost;
static int field_battle_release(FieldBattleHost *h,FILE *trace,char *error,size_t capacity)
{
    c3_n3ds_log_memory(trace,"field_battle_before_drain");
    if(c3_render_recover(trace,"field-to-battle",h->frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)return -1;
    /* A battle start waits for the streamed people. */
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
    char problem[C3_GB_ERROR_SIZE]="";C3BattleEncounterInfo info={0};
    if(!c3_battle_pack_load(&pack,C3_BATTLE_PACK_PATH,problem)){
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu status=no_pack reason=%s\n",(unsigned long)encounter,problem);fflush(trace);}
        snprintf(error,capacity,"battles.c3x: %s",problem);
        return 0;
    }
    if(!c3_battle_pack_info(&pack,encounter,&info)||info.reason){
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu status=refused reason=%s\n",(unsigned long)encounter,
                          c3_battle_pack_reason(info.reason));fflush(trace);}
        snprintf(error,capacity,"%s",c3_battle_pack_reason(info.reason));
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
        if(ok&&!c3_battle_story_prepare(state,battle)){ok=0;snprintf(problem,sizeof problem,"The bag could not be read for the battle.");}
        else if(!problem[0])snprintf(problem,sizeof problem,"The party could not be read for the battle.");
        if(ok){
            int scene=-1;char why[192]="";
#ifdef C3_ROUNDTRIP_PROBE
            {char prefix[32];snprintf(prefix,sizeof prefix,"probe/battle_%lu",(unsigned long)encounter);
             mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);c3_battle_general_3d_set_autoplay(1,prefix);}
#endif
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
#ifdef C3_ROUNDTRIP_PROBE
            c3_battle_general_3d_set_autoplay(0,NULL);
#endif
            if(scene==1)result=2;
            else if(scene<0){if(!problem[0])snprintf(problem,sizeof problem,"The battle engine failed (see the trace).");}
            else result=c3_battle_story_apply(story,state,battle,&pack,NULL,&rewards,problem)?1:-1;
        }
        if(trace){fprintf(trace,"field_battle_engine encounter=%lu trainer=%s mode=played result=%d code=%u prize=%lu\n",
                          (unsigned long)encounter,info.trainer,result,(unsigned)rewards.result_code,(unsigned long)rewards.prize);
            if(result>0){unsigned long exp=0;unsigned levels=0,learned=0,skipped=0,balls=0;
                for(unsigned i=0;i<C3_GB_PARTY;++i){exp+=rewards.exp[i];levels+=rewards.levels_gained[i];learned+=rewards.learned_count[i];skipped+=rewards.skipped_count[i];}
                for(unsigned i=1;i<=12;++i)balls+=rewards.balls_used[i];
                fprintf(trace,"field_battle_writeback encounter=%lu written=%d exp=%lu levels=%u learned=%u forget_prompt_skipped=%u snagged=%u balls_used=%u wipeout=%d\n",
                        (unsigned long)encounter,rewards.written,exp,levels,learned,skipped,(unsigned)rewards.caught_count,balls,rewards.wipeout);
                for(unsigned k=0;k<rewards.caught_count;++k)
                    fprintf(trace,"field_battle_snag encounter=%lu species=%u shadow=%u stored=%u\n",(unsigned long)encounter,
                            battle->party[1][rewards.caught[k]].species,battle->party[1][rewards.caught[k]].shadow,rewards.stored[k]);}
            fflush(trace);}
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
        "shop_camera.c3c","shop.c3l","shop_tv.c3t"};
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

/* A native 22 auto event queued just before a floor link (hero_move.c work). */
static uint32_t carried_auto_event[5];
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
 * every story battle is played on the general engine with both sides on
 * its policy (an encounter it cannot play is reported as a win).
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
    /* GSmodel order (stage_order.h): the map's packs, then each person with
     * its own opaque and translucent passes, so a map light shaft without
     * depth writes no longer washes over a character standing in front. */
    (void)c3_stage_renderer_set_draw_groups(field_map_packs<count?field_map_packs:count);
#ifndef C3_NO_GPU_SKIN
    for(uint32_t i=0;i<count;++i)storage[i].gpu_batches=c3_stage_renderer_gpu_skin_mask(i);
#else
    (void)storage;
#endif
    return 0;
}

#ifdef C3_ROUNDTRIP_PROBE
/* Optional door route (sdmc:/3ds/Colosseum3DS/probe-route.txt): a first
 * line "start <floor>" and then lines "<floor> <door source>", hex, taken
 * in order. In a listed floor the probe fires that door's transition (the
 * original door script picks the target floor and entrance); a source
 * written "i<source>" is an A-button surface instead: the hero is stood in
 * front of it, facing it, and interacts (open dialogue is answered with A);
 * "t<source>" fires that touch surface's link (an elevator, a door).
 * Other floors leave for the world map as before. */
static struct { uint32_t start, start_spawn, floor[64], source[64]; uint8_t interact[64]; unsigned count, next; int loaded;
                uint32_t film_floor, film_frames, film_stride; unsigned shadow_slot, shadow_id;
                uint32_t flag_id[16], flag_value[16]; uint32_t ball_id[4], ball_count[4]; unsigned flags, balls, name_mode; int partner, stayed;
                uint32_t walk_floor, walk_frames, walked, walk_tail, walk_frame; int walk_x, walk_y; int continue_save; uint32_t stop; } probe_route;
static void probe_route_load(void)
{
    if(probe_route.loaded)return;
    probe_route.loaded=1;probe_route.partner=-1;
    FILE *f=fopen("sdmc:/3ds/Colosseum3DS/probe-route.txt","r");char line[64];
    if(!f)return;
    while(fgets(line,sizeof line,f)){
        unsigned long a,b;long x,y;
        /* "start <floor> [spawn]": the first floor and its entrance;
         * "frames <floor> <count>": in that floor, before leaving, save
         * <count> consecutive top-screen frames (probe/film_<floor>_<n>.bmp)
         * to compare animation against an emulator capture. */
        /* "continue": start where the save says (its floor, previous floor
         * and entrance), as the title's Continue does, instead of a world-map
         * area; "stop <floor>": end the probe once that floor's area exits. */
        if(!strncmp(line,"continue",8))probe_route.continue_save=1;
        else if(sscanf(line,"stop %lx",&a)==1)probe_route.stop=(uint32_t)a;
        else if(sscanf(line,"start %lx %lx",&a,&b)==2){probe_route.start=(uint32_t)a;probe_route.start_spawn=(uint32_t)b;}
        else if(sscanf(line,"start %lx",&a)==1)probe_route.start=(uint32_t)a;
        else if(sscanf(line,"frames %lx %lu",&a,&b)==2&&b<=600){probe_route.film_floor=(uint32_t)a;probe_route.film_frames=(uint32_t)b;
            /* "frames <floor> <count> <stride>": every stride-th frame. */
            unsigned long stride;probe_route.film_stride=sscanf(line,"frames %*x %*u %lu",&stride)==1&&stride>=1&&stride<=600?(uint32_t)stride:1u;}
        /* "shadow <slot> <id>": the probe makes that party member a Shadow
         * Pokemon whose heart is open, in memory only (the probe saves to
         * probe-unused.gci), to try the Relic Stone. */
        /* "flag <id> <value>" and "partner <0|1>": story state the probe
         * writes before the first floor (in memory only), to replay an
         * event a save has already passed. */
        else if(sscanf(line,"flag %lx %lx",&a,&b)==2&&probe_route.flags<16){
            probe_route.flag_id[probe_route.flags]=(uint32_t)a;probe_route.flag_value[probe_route.flags++]=(uint32_t)b;}
        else if(sscanf(line,"partner %lx",&a)==1)probe_route.partner=a!=0;
        /* "name <mode>": once the hero has control, open the name entry for
         * that mode as native 16 would, play a fixed input sequence (NEW NAME,
         * a key, R, a key, START) and save each step's bottom screen. */
        else if(sscanf(line,"name %lx",&a)==1&&a<4)probe_route.name_mode=(unsigned)a+1u;
        /* "walk <floor> <frames> <x> <y>": hold the Circle Pad (decimal
         * -127..127) for that many frames once the hero has control there,
         * e.g. to film the partner following. */
        else if(sscanf(line,"walk %lx %lu %ld %ld",&a,&b,&x,&y)==4&&b<=1200&&x>=-127&&x<=127&&y>=-127&&y<=127){
            probe_route.walk_floor=(uint32_t)a;probe_route.walk_frames=(uint32_t)b;probe_route.walk_x=(int)x;probe_route.walk_y=(int)y;}
        else if(sscanf(line,"shadow %lx %lx",&a,&b)==2&&a<6&&b){probe_route.shadow_slot=(unsigned)a;probe_route.shadow_id=(unsigned)b;}
        /* "<floor> p<person>": stand in front of that person (area index),
         * facing it, and talk (interact value 2). */
        else if(sscanf(line,"%lx p%lx",&a,&b)==2&&probe_route.count<64){
            probe_route.floor[probe_route.count]=(uint32_t)a;probe_route.interact[probe_route.count]=3;
            probe_route.source[probe_route.count++]=(uint32_t)b;}
        else if(sscanf(line,"ball %lx %lx",&a,&b)==2&&probe_route.balls<4&&a>=1&&a<=12&&b){
            probe_route.ball_id[probe_route.balls]=(uint32_t)a;probe_route.ball_count[probe_route.balls++]=(uint32_t)b;}
        else if(sscanf(line,"%lx t%lx",&a,&b)==2&&probe_route.count<64){
            probe_route.floor[probe_route.count]=(uint32_t)a;probe_route.interact[probe_route.count]=2;
            probe_route.source[probe_route.count++]=(uint32_t)b;}
        else if(sscanf(line,"%lx i%lx",&a,&b)==2&&probe_route.count<64){
            probe_route.floor[probe_route.count]=(uint32_t)a;probe_route.interact[probe_route.count]=1;
            probe_route.source[probe_route.count++]=(uint32_t)b;}
        else if(sscanf(line,"%lx %lx",&a,&b)==2&&probe_route.count<64){
            probe_route.floor[probe_route.count]=(uint32_t)a;probe_route.source[probe_route.count++]=(uint32_t)b;}
    }
    fclose(f);
}
#endif
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
    C3Title *heal_models[C3_HEAL_BALL_SLOTS]={0};uint8_t heal_balls[C3_HEAL_BALL_SLOTS]={0};
    char heal_files[C3_HEAL_BALL_SLOTS][48];uint32_t heal_count=0;
    char model_paths[C3_STAGE_MAX_MODELS][160]={{0}};
    C3TitleCamera camera;C3TitleRenderStats stats={0};C3N3dsInput input={0};
    uint32_t opened=0,count=0,arrival_done=0,opening_done=0,arrival_count=0;
    unsigned wes_index=0,arrival_index=0,tv_index=UINT32_MAX,partner_index=UINT32_MAX,selection=0,frames=0;
    /* Load timing for the trace: area start, each pack, first control. */
    uint64_t load_start_us=c3_n3ds_time_us(),pack_us=0;int load_reported=0;
    C3BootFsStats fs_base;c3_boot_fs_stats(&fs_base);c3_boot_index_trace(trace);
    /* People load with the room (both cores) and are bound before the first
     * frame; npc.attached is then set and npc_stream_attach is a no-op. */
    NpcStream npc={0};
    c3_title_perf_clock(c3_n3ds_time_us);
    int result=-1,yes=0,dirty=1,arrival_end_sampled=0,entered=0,lights_loaded=0;
    FieldPhase phase=FIELD_SETUP;PausePage page=PAUSE_CLOSED;
    float elapsed=0,phase_seconds=0,arrival_seconds=0;
    uint64_t previous=0,next_console=0,next_trace=0,skin_actors=0;
    C3UiPager ui_pager={0};
    C3NameEntry name_entry;C3NameEntryText name_text;C3Story name_messages={0};int naming=0,naming_probe=0;unsigned naming_step=0;
    (void)naming_step;
    C3ShopMenu shop_menu;C3ShopTable shop_table={0};C3PdaAssets shop_assets={0};
    uint8_t *shop_pda=NULL,*shop_pack=NULL;size_t shop_pack_size=0;int shopping=0;
    RelivePick relive_pick={0};
    C3PcMenu pc_menu;int pc_open=0;unsigned pc_step=0;(void)pc_step;
    ItemPick item_pick={0};
    C3IngameMenu *ingame=calloc(1,sizeof *ingame);
    C3PdaPreview *pda_preview=NULL;
    float pda_seconds=0;
    int pda_audio_ready=0;
    /* New-mail alert (mailMainStartSe / fn_801D2404) for native AD deliveries. */
    C3MailAlert mail_alert={0};uint32_t mail_seen=0;float mail_frames=0;
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
    trace_stage(trace,"field_area_files",-1,load_start_us,&fs_base);
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
    field_map_packs=map.map_pack_count;
    /* The floor's door table (native 60) and the object pack of each door
     * that has one; those rest on their first frame until a script plays a
     * clip. A door without a pack changes only its open flag and collision,
     * as the original does for a door without a model. The pack's map object
     * rows (object models no door row names) loop clip 0 like every map
     * model until natives 73-75 play them. */
    snprintf(path,sizeof path,BOOT_ROOT "%s.c3d",prefix);
    if(exists(path)&&!c3_area_doors_load(&doors,path,error,capacity))goto done;
    for(uint32_t i=0;i<doors.count;++i)if(doors.rows[i].file[0])++count;
    {   /* Native 53 mode 0 (fn_801D0DB0) opens one Poke Ball per party member
         * (party slots 0.., fn_801D0AA0) beside the floor's healing machine:
         * those models load with the room, hidden until the heal. */
        uint32_t machine=c3_field_heal_machine(area.group);int found=0;
        for(uint32_t i=0;i<doors.count;++i)found|=machine&&doors.rows[i].resource==machine&&doors.rows[i].file[0];
        for(uint32_t i=0;found&&i<C3_HEAL_BALL_SLOTS;++i){uint8_t record[0x138];
            if(!c3_game_state_pokemon(state,i,record)||!(record[0]|record[1]))break;
            if(!c3_field_heal_ball(prefix,record[0x0f],heal_files[heal_count],sizeof heal_files[0]))break;
            heal_balls[heal_count++]=record[0x0f];}
        count+=heal_count;
    }
    if(profile->features&C3_FLOOR_TV)tv_index=count++;
    /* The partner's pack (person 0,101), loaded with the room. */
    snprintf(path,sizeof path,BOOT_ROOT "%s_partner.c3t",prefix);
    if(exists(path))partner_index=count++;
    npc.first=count;
    for(uint32_t i=0;i<area.actor_count;++i)if(area.actors[i].file[0]){++count;++npc.count;}
    if(count>C3_STAGE_MAX_MODELS){snprintf(error,capacity,"The area exceeds the model bound.");goto done;}
    storage=calloc(count,sizeof(*storage));
    if(!storage){snprintf(error,capacity,"Field model allocation failed.");goto done;}
    /* From the world map the journey already covered the load and faded
     * out: the bottom screen keeps the map and the top stays black. */
    int from_world_map=summary.previous_floor==C3_FLOOR_WORLD_MAP_ID;
    if(!from_world_map)c3_bottom_screen_notice(profile->label,"Loading the area...","Please wait...");
    unsigned load_total=map.map_pack_count+1u+arrival_count;
    for(uint32_t i=0;i<doors.count;++i)load_total+=doors.rows[i].file[0]!=0;
    load_total+=heal_count;
    int after_journey=from_world_map&&c3_world_map_scene_take_journey_shown();
    if(!after_journey)c3_n3ds_top_loading(0.0f);
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
        for(uint32_t i=0;i<heal_count;++i){
            snprintf(jobs[job].path,sizeof jobs[job].path,BOOT_ROOT "%.47s",heal_files[i]);jobs[job++].title=storage+slot++;}
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
    /* Map packs the world map prefetched (while the journey played) move in
     * as they are; only the rest is read now. */
    size_t failed=0;unsigned prefetched=0;
    for(unsigned j=0;j<load_total;++j)
        if(c3_area_prefetch_take(jobs[j].path,jobs[j].title)){jobs[j].ok=1;jobs[j].error[0]=0;jobs[j].microseconds=0;++prefetched;}
    c3_area_prefetch_release();
    {
        /* The floor's people load in the same parallel batch as the room,
         * so every character is resident before the first frame. They used
         * to stream in on the second core after the fade-in and popped in
         * up to seconds after the map (field_people_visible in the trace). */
        unsigned total=load_total+npc.count;
        C3PackJob *pending=calloc(total?total:1u,sizeof *pending);
        unsigned *slot=calloc(total?total:1u,sizeof *slot),n=0;
        if(!pending||!slot){free(pending);free(slot);snprintf(error,capacity,"Not enough memory for the load list.");free(jobs);goto done;}
        for(unsigned j=0;j<load_total;++j)if(!jobs[j].ok){pending[n]=jobs[j];slot[n++]=j;}
        for(unsigned j=0;j<npc.count;++j){pending[n]=npc.jobs[j];slot[n++]=load_total+j;}
        size_t bad=n?c3_pack_load_all(pending,n,after_journey?NULL:load_progress):0;
        for(unsigned k=0;k<n;++k){C3PackJob *job=slot[k]<load_total?jobs+slot[k]:npc.jobs+(slot[k]-load_total);
            job->ok=pending[k].ok;job->microseconds=pending[k].microseconds;
            job->unpack_us=pending[k].unpack_us;job->decode_us=pending[k].decode_us;job->core=pending[k].core;
            snprintf(job->error,sizeof job->error,"%s",pending[k].error);}
        if(bad&&slot[bad-1]<load_total)failed=slot[bad-1]+1u;
        free(pending);free(slot);
    }
    if(trace){fprintf(trace,"field_prefetch taken=%u of=%u\n",prefetched,load_total);}
    if(trace){fprintf(trace,"field_pack_jobs count=%u us=%llu\n",load_total,(unsigned long long)(c3_n3ds_time_us()-pack_us));}
    for(unsigned j=0;j<load_total;++j){
        if(trace)fprintf(trace,"field_pack_load file=%s bytes=%lu us=%llu ok=%d unpack_us=%llu decode_us=%llu core=%d\n",jobs[j].path,
                         (unsigned long)jobs[j].title->file_bytes,(unsigned long long)jobs[j].microseconds,jobs[j].ok,
                         (unsigned long long)jobs[j].unpack_us,(unsigned long long)jobs[j].decode_us,jobs[j].core);
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
        for(uint32_t i=0;i<heal_count;++i){
            C3Title *model=storage+slot;snprintf(model_paths[slot],sizeof model_paths[slot],"%s",jobs[job++].path);
            models[slot++]=model;heal_models[i]=model;model->hidden=1;
            if(!c3_title_animate(model,0,0)){snprintf(error,capacity,"Original Poke Ball pack is invalid.");free(jobs);goto done;}
        }
    }
    free(jobs);
    /* Bind the people loaded with the room, posed on their idle clip. */
    for(uint32_t j=0;j<npc.count;++j){
        C3PackJob *job=npc.jobs+j;uint32_t slot=npc.first+j,i=npc.area_index[j];
        if(trace)fprintf(trace,"field_pack_load file=%s bytes=%lu us=%llu ok=%d unpack_us=%llu decode_us=%llu core=%d\n",job->path,
                         (unsigned long)job->title->file_bytes,(unsigned long long)job->microseconds,job->ok,
                         (unsigned long long)job->unpack_us,(unsigned long long)job->decode_us,job->core);
        if(!job->ok){snprintf(error,capacity,"%s",job->error);goto done;}
        snprintf(model_paths[slot],sizeof model_paths[slot],"%s",job->path);
        models[slot]=storage+slot;actor_models[i]=storage+slot;
        if(!c3_title_animate(storage+slot,0,0)){snprintf(error,capacity,"Original NPC idle motion is missing.");goto done;}
    }
    free(npc.jobs);npc.jobs=NULL;npc.attached=1;
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
    if(partner_index!=UINT32_MAX){
        snprintf(path,sizeof path,BOOT_ROOT "%s_partner.c3t",prefix);
        if(!c3_title_load(storage+partner_index,path,error,capacity))goto done;
        snprintf(model_paths[partner_index],sizeof model_paths[partner_index],"%s",path);
        models[opened++]=storage+partner_index;
        if(!c3_title_animate(storage+partner_index,0,0))goto done;
    }
    /* Reviewed floors name their event camera; a surveyed floor loads one
     * when tools/prepare_tour.py prepared it. */
    trace_stage(trace,"packs",-1,load_start_us,&fs_base);
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
    trace_stage(trace,"cameras_lights",-1,load_start_us,&fs_base);
    /* Room and people together: one renderer start, no mid-scene restart. */
    if(field_renderer_init(storage,models,count,skin_actors,error,capacity))goto done;
    trace_stage(trace,"renderer",-1,load_start_us,&fs_base);
    if(tv_index!=UINT32_MAX&&!c3_stage_renderer_set_tv(tv_index,1,1,true,error,capacity))goto done;
    snprintf(path,sizeof path,"%s_ready",prefix);
    c3_n3ds_log_memory(trace,path);
    snprintf(path,sizeof path,BOOT_ROOT "%s",profile->story);
    if(!c3_field_story_load(&story,path,state,&map,&area,&player,wes,actor_models,&room_camera,trace,story_error))goto story_failed;
    if(partner_index!=UINT32_MAX&&!c3_field_story_attach_partner(&story,storage+partner_index,story_error))goto story_failed;
    trace_stage(trace,"story",-1,load_start_us,&fs_base);
    if(trace){fprintf(trace,"field_people_visible frame=0 people=%lu streamed=0\n",(unsigned long)npc.count);fflush(trace);}
    {   /* Treasure people of the floor (<prefix>.c3r beside the story pack). */
            char treasure[256];size_t n;snprintf(treasure,sizeof treasure,BOOT_ROOT "%s",profile->story);n=strlen(treasure);
            if(n>4&&!strcmp(treasure+n-4,".c3s")){memcpy(treasure+n-4,".c3r",4);
                if(!c3_field_story_load_treasure(&story,treasure,story_error))goto story_failed;
                if(trace){fprintf(trace,"field_treasure floor=%lu rows=%u\n",(unsigned long)map.floor,c3_field_story_treasure_count(&story));fflush(trace);}}
    }
    if(doors.count){
        uint32_t with_model=0;for(uint32_t i=0;i<doors.count;++i)with_model+=door_models[i]!=NULL;
        if(!c3_field_story_bind_doors(&story,doors.rows,doors.count,door_models,story_error))goto story_failed;
        if(trace){fprintf(trace,"field_doors floor=%lu rows=%lu models=%lu\n",(unsigned long)map.floor,(unsigned long)doors.count,(unsigned long)with_model);fflush(trace);}
    }
    /* GSmodelEnableModulation(128,128,128) on each ball is the renderer's
     * neutral modulation, so the slots keep the default. */
    if(heal_count){
        if(!c3_field_story_bind_heal_balls(&story,heal_models,heal_balls,heal_count,story_error))goto story_failed;
        if(trace){fprintf(trace,"field_heal_balls floor=%lu balls=%lu first=%s\n",(unsigned long)map.floor,
                          (unsigned long)heal_count,heal_files[0]);fflush(trace);}
    }
    c3_field_story_set_floor_support(&story,floor_reachable);
    c3_field_story_bind_cameras(&story,event_cameras,event_camera_count);
    c3_field_story_enable_name_entry(&story,1);
    c3_field_story_set_random(&story,c3_game_state_random16,state);
    c3_field_story_set_species_namer(&story,field_species_name,ingame);
    c3_field_story_enable_shop(&story,1);
    c3_field_story_enable_menu_floor(&story,0x391,1); /* the PC (pc_menu.h) */
    /* Native CC's disc puzzle, floor 0x387 (dna_key_scene.c), when its
     * original models and cameras are on the SD card. */
    c3_field_story_enable_menu_floor(&story,0x387,c3_dna_key_scene_available());
    c3_field_story_set_item_service(&story,receive_item,state);
    c3_field_story_set_item_change_service(&story,change_item,state);
    c3_field_story_set_namer(&story,story_namer,state);
    c3_field_story_enable_item_menu(&story,1);
    c3_field_story_set_party_service(&story,restore_party,state);
    c3_field_story_set_poison_service(&story,clear_party_poison,state);
    purify_trace=trace;c3_field_story_set_purify_service(&story,purify_party,state);
    c3_field_story_set_audio_service(&story,story_audio,NULL);
    {   /* An event scene's scripted models (C3SM rows: the subway train)
         * are part of the scene its init script drives at once, so their
         * packs must be attached before the scripts start. */
        int scene_models=0;
        for(uint32_t i=0;i<area.actor_count;++i)scene_models|=area.actors[i].aux3==0x4333534Du;
        if(scene_models&&!npc_stream_attach(&npc,1,storage,models,actor_models,model_paths,&story,count,skin_actors,
                                            tv_index,1,0,trace,error,capacity))goto done;
    }
    if(carried_auto_event[0]){
        if(trace){fprintf(trace,"field_auto_event floor=%03lx script=%08lx\n",(unsigned long)map.floor,(unsigned long)carried_auto_event[0]);fflush(trace);}
        c3_field_story_set_auto_event(&story,carried_auto_event);memset(carried_auto_event,0,sizeof carried_auto_event);
    }
    if(!c3_field_story_prepare(&story,story_error))goto story_failed;
    view=c3_field_story_view(&story);
    if(trace){fprintf(trace,"field_begin floor=%lu previous=%lu spawn=%u new=%d maps=%lu npcs=%lu models=%lu arrival=%d duration=%.3f\n",
        (unsigned long)map.floor,(unsigned long)summary.previous_floor,summary.spawn,new_game,(unsigned long)map.map_pack_count,
        (unsigned long)area.actor_count,(unsigned long)count,arrival!=NULL,(double)arrival_seconds);fflush(trace);}
    previous=c3_n3ds_active_time_us();result=1;
    while(c3_n3ds_poll(&input)){
        c3_debug_overlay_input(&input);
#if defined(C3_ROUNDTRIP_PROBE) || defined(C3_FINAL_EVENT_PROBE)
        /* Probe: answer open dialogue with A, one press a second. */
        if(view&&(view->dialogue||view->choice||(view->blocked[0]&&!view->battle_id))&&frames%60u==0u)input.pressed|=C3_PAD_A;
        probe_route_load();
        if(view&&view->control&&npc.attached&&probe_route.walk_frames&&probe_route.walk_floor==map.floor&&
           probe_route.walked<probe_route.walk_frames){
            input.stick_x=(int8_t)probe_route.walk_x;input.stick_y=(int8_t)probe_route.walk_y;
            if(++probe_route.walked==probe_route.walk_frames&&trace){
                C3StoryActor *rui=c3_field_story_person(&story,0,101);
                fprintf(trace,"probe_walk_end frames=%lu hero=%.3f,%.3f partner=%d,%.3f,%.3f clip=%lu\n",(unsigned long)probe_route.walked,
                        (double)player.position[0],(double)player.position[2],rui?rui->visible:-1,
                        rui?(double)rui->pose.position[0]:0.0,rui?(double)rui->pose.position[2]:0.0,rui?(unsigned long)rui->clip:0ul);
                fflush(trace);}
        }
        /* Probe: the partner every frame of the walk and the 90 after it
         * (her stop), to check a steady clip clock, step and turn. */
        if(trace&&probe_route.walk_frames&&probe_route.walk_floor==map.floor&&probe_route.walked&&
           probe_route.walked+90u>=probe_route.walk_tail&&probe_route.walk_tail<probe_route.walk_frames+90u&&
           (!probe_route.walk_tail||frames==probe_route.walk_frame+1u)){probe_route.walk_frame=frames;
            C3StoryActor *rui=c3_field_story_person(&story,0,101);++probe_route.walk_tail;
            if(rui)fprintf(trace,"probe_partner n=%lu hero=%.3f,%.3f rui=%.3f,%.3f yaw=%.3f clip=%lu clock=%.3f\n",(unsigned long)probe_route.walk_tail,
                           (double)player.position[0],(double)player.position[2],(double)rui->pose.position[0],(double)rui->pose.position[2],
                           (double)rui->pose.yaw,(unsigned long)rui->clip,(double)rui->pose.animation_seconds);
        }
#endif
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
#ifdef C3_FINAL_EVENT_PROBE
        {
            if(elapsed>1200){snprintf(error,capacity,"Final-event probe timed out");result=-1;break;}
        }
#endif
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
                if(view->blocked[0]&&view->battle_id){
                    uint32_t encounter=view->battle_id;
                    int played=play_story_battle(&story,state,encounter,1,NULL,trace,story_error,sizeof story_error);
                    if(played<0)goto story_failed;
                    campaign_line("floor=%03lx battle=%lu mode=%s",(unsigned long)map.floor,(unsigned long)encounter,
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
        unsigned name_request=view->name_entry;
#ifdef C3_ROUNDTRIP_PROBE
        probe_route_load();
        if(!name_request&&probe_route.name_mode&&view->control&&npc.attached&&frames>60u){
            name_request=probe_route.name_mode;probe_route.name_mode=0;naming_probe=1;}
#endif
        if(name_request&&!naming&&page==PAUSE_CLOSED){
            /* The original name entry (floor 0x390): its texts, presets and
             * key pages come from name_entry_menu's message group. */
            unsigned mode=name_request-1u;size_t bytes=0;uint8_t *group=read_boot_file(BOOT_ROOT "name_entry.c3m",&bytes);
            c3_story_free(&name_messages);
            int ready=group&&c3_story_add_messages(&name_messages,group,bytes,story_error)&&
                      c3_name_entry_text_load(&name_text,&name_messages,mode);
            free(group);
            if(!ready){snprintf(story_error,sizeof story_error,"The name entry needs boot/name_entry.c3m (tools/prepare_menu_messages.py)");goto story_failed;}
            c3_name_entry_init(&name_entry,&name_text,&name_messages);naming=1;dirty=1;
            if(trace){fprintf(trace,"name_entry_open mode=%u presets=%u max=%u\n",mode,name_text.preset_count,name_text.max_length);fflush(trace);}
        }
        if(naming){
#ifdef C3_ROUNDTRIP_PROBE
            /* Probe: for a story request take the first preset (A, A); for a
             * "name" route line play NEW NAME, A, R, A, START with a bottom
             * screen before each step. */
            if(frames%30u==0u){
                static const uint16_t steps[]={C3_PAD_DOWN,C3_PAD_DOWN,C3_PAD_DOWN,C3_PAD_A,C3_PAD_A,C3_PAD_R,C3_PAD_A,C3_PAD_START};
                if(!naming_probe)input.pressed|=C3_PAD_A;
                else{
                    char shot[96];mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                    snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/name_%02u.bmp",naming_step);
                    int ok=c3_n3ds_capture_bottom(shot)==0;
                    if(trace){fprintf(trace,"probe_name step=%u phase=%d length=%u shot=%d\n",naming_step,(int)name_entry.phase,name_entry.length,ok);fflush(trace);}
                    if(naming_step<sizeof steps/sizeof steps[0])input.pressed|=steps[naming_step];
                    ++naming_step;
                }
            }
#endif
            C3NameEvent typed=c3_name_entry_update(&name_entry,input.pressed,input.touch_pressed,input.touch_x,input.touch_y);
            if(typed!=C3_NAME_NONE)dirty=1;
            if(typed==C3_NAME_DONE){
                if(naming_probe){
                    int ok=c3_game_state_set_partner_name(state,name_entry.result);naming_probe=0;
                    if(trace){fprintf(trace,"probe_name_done name=%s stored=%d\n",name_entry.result,ok);fflush(trace);}
                }else if(!c3_field_story_name_result(&story,name_entry.result,story_error))goto story_failed;
                if(trace){fprintf(trace,"name_entry_done name=%s length=%u\n",name_entry.result,name_entry.length);fflush(trace);}
                naming=0;c3_story_free(&name_messages);
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
        /* Native CC pushing the DNA disc puzzle, floor 0x387: the field's
         * models make room as for a 3D battle, the original screen runs on
         * the top display, and its retail result resumes CC. */
        if(view->menu_floor==0x387&&page==PAUSE_CLOSED){
            FieldBattleHost host={storage,models,actor_models,model_paths,&story,&npc,ingame,
                                  count,tv_index,skin_actors,view->tv_enabled,frames,elapsed};
            uint16_t species[3];memcpy(species,view->dna_species,sizeof species);
            if(trace){fprintf(trace,"dna_open floor=387 species=%x,%x,%x\n",species[0],species[1],species[2]);fflush(trace);}
            int released=field_battle_release(&host,trace,story_error,sizeof story_error);
            if(released<0){result=1;break;}
            if(!released)goto story_failed;
            int puzzle=c3_dna_key_scene_run(species,trace,story_error,sizeof story_error);
            int restored=field_battle_restore(&host,0,trace,error,capacity);
            if(restored<0){result=1;break;}
            if(!restored){result=-1;break;}
            if(trace){fprintf(trace,"dna_close floor=387 result=%d\n",puzzle);fflush(trace);}
            if(puzzle<0)goto story_failed;
            if(!c3_field_story_dna_result(&story,puzzle,story_error))goto story_failed;
            previous=c3_n3ds_active_time_us(); /* puzzle time must not advance the field */
            dirty=1;next_console=0;continue;
        }
        /* Native 62 pushing the PC box floor 0x391: the PC menu with the
         * original texts. It owns all input; Save uses the scene's save. */
        if(view->menu_floor==0x391&&!pc_open&&page==PAUSE_CLOSED){
            c3_pc_menu_open(&pc_menu,state,pc_text,&story);pc_open=1;dirty=1;pc_step=0;
            if(trace){fprintf(trace,"pc_open floor=391\n");fflush(trace);}
        }
        if(pc_open){
            uint16_t pc_pressed=input.pressed;
#ifdef C3_ROUNDTRIP_PROBE
            /* Probe: Storage, deposit the first party member in BOX2,
             * withdraw it, back, Exit; the bottom screen before each step. */
            pc_pressed=0;
            if(frames%30u==0u){
                static const uint16_t steps[]={C3_PAD_A,C3_PAD_A,C3_PAD_A,C3_PAD_DOWN,C3_PAD_A,C3_PAD_RIGHT,C3_PAD_A,C3_PAD_A,
                                               C3_PAD_B,C3_PAD_DOWN,C3_PAD_DOWN,C3_PAD_A};
                char shot[96];mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/pc_%02u.bmp",pc_step);
                int ok=c3_n3ds_capture_bottom(shot)==0;
                if(trace){fprintf(trace,"probe_pc step=%u phase=%d box=%u used=%d,%d,%d deposits=%u withdrawals=%u shot=%d\n",pc_step,
                    (int)pc_menu.phase,pc_menu.box,c3_game_state_box_used(state,0),c3_game_state_box_used(state,1),
                    c3_game_state_box_used(state,2),pc_menu.deposits,pc_menu.withdrawals,ok);fflush(trace);}
                if(pc_step<sizeof steps/sizeof steps[0])pc_pressed=steps[pc_step];
                ++pc_step;
            }
#endif
            C3PcEvent pc_event=c3_pc_menu_update(&pc_menu,pc_pressed);
            if(pc_event!=C3_PC_NONE)dirty=1;
            if(pc_event==C3_PC_SAVE){int saved=save_game(state,save_path,trace,message,sizeof message);
                if(trace){fprintf(trace,"pc_save saved=%d\n",saved);fflush(trace);}}
            if(pc_event==C3_PC_EXIT){
                pc_open=0;
                if(trace){fprintf(trace,"pc_close deposits=%u withdrawals=%u\n",pc_menu.deposits,pc_menu.withdrawals);fflush(trace);}
                if(!c3_field_story_menu_floor_done(&story,story_error))goto story_failed;
            }
            input.pressed=0;input.touch_pressed=false;
        }
        /* Native D0: the port's item list while the event waits. */
        if(view->item_menu&&!item_pick.open&&page==PAUSE_CLOSED){
            item_pick_open(&item_pick,&story,state);dirty=1;
            if(trace){fprintf(trace,"item_menu_open items=%u\n",item_pick.count);fflush(trace);}
        }
        if(item_pick.open){
            uint32_t pressed=input.pressed;
#if defined(C3_ROUNDTRIP_PROBE) || defined(C3_WARP_TOUR) || defined(C3_CAMPAIGN)
            pressed=C3_PAD_A; /* probes use the first held key item, or close */
#endif
            uint32_t chosen=item_pick_update(&item_pick,pressed);
            if(pressed)dirty=1;
            if(chosen!=UINT32_MAX){
                item_pick.open=0;
                if(trace){fprintf(trace,"item_menu item=%lu\n",(unsigned long)chosen);fflush(trace);}
                if(!c3_field_story_item_menu_result(&story,chosen,story_error))goto story_failed;
            }
            input.pressed=0;input.touch_pressed=false;
        }
        /* Native C2's party choice at the Relic Stone. It owns all input. */
        if(view->relive_pick&&!relive_pick.open&&page==PAUSE_CLOSED){
            relive_pick_open(&relive_pick,state);dirty=1;
            if(trace){fprintf(trace,"relive_pick_open party=%u cursor=%u pickable=%u\n",relive_pick.count,relive_pick.cursor,
                              relive_pick.count?relive_pick.pickable[relive_pick.cursor]:0u);fflush(trace);}
        }
        if(relive_pick.open){
            uint32_t pressed=input.pressed;
#ifdef C3_ROUNDTRIP_PROBE
            pressed=relive_pick.count&&relive_pick.pickable[relive_pick.cursor]?C3_PAD_A:C3_PAD_B;
#endif
            int chosen=relive_pick_update(&relive_pick,pressed);
            if(pressed)dirty=1;
            if(chosen!=-2){
                relive_pick.open=0;
                if(trace){fprintf(trace,"relive_pick slot=%d\n",chosen);fflush(trace);}
                if(!c3_field_story_relive_pick(&story,chosen,story_error))goto story_failed;
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
            c3_field_story_set_move_input(&story,input.held,input.stick_x,input.stick_y);
            if(!c3_field_story_tick(&story,delta,phase==FIELD_CONTROL?input.pressed:0,story_error))goto story_failed;
            if(view->mail_received!=mail_seen){
                mail_seen=view->mail_received;
                unsigned sort=0,tone=0;
                if(c3_game_state_mail_settings(state,&sort,&tone)&&c3_mail_alert_start(&mail_alert,tone)){
                    char audio_error[192]="";mail_frames=0;
                    if(!pda_audio_ready)pda_audio_ready=c3_pda_audio_open("sdmc:/3ds/Colosseum3DS",audio_error,sizeof audio_error);
                    int playing=pda_audio_ready&&c3_pda_audio_preview(tone,audio_error,sizeof audio_error);
                    if(!playing)mail_alert.tone_playing=0; /* missing ringtones: the envelope still blinks */
                    if(trace){fprintf(trace,"mail_alert start=%lu tone=%u sound=%d reason=%s\n",(unsigned long)mail_alert.started,tone,playing,audio_error);fflush(trace);}
                }
            }
#ifdef C3_FINAL_EVENT_PROBE
            uint32_t encounter;
            if(c3_field_story_battle_pending(&story,&encounter)){
                if(trace){fprintf(trace,"final_event_battle encounter=%lu result=2 mode=skip_as_win party_writeback=0\n",(unsigned long)encounter);fflush(trace);}
                if(!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
            }
#endif
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
                /* heroMoveAddAutoEvent work outlives the floor (Realgam dome doors). */
                if(!c3_field_story_pending_auto_event(&story,carried_auto_event))memset(carried_auto_event,0,sizeof carried_auto_event);
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
            trace_stage(trace,"prepared",(int)frames,load_start_us,&fs_base);}
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
                C3BootFsStats fs;c3_boot_fs_stats(&fs);
                fprintf(trace,"field_fs lookups=%u us=%llu answered=%u session_max_us=%llu session_max=%s\n",fs.lookups-fs_base.lookups,
                        (unsigned long long)(fs.lookup_us-fs_base.lookup_us),fs.answered-fs_base.answered,
                        (unsigned long long)fs.max_us,fs.max_name);
                fprintf(trace,"field_load_to_control floor=%lu us=%llu\n",(unsigned long)map.floor,
                        (unsigned long long)(c3_n3ds_time_us()-load_start_us));fflush(trace);}
        }
        if(phase==FIELD_CONTROL){
            if(page==PAUSE_CLOSED){
#if !defined(C3_CAMPAIGN) && !defined(C3_WARP_TOUR)
                /* (The campaign and warp-tour builds settle battles above.) */
                if(view->blocked[0]&&view->battle_id&&view->battle_refused!=view->battle_id){
                    /* Every story battle, encounter 10 (Willie) included,
                     * runs on the general battle engine as soon as native
                     * 98/66 asks for it: the original goes straight from the
                     * trainer's last line into the battle transition, with
                     * no prompt. Trainer entry, the battle on its original
                     * stage, then the writeback (EXP, prize, HP/PP, whiteout,
                     * snagging) in c3_battle_story_apply resumes native 98
                     * with the real result. An encounter the engine cannot
                     * play is never reported as a result: native 98 keeps
                     * waiting and the reason is shown (START quits). */
                    uint32_t encounter=view->battle_id;
                    FieldBattleHost host={storage,models,actor_models,model_paths,&story,&npc,ingame,
                                          count,tv_index,skin_actors,view?view->tv_enabled:1,frames,elapsed};
                    int played=play_story_battle(&story,state,encounter,0,&host,trace,story_error,sizeof story_error);
                    if(played<0)goto story_failed;
                    if(played==2){result=1;break;}
                    /* Battle time must not advance the paused field scripts. */
                    previous=c3_n3ds_active_time_us();
                    if(played==1){dirty=1;next_console=0;continue;}
#ifdef C3_STORY_BATTLE_SKIP_REFUSED
                    /* Debug builds only: the old shortcut, a refused
                     * encounter reported as the hero's win (result ID 2). */
                    if(!c3_field_story_battle_result(&story,2,story_error))goto story_failed;
                    if(trace){fprintf(trace,"field_battle_skipped encounter=%lu result_reported=2 debug=1 party_writeback=0\n",
                        (unsigned long)encounter);fflush(trace);}
#else
                    {char reason[160];snprintf(reason,sizeof reason,"%s",story_error);
                     if(!c3_field_story_battle_refuse(&story,reason,story_error))goto story_failed;
                     if(trace){fprintf(trace,"field_battle_refused encounter=%lu reason=\"%s\" result_reported=0\n",
                         (unsigned long)encounter,reason);fflush(trace);}}
#endif
                    dirty=1;continue;
                }
#endif
                if(view->blocked[0]&&(input.pressed&(C3_PAD_START|C3_PAD_X))){result=0;break;}
                /* A doorway into a floor whose packs are not on the SD card:
                 * A stays on this floor and play continues. */
                if(view->blocked[0]&&!view->battle_id&&(input.pressed&C3_PAD_A)&&c3_field_story_floor_refused(&story,NULL)){
                    if(!c3_field_story_stay_on_floor(&story,story_error))goto story_failed;
#ifdef C3_ROUNDTRIP_PROBE
                    probe_route.stayed=1;
#endif
                    dirty=1;continue;
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
                        c3_first_person_reset_view(&first_person);
                        input.pressed&=(uint16_t)~(C3_PAD_DOWN|C3_PAD_Z|C3_PAD_R);
                        if(trace){fprintf(trace,"camera_recenter\n");fflush(trace);}
                    }
                    uint16_t view_buttons=0;
                    if(first_person.enabled){
                        /* D-pad: field of view (Up/Down), eye height (Left/Right),
                         * view distance (L + Up/Down); kept from walking. */
                        if(!(input.held&(C3_PAD_Z|C3_PAD_R)))
                            view_buttons=c3_first_person_adjust(&first_person,input.held,map.camera.fov_y_degrees,delta);
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
                        walk_buttons&=(uint16_t)~view_buttons;
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
                /* Stylus taps become the same button edges (Back = B). */
                input.pressed|=c3_ingame_touch(ingame,&input);
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
                if(ingame->sound){c3_pda_sound_play(ingame->sound);ingame->sound=0;}
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
                else if(event.kind==C3_INGAME_RESUME){c3_pda_audio_close();c3_pda_sound_close();pda_audio_ready=0;page=PAUSE_CLOSED;dirty=1;}
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
                    else if(event.mail.script){c3_pda_audio_close();c3_pda_sound_close();pda_audio_ready=0;page=PAUSE_CLOSED;}
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
        /* The tail slots belong to the asynchronous people loader. Until
         * attach completes they are zeroed or being written by that thread;
         * sample only the same admitted models the renderer can draw. */
        if(lights_loaded)for(uint32_t i=0;i<(npc.attached?count:npc.first);++i)if(i!=tv_index)
            sampled&=c3_area_lights_apply(&lights,elapsed,storage+i);
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
        if(mail_alert.active){
            /* A battle ends the alert (mailMainReceiveTerminate in fight.c). */
            if(view->battle_id&&c3_mail_alert_terminate(&mail_alert))c3_pda_audio_stop();
            mail_frames+=delta*60.0f;uint32_t step=(uint32_t)mail_frames;mail_frames-=(float)step;
            int was=ingame->mail_alert;
            c3_mail_alert_tick(&mail_alert,step,mail_alert.tone_playing&&c3_pda_audio_playing());
            ingame->mail_alert=(unsigned)c3_mail_alert_visible(&mail_alert);
            if((int)ingame->mail_alert!=was)dirty=1;
            if(!mail_alert.active){
                if(page==PAUSE_CLOSED&&pda_audio_ready){c3_pda_audio_close();pda_audio_ready=0;}
                if(trace){fprintf(trace,"mail_alert end=%lu frames=%lu\n",(unsigned long)mail_alert.started,(unsigned long)mail_alert.frames);fflush(trace);}
            }
        }
        if(dirty||input.pressed||now>=next_console){
            C3BottomUi *bottom=c3_bottom_screen_begin();
            if(naming)c3_name_entry_draw(&name_entry,bottom);
            else if(shopping)c3_shop_menu_draw(&shop_menu,state,bottom,profile->label);
            else if(pc_open)c3_pc_menu_draw(&pc_menu,bottom);
            else if(relive_pick.open)relive_pick_draw(&relive_pick,bottom);
            else if(item_pick.open)item_pick_draw(&item_pick,bottom);
            else if(page==PAUSE_MAIN&&ingame->active)c3_ingame_draw_to(ingame,state,bottom);
            else if(page==PAUSE_CLOSED&&(phase==FIELD_CONTROL||phase==FIELD_FADE_IN)&&!view->dialogue&&!view->blocked[0]&&
                    hub_menu_ready(ingame,state)>=0){
                /* The P*DA in-game menu hub is the field bottom screen, also
                 * while an event runs without a message (taps open it only
                 * when the hero has control). */
                ingame->idle=1;ingame->page=C3_GAME_HUB;c3_ingame_draw_to(ingame,state,bottom);
            }
            else c3_field_ui_draw_to(bottom,&summary,phase,page,selection,yes,message,view,&ui_pager);
            /* The new-mail envelope over the field's bottom views, as the
             * original draws it over the scene; not over a modal menu. */
            int mail_drawn=0;
            if(ingame->mail_alert&&!naming&&!shopping&&!pc_open&&!relive_pick.open&&!item_pick.open&&hub_menu_ready(ingame,state)>0)
                mail_drawn=c3_ingame_draw_mail_alert(ingame,bottom);
            (void)mail_drawn; /* read by the round-trip probe */
            c3_bottom_screen_present();dirty=0;next_console=now+UINT64_C(100000);
#ifdef C3_ROUNDTRIP_PROBE
            /* Probe: the hub with the new-mail envelope, once per alert. */
            {static uint32_t mail_shot;
             if(mail_drawn&&mail_shot!=mail_alert.started){
                char shot[96];mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/mail_alert_%lu.bmp",(unsigned long)mail_alert.started);
                int ok=c3_n3ds_capture_bottom(shot)==0;mail_shot=mail_alert.started;
                if(trace){fprintf(trace,"probe_mail_alert alert=%lu frames=%lu shot=%d\n",(unsigned long)mail_alert.started,
                                  (unsigned long)mail_alert.frames,ok);fflush(trace);}
             }}
#endif
        }
        if(pda_preview){
            pda_seconds+=delta;
            c3_pda_preview_control(pda_preview,abs(input.cstick_x)>20?input.cstick_x/128.f:0,
                                   abs(input.cstick_y)>20?input.cstick_y/128.f:0,delta);
        }
        if(!pda_preview&&c3_debug_overlay_enabled()){
            char l1[64],l2[64];
            snprintf(l1,sizeof l1,"floor %03lx xyz %.1f %.1f %.1f",(unsigned long)map.floor,
                     (double)player.position[0],(double)player.position[1],(double)player.position[2]);
            if(first_person.enabled)
                snprintf(l2,sizeof l2,"fp fov %.0f eye %.1f dist %.2fx",(double)camera.fov_y_degrees,
                         (double)(first_person.eye_height>0?first_person.eye_height:C3_FIRST_PERSON_EYE_HEIGHT),
                         (double)(first_person.view_distance>0?first_person.view_distance:1.0f));
            else snprintf(l2,sizeof l2,"orbit zoom %.2f yaw %.2f pitch %.2f",(double)orbit.zoom,(double)orbit.yaw,(double)orbit.pitch);
            c3_debug_overlay_frame("field",&camera,0,&stats,l1,l2,NULL);
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
#ifdef C3_FINAL_EVENT_PROBE
        /* Private review frames for every reached dialogue page. */
        {
            static uint32_t captured_message=UINT32_MAX;static size_t captured_offset=SIZE_MAX;
            if(view->dialogue&&(view->message_id!=captured_message||ui_pager.offset!=captured_offset)){
                char shot[160];
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/final-probe/text_%03lx_%05lx_%lu.bmp",(unsigned long)map.floor,(unsigned long)view->message_id,(unsigned long)ui_pager.offset);
                int captured=c3_n3ds_capture_bottom(shot)==0;
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/final-probe/scene_%03lx_%05lx_%lu.bmp",(unsigned long)map.floor,(unsigned long)view->message_id,(unsigned long)ui_pager.offset);
                captured&=c3_n3ds_capture_top(shot)==0;
                if(trace){fprintf(trace,"final_event_dialogue message=%05lx page_offset=%lu captured=%d\n",(unsigned long)view->message_id,(unsigned long)ui_pager.offset,captured);fflush(trace);}
                captured_message=view->message_id;captured_offset=ui_pager.offset;
            }
        }
#endif
        ++frames;
#if defined(C3_CLEAR_CONTINUE_PROBE) || defined(C3_FINAL_EVENT_PROBE) || defined(C3_RETURN_FADE_PROBE)
        /* Observe the loaded Continue checkpoint; never warp or synthesize
         * a story choice. Capture only once the native entry yields control. */
        if(map.floor==2&&npc.attached&&view->control&&frames>=90u){
            int captured=c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/clear-continue.bmp")==0;
            if(trace){fprintf(trace,"clear_continue_frame floor=%03lx previous=%03lx spawn=%u control=%d captured=%d frame=%u\n",(unsigned long)map.floor,(unsigned long)summary.previous_floor,summary.spawn,view->control,captured,frames);fflush(trace);}
            result=captured?0:-1;break;
        }
        if(map.floor==2&&frames>1800u){snprintf(error,capacity,"Continue did not yield control");result=-1;break;}
#endif
#ifdef C3_ROUNDTRIP_PROBE
        /* Leave for the world map once the people have joined; keep the
         * last frame of each area for review (the capture lags one frame). */
        {   static int door_fired;static unsigned next_step,fired_at;
            if(frames<=1u){door_fired=0;next_step=90u;}
            probe_route_load();
            if(npc.attached&&frames>=next_step&&view->control&&!door_fired&&!naming&&!probe_route.name_mode&&probe_route.next<probe_route.count&&
               probe_route.floor[probe_route.next]==map.floor&&(probe_route.interact[probe_route.next]==1||probe_route.interact[probe_route.next]==3)){
                int talk=probe_route.interact[probe_route.next]==3;
                uint32_t source=probe_route.source[probe_route.next++];const C3FieldTriangle *hit=NULL;
                if(!talk)for(uint32_t i=0;i<area.interaction_count&&!hit;++i)if(area.interactions[i].source_id==source)hit=area.interactions+i;
                if(talk&&source<area.actor_count){
                    /* One unit from the person on walkable ground, +z first
                     * (as tests/test_phenac_story.c stands at the sack),
                     * facing it. */
                    C3StoryActor *actor=c3_field_story_actor(&story,area.actors[source].index);
                    const float *at=actor?actor->pose.position:area.actors[source].position;
                    float ground;
                    for(int turn=0;turn<4;++turn){
                        float a=(float)turn*1.5707963f,spot[3]={at[0]+sinf(a),at[1],at[2]+cosf(a)};
                        if(!c3_field_ground(&map,spot,&ground))continue;
                        player.position[0]=spot[0];player.position[1]=ground;player.position[2]=spot[2];
                        memcpy(player.proposed,player.position,sizeof player.position);
                        player.yaw=atan2f(at[0]-spot[0],at[2]-spot[2]);break;
                    }
                    if(trace){fprintf(trace,"probe_talk floor=%03lx person=%lu visible=%d index=%lu\n",(unsigned long)map.floor,
                                      (unsigned long)source,actor?actor->visible:-1,(unsigned long)area.actors[source].index);fflush(trace);}
                }
                if(hit){
                    float c[3]={0},n[2]={hit->normal[0],hit->normal[2]},len;
                    for(int v=0;v<3;++v)for(int k=0;k<3;++k)c[k]+=hit->vertices[v*3+k]/3.0f;
                    len=hypotf(n[0],n[1]);if(len<1e-4f){n[0]=0;n[1]=1;len=1;}
                    n[0]/=len;n[1]/=len;
                    /* The side with ground: in front of the surface, facing it. */
                    for(int side=0;side<2;++side){
                        float sgn=side?-1.0f:1.0f,spot[3]={c[0]+n[0]*sgn*0.5f,c[1],c[2]+n[1]*sgn*0.5f},ground;
                        if(!c3_field_ground(&map,spot,&ground))continue;
                        player.position[0]=spot[0];player.position[1]=ground;player.position[2]=spot[2];
                        memcpy(player.proposed,player.position,sizeof player.position);
                        player.yaw=atan2f(c[0]-spot[0],c[2]-spot[2]);break;
                    }
                }
                if(trace){fprintf(trace,"probe_interact floor=%03lx source=%lu surface=%d\n",(unsigned long)map.floor,
                                  (unsigned long)source,hit!=NULL);fflush(trace);}
                if(!c3_field_story_interact(&story,story_error))goto story_failed;
                next_step=frames+120u;
            }
            /* Dialogue: each new page's bottom screen and its text (markup
             * removed), 20 frames after it opened, at most 64 per run. */
            {static uint32_t page_id,page_hash,page_frame,page_shots;
             uint32_t hash=2166136261u;
             if(view->dialogue){for(const char *c=view->message;*c;++c)hash=(hash^(unsigned char)*c)*16777619u;}
             if(!view->dialogue){page_id=0;page_hash=0;page_frame=0;}
             else if(view->message_id!=page_id||hash!=page_hash){page_id=view->message_id;page_hash=hash;page_frame=frames+20u;}
             if(page_frame&&frames>=page_frame&&page_shots<64u){
                 char shot[96],text[C3_STORY_MESSAGE];mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                 snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/msg_%03lx_%05lx_%02lu.bmp",(unsigned long)map.floor,
                          (unsigned long)page_id,(unsigned long)page_shots);
                 int shot_ok=c3_n3ds_capture_bottom(shot)==0;
                 snprintf(text,sizeof text,"%s",view->message);c3_story_text_plain(text);
                 for(char *c=text;*c;++c)if(*c=='\n')*c='|';
                 if(trace){fprintf(trace,"probe_message floor=%03lx id=%05lx shot=%d text=\"%s\"\n",(unsigned long)map.floor,
                                   (unsigned long)page_id,shot_ok,text);fflush(trace);}
                 page_frame=0;++page_shots;
             }}
            /* Film: consecutive top frames once the hero has control. */
            {static uint32_t filmed;
             /* An event scene (no entrance, no collision) never gives the
              * hero control: film it from its first drawn frame. */
             int event_scene=!map.spawn_count&&!map.walk_count;
             if(probe_route.film_frames&&probe_route.film_floor==map.floor&&npc.attached&&(view->control||event_scene)&&
                filmed<probe_route.film_frames&&frames%(probe_route.film_stride?probe_route.film_stride:1u)==0){
                 char shot[96];mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                 snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/film_%03lx_%03lu.bmp",(unsigned long)map.floor,(unsigned long)filmed);
                 (void)c3_n3ds_capture_top(shot);
                 if(trace&&!filmed){fprintf(trace,"probe_film floor=%03lx frames=%lu frame=%u\n",(unsigned long)map.floor,(unsigned long)probe_route.film_frames,frames);fflush(trace);}
                 if(++filmed==probe_route.film_frames&&trace){fprintf(trace,"probe_film_end frame=%u elapsed=%.3f\n",frames,(double)elapsed);fflush(trace);}
                 next_step=frames+30u;
             }}
            if(npc.attached&&frames>=next_step&&view->control&&!door_fired&&!naming&&!pc_open&&!probe_route.name_mode&&
               (probe_route.walk_floor!=map.floor||probe_route.walked>=probe_route.walk_frames)){
                char shot[80];
                mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
                if(trace){fprintf(trace,"probe_capture floor=%lu prepared=%d control=%d frame=%u\n",(unsigned long)map.floor,view->prepared,view->control,frames);fflush(trace);}
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/area_%03lx_top.bmp",(unsigned long)map.floor);
                (void)c3_n3ds_capture_top(shot);
                snprintf(shot,sizeof shot,"sdmc:/3ds/Colosseum3DS/probe/area_%03lx_bottom.bmp",(unsigned long)map.floor);
                (void)c3_n3ds_capture_bottom(shot);
                probe_route_load();
                if(probe_route.next<probe_route.count&&probe_route.floor[probe_route.next]==map.floor){
                    int touch=probe_route.interact[probe_route.next]==2;
                    uint32_t source=probe_route.source[probe_route.next++];
                    if(trace){fprintf(trace,"probe_door floor=%03lx source=%lu touch=%d\n",(unsigned long)map.floor,(unsigned long)source,touch);fflush(trace);}
                    door_fired=1;fired_at=frames;
                    if(touch){if(c3_field_story_touch(&story,source,story_error)<0)goto story_failed;}
                    else if(!c3_field_story_transition(&story,source,story_error))goto story_failed;
                }else{
                    if(!c3_game_state_set_floor(state,C3_FLOOR_WORLD_MAP_ID,map.floor,0)){result=-1;break;}
                    result=2;break;
                }
            }
            /* A fired door that never links: give up after 20 s. */
            /* A refused doorway kept the hero here: go on with the route. */
            if(door_fired&&probe_route.stayed){probe_route.stayed=0;door_fired=0;next_step=frames+90u;}
            if(door_fired&&frames>=fired_at+1200u){snprintf(error,capacity,"probe door did not link");result=-1;break;}
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
            /* The partner (0,101) beside each field tick: where she stands
             * relative to Wes explains a blocked step. */
            {const C3StoryActor *rui=c3_field_story_person(&story,0,101);
             if(rui)fprintf(trace,"field_partner floor=%lu visible=%d xyz=%.4f,%.4f,%.4f yaw=%.4f clip=%lu gap=%.4f\n",(unsigned long)map.floor,rui->visible,
                 (double)rui->pose.position[0],(double)rui->pose.position[1],(double)rui->pose.position[2],(double)rui->pose.yaw,(unsigned long)rui->clip,
                 (double)hypotf(rui->pose.position[0]-player.position[0],rui->pose.position[2]-player.position[2]));}
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
    c3_story_free(&name_messages);
    /* Every slot, room and people: streamed people attach mid-scene without
     * moving `opened`, and calloc'd slots that never loaded close as no-ops.
     * Closing only `opened` leaked every person pack on each area exit. */
    if(npc.stream){c3_pack_stream_finish(npc.stream);npc.stream=NULL;}
    if(storage)opened=count;
    c3_area_prefetch_release(); /* anything a failed load did not take */
    free(npc.jobs);npc.jobs=NULL;
    c3_pda_audio_close();c3_pda_sound_close();
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
    enum{AREAS=40};
    char stage[64];int result=0;
    c3_n3ds_log_memory(trace,"roundtrip_baseline");
    /* Start in the save's own world-map area (its floor, else the floor it
     * came from, as when a save sits in a building), else Phenac; the world
     * map then visits the unlocked destinations in turn. */
    uint32_t start=3;
    probe_route_load();
    if(probe_route.start)start=probe_route.start;
    else {   C3GameSummary s;
        if(c3_game_state_summary(state,&s))
            for(uint32_t i=0;i<c3_world_map_entry_count();++i){C3WorldMapEntry e;
                if(!c3_world_map_entry(i,&e)||!c3_field_scene_supports(e.floor))continue;
                if(e.floor==s.floor){start=s.floor;break;}
                if(e.floor==s.previous_floor)start=s.previous_floor;} }
    if(trace){fprintf(trace,"roundtrip_start floor=%03lx\n",(unsigned long)start);fflush(trace);}
    if(probe_route.shadow_id){
        uint8_t r[C3_GB_POKEMON_SIZE];unsigned slot=probe_route.shadow_slot,id=probe_route.shadow_id;
        if(c3_game_state_pokemon(state,slot,r)&&(r[0]|r[1])){
            /* Shadow data ID, heart gauge 0, 20000 stored experience, 30
             * stored friendship; the purify flag cleared. */
            r[0xd8]=(uint8_t)(id>>8);r[0xd9]=(uint8_t)id;memset(r+0xdc,0,4);
            r[0xe0]=0;r[0xe1]=0;r[0xe2]=0x4e;r[0xe3]=0x20;r[0xe4]=0;r[0xe5]=30;
            int ok=c3_game_state_set_pokemon(state,slot,r)&&c3_game_state_set_flag(state,c3_relive_purify_flag((uint16_t)id),0);
            if(trace){fprintf(trace,"roundtrip_shadow slot=%u id=%u level=%u ok=%d\n",slot,id,(unsigned)r[0x60],ok);fflush(trace);}
        }
    }
    for(unsigned i=0;i<probe_route.flags;++i){
        int ok=c3_game_state_set_flag(state,probe_route.flag_id[i],probe_route.flag_value[i]);
        if(trace){fprintf(trace,"roundtrip_flag id=%03lx value=%lu ok=%d\n",(unsigned long)probe_route.flag_id[i],(unsigned long)probe_route.flag_value[i],ok);fflush(trace);}
    }
    if(probe_route.partner>=0)c3_game_state_set_partner(state,probe_route.partner);
    for(unsigned i=0;i<probe_route.balls;++i){int ok=c3_game_state_put_item(state,2,probe_route.ball_id[i],probe_route.ball_count[i]);
        if(trace){fprintf(trace,"roundtrip_ball item=%lu count=%lu ok=%d\n",(unsigned long)probe_route.ball_id[i],(unsigned long)probe_route.ball_count[i],ok);fflush(trace);}}
    if(probe_route.continue_save){C3GameSummary s;
        if(trace&&c3_game_state_summary(state,&s)){fprintf(trace,"roundtrip_continue floor=%03lx previous=%03lx spawn=%u\n",
            (unsigned long)s.floor,(unsigned long)s.previous_floor,s.spawn);fflush(trace);}}
    else if(!c3_game_state_set_floor(state,start,C3_FLOOR_WORLD_MAP_ID,(uint8_t)probe_route.start_spawn)){snprintf(error,capacity,"floor rejected");return -1;}
    /* Memory check: after every area exit the allocator must be back near
     * the first exit's level (an area's packs are all released on exit). */
    size_t baseline=0,worst=0;unsigned exits=0;int leak=0;
    for(unsigned i=0;i<AREAS;++i){
        C3GameSummary summary;uint32_t floor=c3_game_state_summary(state,&summary)?summary.floor:0;
        /* The staff roll runs the production ending scene; its clear save
         * goes to a probe card, never the player's save. */
        if(floor==0x39f){mkdir("sdmc:/3ds/Colosseum3DS/probe",0777);
            result=c3_ending_scene_run(state,"sdmc:/3ds/Colosseum3DS/probe/clear.gci",trace,error,capacity);}
        else result=run_area(state,0,"sdmc:/3ds/Colosseum3DS/probe-unused.gci",trace,error,capacity);
        snprintf(stage,sizeof stage,"roundtrip_%u_field_%03lx_end",i,(unsigned long)floor);
        c3_n3ds_log_memory(trace,stage);
        {   size_t used=c3_n3ds_heap_used();
            if(!exits++)baseline=used;
            size_t growth=used>baseline?used-baseline:0;if(growth>worst)worst=growth;
            int ok=growth<=1024u*1024u;leak|=!ok;
            if(trace){fprintf(trace,"roundtrip_memory exit=%u floor=%03lx used=%lu baseline=%lu growth=%lu ok=%d\n",exits,
                              (unsigned long)floor,(unsigned long)used,(unsigned long)baseline,(unsigned long)growth,ok);fflush(trace);} }
        if(result!=2||i+1==AREAS)break;
        if(probe_route.stop&&floor==probe_route.stop){
            if(trace){fprintf(trace,"roundtrip_stop floor=%03lx\n",(unsigned long)floor);fflush(trace);}break;}
        /* A door led to another field floor: no world-map visit. */
        if(c3_game_state_summary(state,&summary)&&summary.floor!=C3_FLOOR_WORLD_MAP_ID)continue;
        result=c3_world_map_scene_run(state,trace,error,capacity);
        snprintf(stage,sizeof stage,"roundtrip_%u_worldmap_end",i);
        c3_n3ds_log_memory(trace,stage);
        if(result!=2)break;
    }
    if(trace){fprintf(trace,"roundtrip_memory_check status=%s exits=%u baseline=%lu worst_growth=%lu limit=1048576\n",
                      leak?"fail":"pass",exits,(unsigned long)baseline,(unsigned long)worst);
              fprintf(trace,"roundtrip_end result=%d error=%s\n",result,result<0?error:"");fflush(trace);}
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
        if(c3_game_state_summary(state,&summary)&&summary.floor==0x39f)
            result=c3_ending_scene_run(state,save_path,trace,error,capacity);
        else if(c3_game_state_summary(state,&summary)&&summary.floor==C3_FLOOR_WORLD_MAP_ID)
            result=c3_world_map_scene_run(state,trace,error,capacity);
        else result=run_area(state,new_game,save_path,trace,error,capacity);
        new_game=0;
    }while(result==2);
    return result;
}
