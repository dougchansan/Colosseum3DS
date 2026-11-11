#ifndef C3_FIELD_STORY_H
#define C3_FIELD_STORY_H

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
typedef struct C3FieldStoryView {
    char message[C3_STORY_MESSAGE];
    char blocked[C3_STORY_ERROR];
    uint32_t message_id, next_floor, next_spawn, battle_id;
    int prepared, control, dialogue, choice, choice_no, tv_enabled;
    int camera_active;
    uint32_t name_entry; /* native 16 mode + 1 while the scene must collect a name */
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
/* Native 16 mode 1 names the partner. When the scene enables it the script
 * waits with view.name_entry set until the scene reports 1..10 printable
 * ASCII characters; otherwise the saved default name is kept. */
void c3_field_story_enable_name_entry(C3FieldStory *story,int enabled);
int c3_field_story_name_result(C3FieldStory *story,const char *name,char *error);
/* Item gifts (script native 77). receive adds `count` of `item` to the bag
 * as heroItemAddItemDataId does and writes the item's display name: 0 when
 * everything was added, a positive amount that did not fit, negative when
 * the item is invalid. Unbound, native 77 fails. */
typedef int (*C3FieldStoryReceive)(void *context,uint32_t item,uint32_t count,char *name,size_t capacity);
void c3_field_story_set_item_service(C3FieldStory *story,C3FieldStoryReceive receive,void *context);
/* Full party restore (script native D8 mode 0, pokemonAllKaihuku). Returns
 * the number of members restored or a negative value. Unbound, D8 fails. */
typedef int (*C3FieldStoryRestore)(void *context);
void c3_field_story_set_party_service(C3FieldStory *story,C3FieldStoryRestore restore,void *context);
/* Floors the scene can load. Without a predicate only floors 1 and 2 pass;
 * any other destination blocks with an explicit notice. */
void c3_field_story_set_floor_support(C3FieldStory *story,int (*supported)(uint32_t floor));
/* The shared 16-bit game RNG (fn_801ADCD8) for native 02. Unbound, that
 * native fails instead of inventing a value. */
void c3_field_story_set_random(C3FieldStory *story,uint16_t (*next16)(void *context),void *context);
/* Prepare runs the original audio/init/pre functions sequentially. Once the
 * arrival movie is done, enter starts the original main/movement tasks. */
int c3_field_story_prepare(C3FieldStory *story, char *error);
int c3_field_story_enter(C3FieldStory *story, char *error);
int c3_field_story_tick(C3FieldStory *story, float seconds, uint16_t pressed,
                        char *error);
const C3FieldStoryView *c3_field_story_view(const C3FieldStory *story);
C3StoryActor *c3_field_story_actor(C3FieldStory *story, uint32_t source_index);
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
