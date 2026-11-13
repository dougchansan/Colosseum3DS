#ifndef C3_SHOP_H
#define C3_SHOP_H
#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/pda.h"
#include <stddef.h>
#include <stdint.h>

/* Shops of script native 19 (menuShopOpen). The stock table is the original
 * one from REL module 2 of pocket_menu.fsys (tools/prepare_shop.py, C3H1):
 * per shop {u8, u8 kind, u16 offset} into 0-terminated u16 item lists.
 * Prices are the item records' bytes 6-7 (big-endian) in the P*DA pack.
 * Only item shops (kind 0) are sold here; the prize-coupon counters (kinds
 * 1..4) are not ported. The menu layout is the port's own bottom screen;
 * the original shop floor 0x38F is not ported. Selling is not ported. */
#define C3_SHOP_MAX_STOCK 24u
typedef struct C3ShopTable { const uint8_t *data; size_t size; uint32_t shops, words; } C3ShopTable;
int c3_shop_table_open(C3ShopTable *table, const void *data, size_t size);
/* The item shop's stock in the original order; 0 for an unknown shop or a
 * coupon counter. */
unsigned c3_shop_stock(const C3ShopTable *table, unsigned shop, uint16_t items[C3_SHOP_MAX_STOCK]);
/* The original buying price; 0 for an unknown item. */
unsigned c3_shop_price(const C3PdaAssets *assets, unsigned item);

typedef enum { C3_SHOP_BOUGHT, C3_SHOP_NO_MONEY, C3_SHOP_BAG_FULL, C3_SHOP_INVALID } C3ShopResult;
/* Buys `quantity` of `item`: money is checked first, then the bag; a refusal
 * changes nothing. Owned memory only; explicit Save persists. */
C3ShopResult c3_shop_buy(C3GameState *state, const C3PdaAssets *assets, unsigned item, unsigned quantity);

/* The bottom-screen menu: a list of the stock with prices, then a quantity
 * (1..99, up to what the money covers) and a result message. */
typedef enum { C3_SHOP_MENU_LIST, C3_SHOP_MENU_QUANTITY, C3_SHOP_MENU_MESSAGE } C3ShopMenuStage;
typedef struct C3ShopMenu {
    uint16_t items[C3_SHOP_MAX_STOCK];
    unsigned prices[C3_SHOP_MAX_STOCK];
    char names[C3_SHOP_MAX_STOCK][24];
    unsigned count, cursor, top, quantity;
    C3ShopMenuStage stage;
    char message[96];
} C3ShopMenu;
typedef enum { C3_SHOP_EVENT_NONE, C3_SHOP_EVENT_CHANGED, C3_SHOP_EVENT_CLOSE } C3ShopEvent;
/* Fills the menu for one shop; returns 0 when the shop sells nothing. */
int c3_shop_menu_open(C3ShopMenu *menu, const C3ShopTable *table, const C3PdaAssets *assets, unsigned shop);
/* pressed uses GameCube PAD bits. LIST: Up/Down choose, A picks, B closes.
 * QUANTITY: Up/Down +-1, Right/Left +-10, A buys, B returns. MESSAGE: A or B
 * returns to the list. Buying happens here, against `state`. */
C3ShopEvent c3_shop_menu_update(C3ShopMenu *menu, C3GameState *state, const C3PdaAssets *assets, uint16_t pressed);
void c3_shop_menu_draw(const C3ShopMenu *menu, const C3GameState *state, C3BottomUi *ui, const char *title);
#endif
