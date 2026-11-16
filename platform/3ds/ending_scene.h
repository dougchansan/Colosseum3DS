#ifndef C3_ENDING_SCENE_H
#define C3_ENDING_SCENE_H
#include "colosseum3ds/game_state.h"
#include <stdio.h>
int c3_ending_scene_available(void);
int c3_ending_scene_run(C3GameState *state,const char *save_path,FILE *trace,char *error,size_t capacity);
#endif
