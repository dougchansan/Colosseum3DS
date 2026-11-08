#ifndef COLOSSEUM3DS_FIELD_SCENE_H
#define COLOSSEUM3DS_FIELD_SCENE_H

#include "colosseum3ds/game_state.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int c3_field_scene_supports(uint32_t floor);
int c3_field_scene_available(void);
/* Return 0 to the main menu, 1 on application exit, -1 on failure. */
int c3_field_scene_run(C3GameState *state, int new_game, const char *save_path,
                       FILE *trace, char *error, size_t capacity);

#endif
