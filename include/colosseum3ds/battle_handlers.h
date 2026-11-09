#ifndef C3_BATTLE_HANDLERS_H
#define C3_BATTLE_HANDLERS_H

#include "colosseum3ds/battle_canceler.h"
#include "colosseum3ds/battle_script.h"

/* Battle-logic move-script opcodes lowered over a C3BattleState, and the
 * interpreter services backed by it (statusGet/SetStatus for kinds 5/6/7 and
 * the fixed target kinds). Opcodes not lowered here report "not lowered";
 * branches not lowered fail with a named reason (guarded lowering). */
typedef struct {
    C3BattleState *state;
    uint32_t canceller_step;  /* lbl_8047B644, reset to 0 at move start */
    char reason[C3_CANCEL_REASON_SIZE];
} C3BattleHandlers;

/* Services for c3_battle_script_init with user = handlers. end_of_move is
 * not lowered yet and fails when reached. */
C3BattleScriptServices c3_battle_handler_services(C3BattleHandlers *handlers);

/* Lowered opcodes so far: 0x00 WS_HITCHECK (attack canceler). */
int c3_battle_handler_dispatch(void *user, C3BattleScript *script, uint8_t opcode, unsigned phase);
#endif
