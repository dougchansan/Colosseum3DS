#ifndef C3_BATTLE_PREVIEW_SCENE_H
#define C3_BATTLE_PREVIEW_SCENE_H
#include "colosseum3ds/battle.h"
#include "colosseum3ds/game_state.h"
#include <stdio.h>
/* Caller first acquires the stage renderer's idle barrier. This modal
 * CPU-console preview restores its framebuffer/console ownership on return.
 * -1 reports a failed preview without changing state;
 * 0 returns to the still-pending field request; 1 requests application exit.
 * No save, native98 continuation or authoritative battle result is exposed. */
int c3_battle_preview_scene_run(const C3BattleRequest *request,FILE *trace);
/* Construct an isolated encounter-10 preview from current party bytes;
 * this does not access or resume the field VM's pending request. */
int c3_battle_preview_from_game_state(const C3GameState *state,FILE *trace);
/* Explicit compile-time diagnostic entry; never selected by a save flag. */
int c3_battle_preview_probe_run(void);
#endif
