#ifndef COLOSSEUM3DS_POKEMON_H
#define COLOSSEUM3DS_POKEMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This is a native record for this slice, not an on-disc PokemonBios layout. */
typedef enum C3PokemonStat {
    C3_STAT_HP = 0,
    C3_STAT_ATTACK,
    C3_STAT_DEFENSE,
    C3_STAT_SPEED,
    C3_STAT_SP_ATTACK,
    C3_STAT_SP_DEFENSE,
    C3_STAT_COUNT
} C3PokemonStat;

typedef struct C3NatureRate {
    uint8_t numerator;
    uint8_t denominator;
} C3NatureRate;

typedef struct C3Pokemon {
    /* Colosseum's internal species ID: Shedinja is 0x12F, not national 292. */
    uint16_t species_id;
    uint8_t level;
    uint16_t base_stats[C3_STAT_COUNT];
    uint8_t iv[C3_STAT_COUNT];
    uint16_t ev[C3_STAT_COUNT];
    /* Explicit rates from the caller: no invented species/nature database. */
    C3NatureRate nature[C3_STAT_COUNT];
    uint16_t stats[C3_STAT_COUNT];
    uint16_t current_hp;
} C3Pokemon;

typedef enum C3PokemonResult {
    C3_POKEMON_OK = 0,
    C3_POKEMON_INVALID_ARGUMENT,
    C3_POKEMON_BUSY,
    C3_POKEMON_RESULT_RANGE,
    C3_POKEMON_ACCESSOR_ERROR
} C3PokemonResult;

/* Set every stat's nature multiplier to 1/1; leave other fields unchanged. */
void c3_pokemon_neutral_nature(C3Pokemon *pokemon);

/*
 * Execute upstream pokemonSetLevelBasisStatus against a native record.
 * target_level must be 1..100, base stats 1..255, IVs 0..31, and EVs
 * 0..255 with total <= 510. Nature ratios must be 0.9, 1.0, or 1.1;
 * HP must be neutral. A non-neutral nature raises and lowers one stat.
 *
 * stats[HP] is the old max HP. Use old max HP = current_hp = 0 when first
 * creating a record. Existing fainted Pokemon remain fainted. Normal level
 * increases retain the number of HP lost. A result outside [0, max HP]
 * (possible for a damaged Pokemon after a large level decrease) is rejected.
 *
 * Success updates level, stats and current_hp. Failure leaves the record
 * unchanged. Calls are guarded; a simultaneous/reentrant call returns BUSY.
 * The caller must separately synchronize access to the same record.
 */
C3PokemonResult c3_pokemon_recalculate(C3Pokemon *pokemon,
                                      uint8_t target_level);

#ifdef __cplusplus
}
#endif
#endif
