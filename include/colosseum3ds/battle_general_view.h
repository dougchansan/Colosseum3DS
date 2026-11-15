#ifndef C3_BATTLE_GENERAL_VIEW_H
#define C3_BATTLE_GENERAL_VIEW_H

#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/battle_preview.h"

/* Present a general battle's event log on the 3D battle stage.
 *
 * The general engine (battle_general.c) logs what happened in a turn; the
 * stage (platform/3ds/battle_stage.c) plays C3BattlePreviewEvent batches.
 * This translates one into the other, one presentation event per logged
 * event, so a presentation cursor maps back to the log index (first+cursor).
 * A batch ends before every "Go, X!" send-out that is not its first event,
 * so the scene can swap that position's model before the send-out plays.
 * Kinds: the first log line of a battle becomes INTRO; MOVE, HP, FAINT and
 * OUTCOME map directly; a send-out becomes SEND_OUT; a recall ("come back")
 * and other lines become MESSAGE; a stage change or a new status condition
 * becomes STATUS with the tracked stages, `move` the move that caused it and
 * `effect` the battle special (lbl_803727C8) the original plays for it. SEND_OUT
 * and FAINT carry the Pokemon's ball item in `effect`, a recall 0x100|ball. An HP event's
 * actor is the move's user when it falls within that move's group. */
typedef struct {
    uint32_t sequence, group, next, last_move_group;
    uint16_t hp[4], last_move;
    uint8_t last_actor;
    int8_t stages[4][7];
    uint8_t introduced;
} C3GeneralView;

/* Present `b`'s current log from its first event (after begin or a turn). */
void c3_general_view_restart(C3GeneralView *view, const C3GeneralBattle *b);
/* Up to `capacity` events (<= C3_PREVIEW_EVENT_LIMIT). Returns the count,
 * 0 when the log is exhausted; *first receives the log index of out[0]. */
uint32_t c3_general_view_batch(C3GeneralView *view, const C3GeneralBattle *b,
                               C3BattlePreviewEvent *out, uint32_t capacity, uint32_t *first);
/* A send-out event (SEND_OUT): the position and the species it brings in. */
int c3_general_view_send_out(const C3GeneralBattle *b, uint32_t index,
                             unsigned *position, uint16_t *species);
/* Move presentation class from the original move row: 1 physical (PKX
 * entry 2), 2 special or status (entry 1), 0 unknown. Gen-3 rule: the
 * move's type decides physical vs special. */
unsigned c3_general_view_move_role(const C3BattlePack *pack, uint16_t move);
#endif
