#ifndef C3_BATTLE_PRESENTATION_H
#define C3_BATTLE_PRESENTATION_H
#include "colosseum3ds/battle_preview.h"
/* This timeline owns copied committed events and no combat/RNG pointer.
 * Durations are diagnostic defaults; source clip timing belongs to the renderer. */
typedef void (*C3BattlePresentationNotify)(const C3BattlePreviewEvent *, int finished, void *);
typedef struct {
    C3BattlePreviewEvent events[C3_PREVIEW_EVENT_LIMIT];
    uint32_t count, cursor, elapsed_ms, duration_ms, last_sequence;
    uint16_t hp[4];
    uint8_t pp[4][4];
    int8_t stages[4][7];
    uint8_t visible[4], confusion[4], taunt[4], reflect[2];
    C3BattlePreviewOutcome outcome;
    uint8_t paused, started, waiting, failed;
    uint32_t wait_ms;
} C3BattlePresentation;
void c3_battle_presentation_init(C3BattlePresentation *, const uint16_t hp[4]);
/* Rejects replacing an unfinished batch and replayed/out-of-order sequences. */
int c3_battle_presentation_submit(C3BattlePresentation *, const C3BattlePreviewEvent *, uint32_t count);
void c3_battle_presentation_pause(C3BattlePresentation *, int paused);
/* Renderer can require a clip/effect completion, then release that wait.
 * An unresolved wait fails after 5000 active milliseconds. */
void c3_battle_presentation_wait(C3BattlePresentation *, int waiting);
int c3_battle_presentation_step(C3BattlePresentation *, uint32_t milliseconds,
                                C3BattlePresentationNotify, void *);
#endif
