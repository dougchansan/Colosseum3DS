#ifndef COLOSSEUM3DS_DEV_FLOOR_H
#define COLOSSEUM3DS_DEV_FLOOR_H

/* DEVELOPER FLOOR LOADER: inspection of original floors that the native story
 * route does not reach yet. This module only names, validates and steps
 * through prepared map/collision/Wes packs. It has no game-state, save,
 * story-flag, GSVM or battle-result interface, by construction: nothing here
 * can make a developer visit count as story progress. */

#include "colosseum3ds/field.h"
#include <stddef.h>
#include <stdint.h>

#define C3_DEV_FLOOR_LABEL "DEVELOPER FLOOR LOADER"
#define C3_DEV_FLOOR_NOTICE "Not story progress. Nothing is saved."
#define C3_DEV_FLOOR_DIRECTORY "dev-floor"

typedef struct {
    const char *name;    /* Display name. */
    const char *prefix;  /* Pack prefix written by tools/prepare_floor.py. */
    uint32_t floor;      /* Original REL125 lifecycle floor identity. */
    uint32_t map_resource;
} C3DevFloorProfile;

typedef enum { C3_DEV_FLOOR_FIELD, C3_DEV_FLOOR_MAP, C3_DEV_FLOOR_WES } C3DevFloorFile;

unsigned c3_dev_floor_profile_count(void);
const C3DevFloorProfile *c3_dev_floor_profile(unsigned index); /* NULL past end */

/* Compose "<root>/<prefix>.c3f", "<root>/<prefix>_map<index>.c3t" or
 * "<root>/<prefix>_wes.c3t". 1 on success, 0 on truncation/invalid input. */
int c3_dev_floor_path(char *out, size_t capacity, const char *root,
                      const C3DevFloorProfile *profile, C3DevFloorFile file, unsigned index);

/* A developer pack set must be the selected original floor and map, with a
 * separate Wes pack, no arrival packs, and map packs that leave Wes a stage
 * slot. 1 accepted, 0 rejected with a message. */
int c3_dev_floor_validate(const C3FieldMap *map, const C3DevFloorProfile *profile,
                          char *error, size_t capacity);

/* Step through the original entrance table in source order, wrapping. */
uint32_t c3_dev_floor_next_spawn(const C3FieldMap *map, uint32_t current, int step);

/* Place a freshly sampled Wes pose at the player's position and yaw: the same
 * rigid Y rotation the field story applies to a parentless field actor. */
void c3_dev_floor_place_actor(C3Title *model, const C3FieldPlayer *player);

#endif
