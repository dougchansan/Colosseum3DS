#ifndef C3_NAME_ENTRY_H
#define C3_NAME_ENTRY_H

#include "colosseum3ds/bottom_ui.h"
#include <stdint.h>

/* Bottom-screen keyboard for the original name-entry request (script native
 * 16). The layout is the port's own: the original name-entry floor 0x390 is
 * not ported. Names are 1..10 printable ASCII characters, the length the
 * save stores (GScharLenCpy(..., 0xB)). No GPU or frame ownership. */
#define C3_NAME_ENTRY_MAX 10u
#define C3_NAME_ENTRY_COLUMNS 10u
#define C3_NAME_ENTRY_ROWS 7u

typedef struct C3NameEntry {
    char text[C3_NAME_ENTRY_MAX + 1u];
    char fallback[C3_NAME_ENTRY_MAX + 1u]; /* restored by an empty confirm */
    unsigned length, row, column; /* row C3_NAME_ENTRY_ROWS is the button row */
    int armed;                    /* key index under the stylus, or -1 */
} C3NameEntry;

typedef enum { C3_NAME_NONE, C3_NAME_CHANGED, C3_NAME_DONE } C3NameEvent;

/* initial is shown first and returned when the player confirms an empty
 * field; it is truncated to ten characters. */
void c3_name_entry_init(C3NameEntry *entry, const char *initial);
/* pressed uses GameCube PAD bits. A types the key under the cursor, B
 * deletes, START confirms. The stylus types a key when it is lifted on the
 * key it first touched. */
C3NameEvent c3_name_entry_update(C3NameEntry *entry, uint16_t pressed,
                                 int touch_held, int touch_pressed, int touch_released,
                                 unsigned touch_x, unsigned touch_y);
void c3_name_entry_draw(const C3NameEntry *entry, C3BottomUi *ui, const char *title);

#endif
