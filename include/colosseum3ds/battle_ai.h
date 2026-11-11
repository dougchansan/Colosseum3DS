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
#endif
