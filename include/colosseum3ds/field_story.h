#ifndef C3_FIELD_STORY_H
#define C3_FIELD_STORY_H

#include "colosseum3ds/relive.h"
#include "colosseum3ds/area.h"
#include "colosseum3ds/area_camera.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/story.h"
#include <stdio.h>

/* Scene adapter for original GSVM. All geometry/game-state objects are
 * borrowed and must outlive this context. No unsupported native is silently
 * resumed. The original battle call remains pending until a backend exists. */
typedef struct C3FieldStory { void *impl; } C3FieldStory;
typedef struct C3StoryActor {
    C3FieldPlayer pose;
    C3Title *model;
    uint32_t index, clip, flags;
    int visible, loop, playing;
} C3StoryActor;
/* One treasure person of the floor (floor.c _floorInitTresure): kind 1 is a
 * chest (people info 0x03770400), 2 and 3 items (0x03780400 / 0x03790400);
 * type is floorEventGetTresure's gift type (1 item, 2 money, 3 special,
 * 4 item without the first message). */
typedef struct C3StoryTreasure {
    uint32_t index, kind, type, item, count, taken_flag, hide_flag;
    float position[3], yaw;
    int visible, taken;
} C3StoryTreasure;
typedef struct C3FieldStoryView {
    char message[C3_STORY_MESSAGE];
    char blocked[C3_STORY_ERROR];
    uint32_t message_id, next_floor, next_spawn, battle_id;
    int prepared, control, dialogue, choice, choice_no, tv_enabled;
    int camera_active;
    uint32_t name_entry; /* native 16 mode + 1 while the scene must collect a name */
    uint32_t shop;       /* native 19 shop number + 1 while the scene shows the shop */
    uint32_t menu_floor; /* native 62 menu floor (0x391 PC box ...) while the scene shows it */
    uint16_t dna_species[3]; /* native CC: the discs' species while menu_floor is 0x387 */
    uint32_t unported_menu; /* last native whose menu step is not ported (took the decline branch) */
    int relive_pick;        /* native C2 waits for a party choice (c3_field_story_relive_pick) */
    int item_menu;          /* native D0 waits while the scene shows the pause menu's Items */
    uint32_t battle_refused; /* the waiting encounter the scene could not play (c3_field_story_battle_refuse) */
    uint32_t mail_received; /* native AD deliveries so far (mailMainSendByScrpt): a change
                             * starts the scene's new-mail alert (colosseum3ds/mail_alert.h) */
    float fade, camera_seconds;
    C3TitleCamera camera;
} C3FieldStoryView;

int c3_field_story_load(C3FieldStory *story, const char *path,
                        C3GameState *game, C3FieldMap *map, const C3Area *area,
                        C3FieldPlayer *hero, C3Title *hero_model,
                        C3Title *const *actor_models, const C3AreaCamera *camera,
                        FILE *trace, char *error);
void c3_field_story_close(C3FieldStory *story);
/* Admits encounter data, not a battle executor. The native remains pending. */
int c3_field_story_bind_battle(C3FieldStory *story,const char *path,char *error);
/* Native 98 stays suspended until the battle scene reports the original
 * fight result ID (fn_801EF634; 2 is the hero's win in the field scripts).
 * pending reports the waiting encounter; result resumes the script exactly
 * once and makes the same ID readable through native 67. */
int c3_field_story_battle_pending(const C3FieldStory *story,uint32_t *encounter);
int c3_field_story_battle_result(C3FieldStory *story,uint32_t result,char *error);
/* The scene cannot play the waiting encounter (no battle pack, or the pack
 * refuses it). No result is invented: native 98 keeps waiting, view.blocked
 * names the encounter and the reason, and view.battle_refused holds it. */
int c3_field_story_battle_refuse(C3FieldStory *story,const char *reason,char *error);
/* A floor link to a floor the scene cannot load (see floor_supported)
 * waits with view.blocked set. refused reports the requested floor;
 * stay_on_floor resumes the script without linking, so play continues. */
int c3_field_story_floor_refused(const C3FieldStory *story,uint32_t *floor);
int c3_field_story_stay_on_floor(C3FieldStory *story,char *error);
/* Hero lost an encounter with the wipeout (zenmetu) flag: set flag 0E05,
 * restore the party (party service), and leave for the whiteout point
 * recorded by native A1, else the last save point
 * (c3_game_state_save_point); resume with result 3 if neither is known. */
int c3_field_story_battle_whiteout(C3FieldStory *story,char *error);
/* Native 16 mode 1 names the partner. When the scene enables it the script
 * waits with view.name_entry set until the scene reports 1..10 printable
 * ASCII characters; otherwise the saved default name is kept. */
void c3_field_story_enable_name_entry(C3FieldStory *story,int enabled);
int c3_field_story_name_result(C3FieldStory *story,const char *name,char *error);
/* Native 19 (menuShopOpen). Enabled, it waits with view.shop set until the
 * scene reports that the player left the shop; disabled, the shop closes at
 * once. The scene performs the purchases itself (colosseum3ds/shop.h). */
void c3_field_story_enable_shop(C3FieldStory *story,int enabled);
/* Native 62 (fn_800FF730) pushes a menu floor. Floors the scene enables
 * wait with view.menu_floor set until menu_floor_done; others return at
 * once (recorded as not ported). */
void c3_field_story_enable_menu_floor(C3FieldStory *story,uint32_t floor,int enabled);
int c3_field_story_menu_floor_done(C3FieldStory *story,char *error);
/* Native CC (fn_801C8688) checks three DNA discs against the species grid
 * and pushes floor 0x387, the disc puzzle. With 0x387 enabled as a menu
 * floor the script waits with view.menu_floor = 0x387 and view.dna_species
 * set until dna_result reports the puzzle's retail result (0 matched,
 * 1 cancelled, 2 invalid); CC then returns 1 only for 0. Not enabled, the
 * puzzle is recorded as solved (temporary, as before). */
int c3_field_story_dna_result(C3FieldStory *story,int result,char *error);
/* An original message (field, common or names group) as plain text, for
 * the scene's menus; name answers value controls 32/36. 0 when absent. */
int c3_field_story_text(C3FieldStory *story,uint32_t id,const char *name,char *out,size_t capacity);
int c3_field_story_shop_done(C3FieldStory *story,char *error);
/* Native D0 (fn_801C8628) opens the pause menu on Items so the player can
 * use a key item. Enabled, the script waits with view.item_menu set until
 * the scene reports the item used (0: menu closed). An item registered by
 * native 11 queues its script, which runs once the current event ends;
 * items_menu_items lists those registered items. Disabled, the menu
 * closes at once. */
void c3_field_story_enable_item_menu(C3FieldStory *story,int enabled);
/* Treasure: <prefix>.c3r (tools/prepare_story.py --doors-only). A missing
 * file means the floor has none. Interact takes a visible treasure in
 * front of the hero with the original messages; native CB's row changes
 * last for the session. Needs the item service. */
int c3_field_story_load_treasure(C3FieldStory *story,const char *path,char *error);
/* Diagnostics: the x/z boxes (x0,z0,x1,z1 in field units) the floor's
 * scripts have tested the hero against with native 3A so far. */
unsigned c3_field_story_hero_regions(const C3FieldStory *story,float out[][4],unsigned max);
unsigned c3_field_story_treasure_count(const C3FieldStory *story);
const C3StoryTreasure *c3_field_story_treasure(const C3FieldStory *story,unsigned index);
unsigned c3_field_story_item_menu_items(const C3FieldStory *story,uint32_t items[8]);
int c3_field_story_item_menu_result(C3FieldStory *story,uint32_t item,char *error);
/* Item gifts (script native 77). receive adds `count` of `item` to the bag
 * as heroItemAddItemDataId does and writes the item's display name: 0 when
 * everything was added, a positive amount that did not fit, negative when
 * the item is invalid. Unbound, native 77 fails. */
typedef int (*C3FieldStoryReceive)(void *context,uint32_t item,uint32_t count,char *name,size_t capacity);
void c3_field_story_set_item_service(C3FieldStory *story,C3FieldStoryReceive receive,void *context);
/* Direct scriptAddItem (DB): signed add/remove, returning the unmet count
 * or a negative invalid-request result. No item window or jingle. */
typedef int (*C3FieldStoryItemChange)(void *context,uint32_t item,int32_t count);
void c3_field_story_set_item_change_service(C3FieldStory *story,C3FieldStoryItemChange change,void *context);
/* Item removal (native DB with a negative count, heroItemDecItemDataId):
 * removes `count` of `item`; returns 0 when all were removed, the amount
 * missing otherwise, negative when invalid. Unbound, those natives fail. */
typedef int (*C3FieldStoryTake)(void *context,uint32_t item,uint32_t count);
void c3_field_story_set_item_take_service(C3FieldStory *story,C3FieldStoryTake take,void *context);
/* Full party restore (script native D8 mode 0, pokemonAllKaihuku). Returns
 * the number of members restored or a negative value. Unbound, D8 fails. */
typedef int (*C3FieldStoryRestore)(void *context);
/* Purification at the Relic Stone (native C2, preReliveSetParameter ->
 * preReliveMain; colosseum3ds/relive.h). The scene purifies party `slot`
 * (forced: the heart gauge is opened first, preReliveMain's mode 1) with
 * c3_relive_purify and writes the Pokemon's nickname and first move's name
 * for the messages. Returns 1 when purified, 0 when the slot holds no
 * Shadow Pokemon, negative on failure. Unbound, purification fails. */
typedef struct C3FieldStoryPurified {
    C3ReliveResult result;
    char nickname[24], move_name[24];
} C3FieldStoryPurified;
typedef int (*C3FieldStoryPurify)(void *context, unsigned slot, int forced, C3FieldStoryPurified *out);
void c3_field_story_set_purify_service(C3FieldStory *story, C3FieldStoryPurify purify, void *context);
/* menuPokemonOpen(7) in preReliveMain: while view.relive_pick is set the
 * scene lets the player choose a party member whose Shadow heart is open
 * (c3_relive_pickable) and reports its slot, or -1 to cancel. */
int c3_field_story_relive_pick(C3FieldStory *story, int slot, char *error);
void c3_field_story_set_party_service(C3FieldStory *story,C3FieldStoryRestore restore,void *context);
/* D8 mode1: clear poison/toxic only, returning cured members or -1. */
void c3_field_story_set_poison_service(C3FieldStory *story,C3FieldStoryRestore clear,void *context);
/* Music (GSsnd wrappers). Natives 68/69 call fn_80165A20/fn_801659FC with
 * (sound, fade, volume), loading the 0x406/0x407 song slots; 6A is
 * soundStop(sound, fade); 6D is fn_80165718(volume, fade), the music master
 * volume. Unbound, these natives only log, as before. */
/* JINGLE (fn_80165668(sound, fade, volume)) pauses the music and plays a
 * one-shot song, returning 1 when it started; STATUS is fn_801666BC (2
 * while the sound plays); JINGLE_END resumes the music. */
enum { C3_STORY_AUDIO_PLAY_406=1, C3_STORY_AUDIO_PLAY_407, C3_STORY_AUDIO_STOP, C3_STORY_AUDIO_MASTER,
       C3_STORY_AUDIO_JINGLE, C3_STORY_AUDIO_STATUS, C3_STORY_AUDIO_JINGLE_END };
typedef int (*C3FieldStoryAudio)(void *context,unsigned op,uint32_t a,uint32_t b,uint32_t c);
void c3_field_story_set_audio_service(C3FieldStory *story,C3FieldStoryAudio audio,void *context);
/* Doors of this floor for script native 60 (floorEventCtrlDoor). rows and
 * models are borrowed; models[i] may be NULL (the native then returns -1
 * after its flag and collision changes, as for a missing model). A door
 * model's clips are the source animation numbers. The scene reads each
 * door's current clip and time to sample its model. Map object rows
 * (C3_AREA_MAP_OBJECT_ROW) loop clip 0 from binding, as floorOpenObject
 * starts them; natives 73-75 play, stop and wait on any row's model by its
 * resource (fn_801CB834, fn_801CB7C4, scriptWaitSyncMotion). rate scales
 * the pack's clock: 1 is the original rate 0.5, 2 its rate 1.0. */
typedef struct C3StoryDoor { C3Title *model; uint32_t clip; float seconds; int playing, loop; float rate; } C3StoryDoor;
/* Native 53 mode 0 (fn_801D0DB0): the floor's healing machine is the map
 * object whose resource battleIntroModels gives for the area group (0 when
 * none). Its pack keeps source parts 9-14, the ball slots, as its last six
 * bones. c3_field_heal_ball names the Poke Ball pack of a catch ball id
 * (battleIntroObjectIds; NULL above 12): <prefix>_ball_<group>.c3t. */
#define C3_HEAL_BALL_SLOTS 6u
uint32_t c3_field_heal_machine(uint32_t group);
int c3_field_heal_ball(const char *prefix,uint32_t catch_ball,char *out,size_t capacity);
/* The ball models the scene loaded for party slots 0..count-1, and the catch
 * ball id each one shows. Borrowed; the story places, scales (1.5) and
 * shows them during the heal and keeps them hidden otherwise. */
int c3_field_story_bind_heal_balls(C3FieldStory *story,C3Title *const *models,const uint8_t *balls,
                                   uint32_t count,char *error);
int c3_field_story_bind_doors(C3FieldStory *story,const C3AreaDoor *rows,uint32_t count,
                              C3Title *const *models,char *error);
const C3StoryDoor *c3_field_story_door(const C3FieldStory *story,uint32_t position);
/* Floors the scene can load. Without a predicate only floors 1 and 2 pass;
 * any other destination blocks with an explicit notice. */
/* Further event cameras of the floor (native 2A plays the one whose
 * resource the script names). The array must outlive the story. */
void c3_field_story_bind_cameras(C3FieldStory *story,const C3AreaCamera *cameras,uint32_t count);
void c3_field_story_set_floor_support(C3FieldStory *story,int (*supported)(uint32_t floor));
/* The shared 16-bit game RNG (fn_801ADCD8) for native 02. Unbound, that
 * native fails instead of inventing a value. */
void c3_field_story_set_random(C3FieldStory *story,uint16_t (*next16)(void *context),void *context);
/* Speaker names for Pokemon people: the scene turns an internal species
 * index into its original name (returns 0 when it cannot). */
void c3_field_story_set_species_namer(C3FieldStory *story,
    int (*name)(void *context,unsigned species,char *out,size_t capacity),void *context);
/* Names the message controls print from the game's data tables (item,
 * move, species and trainer-class names): set value->message to the name's
 * original message ID, or value->text. Return 0 when unknown. */
enum { C3_STORY_NAME_ITEM = 1, C3_STORY_NAME_MOVE, C3_STORY_NAME_SPECIES, C3_STORY_NAME_TRAINER_KIND };
typedef int (*C3StoryNamer)(void *context, unsigned kind, uint32_t id, C3StoryValue *value);
void c3_field_story_set_namer(C3FieldStory *story, C3StoryNamer namer, void *context);
/* A string variable (msgctrlSetValue with a GSchar pointer: 0D/0E/41
 * EvStrBuf, 23 TrainerName,
 * 33, 36 MenuPokemon, 37/51 MenuMsg, 4D/57 String) the next
 * messages print. Returns 0 for a control that is not a value control. */
int c3_field_story_set_message_string(C3FieldStory *story, uint8_t control, const char *text);
/* 1 when the field message source answers this value control. */
int c3_field_story_message_control(uint8_t control);
/* A person's model that was loaded after the story (field_scene streams
 * people in after the room): binds it and applies the visibility and the
 * last motion the scripts asked for meanwhile. area_index is the person's
 * row in the area. */
int c3_field_story_attach_actor(C3FieldStory *story,uint32_t area_index,C3Title *model,char *error);
/* The partner's field model (<prefix>_partner.c3t; person 0,101). Shown when
 * the save's partner byte is set (native 23 changes it); while the player
 * has control she follows Wes as hero_move.c's follower does. */
int c3_field_story_attach_partner(C3FieldStory *story, C3Title *model, char *error);
/* Internal species index of a field Pokemon model resource, or 0. */
unsigned c3_field_story_model_species(uint32_t resource);
/* Diagnostics: one "task:wait:script@pc:native" entry per waiting script. */
void c3_field_story_describe_waits(const C3FieldStory *story,char *out,size_t capacity);
/* Prepare runs the original audio/init/pre functions sequentially. Once the
 * arrival movie is done, enter starts the original main/movement tasks. */
int c3_field_story_prepare(C3FieldStory *story, char *error);
int c3_field_story_enter(C3FieldStory *story, char *error);
/* Native 6F reads held directions and signed controller axes (dead zone 2),
 * independently of the edge presses consumed by dialogue/choice windows. */
void c3_field_story_set_move_input(C3FieldStory *story,uint16_t held,int stick_x,int stick_y);
/* heroMoveAddAutoEvent (native 22) is hero-move work that survives a floor
 * link: a scene reads the pending script before closing a floor's story and
 * hands it to the next floor's story; it starts once the hero has control. */
int c3_field_story_pending_auto_event(const C3FieldStory *story,uint32_t out[5]);
void c3_field_story_set_auto_event(C3FieldStory *story,const uint32_t in[5]);
int c3_field_story_tick(C3FieldStory *story, float seconds, uint16_t pressed,
                        char *error);
const C3FieldStoryView *c3_field_story_view(const C3FieldStory *story);
C3StoryActor *c3_field_story_actor(C3FieldStory *story, uint32_t source_index);
/* A person by original (group, ID), as natives address them: (0,100) is
 * Wes, (0,101) the partner once her model is attached. */
C3StoryActor *c3_field_story_person(C3FieldStory *story, uint32_t group, uint32_t index);
/* A-button searches original NPC radii/facing, then original CCD interaction
 * surfaces. Door contacts use the separately decoded original link table. */
int c3_field_story_interact(C3FieldStory *story, char *error);
int c3_field_story_transition(C3FieldStory *story, uint32_t surface_id,
                              char *error);
/* Kind-2 event of a touched surface (c3_area_touch_events). Returns 1 when
 * its original script was started, 0 when there is nothing to run and -1 on
 * failure. */
int c3_field_story_touch(C3FieldStory *story, uint32_t surface_id, char *error);
/* Resolve the first-area human cylinders after map collision, with a ground
 * check, so moving into a visible NPC does not pass through it. */
void c3_field_story_collide(C3FieldStory *story, const float before[3]);
int c3_field_story_sample(C3FieldStory *story, char *error);
/* Queue the original mail read callback before resuming the field. The caller
 * may cancel it only before the next tick if the save-side transaction fails. */
int c3_field_story_menu_event(C3FieldStory *story,uint32_t script,char *error);
void c3_field_story_cancel_menu_event(C3FieldStory *story);

#endif
