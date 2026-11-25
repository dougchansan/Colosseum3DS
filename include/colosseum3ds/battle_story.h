#ifndef C3_BATTLE_STORY_H
#define C3_BATTLE_STORY_H

#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/pda.h"

/* Feed a finished general battle back to the save and the waiting native 98:
 * party HP/PP/experience/levels, prize money on a win, and the result.
 *  - win: result 2;
 *  - loss without the encounter's wipeout flag: result 3, script resumes;
 *  - loss with it: money halved, party restored (restore_party, when given),
 *    then c3_field_story_battle_whiteout.
 * Snagged Shadow Pokemon go to the party (else the PC box) with their snag
 * flag; thrown balls leave the bag. Nothing is written when the fight kind
 * has no backSaveDataFlag.
 * Returns 1 on success; on failure the save may hold the party writeback
 * but the story is left waiting. */
int c3_battle_story_apply(C3FieldStory *story, C3GameState *state,
                          const C3GeneralBattle *battle, const C3BattlePack *pack,
                          int (*restore_party)(void *context),
                          C3GeneralRewards *rewards, char *error);

/* Before the first turn: the Snag Machine is ready, the bag's ball
 * pocket (pocket 2) supplies the throwable balls and side_name[0] takes the
 * hero's name (the prize line, fight message 0x7546). */
int c3_battle_story_prepare(const C3GameState *state, C3GeneralBattle *battle);

/* The in-battle bag (fight_menu.c SubItem): the Items and Berries pockets
 * as the battle begins, each slot with its count, its item row's battle
 * use (battleUseFunc) and effect row (itemEffectParam) and its name from
 * the P*DA assets. Without assets the slots are listed but none can be
 * used. Used items leave the save's bag in c3_battle_story_apply, with the
 * thrown balls. */
int c3_battle_story_prepare_bag(const C3GameState *state, const C3PdaAssets *assets, C3GeneralBattle *battle);

/* Unattended battle (both sides use the opponent policy), then apply. For
 * headless runs and the warp tour; never used for the player's own battles. */
int c3_battle_story_autoplay(C3FieldStory *story, C3GameState *state,
                             const C3BattlePack *pack, uint32_t encounter, uint32_t seed,
                             int (*restore_party)(void *context),
                             C3GeneralRewards *rewards, unsigned *turns, char *error);
#endif
