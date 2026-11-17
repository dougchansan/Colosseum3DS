#ifndef C3_MOVE_FORGET_SCENE_H
#define C3_MOVE_FORGET_SCENE_H
#include "colosseum3ds/battle_general.h"
#include <stdio.h>

/* The forget-a-move prompt on the bottom screen (evolutionWazaLearn's
 * questions with the original messages from the PDA pack; colosseum3ds/
 * move_forget.h). Bind it with c3_general_set_forget_prompt(
 * c3_move_forget_scene_prompt, NULL) so the battle writeback and
 * purification's _expRecover ask the player. Blocks until answered. */
int c3_move_forget_scene_prompt(void *context, const C3BattlePack *pack,
                                const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move);
/* Bind the prompt (and its trace, may be NULL) to the engine. */
void c3_move_forget_scene_bind(FILE *trace);
/* Probes: answer from a script instead of the pad. answers: per decision,
 * 1 yes / 0 no on the questions, 0..3 or 4 (cancel) on the move list;
 * each page stays on screen hold_ms (messages advance by themselves). */
void c3_move_forget_scene_set_script(const int *answers, unsigned count, unsigned hold_ms);
#endif
