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
 * the original shop floor 0x38F is not ported.
 *
 * Selling follows the original sell path (fn_800181C4): an item whose price
 * is 0 cannot be sold; otherwise each one brings price / 2 (rounded toward
 * zero), credited with heroAddPokedoru. Key and important items cannot be
 * sold (the bag refuses to toss them). The port also stops the quantity
 * where the money would pass 9,999,999. */
#define C3_SHOP_MAX_STOCK 24u
typedef struct C3ShopTable { const uint8_t *data; size_t size; uint32_t shops, words; } C3ShopTable;
int c3_shop_table_open(C3ShopTable *table, const void *data, size_t size);
/* The item shop's stock in the original order; 0 for an unknown shop or a
 * coupon counter. */
unsigned c3_shop_stock(const C3ShopTable *table, unsigned shop, uint16_t items[C3_SHOP_MAX_STOCK]);
/* The original buying price; 0 for an unknown item. */
unsigned c3_shop_price(const C3PdaAssets *assets, unsigned item);

typedef enum { C3_SHOP_BOUGHT, C3_SHOP_NO_MONEY, C3_SHOP_BAG_FULL, C3_SHOP_INVALID,
               C3_SHOP_SOLD, C3_SHOP_CANNOT_SELL, C3_SHOP_MONEY_FULL } C3ShopResult;
/* Buys `quantity` of `item`: money is checked first, then the bag; a refusal
 * changes nothing. Owned memory only; explicit Save persists. */
C3ShopResult c3_shop_buy(C3GameState *state, const C3PdaAssets *assets, unsigned item, unsigned quantity);
/* What one item brings when sold (price / 2); 0 when it cannot be sold. */
unsigned c3_shop_sell_price(const C3PdaAssets *assets, unsigned item);
/* Sells `quantity` from one bag slot; a refusal changes nothing. */
C3ShopResult c3_shop_sell(C3GameState *state, const C3PdaAssets *assets, unsigned pocket, unsigned slot, unsigned quantity);

/* The bottom-screen menu: BUY / SELL / QUIT, then for buying the stock with
 * prices and a quantity (1..99, up to what the money covers), for selling the
 * bag's items with what they bring and a quantity (up to the stack), and a
 * result message after each. */
#define C3_SHOP_MAX_SELL 64u
typedef enum { C3_SHOP_MENU_CHOICE, C3_SHOP_MENU_LIST, C3_SHOP_MENU_QUANTITY, C3_SHOP_MENU_MESSAGE,
               C3_SHOP_MENU_SELL_LIST, C3_SHOP_MENU_SELL_QUANTITY } C3ShopMenuStage;
typedef struct C3ShopSellEntry { uint16_t item, count; uint8_t pocket, slot; unsigned price; char name[24]; } C3ShopSellEntry;
typedef struct C3ShopMenu {
    uint16_t items[C3_SHOP_MAX_STOCK];
    unsigned prices[C3_SHOP_MAX_STOCK];
    char names[C3_SHOP_MAX_STOCK][24];
    unsigned count, cursor, top, quantity, choice;
    C3ShopSellEntry sell[C3_SHOP_MAX_SELL];
    unsigned sell_count, sell_cursor, sell_top;
    C3ShopMenuStage stage, after_message;
    char message[96];
} C3ShopMenu;
typedef enum { C3_SHOP_EVENT_NONE, C3_SHOP_EVENT_CHANGED, C3_SHOP_EVENT_CLOSE } C3ShopEvent;
/* Fills the menu for one shop; returns 0 when the shop sells nothing. */
int c3_shop_menu_open(C3ShopMenu *menu, const C3ShopTable *table, const C3PdaAssets *assets, unsigned shop);
/* pressed uses GameCube PAD bits. CHOICE: Up/Down choose BUY/SELL/QUIT, A
 * picks, B closes. Lists: Up/Down choose, A picks, B returns to CHOICE.
 * Quantities: Up/Down +-1, Right/Left +-10, A buys or sells, B returns.
 * MESSAGE: A or B returns to the list. Trades happen here, against `state`. */
C3ShopEvent c3_shop_menu_update(C3ShopMenu *menu, C3GameState *state, const C3PdaAssets *assets, uint16_t pressed);
void c3_shop_menu_draw(const C3ShopMenu *menu, const C3GameState *state, C3BottomUi *ui, const char *title);
#endif
