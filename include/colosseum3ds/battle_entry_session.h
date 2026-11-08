#ifndef C3_BATTLE_ENTRY_SESSION_H
#define C3_BATTLE_ENTRY_SESSION_H

#include "colosseum3ds/battle_entry_records.h"

/* Owner of one isolated two-side trainer-entry session (story encounters with
 * one trainer per side, such as encounter 10). It composes PR9's resumable
 * action frame, trainer-entry action, record services and native constructor
 * bindings with caller-supplied original tables and scene services.
 *
 * Everything written lives in this session object: runtime trainers, Hero and
 * FightPokemon records, party-sort scratch, RNG and the entry counter. It never
 * touches the live save/game state, native98, story flags or results.
 *
 * Native choices that are NOT original observations are explicit inputs:
 *  - seed: the session RNG seed. The original RNG state at battle entry is not
 *    modelled, so callers must label which seed they used.
 *  - Initial stack backing (the action's 0xB1C Hero image, the parent Pokemon
 *    scratch and party-sort scratch) is zero-filled by this owner. The
 *    original reads whatever its stack held; that is not reproducible here.
 *  - The entry counter starts at zero, as fightFloorInit leaves it.
 */
typedef struct {
    C3BattleEntryTableView tables[C3_ENTRY_TABLE_COUNT];
    C3BattlePartySortData sort_data;      /* original rows; host_side set by owner */
    C3BattleTrainerData trainer_data[2];  /* [0] player side, [1] opponent side */
    C3BattleTrainerRequest requests[2];   /* trainer_id/device from the encounter */
    uint16_t side_data_id[2];             /* floor side IDs for buff 4 and 5 */
    uint16_t encounter_id;
    /* Original action data rows (ending with the kind-zero sentinel) and the
     * kind table indexed by kind. entry_kind must flow to 0x8020D1FC. Battle
     * start creates (entry_kind, buff 5) and then (entry_kind, buff 4). */
    const C3BattleActionData *actions;
    size_t action_count;
    const C3BattleActionKind *kinds;
    size_t kind_count;
    uint16_t entry_kind;
    uint8_t preset_mode;                  /* fn_8006B57C during this battle */
    uint32_t seed;
    /* Scene ownership: real model sequences, grid placement and visibility. */
    C3BattleSequenceServices sequence;
    const C3BattleSequenceResource *resources;
    size_t resource_count;
    int (*grid_add)(void *scene, uint32_t sequence, uint8_t x, uint8_t y);
    int (*grid_update)(void *scene);
    int (*set_visible)(void *scene, uint32_t sequence, int visible);
    void (*abort_owned)(void *scene, const C3BattleEntryOwnership *, size_t count);
    void *scene;
} C3BattleEntrySessionConfig;

typedef enum {
    C3_ENTRY_SESSION_IDLE = 0, C3_ENTRY_SESSION_RUNNING,
    C3_ENTRY_SESSION_DONE, C3_ENTRY_SESSION_FAILED, C3_ENTRY_SESSION_ABORTED
} C3BattleEntrySessionState;

typedef struct C3BattleEntrySession {
    C3BattleEntrySessionState state;
    C3BattleEntrySessionConfig config;
    C3BattleEntryRecords records;
    C3BattleEntryNativeContext native;
    C3BattleEntryNativeSlot slots[2];
    C3BattleEntryNativeHero heroes[2];
    uint8_t runtime[2][C3_BATTLE_RUNTIME_TRAINER_BYTES];
    C3BattlePartySortState sort[2];
    uint8_t pokemon_scratch[C3_BATTLE_POKEMON_BYTES];
    uint8_t initial_hero[C3_TRAINER_ENTRY_HERO_BYTES];
    C3BattleRng rng;
    uint16_t entry_counter;
    C3BattleActionRuntime action_runtime;
    C3BattleRoutineBinding binding;
    C3BattleResumeRuntime resume_runtime;
    C3BattleTrainerEntryServices services;
    C3BattleSequenceServices sequence;   /* unwraps the native context to scene */
    C3BattleEntryOwnership owned[C3_TRAINER_ENTRY_MAX_TRAINERS];
    size_t owned_count;       /* trainers from a side that already returned */
    C3BattleTrainerEntryFrame entry;
    C3BattleActionFrame frame;
    C3BattleActionQueue queue;
    C3BattleAction action;
    unsigned side;            /* index buff-4: 0 player (buff 4), 1 opponent (buff 5) */
    unsigned flows;           /* actions created so far (opponent side first) */
    uint32_t source_result[2];
    uint32_t steps, waits;
    const char *failure;      /* static phase/service name, no private data */
} C3BattleEntrySession;

/* Stable native identities used by this owner (never host pointers). */
enum {
    C3_ENTRY_SESSION_TRAINER = 0x100u, C3_ENTRY_SESSION_HERO = 0x101u,
    C3_ENTRY_SESSION_PARTY = 0x110u, C3_ENTRY_SESSION_FIGHTER = 0x120u,
    C3_ENTRY_SESSION_SIDE = 0x300u, C3_ENTRY_SESSION_ENCOUNTER = 0x500u,
    C3_ENTRY_SESSION_SIDE_STRIDE = 0x100u
};

/* Validate inputs and prepare both actions. Returns 1 on success. The session
 * must be zero-initialized; the config is copied and its borrowed tables,
 * callbacks and scene must outlive the session. */
int c3_battle_entry_session_begin(C3BattleEntrySession *session,
                                  const C3BattleEntrySessionConfig *config);

/* Advance at most one action-frame step. Returns 1 while running (including
 * sequence waits), 0 when both sides finished, -1 on failure. */
int c3_battle_entry_session_step(C3BattleEntrySession *session);

/* Cancel an unfinished session once. Successful constructions are handed to
 * the scene's abort_owned; mutated battle scratch must then be discarded. */
void c3_battle_entry_session_abort(C3BattleEntrySession *session);

/* Read-only views of the constructed battle scratch for diagnostics. */
const uint8_t *c3_battle_entry_session_runtime(const C3BattleEntrySession *session,
                                               unsigned side);
const uint8_t *c3_battle_entry_session_fighter(const C3BattleEntrySession *session,
                                               unsigned side, unsigned slot);
#endif
