#ifndef C3_BATTLE_SEQUENCE_H
#define C3_BATTLE_SEQUENCE_H

#include "colosseum3ds/battle_resume.h"

/* Native owner handles: zero is an observed NULL, never a placeholder success.
 * No value is a narrowed host pointer or a serialized original address. */
typedef struct { uint32_t group, member; } C3BattleSequenceResource;
typedef struct {
    int32_t (*resource_state)(void *, uint32_t group);
    int (*request_group)(void *, uint32_t group);
    uint32_t (*find_resource)(void *, uint32_t group, uint32_t member);
    uint32_t (*allocate)(void *);
    int (*set_fields)(void *, uint32_t sequence, uint16_t index,
                      uint16_t group, uint8_t flags);
    /* load returns1 success,0 observed source load failure,-1 native error.
     * It must not yield: it represents sequenceLoad after the resource wait.
     * The real model/particle/animation-callback ownership must be supplied by
     * the scene adapter. No production substitute is registered by this API. */
    int (*load)(void *, uint32_t sequence, uint32_t resource);
    void (*release)(void *, uint32_t sequence);
    int (*hide)(void *, uint32_t sequence);
    void (*report_negative_state)(void *, uint32_t group);
} C3BattleSequenceServices;
typedef enum {
    C3_SEQUENCE_IDLE = 0, C3_SEQUENCE_POLL_RESOURCE,
    C3_SEQUENCE_DONE, C3_SEQUENCE_FAILED, C3_SEQUENCE_CANCELLED
} C3BattleSequencePhase;
typedef struct {
    C3BattleSequencePhase phase;
    C3BattleSequenceServices services;
    void *user;
    uint32_t group, member, result, owned_sequence;
    uint16_t index;
    uint8_t trainer, in_call;
} C3BattleSequenceFrame;

/* Source-derived fn_801DE418 / fightTrainerCreateSequence loader control.
 * Resource table, count and index come from validated original metadata.
 * The caller resolves trainer selector9; this API does not guess an actor ID.
 *
 * begin follows the original two state queries and fallback-to-row1 rules.
 * step polls at most one source loop iteration. Negative states log/yield;
 * they DO NOT trigger repeated requests. There is no guessed timeout.
 * Failed resource lookup/allocation/load returns source NULL, whereas missing
 * services/native errors return ROUTINE_ERROR and cannot masquerade as NULL.
 * On success, ownership of the actual sequence handle transfers to the caller.
 * Cancellation before allocation leaves shared archive loading with its owner;
 * it must not unload resources needed by other actors. All methods are serial
 * on the scene thread. Services and any owned payloads must outlive the frame.
 *
 * This ports loader CONTROL, not GSres/sequenceLoad/model allocation. It is not
 * registered with native98 until those real scene services and the rest of
 * battle entry exist. No RNG, saved state, result or story flag is changed.
 */
C3BattleRoutineReply c3_battle_sequence_begin(C3BattleSequenceFrame *frame,
    uint16_t index,const C3BattleSequenceResource *table,size_t count,
    const C3BattleSequenceServices *services,void *user,int trainer);
C3BattleRoutineReply c3_battle_sequence_step(C3BattleSequenceFrame *frame);
C3BattleRoutineReply c3_battle_sequence_cancel(C3BattleSequenceFrame *frame);

#endif
