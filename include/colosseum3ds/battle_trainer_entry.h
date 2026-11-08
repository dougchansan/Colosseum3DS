#ifndef C3_BATTLE_TRAINER_ENTRY_H
#define C3_BATTLE_TRAINER_ENTRY_H

#include "colosseum3ds/battle_sequence.h"

/* Original fightActionFlowKaisiNyuujouTrainer stack backing. heroBiosCopy
 * writes only the first 0xB18 bytes. The remaining four bytes are retained. */
#define C3_TRAINER_ENTRY_HERO_BYTES 0xB1Cu
#define C3_TRAINER_ENTRY_MAX_TRAINERS 2u /* the original FightSide slots */

typedef struct { uint32_t trainer, sequence; } C3BattleEntryOwnership;

/* Source accessors return success separately from the original value, so an
 * unavailable native service cannot masquerade as a source NULL/false. All
 * references are stable opaque 32-bit identities, never narrowed host pointers.
 * The original status selector and index are retained on the status interfaces.
 * Calls are serialized on the sole scene thread. No service may yield except
 * the explicit sequence child; other yielding closures need their own frames.
 *
 * create_hero/runtime/sort/create_fighter are adapter seams for the existing
 * qualified battle_trainer/runtime_trainer/party_sort/fight_pokemon services.
 * Their implementation MUST supply original input context, backing and shared
 * RNG/counter ownership. There are no default successful implementations. */
typedef struct {
    int (*action_buff)(void *, const C3BattleAction *, uint32_t *);
    int (*floor_status)(void *, unsigned selector, uint32_t *);
    int (*encounter)(void *, uint16_t id, uint32_t *);
    int (*target_side)(void *, uint16_t buff, uint16_t side_id, uint32_t *);
    int (*side_valid)(void *, uint32_t side, uint8_t *);
    int (*side_status)(void *, uint32_t side, unsigned selector, unsigned index, uint32_t *);
    /* field 0 = original trainer-data ID, field 1 = input device. */
    int (*encounter_status)(void *, uint32_t encounter, unsigned field, uint8_t index, uint16_t *);
    int (*trainer_data_valid)(void *, uint16_t id, uint16_t device, uint8_t *);
    int (*preset_mode)(void *, uint8_t *);
    int (*copy_preset)(void *, uint8_t index, uint8_t hero[C3_TRAINER_ENTRY_HERO_BYTES]);
    int (*create_hero)(void *, uint16_t id, uint16_t device, uint8_t hero[C3_TRAINER_ENTRY_HERO_BYTES]);
    int (*sequence_index)(void *, uint16_t trainer_id, uint16_t *);
    int (*create_runtime)(void *, uint32_t trainer, const uint8_t hero[C3_TRAINER_ENTRY_HERO_BYTES],
                          uint16_t id, uint16_t device, uint32_t sequence);
    int (*trainer_status)(void *, uint32_t trainer, unsigned selector, unsigned index, uint32_t *);
    int (*trainer_valid)(void *, uint32_t trainer, uint8_t *);
    int (*sort_party)(void *, uint32_t trainer, uint16_t maximum, uint16_t active);
    int (*hero_pokemon)(void *, uint32_t hero, unsigned slot, uint32_t *);
    /* fight_out=1 selects pokemonCheckFightOut; 0 selects pokemonCheckValid. */
    int (*pokemon_eligible)(void *, uint32_t pokemon, int fight_out, uint8_t *);
    int (*already_entered)(void *, uint32_t trainer, uint32_t pokemon, uint32_t *);
    int (*next_entry_counter)(void *, uint32_t *);
    int (*create_fighter)(void *, uint32_t destination, uint32_t pokemon, uint32_t entry_id);
    int (*is_gc_hero)(void *, uint32_t trainer, uint8_t *);
    int (*friend_filter)(void *, uint32_t fighter, uint8_t filter);
    int (*side_fighting_count)(void *, uint32_t side, uint8_t *);
    int (*valid_trainer)(void *, uint32_t side, unsigned slot, uint32_t *);
    int (*grid_parameters)(void *, uint16_t side, uint8_t count, unsigned slot, uint8_t *, uint8_t *);
    int (*grid_add)(void *, uint32_t sequence, uint8_t x, uint8_t y);
    int (*grid_update)(void *);
    int (*set_visible)(void *, uint32_t sequence, int visible);
    /* Called once on abort for successfully constructed trainers. This hands
     * their cleanup to the session owner; it does NOT promise immediate GPU
     * destruction or restore mutated runtime/RNG. The session must stop entry,
     * drain GPU work, release its handles and discard partial battle state.
     * It must not reset queues from inside this callback. */
    void (*abort_owned)(void *, const C3BattleEntryOwnership *, size_t count);
} C3BattleTrainerEntryServices;

typedef enum {
    C3_TRAINER_ENTRY_IDLE = 0, C3_TRAINER_ENTRY_READY,
    C3_TRAINER_ENTRY_RUNNING, C3_TRAINER_ENTRY_WAIT_SEQUENCE,
    C3_TRAINER_ENTRY_DONE, C3_TRAINER_ENTRY_FAILED, C3_TRAINER_ENTRY_ABORTED
} C3BattleTrainerEntryPhase;

typedef struct {
    C3BattleTrainerEntryPhase phase;
    C3BattleTrainerEntryServices services;
    C3BattleSequenceServices sequence_services;
    const C3BattleSequenceResource *resources;
    size_t resource_count;
    void *user;
    C3BattleSequenceFrame child;
    uint8_t hero[C3_TRAINER_ENTRY_HERO_BYTES];
    C3BattleEntryOwnership ownership[C3_TRAINER_ENTRY_MAX_TRAINERS];
    size_t ownership_count;
    uint32_t encounter, side, trainer, hero_reference, owned_sequence;
    uint16_t buff, side_id, trainer_count, maximum, active, grid_side;
    uint16_t trainer_id, device, sequence_index, offset;
    unsigned trainer_index;
    uint32_t source_result;
    const char *failure; /* static service/phase name; no private data */
    uint8_t in_call, abort_called;
} C3BattleTrainerEntryFrame;

/* Zero-initialize fresh storage. setup rejects a live/failed invocation; call
 * abort first after a failure. initial_hero must be the caller-supplied initial
 * 0xB1C backing, not expected output; it is copied once and retained across all
 * trainers and waits. No uninitialized C stack bytes are consumed.
 * Tables/user/runtime objects must outlive start through return/abort. Service
 * structs are copied. Do not overlap frame with inputs or mutate it externally.
 * The fixed two-trainer admission matches the source side layout; a corrupted
 * floor requesting more is a native error, not a fabricated source return.
 * No source RNG seed, sequence identity or hero contents are guessed here. */
int c3_battle_trainer_entry_setup(C3BattleTrainerEntryFrame *frame,
    const C3BattleTrainerEntryServices *services,
    const C3BattleSequenceServices *sequence_services,
    const C3BattleSequenceResource *resources, size_t resource_count, void *user,
    const uint8_t initial_hero[C3_TRAINER_ENTRY_HERO_BYTES]);

/* Directly usable as a C3BattleRoutineBinding. start reads the original source
 * locals once; resume continues AFTER the sequence child, never before hero
 * creation. The action need not be reread on resume. Source return0/1 remains
 * distinct from YIELD/ERROR. All writes are to caller-owned battle scratch;
 * no saved-party/result/native98 authority is implied. */
C3BattleRoutineReply c3_battle_trainer_entry_start(void *, C3BattleAction *);
C3BattleRoutineReply c3_battle_trainer_entry_resume(void *, C3BattleAction *);
void c3_battle_trainer_entry_abort(void *, C3BattleAction *);
#endif
