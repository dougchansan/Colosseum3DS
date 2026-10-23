#ifndef C3_GAME_ITEMS_H
#define C3_GAME_ITEMS_H
#include "colosseum3ds/game_state.h"

#define C3_ITEM_COUNT 548u
#define C3_ITEM_CATALOG_SIZE (128u + C3_ITEM_COUNT * 128u)
#define C3_BAG_MAX_SLOTS 64u

/* Initialize to {0}, then load/decode and free. Catalog owns private table
 * bytes, never code pointers. Hashes detect damage, not signed provenance. */
typedef struct C3ItemCatalog { void *impl; } C3ItemCatalog;
typedef struct C3ItemInfo {
    uint16_t id;
    uint8_t pocket, field_supported;
    char name[64];
} C3ItemInfo;
typedef struct C3BagEntry { uint16_t id, quantity; } C3BagEntry;
typedef struct C3BagView {
    uint8_t pocket, capacity, occupied;
    C3BagEntry slots[C3_BAG_MAX_SLOTS];
} C3BagView;
typedef struct C3BagPokemon {
    char name[32];
    uint16_t species, hp, maximum_hp, condition;
    uint8_t level, egg, invalid, hyper;
} C3BagPokemon;
typedef struct C3ItemUseResult {
    uint16_t item, quantity_before, quantity_after;
    uint16_t hp_before, hp_after, condition_before, condition_after;
    uint8_t party_slot, revived, cured;
} C3ItemUseResult;
typedef enum C3ItemUseStatus {
    C3_ITEM_ERROR = -1, C3_ITEM_NO_EFFECT = 0,
    C3_ITEM_APPLIED = 1, C3_ITEM_UNSUPPORTED = 2
} C3ItemUseStatus;

void c3_item_catalog_free(C3ItemCatalog *catalog);
int c3_item_catalog_decode(C3ItemCatalog *catalog, const void *data, size_t length,
                           char error[C3_GAME_ERROR_SIZE]);
int c3_item_catalog_load(C3ItemCatalog *catalog, const char *path,
                         char error[C3_GAME_ERROR_SIZE]);
int c3_item_info(const C3ItemCatalog *catalog, uint16_t id, C3ItemInfo *out);
const char *c3_bag_pocket_name(unsigned pocket);
int c3_game_bag_view(const C3GameState *state, const C3ItemCatalog *catalog,
                     unsigned pocket, C3BagView *out, char error[C3_GAME_ERROR_SIZE]);
int c3_game_bag_party(const C3GameState *state, C3BagPokemon out[C3_GAME_PARTY_COUNT],
                      char error[C3_GAME_ERROR_SIZE]);

/* Field only. Supported ordinary restorative items are original IDs 13..29.
 * pocket/slot/expected_item identify the user's selection, never an arbitrary
 * item injection. dry_run==1 reports the effect without mutation. dry_run==0
 * commits selected Pokemon + bag decrement/compaction together, only on APPLIED.
 * NO_EFFECT, UNSUPPORTED and ERROR preserve body, RNG, cards and result output.
 * Cancellation belongs to the caller and makes no mutation call. The existing
 * explicit Save writes the changed in-memory body; this API never writes disk.
 * Error/result buffers must not alias the owned state/catalog. Serialize calls. */
C3ItemUseStatus c3_game_item_use(C3GameState *state, const C3ItemCatalog *catalog,
                                 unsigned pocket, unsigned slot, uint16_t expected_item,
                                 unsigned party_slot, int dry_run, C3ItemUseResult *out,
                                 char error[C3_GAME_ERROR_SIZE]);
#endif
