#ifndef C3_BATTLE_ENTRY_RECORDS_H
#define C3_BATTLE_ENTRY_RECORDS_H

#include "colosseum3ds/battle_trainer_entry_native.h"

/* Borrowed original big-endian rows; no C struct overlay or PPC dereference.
 * limit is the source COUNT for encounter/trainer/species/side. For type/kind
 * it is the original INCLUSIVE maximum index (those BIOS getters use >).
 * byte_size must include every admitted row, including that boundary row. */
enum C3BattleEntryTable {
    C3_ENTRY_ENCOUNTER, C3_ENTRY_TRAINER, C3_ENTRY_SPECIES,
    C3_ENTRY_SIDE, C3_ENTRY_TYPE, C3_ENTRY_KIND, C3_ENTRY_TABLE_COUNT
};
typedef struct {
    const uint8_t *bytes;
    size_t byte_size;
    uint32_t limit;
} C3BattleEntryTableView;

typedef struct {
    uint32_t reference;
    uint16_t data_id;
    uint32_t trainers[2];
} C3BattleEntrySideView;

typedef struct C3BattleEntryRecords {
    C3BattleEntryNativeContext *native;
    C3BattleEntryTableView tables[C3_ENTRY_TABLE_COUNT];
    const C3BattleActionData *action_data;
    size_t action_count;
    C3BattleEntrySideView sides[2];
    uint32_t encounter_reference;
    uint16_t encounter_id; /* source floor+0x12, read when entry starts */
    uint16_t battle_result; /* source fn_801EF634; 1 inhibits validity */
    uint8_t preset_mode; /* observed fn_8006B57C, not a guessed mode */
    void *scene_user; /* actual owner for remaining resource/grid services */
} C3BattleEntryRecords;

/* native->owner MUST point to this object. The owner supplies the current
 * source floor/side/global views and admitted tables, not preview defaults.
 * Runtime records are the mutable battle scratch from native->slots. Reads
 * remain live across yields; this API does not bootstrap RNG or a session.
 *
 * Unknown nonzero handles are errors, not source NULL. A genuinely invalid
 * source table ID retains the original zero/NULL behavior. No service writes
 * party, queue, RNG, saves, visibility or graphics. Failure preserves outputs.
 * Output storage must be disjoint from the complete borrowed context/records.
 * Serialize all calls and keep borrowed bytes stable for each call. */
int c3_battle_entry_records_validate(const C3BattleEntryRecords *records);

/* Fills 17 source-data/predicate services alongside native_bind. Preserves
 * constructors plus all owner hooks. Caller still supplies friend_filter,
 * grid_add/update, set_visible, abort_owned and real sequence services.
 * There is intentionally no successful fallback for an absent owner. */
void c3_battle_entry_records_bind(C3BattleTrainerEntryServices *services);

#endif
