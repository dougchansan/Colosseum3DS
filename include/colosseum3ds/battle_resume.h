#ifndef C3_BATTLE_RESUME_H
#define C3_BATTLE_RESUME_H

#include "colosseum3ds/battle_action.h"

/* Native continuations, not serialized game memory. A source return value
 * (including 2 or 3) is NEVER interpreted as a cooperative suspension. */
typedef enum {
    C3_BATTLE_WAIT_NONE = 0, C3_BATTLE_WAIT_RESOURCE,
    C3_BATTLE_WAIT_ANIMATION, C3_BATTLE_WAIT_INPUT, C3_BATTLE_WAIT_CHILD
} C3BattleWaitKind;
typedef enum {
    C3_BATTLE_ROUTINE_RETURN = 0, C3_BATTLE_ROUTINE_YIELD,
    C3_BATTLE_ROUTINE_ERROR
} C3BattleRoutineState;
typedef struct {
    C3BattleRoutineState state;
    C3BattleWaitKind wait;
    uint32_t value;
} C3BattleRoutineReply;
typedef C3BattleRoutineReply (*C3BattleRoutineCall)(void *, C3BattleAction *);
typedef struct {
    uint32_t ref;
    C3BattleRoutineCall start, resume;
    /* Abort must release only this invocation's resources, after its owner
     * has made them safe. It must not block, reset the queue or re-enter it. */
    void (*abort)(void *, C3BattleAction *);
    void *user; /* Caller-owned TYPED frame, alive until return/abort. */
} C3BattleRoutineBinding;
typedef struct {
    const C3BattleActionRuntime *source;
    const C3BattleRoutineBinding *routines;
    size_t routine_count;
} C3BattleResumeRuntime;
typedef enum {
    C3_BATTLE_FRAME_IDLE = 0, C3_BATTLE_FRAME_READY,
    C3_BATTLE_FRAME_WAITING, C3_BATTLE_FRAME_RETURNED,
    C3_BATTLE_FRAME_FAILED, C3_BATTLE_FRAME_ABORTED
} C3BattleFrameState;
typedef enum { C3_BATTLE_FRAME_FLOW = 0, C3_BATTLE_FRAME_DISPLAY } C3BattleFrameMode;
typedef struct {
    C3BattleFrameState state;
    C3BattleWaitKind wait;
    C3BattleActionStatus status;
    uint32_t source_result;
} C3BattleFrameResult;

typedef struct {
    C3BattleFrameState state;
    C3BattleFrameMode mode;
    C3BattleWaitKind wait;
    C3BattleActionStatus status;
    C3BattleAction action; /* Persistent, private copy of the source local. */
    C3BattleActionQueue *queue;
    C3BattleAction *flow_output;
    C3BattleActionBinding synchronous;
    C3BattleRoutineBinding routine;
    uint32_t slot, source_result;
    uint8_t started, in_call, abort_called, asynchronous;
} C3BattleActionFrame;

/* Zero-initialize fresh storage. Do not reinitialize an active frame. One
 * owner on the scene thread serializes calls, queue operations and all driver
 * state. Functions do not allocate, wait in a loop, manipulate game RNG, or
 * invoke OS thread/context switching. No native98/result authority is implied.
 *
 * begin performs the ORIGINAL enqueue-before-flow / pop-before-display once.
 * start runs at the first step; each subsequent step while suspended calls
 * resume once. A resume must continue its typed frame, never replay start.
 * Nonzero callback refs need exactly ONE synchronous OR resumable binding.
 * Each resumable binding requires start/resume/abort. Unknown/ambiguous refs
 * reject before the queue changes. The runtime tables are borrowed during
 * begin; the chosen binding is copied and remains fixed for this invocation.
 *
 * The caller's action and queue must outlive a pending FLOW invocation. The
 * display action lives entirely in this frame; changes are not written back.
 * Pending flow callbacks may create source-ordered nested work, but a separate
 * producer/consumer must not race or independently drain/reinitialize the queue.
 * The flow completion retains the source's remembered-slot copy/head-drop
 * semantics; do not treat this single-frame API as a full nested scheduler.
 *
 * Driver errors are sticky, not fabricated source returns. Once begin has
 * accepted an invocation, side effects cannot in general be rolled back.
 * Abort/terminate the owning battle session on failure. abort never requeues
 * a popped action or silently resets a queue. Only reset the session's queue
 * AFTER all resource owners have safely cancelled. Preserve failed frame data
 * until diagnostics/cleanup are complete. No cancellation is a battle result.
 */
int c3_battle_frame_reset(C3BattleActionFrame *frame);
C3BattleFrameResult c3_battle_frame_begin_flow(C3BattleActionFrame *frame,
    C3BattleActionQueue *queue, C3BattleAction *action,
    const C3BattleResumeRuntime *runtime);
/* Source create-flow search/init followed by the resumable flow. The caller's
 * output is published only when the original local candidate would return.
 * The final data-table row must be the kind-zero sentinel. */
C3BattleFrameResult c3_battle_frame_create_flow(C3BattleActionFrame *frame,
    C3BattleActionQueue *queue, C3BattleAction *action, uint32_t moto_ref,
    uint32_t actor_ref, uint16_t kind, uint32_t buff,
    const C3BattleResumeRuntime *runtime);
C3BattleFrameResult c3_battle_frame_begin_display(C3BattleActionFrame *frame,
    C3BattleActionQueue *queue, const C3BattleResumeRuntime *runtime);
C3BattleFrameResult c3_battle_frame_step(C3BattleActionFrame *frame);
C3BattleFrameResult c3_battle_frame_abort(C3BattleActionFrame *frame);

/* A positive budget limits callback invocations/actions admitted per tick.
 * YIELD returns immediately: there is no polling busy loop. Zero does nothing.
 * End-of-queue reports source return3. A returned source value whose low byte
 * is not1 ends this drain; the next call starts the next source drain. */
C3BattleFrameResult c3_battle_frame_drain(C3BattleActionFrame *frame,
    C3BattleActionQueue *queue, const C3BattleResumeRuntime *runtime,
    size_t budget, size_t *completed);

#endif
