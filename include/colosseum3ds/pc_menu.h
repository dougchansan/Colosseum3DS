#ifndef C3_PC_MENU_H
#define C3_PC_MENU_H

#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/game_state.h"
#include <stdint.h>

/* The PC (script native 62 pushing menu floor 0x391) on the bottom screen,
 * with the original texts of the common message group (0x1B59-0x1B95):
 *   PC: "Which would you like to access?" POKeMON Storage / Item Storage /
 *   Save / Exit;
 *   POKeMON Storage: the party beside the current BOX (L/R or the box
 *   header switch boxes); a party member can be deposited ("Deposit in
 *   which BOX?", "The BOX is full.", "That's your last POKeMON!") and a
 *   box Pokemon withdrawn ("Your party's full.").
 * The data layer is pcbox.c through game_state (three boxes of thirty).
 * Not ported: the original PC floor's 3D box view and animations, Item
 * Storage (shown disabled), Move/Mark/Release/Summary and box naming. */
typedef int (*C3PcText)(void *context, uint32_t message, const char *name, char *out, size_t capacity);

typedef enum { C3_PC_TOP, C3_PC_STORAGE, C3_PC_ACTION, C3_PC_BOX_CHOICE, C3_PC_NOTICE } C3PcPhase;
typedef enum { C3_PC_NONE, C3_PC_CHANGED, C3_PC_SAVE, C3_PC_EXIT } C3PcEvent;

typedef struct C3PcMenu {
    C3GameState *state;
    C3PcText text;
    void *context;
    C3PcPhase phase, notice_return;
    unsigned top, side, party, box, box_cursor, box_scroll, action, choice;
    char notice[128];
    unsigned deposits, withdrawals;
} C3PcMenu;

void c3_pc_menu_open(C3PcMenu *menu, C3GameState *state, C3PcText text, void *context);
/* pressed: GameCube PAD bits (A, B, L, R, D-pad). */
C3PcEvent c3_pc_menu_update(C3PcMenu *menu, uint16_t pressed);
void c3_pc_menu_draw(const C3PcMenu *menu, C3BottomUi *ui);

#endif
