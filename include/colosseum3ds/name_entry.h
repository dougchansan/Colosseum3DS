#ifndef C3_NAME_ENTRY_H
#define C3_NAME_ENTRY_H

#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/story.h"
#include <stdint.h>

/* The original name entry (menuNameEntry.c, name-entry floor 0x390) on the
 * bottom screen. Its text and key layout are the original message group
 * 02FD0A00 of name_entry_menu.fsys (prepared as boot/name_entry.c3m):
 *   - modes 0/1 (hero, partner) first offer the preset names of
 *     lbl_80266DD8 (WES/SETH/THOMAS, RUI/ANCA/YUKI) with "NEW NAME"; a
 *     preset is confirmed with "Is <name> OK?" (2EF6, yes/no);
 *   - NEW NAME or B opens the keyboard: the UPPER and lower pages (2F04-2F07,
 *     2F08-2F0B), four rows of fifteen cells, the R button switching pages,
 *     with UPPER, lower, BACK and DONE buttons, prefilled with the first
 *     preset (menuNameEntry's nameBuf for modes 0/1);
 *   - the length limit is lbl_80266DD8's: 7 for the hero and partner,
 *     10 for a Pokemon nickname, 8 for a box name.
 * The 3D back panel (people model) and window animation are not drawn.
 * Names are UTF-8 strings of GSchars from the font. No GPU ownership. */
#define C3_NAME_ENTRY_MAX 10u
#define C3_NAME_ENTRY_COLUMNS 15u
#define C3_NAME_ENTRY_ROWS 4u
#define C3_NAME_ENTRY_PRESETS 3u

typedef struct C3NameEntryText {
    char title[48], new_name[24], upper[16], lower[16], done[16], back[16], toggle[32];
    char presets[C3_NAME_ENTRY_PRESETS][24];
    unsigned preset_count, max_length;
    uint16_t keys[2][C3_NAME_ENTRY_ROWS][C3_NAME_ENTRY_COLUMNS]; /* 0: no key */
} C3NameEntryText;

typedef enum { C3_NAME_PRESETS, C3_NAME_CONFIRM, C3_NAME_KEYBOARD } C3NamePhase;

typedef struct C3NameEntry {
    const C3NameEntryText *text;
    const C3Story *messages;  /* for the confirm prompt; may be NULL */
    uint16_t name[C3_NAME_ENTRY_MAX + 1u];
    unsigned length, page, row, column, choice, yes;
    C3NamePhase phase;
    char result[C3_NAME_ENTRY_MAX * 3u + 1u]; /* UTF-8, set on DONE */
    char prompt[64];
} C3NameEntry;

typedef enum { C3_NAME_NONE, C3_NAME_CHANGED, C3_NAME_DONE } C3NameEvent;

/* Load mode 0-3's text from the message group (c3_story_add_messages).
 * Returns 0 when a message is missing. */
int c3_name_entry_text_load(C3NameEntryText *text, const C3Story *messages, unsigned mode);
/* Begin naming: modes with presets start on the preset list. */
void c3_name_entry_init(C3NameEntry *entry, const C3NameEntryText *text, const C3Story *messages);
/* pressed uses GameCube PAD bits (A, B, START, R, D-pad). A touch on a key,
 * button or list row selects and activates it. */
C3NameEvent c3_name_entry_update(C3NameEntry *entry, uint16_t pressed,
                                 int touch_pressed, unsigned touch_x, unsigned touch_y);
void c3_name_entry_draw(const C3NameEntry *entry, C3BottomUi *ui);

#endif
