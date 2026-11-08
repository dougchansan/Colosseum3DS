#ifndef C3_BATTLE_TRAINER_H
#define C3_BATTLE_TRAINER_H
#include "colosseum3ds/battle_pokemon.h"
#define C3_BATTLE_TRAINER_BYTES 2840u /* original heroBiosCopy: 0xB18 */

typedef struct C3BattleTrainerData {
    const uint8_t *trainer; /* original 52-byte trainer row for trainer_id */
    const uint8_t *saved_player; /* optional original 0xB18 bytes, player kind */
    /* Resolve original roster rows including the terminating species-zero row.
     * Nonzero species requires the complete qualified PokemonData context. */
    int (*roster)(void *user,uint16_t id,C3BattlePokemonData *out);
    int (*message)(void *user,uint32_t id,const uint8_t **bytes,size_t *size);
    /* Original itemGetStatus(NULL,id,1/2) projection after ID indirection.
     * Return 0 for missing context; name=0 represents a source-invalid item. */
    int (*item)(void *user,uint16_t id,uint32_t *name,uint8_t *kind);
    uint32_t item_count; /* original lbl_80478BD8, not a synthetic maximum */
    uint32_t special_trainer_id; /* original value at 0x80478960 */
    const uint8_t *special_name; /* bytes pointed to by 0x8047A670 */
    size_t special_name_size;
    void *user;
} C3BattleTrainerData;

typedef struct C3BattleTrainerRequest {
    uint16_t trainer_id;
    int32_t input_device;
    uint8_t attest[4]; /* independent original gamedataGetStatus(0,1) bytes */
} C3BattleTrainerRequest;

/* Source heroInit field reset on an explicit 0xB18 backing image. NULL is
 * rejected (implicit global saved-hero access is outside this boundary).
 * Failure/busy nested Pokemon initialization preserves every input byte. */
int c3_battle_trainer_init_hero(uint8_t hero[C3_BATTLE_TRAINER_BYTES]);

/* Source parent 801F9CBC through final hero party, items, and badges. All output
 * is BE backing storage; preserve caller-provided unknown bytes. scratch is the
 * parent's local Pokemon312 INITIAL backing (original entrySP-0x148), reused
 * across roster calls and returned with its final pre-catch contents. Supply
 * the pre-heroCreate shared RNG. Failure changes neither hero, scratch nor RNG.
 * A source early-return/no-op is success. Borrowed inputs must stay immutable;
 * callbacks must be serialized, side-effect-free and may not alias outputs.
 * At most seven roster calls cover six slots plus the source full-party
 * rejection (or terminating species-zero row).
 * Shadow/cache and GSchar control codes are rejected. This does not construct
 * FightPokemon, sort the party, resume native98 or write story results. */
int c3_battle_trainer_create(uint8_t hero[C3_BATTLE_TRAINER_BYTES],
    uint8_t scratch[C3_BATTLE_POKEMON_BYTES],const C3BattleTrainerRequest *request,
    const C3BattleTrainerData *data,C3BattleRng *rng);
#endif
