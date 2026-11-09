#ifndef C3_BATTLE_SCRIPT_H
#define C3_BATTLE_SCRIPT_H

#include <stddef.h>
#include <stdint.h>

/* Original move-script interpreter (fn_80211B94) over the private C3W1 pack.
 *
 * Scripts are original bytecode addressed by GameCube address. Script operands
 * that name battle variables are resolved through three explicit memory
 * windows with the original big-endian layout:
 *   0x80478D78..0x80478D88  .sdata script work bytes (initial bytes from pack)
 *   0x8047B600..0x8047B640  .sbss sequence variables (zero at battle start)
 *   0x8038FF5A..0x80390000  .data script variables (initial bytes from pack)
 * The interpreter's own PC (0x8047B610), state (0x8047B614) and current action
 * (0x8047B62C) live in native fields; script access to those slots, or to any
 * address outside the windows, fails. Nothing here aliases host memory.
 *
 * Battle logic outside the script engine (status records, targets, end-of-
 * move passes, waits) goes through C3BattleScriptServices. Opcodes that are
 * not lowered yet fail explicitly with their original handler name; none is
 * stubbed. */

#define C3_SCRIPT_EFFECTS 214u
#define C3_SCRIPT_REGION_BASE 0x80375E60u
#define C3_SCRIPT_REGION_BYTES 0x3D94u
#define C3_SCRIPT_FAINT 0x80378798u /* lbl_80378798, run after a move ends */
#define C3_SCRIPT_WSD_BASE 0x80478D78u
#define C3_SCRIPT_WSD_BYTES 0x10u
#define C3_SCRIPT_SBSS_BASE 0x8047B600u
#define C3_SCRIPT_SBSS_BYTES 0x40u
#define C3_SCRIPT_WDT_BASE 0x8038FF5Au
#define C3_SCRIPT_WDT_BYTES 0xA6u
#define C3_SCRIPT_MAX_DEPTH 4u
#define C3_SCRIPT_ERROR_SIZE 160u
#define C3_CONDITION_ROW_BYTES 0x14u
#define C3_CONDITION_TABLE_BYTES 0x6B8u

typedef struct {
    const uint8_t *script;                  /* borrowed: C3_SCRIPT_REGION_BYTES */
    uint32_t effects[C3_SCRIPT_EFFECTS];    /* GameCube script addresses */
    uint8_t wsd[C3_SCRIPT_WSD_BYTES];
    uint8_t wdt[C3_SCRIPT_WDT_BYTES];
    const uint8_t *conditions;              /* borrowed condition rows (0x14 each) */
    uint32_t condition_count;               /* lbl_80478B48 */
} C3BattleScriptPack;

/* Validates header, digest, section order/bases/lengths and every effect
 * pointer. pack->script borrows bytes, which must outlive the pack. */
int c3_battle_script_pack_decode(C3BattleScriptPack *pack, const uint8_t *bytes,
                                 size_t size, char *error);

/* End-of-move passes of fn_80211B94, named by their original callbacks. Each
 * is fightFloorLoopValidFightOutPokemon(0, callback, &arg, 0). */
typedef enum {
    C3_SCRIPT_PASS_FAINT_MARK = 1,   /* 0x8023011C before the faint script */
    C3_SCRIPT_PASS_POST_FAINT_A,     /* 0x8022E34C, arg 1 */
    C3_SCRIPT_PASS_POST_FAINT_B,     /* 0x8022E1F8 */
    C3_SCRIPT_PASS_POST_FAINT_C,     /* 0x80230088 */
    C3_SCRIPT_PASS_POST_FAINT_D,     /* 0x8022EB9C, arg 0 */
    C3_SCRIPT_PASS_MOVE_CLEANUP      /* 0x802136A4 */
} C3BattleScriptPass;

struct C3BattleScript;
typedef struct {
    void *user;
    /* statusGetStatus / statusSetStatus (status.c). arg values are the
     * original 32-bit arguments; record arguments are opaque native handles. */
    int (*status_get)(void *user, uint8_t kind, uint32_t a1, uint32_t a2,
                      uint32_t field, uint32_t a4, uint32_t *value);
    int (*status_set)(void *user, uint8_t kind, uint32_t a1, uint32_t a2,
                      uint32_t field, uint32_t a4, uint32_t value);
    /* fightTargetGetPtrAsNowFightType(kind, arg) -> FightOutPokemon handle */
    int (*target)(void *user, uint8_t kind, uint32_t arg, uint32_t *handle);
    int (*end_of_move)(void *user, C3BattleScriptPass pass, uint8_t arg);
    /* Optional battle-logic opcodes lowered outside the interpreter. Called
     * for an opcode the interpreter does not handle itself; returns one of
     * C3_SCRIPT_HANDLER_*. NULL means none. */
    int (*handler)(void *user, struct C3BattleScript *script, uint8_t opcode, unsigned phase);
} C3BattleScriptServices;

/* External handler results. A handler that fails calls c3_battle_script_fail
 * first; WAIT sets script->wait_frames; NONE means "not lowered". */
enum { C3_SCRIPT_HANDLER_FAIL = 0, C3_SCRIPT_HANDLER_DONE = 1, C3_SCRIPT_HANDLER_WAIT = 2,
       C3_SCRIPT_HANDLER_NONE = 3 };

typedef enum {
    C3_SCRIPT_IDLE = 0, C3_SCRIPT_RUNNING, C3_SCRIPT_WAITING,
    C3_SCRIPT_DONE, C3_SCRIPT_FAILED
} C3BattleScriptStatus;

typedef enum {
    C3_SCRIPT_PHASE_RUN = 0, C3_SCRIPT_PHASE_FAINT_SCRIPT,
    C3_SCRIPT_PHASE_POST_A, C3_SCRIPT_PHASE_POST_B, C3_SCRIPT_PHASE_POST_C,
    C3_SCRIPT_PHASE_POST_D, C3_SCRIPT_PHASE_CLEANUP, C3_SCRIPT_PHASE_EXIT
} C3BattleScriptPhase;

typedef struct {
    uint32_t saved_pc, saved_action; /* restored on exit (fn_80211B94 locals) */
    uint8_t saved_state, top_level;
    C3BattleScriptPhase phase;
    uint32_t faint_pc, faint_action; /* faint sub-script locals uVar7/uVar6 */
    uint8_t faint_state;             /* uVar2 */
} C3BattleScriptFrame;

typedef struct C3BattleScript {
    const C3BattleScriptPack *pack;
    C3BattleScriptServices services;
    uint8_t wsd[C3_SCRIPT_WSD_BYTES];
    uint8_t sbss[C3_SCRIPT_SBSS_BYTES];
    uint8_t wdt[C3_SCRIPT_WDT_BYTES];
    uint32_t pc;      /* lbl_8047B610 */
    uint8_t state;    /* lbl_8047B614 */
    uint32_t action;  /* lbl_8047B62C: opaque FightAction handle */
    C3BattleScriptFrame frames[C3_SCRIPT_MAX_DEPTH];
    unsigned depth;
    C3BattleScriptStatus status;
    uint8_t handler_phase;  /* resumable handler step after a wait */
    uint16_t wait_frames;   /* frames requested by the pending wait */
    uint32_t opcodes;       /* opcodes completed */
    uint32_t failure_pc;
    char failure[C3_SCRIPT_ERROR_SIZE];
} C3BattleScript;

/* Initialise battle-start memory: windows from the pack, .sbss zero, state 0. */
int c3_battle_script_init(C3BattleScript *script, const C3BattleScriptPack *pack,
                          const C3BattleScriptServices *services);

/* fn_80211B94(action, start, top_level). top_level != 0 is the move-use call
 * (end-of-move faint script and cleanup); 0 is a nested sequence. */
int c3_battle_script_call(C3BattleScript *script, uint32_t start, uint32_t action,
                          int top_level);

/* Advance by one opcode or pass. Returns RUNNING, WAITING (wait_frames set;
 * call c3_battle_script_resume when they have elapsed), DONE or FAILED. */
C3BattleScriptStatus c3_battle_script_step(C3BattleScript *script);
int c3_battle_script_resume(C3BattleScript *script);

/* Typed access to the script windows by GameCube address (big-endian). */
int c3_battle_script_read(const C3BattleScript *script, uint32_t address,
                          unsigned width, uint32_t *value);
int c3_battle_script_write(C3BattleScript *script, uint32_t address,
                           unsigned width, uint32_t value);

/* Helpers for external handlers: bytecode fetch relative to the PC (bounded
 * to the script region), a checked jump, and failure with a message. Each
 * returns 1 on success, 0 after recording the failure. */
int c3_battle_script_fetch(struct C3BattleScript *script, uint32_t offset, unsigned width, uint32_t *value);
int c3_battle_script_jump(struct C3BattleScript *script, uint32_t target);
int c3_battle_script_fail(struct C3BattleScript *script, const char *format, ...);

/* Original handler name for an opcode (decomp symbol), for diagnostics. */
const char *c3_battle_script_opcode_name(uint8_t opcode);
#endif
