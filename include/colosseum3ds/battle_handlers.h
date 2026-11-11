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
    C3_PRESENT_ACTOR_END,        /* fn_8026532C(attacker, fight type, 0) */
    C3_PRESENT_STATUS_MENUS_CLOSE, /* fightMenuAllFightOutPokemonCloseStatusMenu(0) */
    C3_PRESENT_STATUS_MENU_SHOWN, /* menuIsCheck(status menu of a, fight type b): 1 shown */
    C3_PRESENT_HP_BAR,            /* menuFightStatusStartAnimHP(status menu of a, HP b) */
    C3_PRESENT_MESSAGE_OPEN,      /* fightMenuOpenMsg(message a): 1 when a window opened */
    C3_PRESENT_EFFECT_MESSAGE,    /* fightMenuWazaKoukaMsg(message a, target b, item 0):
                                     1 when a window opened */
    C3_PRESENT_HP_BAR_BUSY,       /* menuFightStatusWaitAnimeHP poll: 1 while the HP bar
                                     of target a is still animating */
    C3_PRESENT_TRAINER_MENUS_CLOSE, /* fightMenuAllFightTrainerCloseStatusMenu(0) */
    C3_PRESENT_CRY,               /* the cry of battler a, species b (fn_80166A50 with
                                     its species' voice, data field 0x61) */
    C3_PRESENT_EXP_CAMERA,        /* battleGridUpdate(); fn_801EF8F4(1): the EXP camera */
    C3_PRESENT_EXP_BAR,           /* menuFightStatusStartAnimEXP(status menu of a, EXP b
                                     into the level) */
    C3_PRESENT_EXP_BAR_BUSY,      /* menuFightStatusWaitAnimeEXP poll: 1 while battler a's
                                     EXP bar is still filling */
    C3_PRESENT_STATUS_RENEW       /* fightMenuFightOutPokemonRenewStatusMenu(a, fight type b, 1) */
} C3BattlePresent;

/* fightWazaDoEffectFunc(move, attacker, target, count): the move's effect
 * (WZX type/variation functions from move row fields 0x20/0x21). count is
 * the effect counter byte 0x8038FFF9. Returns 1 when the effect has run; the
 * script continues after it returns. */
typedef int (*C3BattlePresentMove)(void *user, uint16_t move, uint32_t attacker, uint32_t target, uint8_t count);
/* fn_801DA5C4(signal): has the running move effect reached `signal`?
 * 1 yes, 0 not yet (the step waits one frame and asks again), -1 error. */
typedef int (*C3BattleEffectSignal)(void *user, uint8_t signal);

/* One recipient of fn_802232F4's EXP award. */
typedef struct { uint32_t trainer, fight_pokemon, gain; uint16_t message; } C3BattleExpAward;

typedef struct {
    C3BattleState *state;
    uint32_t canceller_step;  /* lbl_8047B644, reset to 0 at move start */
    char reason[C3_CANCEL_REASON_SIZE];
    int (*present)(void *user, C3BattlePresent request, uint32_t a, uint32_t b);
    void *present_user;
    C3BattlePresentMove present_move;  /* NULL: the move effect step refuses */
    C3BattleEffectSignal effect_signal; /* NULL: the effect wait step refuses */
    uint8_t message_open;     /* attack string window to close after the wait */
    C3BattleDamageTables damage;     /* original tables for fn_80232110 */
    uint32_t saved_appoint;          /* floor 0x4B held across the faint sequence */
    C3BattleExpAward exp[12];        /* 0x23: recipients, handed out across waits */
    uint8_t exp_count, exp_index, exp_step, exp_camera, exp_opened;
    uint16_t exp_species;            /* defeated species (effort values) */
    uint32_t exp_battler;            /* recipient in battle whose EXP bar is filling */
    unsigned pass_cursor;            /* faint mark pass: next battler after a nested call */
    const uint8_t *critical;         /* lbl_80478D58 critical denominators */
    uint32_t critical_count;         /* lbl_80478D60 */
} C3BattleHandlers;

/* Services for c3_battle_script_init with user = handlers. end_of_move runs
 * the end-of-move passes; a pass that would run a nested sequence fails the
 * script by name. */
C3BattleScriptServices c3_battle_handler_services(C3BattleHandlers *handlers);

/* Lowered opcodes so far: 0x00 WS_HITCHECK (attack canceler), 0x01
 * accuracy check, 0x02 attack string, 0x03 PP reduce, 0x04 critical check,
 * 0x05 damage calculation, 0x06 type check, 0x07/0x08 damage loss, 0x09
 * move effect, 0x0A effect wait, 0x0B HP update, 0x0D critical-hit message,
 * 0x0C HP bar wait, 0x0E (FightWaza read, no effect), 0x0F effectiveness
 * message, 0x12 timed message close, 0x15 secondary-effect chance, 0x19
 * faint check, 0x1A / 0x56 / 0x10 / 0x1B faint sequence, 0x23 EXP award, 0x24 battle end check, 0x49 move end, 0x5C target damage blink (no-op without a
 * battler model). */
int c3_battle_handler_dispatch(void *user, C3BattleScript *script, uint8_t opcode, unsigned phase);
#endif
