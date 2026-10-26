#ifndef C3_BATTLE_POKEMON_H
#define C3_BATTLE_POKEMON_H
#include "colosseum3ds/battle_rng.h"
#include <stddef.h>
#include <stdint.h>

#define C3_BATTLE_POKEMON_BYTES 312u
#define C3_BATTLE_POKEMON_RETRY_LIMIT 65536u

/* Borrowed, immutable original big-endian table rows. No native struct overlay.
 * message returns a bounded original GSchar byte string, including its final
 * zero word. All rows and message storage stay alive for the whole call.
 * Callback failure is fatal and rolls back output and RNG. */
typedef struct C3BattlePokemonData {
    const uint8_t *roster; /* 80 bytes, for roster_id */
    const uint8_t *species; /* 284 bytes, for roster species */
    uint32_t species_count;
    const uint8_t *moves; /* move_count contiguous 56-byte rows */
    uint32_t move_count;
    const uint8_t *growth; /* 101 big-endian u32 thresholds, species growth */
    const uint8_t *natures; /* 25 contiguous 40-byte rows */
    const uint8_t *nature_rates; /* rate_count two-byte numerator/denominator */
    uint32_t rate_count;
    int (*message)(void *user, uint32_t id, const uint8_t **bytes, size_t *size);
    void *user;
} C3BattlePokemonData;

typedef struct C3BattlePokemonRequest {
    uint16_t roster_id;
    uint32_t opponent_trainer_id; /* post-heroCreate trainer RNG result */
    uint8_t attest[4]; /* bytes pointed to by gamedataGetStatus(0,1) */
} C3BattlePokemonRequest;

/* Temporary pre-catch PokemonBios constructor at original 801F9F78. Caller
 * supplies all 312 initial backing bytes; untouched bytes remain identical.
 * This does not set the parent's roster selector C9, catch metadata, or create
 * FightPokemon. Shadow/cache rows are explicitly unsupported at this stage.
 * Returns 1 on success, 0 on invalid/missing context, overlap, busy context,
 * unsupported Shadow, exhausted personality retries, or RNG overflow. Failure
 * changes neither backing nor RNG. Serialize access to borrowed storage. */
int c3_battle_pokemon_create_pre_catch(uint8_t backing[C3_BATTLE_POKEMON_BYTES],
                                     const C3BattlePokemonRequest *request,
                                     const C3BattlePokemonData *data,
                                     C3BattleRng *rng);
#endif
