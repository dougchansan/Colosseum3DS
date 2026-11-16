#ifndef C3_ENDING_H
#define C3_ENDING_H
#include "colosseum3ds/game_state.h"

/* movie.c fn_800361C0 / fn_80035F64. Services are synchronous; the
 * movie returns 0 only at its natural end, 1 on application exit, -1 on
 * error. Choice returns 0 yes, 1 no, -1 on exit/error. No key injection. */
typedef struct C3EndingServices {
    int (*movie)(void *context);
    int (*choice)(void *context, uint32_t message, int initial_no);
    void (*save_error)(void *context, const char *error);
    void *context;
} C3EndingServices;

/* Only entry through staffroll (0x39F) is admitted. Never grants 0x476.
 * Returns 0 after the source return link to floor 2 entrance 1, 1 on exit,
 * -1 on error. The caller supplies an explicitly chosen save destination.
 * saved reports a successful clear save, not eligibility or a declined prompt. */
int c3_ending_run(C3GameState *state, const char *save_path,
                  const C3EndingServices *services, int *saved,
                  char error[C3_GAME_ERROR_SIZE]);
#endif
