#ifndef C3_BATTLE_RUNTIME_TRAINER_H
#define C3_BATTLE_RUNTIME_TRAINER_H
#include "colosseum3ds/battle_trainer.h"
#define C3_BATTLE_RUNTIME_TRAINER_BYTES 10468u /* 0x28E4 */

/* Original single-record fightTrainerInit reset. NULL is the source no-op.
 * Source field initialization preserves every untouched backing byte. */
int c3_battle_runtime_trainer_init(uint8_t backing[C3_BATTLE_RUNTIME_TRAINER_BYTES]);

/* Original 801FA6D8 deterministic field constructor. source_hero is all 0xB18
 * original hero bytes. sequence_identity is the actual caller-supplied opaque
 * 32-bit sequence result, including zero only for an actual source result.
 * It is never derived from a host pointer. This does not create that sequence:
 * resource loading, sequence-pool allocation and model/render callbacks remain
 * owner integration requirements. All internal pointer fields are cleared;
 * no nonzero self/member identity is stored by this initializer.
 *
 * NULL backing or source_hero succeeds as the source no-op, ignoring scalars.
 * Otherwise source/output overlap is rejected (adapter admission restriction).
 * Success stages the full 0x28E4 field reset and hero copy, stores low16 trainer
 * ID, low8 controller and the sequence identity, preserving original padding.
 * Any failure, including busy shared initializers, changes neither buffer.
 * This pinned closure consumes no RNG: its source status3F kaisuu row has
 * min=max=-1. An unexpected draw path is rejected. This does not initialize
 * active fighters, sort the party, resume native98 or commit story results. */
int c3_battle_runtime_trainer_create(
    uint8_t backing[C3_BATTLE_RUNTIME_TRAINER_BYTES],
    const uint8_t source_hero[C3_BATTLE_TRAINER_BYTES],uint16_t trainer_id,
    uint32_t controller_id,uint32_t sequence_identity);

/* Original fightOutPokemonInit (fight_out_pokemon_exact_80206A04.c) on one
 * 0x6E0-byte FightOutPokemon record: the same compiled source the trainer
 * constructor runs for its slots. NULL is the source no-op. Failure (busy
 * shared state or an unexpected accessor path) leaves the record unchanged. */
#define C3_BATTLE_FIGHT_OUT_POKEMON_RECORD 0x6E0u
int c3_battle_fight_out_pokemon_init(uint8_t record[C3_BATTLE_FIGHT_OUT_POKEMON_RECORD]);
#endif
