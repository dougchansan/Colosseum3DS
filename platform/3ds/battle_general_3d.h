#ifndef C3_BATTLE_GENERAL_3D_H
#define C3_BATTLE_GENERAL_3D_H
#include "colosseum3ds/battle_general.h"
#include <stdio.h>
#include <stddef.h>

#define C3_BATTLE3D_ROOT "sdmc:/3ds/Colosseum3DS/boot/battle3d"

/* A general story battle on its original stage: the top screen shows the
 * battle floor, both sides' Pokemon and trainers (battle_general_stage.c),
 * the bottom screen the commands and messages (battle_general_scene.c).
 *
 * ready: whether the prepared 3D assets cover this battle's floor, its
 * opening Pokemon and both trainers. Cheap (reads the small index only);
 * call it before releasing the field's models. why receives the reason
 * when it returns 0.
 * run: caller has drained the GPU and released the field's models. Returns
 * 0 when the battle finished, 1 when the application must exit, -1 when
 * the 3D stage failed before the battle finished (the battle state is
 * intact and the caller may continue it on the 2D scene). */
int c3_battle_general_3d_ready(const C3GeneralBattle *battle, FILE *trace, char *why, size_t capacity);
int c3_battle_general_3d_run(const C3BattlePack *pack, C3GeneralBattle *battle, FILE *trace);
#endif
