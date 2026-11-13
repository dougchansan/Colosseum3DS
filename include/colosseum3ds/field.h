#ifndef COLOSSEUM3DS_FIELD_H
#define COLOSSEUM3DS_FIELD_H

#include "colosseum3ds/title.h"
#include <stddef.h>
#include <stdint.h>

#define C3_FIELD_MAX_BYTES (512u * 1024u)
#define C3_FIELD_MAX_SPAWNS 64u
#define C3_FIELD_MAX_TRIANGLES 4096u
/* Map plus arrival packs; Wes takes the last of the stage's 48 model slots
 * (C3_STAGE_MAX_MODELS). Scenes with NPC/TV models still check their
 * complete model count. */
#define C3_FIELD_MAX_MODEL_PACKS 47u

typedef struct {
    float vertices[9], normal[3];
    uint16_t flags, source_id, object;
} C3FieldTriangle;

typedef struct {
    float position[3], yaw;
    uint32_t source_index;
} C3FieldSpawn;

typedef struct {
    C3FieldSpawn *spawns;
    C3FieldTriangle *walk, *walls;
    uint32_t floor, map_resource, spawn_count, walk_count, wall_count, object_count;
    float scale, radius, walk_distance, run_distance;
    float walk_frames, run_frames, idle_frames;
    uint32_t camera_mode;
    C3TitleCamera camera;
    float camera_height, camera_distance, camera_yaw, camera_target_height;
    uint32_t actor_first_vertex, actor_vertex_count, actor_bone;
    uint32_t idle_clip, walk_clip, run_clip;
    uint32_t map_pack_count, arrival_pack_count;
    uint8_t disabled_objects[32]; /* Original CCD object enable bit, one per ordinal. */
} C3FieldMap;

typedef struct {
    float position[3], yaw, animation_seconds, speed;
    uint32_t motion; /* 0 idle, 1 walk, 2 run */
    uint32_t collision_count;
    /* The step c3_field_move proposed before wall correction. people.c
     * queries touched event surfaces (GScolsys2ThruGetEventID) with this
     * point and only then applies GScolsys2HumanCollision, so a touch
     * surface behind a wall is still reached. Equals position when idle. */
    float proposed[3];
} C3FieldPlayer;

/* Initialize the map to zero. Parse/load own their decoded tables and replace
 * an existing map only after all input has been validated. 1 success, 0 failure. */
int c3_field_parse(C3FieldMap *map, const void *data, size_t bytes,
                    char *error, size_t capacity);
int c3_field_load(C3FieldMap *map, const char *path, char *error, size_t capacity);
void c3_field_close(C3FieldMap *map);
int c3_field_spawn(const C3FieldMap *map, uint32_t source_index, C3FieldPlayer *player);
int c3_field_set_object_enabled(C3FieldMap *map, uint32_t object, int enabled);
int c3_field_object_enabled(const C3FieldMap *map, uint32_t object);
/* Resolve a proposed foot position after an actor pushes the player. Walls
 * retain priority; unavailable ground restores the previous foot position. */
int c3_field_resolve_position(const C3FieldMap *map,C3FieldPlayer *player,
                              const float before[3]);

/* Source controller shaping and walk/run speed curves. Stick values use the
 * native input adapter's -127..127 range, positive Y up. Button bits retain
 * GameCube PAD positions. Collision uses the supplied original CCD surfaces.
 * Active-time stalls are counted by the game clock; movement is bounded to
 * 0.25s per update to avoid a long I/O stall teleporting a held controller. */
int c3_field_move(const C3FieldMap *map, C3FieldPlayer *player,
                   int stick_x, int stick_y, uint16_t buttons,
                   float camera_yaw, float seconds);
int c3_field_ground(const C3FieldMap *map, const float point[3], float *height);
void c3_field_camera(const C3FieldMap *map, const C3FieldPlayer *player,
                      C3TitleCamera *camera);

#endif
