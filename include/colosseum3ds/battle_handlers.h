#ifndef C3_BATTLE_HANDLERS_H
#define C3_BATTLE_HANDLERS_H

#include "colosseum3ds/battle_canceler.h"
#include "colosseum3ds/battle_damage_state.h"
#include "colosseum3ds/battle_script.h"

/* Battle-logic move-script opcodes lowered over a C3BattleState, and the
 * interpreter services backed by it (statusGet/SetStatus for kinds 5/6/7 and
 * the fixed target kinds). Opcodes not lowered here report "not lowered";
 * branches not lowered fail with a named reason (guarded lowering). */
/* Presentation requests the original makes through its message, camera and
 * model code. present() returns 1 for C3_PRESENT_ATTACK_STRING when a
 * message window was opened (the original then closes it). */
typedef enum {
    C3_PRESENT_ACTOR_BEGIN = 1,  /* fn_80265598(attacker, fight type, 1) */
    C3_PRESENT_ATTACK_STRING,    /* fightMenuWazaOutMsg(attacker, move) */
    C3_PRESENT_MESSAGE_CLOSE,    /* fightMenuCloseMsg() */
    C3_PRESENT_ACTOR_END         /* fn_8026532C(attacker, fight type, 0) */
} C3BattlePresent;

typedef struct {
    C3BattleState *state;
    uint32_t canceller_step;  /* lbl_8047B644, reset to 0 at move start */
    char reason[C3_CANCEL_REASON_SIZE];
    int (*present)(void *user, C3BattlePresent request, uint32_t a, uint32_t b);
    void *present_user;
    uint8_t message_open;     /* attack string window to close after the wait */
    C3BattleDamageTables damage;     /* original tables for fn_80232110 */
    const uint8_t *critical;         /* lbl_80478D58 critical denominators */
    uint32_t critical_count;         /* lbl_80478D60 */
} C3BattleHandlers;

/* Services for c3_battle_script_init with user = handlers. end_of_move is
 * not lowered yet and fails when reached. */
C3BattleScriptServices c3_battle_handler_services(C3BattleHandlers *handlers);

/* Lowered opcodes so far: 0x00 WS_HITCHECK (attack canceler), 0x01
 * accuracy check, 0x02 attack string, 0x03 PP reduce, 0x04 critical check,
 * 0x05 damage calculation. */
int c3_battle_handler_dispatch(void *user, C3BattleScript *script, uint8_t opcode, unsigned phase);
#endif
