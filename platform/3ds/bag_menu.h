#ifndef C3_BAG_MENU_H
#define C3_BAG_MENU_H
#include "colosseum3ds/game_items.h"
#include "n3ds_platform.h"

typedef enum {
    C3_BAG_LIST, C3_BAG_TARGET, C3_BAG_CONFIRM, C3_BAG_NOTICE, C3_BAG_CLOSED
} C3BagPage;
typedef enum {
    C3_BAG_EVENT_NONE, C3_BAG_EVENT_CLOSED, C3_BAG_EVENT_USED
} C3BagEvent;
typedef struct C3BagMenu {
    C3BagPage page, return_page;
    unsigned pocket, selected, target;
    uint16_t item;
    uint8_t yes;
    C3BagView view;
    C3BagPokemon party[6];
    C3ItemUseResult proposed;
    char notice[C3_GAME_ERROR_SIZE];
} C3BagMenu;

/* Pure controller/state operations. No graphics ownership, implicit save,
 * scripted damage, RNG or original battle result handling. Cancel wins over A. */
void c3_bag_menu_init(C3BagMenu *menu, const C3GameState *state, const C3ItemCatalog *catalog);
C3BagEvent c3_bag_menu_step(C3BagMenu *menu, C3GameState *state,
                            const C3ItemCatalog *catalog, uint16_t pressed,
                            C3ItemUseResult *used);
/* Draw only to the already-selected bottom text console. No consoleInit,
 * renderer init/shutdown, frame submit, texture or display ownership change. */
void c3_bag_menu_draw(const C3BagMenu *menu, const C3ItemCatalog *catalog);
#endif
