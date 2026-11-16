#ifndef C3_LINK_PLAY_H
#define C3_LINK_PLAY_H
#include "colosseum3ds/link_battle.h"
#include "colosseum3ds/game_state.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Link play on 3DS local wireless (docs/link-play.md). */

extern int c3_link_play_console; /* also print log lines on the console (probe) */
void c3_link_play_log(FILE *log, const char *fmt, ...);
/* One frame: poll input, pump the session, wait for vblank. 0 = exit. */
int c3_link_play_pump(C3LinkSession *s);

/* A link battle from setup to result on the 3D stage (a headless lockstep
 * when the prepared assets do not cover the teams). The host chooses mode,
 * type, rule and seed (0: from the clock); clients follow the host's setup.
 * party: this console's six records for Colosseum (read only), else NULL.
 * autoplay: probe builds choose with a local generator. Returns 0 when the
 * battle finished; otherwise message says why. Never writes a save. */
int c3_link_play_battle(C3LinkSession *s, C3LinkMode mode, C3LinkBattleType type, unsigned rule, uint32_t seed,
                        const uint8_t party[6][C3_GB_POKEMON_SIZE], int autoplay, FILE *log,
                        char *message, size_t capacity);

/* The title menu's COLOSSEUM BATTLE (mode 2) and BATTLE NOW (mode 3): host
 * or join over local wireless, then battle (and, from Colosseum Battle,
 * trade). state: the loaded story save, used read-only for battles; a
 * trade changes it in memory and only an explicit Save writes save_path.
 * Returns 0 to go back to the title, 1 when the application must exit. */
int c3_link_lobby_run(int mode, C3GameState *state, const char *save_path, FILE *trace);
#endif
