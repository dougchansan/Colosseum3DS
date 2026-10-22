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
/* Resolve the first-area human cylinders after map collision, with a ground
 * check, so moving into a visible NPC does not pass through it. */
void c3_field_story_collide(C3FieldStory *story, const float before[3]);
int c3_field_story_sample(C3FieldStory *story, char *error);

#endif
