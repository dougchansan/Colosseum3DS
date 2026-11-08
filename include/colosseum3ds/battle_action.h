#ifndef COLOSSEUM3DS_BATTLE_ACTION_H
#define COLOSSEUM3DS_BATTLE_ACTION_H

#include <stddef.h>
#include <stdint.h>

/* A canonical big-endian GC action, never a host pointer-bearing struct. */
enum { C3_ACTION_BYTES = 48, C3_ACTION_SLOTS = 32 };
enum {
    C3_ACTION_BUFF = 4, C3_ACTION_DATA = 8, C3_ACTION_BUFF_DATA = 12,
    C3_ACTION_BUFF_ID = 16, C3_ACTION_ACTOR = 20, C3_ACTION_MOTO = 24,
    C3_ACTION_FIFO_INDEX = 28, C3_ACTION_DISPLAY = 32
};
typedef struct { uint8_t bytes[C3_ACTION_BYTES]; } C3BattleAction;
typedef struct {
    C3BattleAction slots[C3_ACTION_SLOTS];
    uint32_t write_index, read_index;
} C3BattleActionQueue;
typedef struct { uint32_t ref; uint16_t kind; uint32_t buff; } C3BattleActionData;
typedef struct { uint16_t kind; uint32_t flow_ref, display_ref; } C3BattleActionKind;
/* The implementation is supplied explicitly. No real combat callback is registered here. */
typedef uint32_t (*C3BattleActionCallback)(void *user, C3BattleAction *action);
typedef struct {
    uint32_t ref;
    C3BattleActionCallback call;
    void *user;
} C3BattleActionBinding;
typedef struct {
    const C3BattleActionData *data;
    size_t data_count;
    const C3BattleActionKind *kinds;
    size_t kind_count;
    const C3BattleActionBinding *bindings;
    size_t binding_count;
} C3BattleActionRuntime;
typedef enum {
    C3_ACTION_OK = 0, C3_ACTION_BAD_ARGUMENT, C3_ACTION_UNKNOWN_DATA,
    C3_ACTION_UNKNOWN_KIND, C3_ACTION_UNIMPLEMENTED, C3_ACTION_BUDGET,
    C3_ACTION_AMBIGUOUS_REFERENCE
} C3BattleActionStatus;
typedef struct {
    C3BattleActionStatus status;
    uint32_t source_result;
    size_t dispatched;
} C3BattleActionResult;

uint16_t c3_battle_action_kind(const C3BattleAction *action);
void c3_battle_action_set_kind(C3BattleAction *action, uint16_t kind);
/* Offsets must be one of the aligned 32-bit fields above (or DISPLAY + 4*i). */
uint32_t c3_battle_action_word(const C3BattleAction *action, unsigned offset);
void c3_battle_action_set_word(C3BattleAction *action, unsigned offset, uint32_t value);
/* Init clears fields but intentionally retains the original bytes 2..3 padding.
 * Zero-initialize fresh action/queue storage first, matching original BSS;
 * use init on existing records when their padding must be retained. */
void c3_battle_action_init(C3BattleAction *action);
void c3_battle_action_queue_init(C3BattleActionQueue *queue);
size_t c3_battle_action_queue_count(const C3BattleActionQueue *queue);
C3BattleActionResult c3_battle_action_flow(C3BattleActionQueue *queue,
    C3BattleAction *action, const C3BattleActionRuntime *runtime);
/* action storage must not overlap queue storage. Runtime tables are borrowed,
 * immutable for the duration of a call, and must not overlap mutable records.
 * Callbacks must not corrupt queue indices or recursively call display. */
/* Bounded, sentinel-terminated original 8-byte data-table search. The final
 * element must have kind zero. refs identify records, not original addresses. */
C3BattleActionResult c3_battle_action_create_flow(C3BattleActionQueue *queue,
    C3BattleAction *action, uint32_t moto_ref, uint32_t actor_ref,
    uint16_t kind, uint32_t buff, const C3BattleActionRuntime *runtime);
/* Pops before display as the source does. A wait result is not requeued;
 * display-local mutations are discarded. Budget exhaustion retains remaining
 * entries. Unknown refs are rejected before popping, never treated as NULL. */
C3BattleActionResult c3_battle_action_display(C3BattleActionQueue *queue,
    const C3BattleActionRuntime *runtime, size_t budget);

#endif
