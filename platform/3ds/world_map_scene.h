#ifndef COLOSSEUM3DS_WORLD_MAP_SCENE_H
#define COLOSSEUM3DS_WORLD_MAP_SCENE_H

#include "colosseum3ds/game_state.h"
#include <stddef.h>
#include <stdio.h>

/* Destination choice for the original world-map floor 0x38E. The enabled
 * entries, the cancel rule and floorLink(floor, 0) follow src/world_map.c.
 * The bottom screen shows the original map and its cursor; a travel choice
 * plays the original journey (menu 0xC3) on the top screen while the
 * destination's map packs load. The 3D Orre map and location images are
 * not drawn. */
int c3_world_map_scene_available(void);
/* The game state must be on floor 0x38E. Returns 2 after the chosen floor
 * and entrance 0 were written to the state, 0 for the main menu, 1 on
 * application exit and -1 on failure. Nothing is saved to the card. */
int c3_world_map_scene_run(C3GameState *state, FILE *trace, char *error, size_t capacity);
/* 1 once after a journey ended faded out (the next field load keeps the
 * top screen black); clears the flag. */
int c3_world_map_scene_take_journey_shown(void);

#endif
