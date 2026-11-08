#ifndef C3_BATTLE_ENTRY_CONSOLE_H
#define C3_BATTLE_ENTRY_CONSOLE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "colosseum3ds/battle_preview.h"

/* Run the original two-side trainer entry for an encounter in an isolated
 * session, from the live save body (read only), trace the constructed records,
 * and start *battle from them (active FightPokemon and the session RNG).
 * Returns 1 when the battle was started from the entry, 0 when the private
 * pack is absent (skipped; *battle untouched), -1 on failure with error set.
 * It writes nothing to the save, native98, story flags or results. */
int c3_battle_entry_console_run(uint16_t encounter_id, const uint8_t *save_body,
                                size_t body_size, FILE *trace,
                                const C3BattleRequest *request,
                                const C3BattlePreviewTables *tables,
                                C3BattlePreview *battle, char *error);
#endif
