#include "colosseum3ds/shop.h"
#include "colosseum3ds/inventory.h"
#include <stdio.h>
#include <string.h>

enum { PAD_LEFT = 1, PAD_RIGHT = 2, PAD_DOWN = 4, PAD_UP = 8, PAD_A = 0x100, PAD_B = 0x200 };
enum { LIST_Y = 52, ROW_H = 21, ROWS = 7, MAX_QUANTITY = 99 };

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

int c3_shop_table_open(C3ShopTable *table, const void *data, size_t size)
{
    const uint8_t *p = data;
    if (!table || !p || size < 20 || memcmp(p, "C3H1", 4) || le32(p + 4) != 1 || le32(p + 8) != size) return 0;
    uint32_t shops = le32(p + 12), words = le32(p + 16);
    if (!shops || shops > 64 || !words || words > 1024 || size != 20u + shops * 4u + words * 2u) return 0;
    for (uint32_t i = 0; i < shops; ++i) {
        unsigned offset = le16(p + 20 + i * 4 + 2), j;
        for (j = offset; j < words && le16(p + 20 + shops * 4 + j * 2); ++j) {}
        if (offset >= words || j >= words) return 0; /* out of bounds or unterminated */
    }
    table->data = p; table->size = size; table->shops = shops; table->words = words;
    return 1;
}

unsigned c3_shop_stock(const C3ShopTable *table, unsigned shop, uint16_t items[C3_SHOP_MAX_STOCK])
{
    if (!table || !table->data || !items || shop >= table->shops) return 0;
    const uint8_t *entry = table->data + 20 + shop * 4, *list = table->data + 20 + table->shops * 4;
    if (entry[1] != 0) return 0; /* prize-coupon counters are not ported */
    unsigned count = 0;
    for (unsigned j = le16(entry + 2); j < table->words && count < C3_SHOP_MAX_STOCK; ++j) {
        unsigned item = le16(list + j * 2);
        if (!item) break;
        items[count++] = (uint16_t)item;
    }
    return count;
}

unsigned c3_shop_price(const C3PdaAssets *assets, unsigned item)
{
    size_t size;
    const uint8_t *record = item ? c3_pda_entry(assets, 7, item, &size) : NULL;
    return record && size == 40 ? (unsigned)record[6] << 8 | record[7] : 0;
}

C3ShopResult c3_shop_buy(C3GameState *state, const C3PdaAssets *assets, unsigned item, unsigned quantity)
{
    C3GameSummary summary;
    unsigned price = c3_shop_price(assets, item);
    if (!state || !price || !quantity || quantity > MAX_QUANTITY || !c3_game_state_summary(state, &summary))
        return C3_SHOP_INVALID;
    uint64_t total = (uint64_t)price * quantity;
    if (total > summary.money) return C3_SHOP_NO_MONEY;
    int left = c3_inventory_receive(state, assets, item, quantity);
    if (left < 0) return C3_SHOP_INVALID;
    if (left > 0) return C3_SHOP_BAG_FULL; /* nothing was added */
    if (!c3_game_state_add_money(state, -(int64_t)total)) return C3_SHOP_INVALID;
    return C3_SHOP_BOUGHT;
}

int c3_shop_menu_open(C3ShopMenu *menu, const C3ShopTable *table, const C3PdaAssets *assets, unsigned shop)
{
    uint16_t items[C3_SHOP_MAX_STOCK];
    if (!menu) return 0;
    memset(menu, 0, sizeof *menu);
    unsigned count = c3_shop_stock(table, shop, items);
    for (unsigned i = 0; i < count; ++i) {
        C3InventoryItem info;
        unsigned price = c3_shop_price(assets, items[i]);
        if (!price || !c3_inventory_item(assets, items[i], &info)) continue;
        menu->items[menu->count] = items[i];
        menu->prices[menu->count] = price;
        if (!c3_pda_text(assets, info.name, "", "", menu->names[menu->count], sizeof menu->names[0]))
            snprintf(menu->names[menu->count], sizeof menu->names[0], "ITEM %u", items[i]);
        ++menu->count;
    }
    menu->quantity = 1;
    return menu->count != 0;
}

static unsigned affordable(const C3ShopMenu *menu, const C3GameState *state)
{
    C3GameSummary summary;
    if (!c3_game_state_summary(state, &summary) || !menu->prices[menu->cursor]) return 0;
    unsigned most = summary.money / menu->prices[menu->cursor];
    return most < MAX_QUANTITY ? most : MAX_QUANTITY;
}

C3ShopEvent c3_shop_menu_update(C3ShopMenu *menu, C3GameState *state, const C3PdaAssets *assets, uint16_t pressed)
{
    if (!menu || !menu->count) return C3_SHOP_EVENT_CLOSE;
    if (menu->stage == C3_SHOP_MENU_MESSAGE) {
        if (pressed & (PAD_A | PAD_B)) { menu->stage = C3_SHOP_MENU_LIST; return C3_SHOP_EVENT_CHANGED; }
        return C3_SHOP_EVENT_NONE;
    }
    if (menu->stage == C3_SHOP_MENU_LIST) {
        if (pressed & PAD_B) return C3_SHOP_EVENT_CLOSE;
        if (pressed & (PAD_UP | PAD_DOWN)) {
            menu->cursor = pressed & PAD_UP ? (menu->cursor ? menu->cursor - 1 : menu->count - 1)
                                            : (menu->cursor + 1 < menu->count ? menu->cursor + 1 : 0);
            if (menu->cursor < menu->top) menu->top = menu->cursor;
            if (menu->cursor >= menu->top + ROWS) menu->top = menu->cursor - ROWS + 1;
            return C3_SHOP_EVENT_CHANGED;
        }
        if (pressed & PAD_A) {
            if (!affordable(menu, state)) {
                snprintf(menu->message, sizeof menu->message, "You don't have enough money.");
                menu->stage = C3_SHOP_MENU_MESSAGE;
            } else { menu->quantity = 1; menu->stage = C3_SHOP_MENU_QUANTITY; }
            return C3_SHOP_EVENT_CHANGED;
        }
        return C3_SHOP_EVENT_NONE;
    }
    /* QUANTITY */
    unsigned most = affordable(menu, state);
    if (pressed & PAD_B) { menu->stage = C3_SHOP_MENU_LIST; return C3_SHOP_EVENT_CHANGED; }
    if (pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
        int q = (int)menu->quantity;
        if (pressed & PAD_UP) q += 1;
        if (pressed & PAD_DOWN) q -= 1;
        if (pressed & PAD_RIGHT) q += 10;
        if (pressed & PAD_LEFT) q -= 10;
        /* Stays within 1..what the money covers; stepping past either end
         * from the end itself wraps to the other one. */
        if (q < 1) q = menu->quantity == 1 ? (int)most : 1;
        if (q > (int)most) q = menu->quantity == most ? 1 : (int)most;
        menu->quantity = (unsigned)q;
        return C3_SHOP_EVENT_CHANGED;
    }
    if (pressed & PAD_A) {
        C3ShopResult result = c3_shop_buy(state, assets, menu->items[menu->cursor], menu->quantity);
        if (result == C3_SHOP_BOUGHT)
            snprintf(menu->message, sizeof menu->message, "You bought %u %s.", menu->quantity, menu->names[menu->cursor]);
        else if (result == C3_SHOP_NO_MONEY)
            snprintf(menu->message, sizeof menu->message, "You don't have enough money.");
        else if (result == C3_SHOP_BAG_FULL)
            snprintf(menu->message, sizeof menu->message, "There is no room in the BAG.");
        else
            snprintf(menu->message, sizeof menu->message, "That item cannot be bought.");
        menu->stage = C3_SHOP_MENU_MESSAGE;
        return C3_SHOP_EVENT_CHANGED;
    }
    return C3_SHOP_EVENT_NONE;
}

void c3_shop_menu_draw(const C3ShopMenu *menu, const C3GameState *state, C3BottomUi *ui, const char *title)
{
    C3GameSummary summary;
    char value[32], money[32];
    if (!menu || !ui) return;
    if (!state || !c3_game_state_summary(state, &summary)) summary.money = 0;
    snprintf(money, sizeof money, "Money P%lu", (unsigned long)summary.money);
    c3_ui_header(ui, title ? title : "SHOP", money);
    for (unsigned row = 0; row < ROWS && menu->top + row < menu->count; ++row) {
        unsigned i = menu->top + row;
        snprintf(value, sizeof value, "P%u", menu->prices[i]);
        c3_ui_row_value(ui, LIST_Y + (int)row * ROW_H, menu->names[i], value, i == menu->cursor, 0);
    }
    if (menu->stage == C3_SHOP_MENU_QUANTITY) {
        char line[64];
        c3_ui_panel(ui, 40, 200 - 46, 240, 40);
        snprintf(line, sizeof line, "x%u   P%lu", menu->quantity,
                 (unsigned long)menu->prices[menu->cursor] * menu->quantity);
        c3_ui_text(ui, 56, 200 - 38, 210, 1, line, C3_UI_GOLD);
        c3_ui_footer(ui, "Up/Down: 1   Left/Right: 10   A: buy   B: back");
    } else if (menu->stage == C3_SHOP_MENU_MESSAGE) {
        c3_ui_panel(ui, 20, 200 - 46, 280, 40);
        c3_ui_text(ui, 32, 200 - 38, 256, 1, menu->message, C3_UI_WHITE);
        c3_ui_footer(ui, "A / B: continue");
    } else {
        c3_ui_footer(ui, "A: buy   B: leave");
    }
}
