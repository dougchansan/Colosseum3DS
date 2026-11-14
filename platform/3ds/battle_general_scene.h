#ifndef C3_BATTLE_GENERAL_SCENE_H
#define C3_BATTLE_GENERAL_SCENE_H
#include "colosseum3ds/battle_general.h"
#include <stdio.h>

#define C3_BATTLE_PACK_PATH "sdmc:/3ds/Colosseum3DS/boot/battles.c3x"

/* Modal story battle on the bottom screen: plays back each turn's events,
 * takes the hero's commands (move, target, switch) and runs until the
 * battle ends. Draws no top-screen frame and allocates no GPU memory.
 * Returns 0 when the battle finished, 1 when the application must exit,
 * -1 on an engine error (the battle is left unfinished). */
int c3_battle_general_scene_run(const C3BattlePack *pack, C3GeneralBattle *battle, FILE *trace);
#endif
