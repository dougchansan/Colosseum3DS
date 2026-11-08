#ifndef C3_BATTLE_INIT_H
#define C3_BATTLE_INIT_H

#include "colosseum3ds/battle.h"
#include "colosseum3ds/battle_rng.h"
#include "colosseum3ds/pokemon.h"

#define C3_BATTLE_INIT_PACK_SIZE 768u
#define C3_BATTLE_INIT_TABLE_BYTES 640u
#define C3_BATTLE_INIT_RETRY_LIMIT 65536u

typedef struct C3BattleInitTables {
    /* Owned canonical bytes. Decode before use; the initializer revalidates. */
    uint8_t bytes[C3_BATTLE_INIT_PACK_SIZE];
} C3BattleInitTables;

typedef struct C3BattleInitialMove {
    uint16_t id;
    uint8_t pp, pp_ups, maximum_pp;
} C3BattleInitialMove;

typedef struct C3BattleInitialOpponent {
    C3Pokemon pokemon; /* Native stat order from pokemon.h, not PokemonBios. */
    uint32_t initial_personality, personality, personality_attempts;
    uint32_t nickname_message;
    uint16_t roster_id, held_item, friendship;
    uint8_t sex, nature, ability_flag, ability_id;
    C3BattleInitialMove moves[4];
} C3BattleInitialOpponent;

typedef enum C3BattleInitStage {
    C3_BATTLE_INIT_EMPTY = 0,
    /* Parameters only: NOT a serialized Pokemon, FightPokemon, or runnable turn. */
    C3_BATTLE_INIT_PARTY_PARAMETERS = 1
} C3BattleInitStage;

typedef struct C3BattleInitialState {
    C3BattleInitStage stage;
    C3BattleRequest request; /* All six original player records stay identical. */
    uint32_t opponent_trainer_id;
    C3BattleRngSnapshot rng_before, rng_after;
    C3BattleInitialOpponent opponents[2];
    uint8_t opponent_count;
} C3BattleInitialState;

/* Return 1 on success. Failure preserves an existing output. A payload hash
 * detects damage, not malicious provenance; run the private-input preparer.
 * Error buffers hold C3_BATTLE_ERROR_SIZE bytes and must not alias inputs. */
int c3_battle_init_decode(C3BattleInitTables *out, const void *pack, size_t size,
                          char *error);
int c3_battle_init_load(C3BattleInitTables *out, const char *path,
                        char *error);

/* Source-derived, headless encounter-10 parameter construction. The caller
 * supplies the ORIGINAL opponent trainer ID and RNG at the start of roster
 * generation (AFTER heroCreate). This is not a battle-entry RNG guess.
 *
 * pokemonCreate's discarded personality/IV/ability draws still occur before
 * the roster overrides and bounded RndFit rejection loop. The API consumes
 * no RNG on failure, including overflow or retry exhaustion. It copies the
 * pending request/player bytes and never updates them, a save, or GSVM.
 *
 * Only the reviewed ordinary, item-free, zero-PP-Up, explicit-move roster
 * is admitted.
 * Full PokemonBios capture metadata, FightPokemon initialization, AI, turns,
 * presentation, rewards and result writeback remain later integration gates.
 * No success here authorizes native98 to resume. Output and RNG must not
 * overlap each other or the borrowed inputs. Serialize shared-object calls. */
int c3_battle_init_prepare(C3BattleInitialState *out,
                           const C3BattleRequest *request,
                           const C3BattleInitTables *tables,
                           uint32_t opponent_trainer_id, C3BattleRng *rng,
                           char *error);

#endif
