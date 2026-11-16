#ifndef C3_BATTLE_PREVIEW_MENU_H
#define C3_BATTLE_PREVIEW_MENU_H
#include "colosseum3ds/battle_preview.h"

typedef enum {
    C3_PREVIEW_MENU_EVENTS, C3_PREVIEW_MENU_MOVE, C3_PREVIEW_MENU_TARGET,
    C3_PREVIEW_MENU_CONFIRM, C3_PREVIEW_MENU_RESULT,
    C3_PREVIEW_MENU_LEAVE, C3_PREVIEW_MENU_EXIT
} C3BattlePreviewMenuPage;
typedef struct {
    C3BattlePreviewMenuPage page, previous;
    unsigned actor, selection, target, event;
    C3BattlePreviewCommand commands[2];
} C3BattlePreviewMenu;
/* GameCube-style button edges, matching the existing platform adapter. */
enum { C3_PREVIEW_LEFT=1, C3_PREVIEW_RIGHT=2, C3_PREVIEW_DOWN=4, C3_PREVIEW_UP=8,
       C3_PREVIEW_A=0x100, C3_PREVIEW_B=0x200, C3_PREVIEW_X=0x400 };
void c3_battle_preview_menu_init(C3BattlePreviewMenu *menu);
/* Turn execution is exclusive to the explicit confirmation page. Selection,
 * target changes and cancellation do not advance RNG or change combat state. */
int c3_battle_preview_menu_step(C3BattlePreviewMenu *menu,C3BattlePreview *battle,
                                const C3BattlePreviewTables *tables,uint16_t pressed,
                                char *error);
/* Stylus tap at bottom-screen pixel (x, y), using the row layout of
 * c3_battle_ui_draw_to. A tap on a legal move or a standing target selects
 * it; the returned button bits (A or none) are then passed to
 * c3_battle_preview_menu_step. Message pages and the confirm panel accept a
 * tap as A; the leave question needs a button. */
uint16_t c3_battle_preview_menu_touch(C3BattlePreviewMenu *menu,const C3BattlePreview *battle,
                                      const C3BattlePreviewTables *tables,unsigned x,unsigned y);
#endif
