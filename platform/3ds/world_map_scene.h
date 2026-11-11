#ifndef COLOSSEUM3DS_WORLD_MAP_SCENE_H
#define COLOSSEUM3DS_WORLD_MAP_SCENE_H

#include "colosseum3ds/game_state.h"
#include <stddef.h>
#include <stdio.h>

/* Destination choice for the original world-map floor 0x38E. The enabled
 * entries, the cancel rule and floorLink(floor, 0) follow src/world_map.c.
 * Presentation is a bottom-screen touch list; the original 3D map, journey
 * and location images are not drawn yet. */
int c3_world_map_scene_available(void);
/* The game state must be on floor 0x38E. Returns 2 after the chosen floor
 * and entrance 0 were written to the state, 0 for the main menu, 1 on
 * application exit and -1 on failure. Nothing is saved to the card. */
int c3_world_map_scene_run(C3GameState *state, FILE *trace, char *error, size_t capacity);

#endif
