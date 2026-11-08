#ifndef C3_BATTLE_ENTRY_CONSOLE_H
#define C3_BATTLE_ENTRY_CONSOLE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Run the original two-side trainer entry for an encounter in an isolated
 * session, from the live save body (read only), and trace the constructed
 * records. Returns 1 when entry completed, 0 when the private pack is absent
 * (skipped), -1 on failure. It writes nothing to the save, native98, story
 * flags or results; the preview still drives the battle afterwards. */
int c3_battle_entry_console_run(uint16_t encounter_id, const uint8_t *save_body,
                                size_t body_size, FILE *trace);
#endif
