#ifndef C3_BATTLE_AI_H
#define C3_BATTLE_AI_H
/* Computer trainer AI (fight_trainer_ai*.c, fight_range_80211A00.c), lowered
 * for the paths Willie's battle reaches; every other path fails the script
 * with a named reason. See docs/original-battle-turns.md, "AI move choice
 * for Willie". */
#include "colosseum3ds/battle_handlers.h"

/* fn_80234A0C(trainer): the AI's view of the opposing battlers in battle.
 * Clears the five flags of the trainer's valid enemy entries
 * (fightTrainerInitEnemyPokemonFightOutStatus), then for each opposing
 * battler with an entry ID: HP percentage, level, base defenses (data 5 + 7),
 * base stat total x level, and the summed damage estimate (fn_8023C370) of
 * each of its type-ID-1 moves against each of the trainer's battlers in
 * battle. The lowest HP, level and defenses and the highest damage and
 * stats (ties included) set the entry's flags 0x52..0x56. Damage estimates
 * use h->ai_moves (lbl_8035C430). sc is the battle's script instance, used
 * as the estimate's working state and restored. Returns 0 (sc failed by
 * name) when a path is refused. */
int c3_battle_ai_prepare(C3BattleHandlers *h, C3BattleScript *sc, uint32_t trainer);

/* fn_8023C370(trainer, attacker, move, target, 0): 0 unless the trainer's AI
 * data enables damage estimates (byte 0x2D); otherwise the move's AI damage
 * routine (lbl_8035C430 +8 through move data +0x1C). Implemented routines:
 * fightTrainerAiWazaDamage000 / 031 / 076 / 197 (the normal estimate, doubled
 * for move 0x39 against condition 0x21 with AI byte 0x24) and 121 (Return:
 * power from friendship). */
int c3_battle_ai_damage_estimate(C3BattleHandlers *h, C3BattleScript *sc, uint32_t trainer, uint32_t attacker,
                                 uint16_t move, uint32_t target, uint32_t *damage);

/* fn_802342CC(trainer, fight type): the computer trainer's turn. The
 * preparation; its battlers' action buffers reset; three rolls, one draw
 * each (AI byte 0x26 switch, 0x27 item only with floor 0x20, 0x25 action);
 * a selected item roll sets no action when the trainer's item pocket is
 * empty (any item is refused), the switch and action selections are refused
 * by name; then its battlers that must choose
 * a move (no Struggle, no Encore), sorted by speed into order[] (ties draw
 * once), each in turn through the move choice fn_8023A308: with AI bytes
 * 0x30 / 0x31 set, one draw picks an opposing battler and one a usable move
 * (the move's target category may override the target), set as the
 * battler's attack action (kind 0x13) and FightWaza. The scored choices are
 * refused. Returns 1 when every battler that needed a move has one. */
int c3_battle_ai_select(C3BattleHandlers *h, C3BattleScript *sc, uint32_t trainer, uint32_t order[8]);

/* fightFloorSortFightOutPokemonPtrArySub(0, list, 8, 0): list by speed
 * (fightOutPokemonGetNowNimbleness), each tie drawing once; empty entries go
 * last. Shared by the AI entry and the start of battle. */
int c3_battle_sort_by_speed(C3BattleHandlers *h, C3BattleScript *sc, uint32_t list[8]);
#endif
