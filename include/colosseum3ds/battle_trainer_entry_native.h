#ifndef C3_BATTLE_TRAINER_ENTRY_NATIVE_H
#define C3_BATTLE_TRAINER_ENTRY_NATIVE_H
#include "colosseum3ds/battle_trainer_entry.h"
#include "colosseum3ds/battle_runtime_trainer.h"
#include "colosseum3ds/battle_party_sort.h"

/* Native record bindings, not original guest addresses. Each record remains at
 * a stable address throughout entry; reordering changes bytes, not identities.
 * runtime and sort_state are caller-owned battle scratch, NEVER the live save.
 * sort_state's scratch/selected/candidate backing is supplied by the owner;
 * it is not inferred from expected results or silently initialized to zero. */
typedef struct {
    uint32_t trainer, hero, party[6], fighter[6];
    uint8_t *runtime;
    size_t runtime_size;
    C3BattlePartySortState *sort_state;
    const C3BattlePartySortData *sort_data;
} C3BattleEntryNativeSlot;
typedef struct {
    C3BattleTrainerRequest request;
    const C3BattleTrainerData *data;
    uint8_t *pokemon_scratch; /* entire independently initialized 312 bytes */
} C3BattleEntryNativeHero;
typedef struct {
    C3BattleEntryNativeSlot *slots;
    size_t slot_count; /* zero through two actual source side slots */
    const C3BattleEntryNativeHero *heroes;
    size_t hero_count; /* bounded request/data contexts, no duplicate id/device */
    const uint8_t *preset[4]; /* optional 0xB18 Hero source buffers */
    C3BattleRng *rng;
    uint16_t *entry_counter;
    void *owner; /* actual scene/resource/accessor owner for remaining services */
} C3BattleEntryNativeContext;

/* Validate handle uniqueness and known mutable spans before any write. All
 * nested table rows/callbacks must meet the existing constructor contracts.
 * APIs below return failure without replacing missing context with preview
 * state. RNG/counter belong to the same live SESSION stream, not a reseed.
 * Session isolation and cleanup after later errors remain the owner's job. */
int c3_battle_entry_native_validate(const C3BattleEntryNativeContext *);

/* Populate only the implemented record/constructor members of services. The
 * caller must supply every remaining source accessor and actual sequence/grid
 * service. user MUST be C3BattleEntryNativeContext; scene callbacks may use its
 * owner field. This does not register an action or enable native98. */
void c3_battle_entry_native_bind(C3BattleTrainerEntryServices *services);
#endif
