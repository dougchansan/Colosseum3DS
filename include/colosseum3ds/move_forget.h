#ifndef C3_MOVE_FORGET_H
#define C3_MOVE_FORGET_H
#include "colosseum3ds/battle_general.h"
#include <stddef.h>
#include <stdint.h>

/* The forget-a-move prompt as evolutionWazaLearn (evolution.c:215) runs it
 * when a Pokemon with four moves reaches a level-up move:
 *
 *   ASK_FORGET  msg 0x4243 (yes/no): yes -> CHOOSE, no -> ASK_GIVE_UP
 *   CHOOSE      the callback (cbWazaForget) picks a slot: a move -> FORGOT,
 *               cancel -> ASK_GIVE_UP; an HM can't be picked
 *   ASK_GIVE_UP msg 0x4242 (yes/no): yes -> DID_NOT_LEARN, no -> ASK_FORGET
 *   FORGOT      msg 0x4248 (the old move; its sound control plays the
 *               sound msgctrl 0x5D holds, 0x468), then
 *   LEARNED     msg 0x423D (the new move) -> DONE with the slot
 *   DID_NOT_LEARN msg 0x4241 -> DONE with -1
 *
 * Messages print with msgctrl 0x32 = the nickname and 0x39 = the move. */
#define C3_MF_MSG_ASK_FORGET 0x4243u
#define C3_MF_MSG_ASK_GIVE_UP 0x4242u
#define C3_MF_MSG_DID_NOT_LEARN 0x4241u
#define C3_MF_MSG_FORGOT 0x4248u
#define C3_MF_MSG_LEARNED 0x423Du
#define C3_MF_SOUND_FORGOT 0x468u   /* msgctrlSetValue(0x5d, 0x468): msgctrlSndPlay's sound */

typedef enum {
    C3_MF_ASK_FORGET, C3_MF_CHOOSE, C3_MF_ASK_GIVE_UP, C3_MF_FORGOT, C3_MF_LEARNED,
    C3_MF_DID_NOT_LEARN, C3_MF_DONE
} C3MoveForgetStep;

typedef struct C3MoveForget {
    C3MoveForgetStep step;
    uint16_t move, known[4], forgotten;
    uint8_t hm[4];
    int slot;           /* the result once DONE: 0..3, or -1 */
    int hm_refused;     /* the last CHOOSE answer was an HM */
} C3MoveForget;

void c3_move_forget_begin(C3MoveForget *f, const C3BattlePack *pack,
                          const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move);
/* ASK steps: answer 1 yes, 0 no. CHOOSE: 0..3 a slot, 4 cancel. Message
 * steps: any answer moves on. Returns the new step. */
C3MoveForgetStep c3_move_forget_answer(C3MoveForget *f, int answer);
/* The message the current step shows (0 for CHOOSE and DONE), and the move
 * its 0x39 control prints. */
uint32_t c3_move_forget_message(const C3MoveForget *f, uint16_t *move);

/* Original message text: lookup returns the raw UTF-16BE message (the PDA
 * pack's kind 1 entries). Handles controls 00 (newline), 02 (end), 03
 * (page), 07 xx (font), 08 xxxxxxxx (colour), 09 xx (wait), 32 (nickname),
 * 39 (move name), 5D/5E (play / wait for the msgctrl sound: no text; *sound
 * is set, may be NULL) and the ASCII-foldable characters; returns 0 for
 * anything else (the caller shows its own fallback). */
typedef const uint8_t *(*C3MoveForgetLookup)(void *context, uint32_t id, size_t *size);
int c3_move_forget_text(C3MoveForgetLookup lookup, void *context, uint32_t id,
                        const char *nickname, const char *move, char *out, size_t capacity, int *sound);
#endif
