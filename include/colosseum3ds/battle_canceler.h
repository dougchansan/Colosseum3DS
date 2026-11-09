#ifndef C3_BATTLE_CANCELER_H
#define C3_BATTLE_CANCELER_H

#include "colosseum3ds/battle_state.h"

/* Attack-canceller pieces lowered from source by guarded lowering: every
 * branch condition is evaluated as in the original (in source order, with
 * its random draws only where the original draws), and the bodies taken by
 * status-free battlers are lowered. A branch whose condition holds but whose
 * body is not lowered yet returns C3_CANCEL_UNPORTED with a reason naming
 * it; nothing is skipped or approximated. */
#define C3_CANCEL_UNPORTED (-1)
#define C3_CANCEL_REASON_SIZE 96u

/* fightOutPokemonWriteJoutaiDataId: clear condition `id` from the slot of the
 * record its category names (Pokemon 0x7C/0xC8, FightPokemon 0xCD,
 * FightOutPokemon 0xD8; others: nothing). Ids 0/7/8 also update the
 * battler's model sequence (0xEE) in the original: with a sequence present
 * that is not lowered and returns C3_CANCEL_UNPORTED. Returns 1 otherwise. */
int c3_battle_condition_clear(C3BattleState *state, uint32_t fight_out, uint16_t id, char *reason);

/* pokemonIsDarkPokemon (Shadow): darkpokemonDataId != 0 and dp >= 0. */
int c3_battle_pokemon_is_dark(const C3BattleState *state, uint32_t pokemon);

/* fightFloorGetFightOutPokemonPtrToFightTrainerPtr: the trainer whose
 * FightPokemon slots hold the battler's original FightPokemon (0xD5). */
uint32_t c3_battle_fight_out_trainer(const C3BattleState *state, uint32_t fight_out);

/* fn_8022EDEC (Shadow Pokemon reverse/hyper check), mode 0 or 2: 0 for a
 * non-Shadow battler; C3_CANCEL_UNPORTED where the Shadow path is reached. */
int c3_battle_shadow_check(C3BattleState *state, uint32_t fight_out, unsigned mode, char *reason);

/* fn_8022A6C8: the Shadow Hyper Mode canceller for `fight_out`. 0 when it
 * does not act; C3_CANCEL_UNPORTED where its Hyper Mode body is reached. */
int c3_battle_hyper_canceller(C3BattleState *state, uint32_t fight_out, char *reason);

/* fn_8022F2F8: the status canceller for the floor's attacker (target 0x11),
 * resuming at *step (lbl_8047B644, reset to 0 at move start by
 * fn_802128D0). Returns its result (0: the move proceeds) or
 * C3_CANCEL_UNPORTED; *step is left where the original leaves it. */
int c3_battle_status_canceller(C3BattleState *state, uint32_t *step, char *reason);
#endif
