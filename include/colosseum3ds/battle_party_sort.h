#ifndef C3_BATTLE_PARTY_SORT_H
#define C3_BATTLE_PARTY_SORT_H
#include "colosseum3ds/battle_fight_pokemon.h"

#define C3_BATTLE_PARTY_SLOTS 6u
#define C3_BATTLE_PARTY_HERO_BYTES 2840u

enum C3BattlePartyTable {
    C3_PARTY_TRAINER, C3_PARTY_AI, C3_PARTY_ROSTER,
    C3_PARTY_SPECIES, C3_PARTY_TYPE, C3_PARTY_TABLE_COUNT
};

/* Raw immutable BE rows: trainer52, AI40, roster80, species284, type44.
 * lookup must return at least that row's exact extent and remain immutable
 * through the call. Failure for an in-range ID is fatal. No ranking, validity,
 * category, or RNG decisions are delegated to callbacks.
 * target_side projects original fightTargetGetPtr(2,target,host_side) to an
 * opaque 32-bit side handle, including zero for the original NULL result.
 * It is needed only by the alternate type-score ordering branch. */
typedef struct C3BattlePartySortData {
    uint32_t counts[C3_PARTY_TABLE_COUNT];
    int (*lookup)(void *user, uint32_t table, uint32_t id,
                  const uint8_t **row, size_t *size);
    int (*target_side)(void *user, uint32_t target, uint16_t host_side,
                       uint32_t *side);
    void *user;
    uint16_t battle_result; /* original no-arg fn_801EF634 global getter */
    uint16_t host_side; /* original floor status14, mutable encounter trainer byte */
} C3BattlePartySortData;

typedef struct C3BattlePartySortRequest {
    uint32_t trainer_reference;
    uint32_t hero_reference;
    uint32_t fight_references[2];
    uint32_t party_references[6];
    uint16_t trainer_id; /* original BE16 runtime trainer head */
    uint16_t selection_count; /* wrapper clamps to6; driver/enumerator admit0..6 */
    uint16_t selection_mode; /* driver phase quota unless source AceBoss flag */
    uint8_t filter; /* enumerator category0..3 */
} C3BattlePartySortRequest;

/* All backing is original BE bytes. scratch is the independent INITIAL six
 * Pokemon stack records (wrapper entrySP-0x770), not zero-filled or output-fed.
 * selected/candidates use the explicit party slot references, never host
 * addresses. Handles denote physical source slots; reordering copies bytes
 * between those slots and does not relabel their addresses. The first two
 * FightPokemon records are restored by successful selection/enumeration.
 * All fields are staged and committed together, including RNG/counter/scratch.
 * selected/candidates are observed internal scratch outputs; wrapper/driver
 * preserve fields they do not write. candidate_count is the last enumeration
 * result, or its prior value if none occurred. Counter wraps with original low16 semantics. */
typedef struct C3BattlePartySortState {
    uint8_t hero[C3_BATTLE_PARTY_HERO_BYTES];
    uint8_t fight_pokemon[2][C3_BATTLE_FIGHT_POKEMON_BYTES];
    uint8_t scratch[6][C3_BATTLE_POKEMON_BYTES];
    uint32_t selected[6];
    uint32_t candidates[6];
    uint16_t candidate_count;
    uint16_t entry_counter;
    C3BattleRng rng;
} C3BattlePartySortState;

/* 1=success/source no-op,0=missing context, invalid handle/count/filter,
 * callback failure, overlap, initializer failure, or RNG overflow. Every error
 * leaves state unchanged. Source references are opaque nonzero identities for
 * present objects; zero trainer/Hero/FP references model their NULL pointers.
 * All present identities must be distinct. Original-game oracles provide
 * independently captured guest addresses. Native callers supply stable opaque
 * identities. Borrowed data/callbacks must be serialized and side-effect-free.
 * These are source prerequisites, not full battle/native98 qualification. */
int c3_battle_party_sort(C3BattlePartySortState *state,
    const C3BattlePartySortRequest *request, const C3BattlePartySortData *data);
int c3_battle_party_select(C3BattlePartySortState *state,
    const C3BattlePartySortRequest *request, const C3BattlePartySortData *data);
int c3_battle_party_candidates(C3BattlePartySortState *state,
    const C3BattlePartySortRequest *request, const C3BattlePartySortData *data);
#endif
