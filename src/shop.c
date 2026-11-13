#include "colosseum3ds/shop.h"
#include "colosseum3ds/inventory.h"
#include <stdio.h>
#include <string.h>

enum { PAD_LEFT = 1, PAD_RIGHT = 2, PAD_DOWN = 4, PAD_UP = 8, PAD_A = 0x100, PAD_B = 0x200 };
enum { LIST_Y = 52, ROW_H = 21, ROWS = 7, MAX_QUANTITY = 99 };
/* The quantity and message panel sits below the fifth row; while it is open
 * only rows that end above it are drawn, so no row shows through. */
enum { PANEL_Y = 160, PANEL_H = 36, PANEL_ROWS = 5 };
#define MONEY_LIMIT 9999999u

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

unsigned c3_shop_sell_price(const C3PdaAssets *assets, unsigned item)
{
    /* fn_800181C4: a price of 0 cannot be sold; else (price / 2) per item. */
    return c3_shop_price(assets, item) / 2u;
}

C3ShopResult c3_shop_sell(C3GameState *state, const C3PdaAssets *assets, unsigned pocket, unsigned slot, unsigned quantity)
{
    C3GameSummary summary;
    unsigned item, count;
    if (!state || !quantity || !c3_inventory_slot(state, pocket, slot, &item, &count) || quantity > count ||
        !c3_game_state_summary(state, &summary)) return C3_SHOP_INVALID;
    unsigned each = c3_shop_sell_price(assets, item);
    if (!each) return C3_SHOP_CANNOT_SELL;
    uint64_t total = (uint64_t)each * quantity;
    if (summary.money + total > MONEY_LIMIT) return C3_SHOP_MONEY_FULL;
    C3InventoryRequest request = {.action = C3_INVENTORY_TOSS, .pocket = pocket, .slot = slot, .quantity = quantity,
                                  .move = -1, .replacement = -1, .evolution = -1};
    C3InventoryResult result;
    C3InventoryCode code = c3_inventory_apply(state, assets, &request, &result);
    if (code == C3_INVENTORY_FORBIDDEN) return C3_SHOP_CANNOT_SELL; /* key and important items */
    if (code != C3_INVENTORY_OK) return C3_SHOP_INVALID;
    if (!c3_game_state_add_money(state, (int64_t)total)) return C3_SHOP_INVALID;
    return C3_SHOP_SOLD;
}

static void item_name(const C3PdaAssets *assets, unsigned item, char *name, size_t capacity)
{
    C3InventoryItem info;
    if (!c3_inventory_item(assets, item, &info) || !c3_pda_text(assets, info.name, "", "", name, capacity))
        snprintf(name, capacity, "ITEM %u", item);
}

int c3_shop_menu_open(C3ShopMenu *menu, const C3ShopTable *table, const C3PdaAssets *assets, unsigned shop)
{
    uint16_t items[C3_SHOP_MAX_STOCK];
    if (!menu) return 0;
    memset(menu, 0, sizeof *menu);
    unsigned count = c3_shop_stock(table, shop, items);
    for (unsigned i = 0; i < count; ++i) {
        unsigned price = c3_shop_price(assets, items[i]);
        if (!price) continue;
        menu->items[menu->count] = items[i];
        menu->prices[menu->count] = price;
        item_name(assets, items[i], menu->names[menu->count], sizeof menu->names[0]);
        ++menu->count;
    }
    menu->quantity = 1;
    menu->stage = C3_SHOP_MENU_CHOICE;
    return menu->count != 0;
}

/* The bag's items in pocket order, except the key-item pocket (1). Each
 * carries what one brings when sold; 0 marks one that cannot be sold. */
static void build_sell_list(C3ShopMenu *menu, const C3GameState *state, const C3PdaAssets *assets)
{
    static const unsigned pockets[] = {0, 2, 4, 3, 5};
    menu->sell_count = 0;
    for (unsigned p = 0; p < sizeof pockets / sizeof pockets[0]; ++p)
        for (unsigned s = 0; s < c3_inventory_capacity(pockets[p]) && menu->sell_count < C3_SHOP_MAX_SELL; ++s) {
            unsigned item, count;
            if (!c3_inventory_slot(state, pockets[p], s, &item, &count)) continue;
            C3ShopSellEntry *e = &menu->sell[menu->sell_count++];
            e->item = (uint16_t)item; e->count = (uint16_t)count; e->pocket = (uint8_t)pockets[p]; e->slot = (uint8_t)s;
            e->price = c3_shop_sell_price(assets, item);
            item_name(assets, item, e->name, sizeof e->name);
        }
    if (menu->sell_cursor >= menu->sell_count) menu->sell_cursor = menu->sell_count ? menu->sell_count - 1 : 0;
    if (menu->sell_top > menu->sell_cursor) menu->sell_top = menu->sell_cursor;
}

static unsigned affordable(const C3ShopMenu *menu, const C3GameState *state)
{
    C3GameSummary summary;
    if (!c3_game_state_summary(state, &summary) || !menu->prices[menu->cursor]) return 0;
    unsigned most = summary.money / menu->prices[menu->cursor];
    return most < MAX_QUANTITY ? most : MAX_QUANTITY;
}

/* Up to the stack, and only as many as keep the money within its limit. */
static unsigned sellable(const C3ShopMenu *menu, const C3GameState *state)
{
    C3GameSummary summary;
    if (!menu->sell_count || !c3_game_state_summary(state, &summary)) return 0;
    const C3ShopSellEntry *e = &menu->sell[menu->sell_cursor];
    if (!e->price || summary.money >= MONEY_LIMIT) return 0;
    unsigned room = (MONEY_LIMIT - summary.money) / e->price;
    return e->count < room ? e->count : room;
}

static void move_cursor(unsigned *cursor, unsigned *top, unsigned count, uint16_t pressed)
{
    if (!count) return;
    *cursor = pressed & PAD_UP ? (*cursor ? *cursor - 1 : count - 1) : (*cursor + 1 < count ? *cursor + 1 : 0);
    if (*cursor < *top) *top = *cursor;
    if (*cursor >= *top + ROWS) *top = *cursor - ROWS + 1;
}

/* 1..most; stepping past either end from the end itself wraps to the other. */
static unsigned step_quantity(unsigned quantity, unsigned most, uint16_t pressed)
{
    int q = (int)quantity;
    if (pressed & PAD_UP) q += 1;
    if (pressed & PAD_DOWN) q -= 1;
    if (pressed & PAD_RIGHT) q += 10;
    if (pressed & PAD_LEFT) q -= 10;
    if (q < 1) q = quantity == 1 ? (int)most : 1;
    if (q > (int)most) q = quantity == most ? 1 : (int)most;
    return (unsigned)q;
}

static C3ShopEvent show(C3ShopMenu *menu, C3ShopMenuStage after, const char *text)
{
    snprintf(menu->message, sizeof menu->message, "%s", text);
    menu->after_message = after; menu->stage = C3_SHOP_MENU_MESSAGE;
    return C3_SHOP_EVENT_CHANGED;
}

C3ShopEvent c3_shop_menu_update(C3ShopMenu *menu, C3GameState *state, const C3PdaAssets *assets, uint16_t pressed)
{
    char text[96];
    if (!menu || !menu->count) return C3_SHOP_EVENT_CLOSE;
    switch (menu->stage) {
    case C3_SHOP_MENU_MESSAGE:
        if (pressed & (PAD_A | PAD_B)) { menu->stage = menu->after_message; return C3_SHOP_EVENT_CHANGED; }
        return C3_SHOP_EVENT_NONE;
    case C3_SHOP_MENU_CHOICE:
        if (pressed & PAD_B) return C3_SHOP_EVENT_CLOSE;
        if (pressed & (PAD_UP | PAD_DOWN)) {
            menu->choice = pressed & PAD_UP ? (menu->choice + 2) % 3 : (menu->choice + 1) % 3;
            return C3_SHOP_EVENT_CHANGED;
        }
        if (pressed & PAD_A) {
            if (menu->choice == 2) return C3_SHOP_EVENT_CLOSE;
            if (menu->choice == 0) { menu->stage = C3_SHOP_MENU_LIST; return C3_SHOP_EVENT_CHANGED; }
            build_sell_list(menu, state, assets);
            if (!menu->sell_count) return show(menu, C3_SHOP_MENU_CHOICE, "You have nothing to sell.");
            menu->stage = C3_SHOP_MENU_SELL_LIST;
            return C3_SHOP_EVENT_CHANGED;
        }
        return C3_SHOP_EVENT_NONE;
    case C3_SHOP_MENU_LIST:
        if (pressed & PAD_B) { menu->stage = C3_SHOP_MENU_CHOICE; return C3_SHOP_EVENT_CHANGED; }
        if (pressed & (PAD_UP | PAD_DOWN)) { move_cursor(&menu->cursor, &menu->top, menu->count, pressed); return C3_SHOP_EVENT_CHANGED; }
        if (pressed & PAD_A) {
            if (!affordable(menu, state)) return show(menu, C3_SHOP_MENU_LIST, "You don't have enough money.");
            menu->quantity = 1; menu->stage = C3_SHOP_MENU_QUANTITY;
            return C3_SHOP_EVENT_CHANGED;
        }
        return C3_SHOP_EVENT_NONE;
    case C3_SHOP_MENU_QUANTITY:
        if (pressed & PAD_B) { menu->stage = C3_SHOP_MENU_LIST; return C3_SHOP_EVENT_CHANGED; }
        if (pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
            menu->quantity = step_quantity(menu->quantity, affordable(menu, state), pressed);
            return C3_SHOP_EVENT_CHANGED;
        }
        if (pressed & PAD_A) {
            C3ShopResult result = c3_shop_buy(state, assets, menu->items[menu->cursor], menu->quantity);
            if (result == C3_SHOP_BOUGHT) snprintf(text, sizeof text, "You bought %u %s.", menu->quantity, menu->names[menu->cursor]);
            else snprintf(text, sizeof text, "%s", result == C3_SHOP_NO_MONEY ? "You don't have enough money." :
                          result == C3_SHOP_BAG_FULL ? "There is no room in the BAG." : "That item cannot be bought.");
            return show(menu, C3_SHOP_MENU_LIST, text);
        }
        return C3_SHOP_EVENT_NONE;
    case C3_SHOP_MENU_SELL_LIST:
        if (pressed & PAD_B) { menu->stage = C3_SHOP_MENU_CHOICE; return C3_SHOP_EVENT_CHANGED; }
        if (pressed & (PAD_UP | PAD_DOWN)) { move_cursor(&menu->sell_cursor, &menu->sell_top, menu->sell_count, pressed); return C3_SHOP_EVENT_CHANGED; }
        if (pressed & PAD_A) {
            if (!menu->sell[menu->sell_cursor].price) return show(menu, C3_SHOP_MENU_SELL_LIST, "That item cannot be sold.");
            if (!sellable(menu, state)) return show(menu, C3_SHOP_MENU_SELL_LIST, "You can't carry any more money.");
            menu->quantity = 1; menu->stage = C3_SHOP_MENU_SELL_QUANTITY;
            return C3_SHOP_EVENT_CHANGED;
        }
        return C3_SHOP_EVENT_NONE;
    case C3_SHOP_MENU_SELL_QUANTITY:
        if (pressed & PAD_B) { menu->stage = C3_SHOP_MENU_SELL_LIST; return C3_SHOP_EVENT_CHANGED; }
        if (pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
            menu->quantity = step_quantity(menu->quantity, sellable(menu, state), pressed);
            return C3_SHOP_EVENT_CHANGED;
        }
        if (pressed & PAD_A) {
            const C3ShopSellEntry e = menu->sell[menu->sell_cursor];
            C3ShopResult result = c3_shop_sell(state, assets, e.pocket, e.slot, menu->quantity);
            if (result == C3_SHOP_SOLD)
                snprintf(text, sizeof text, "You sold %u %s for P%lu.", menu->quantity, e.name,
                         (unsigned long)e.price * menu->quantity);
            else snprintf(text, sizeof text, "%s", result == C3_SHOP_MONEY_FULL ? "You can't carry any more money." :
                          "That item cannot be sold.");
            build_sell_list(menu, state, assets);
            return show(menu, menu->sell_count ? C3_SHOP_MENU_SELL_LIST : C3_SHOP_MENU_CHOICE, text);
        }
        return C3_SHOP_EVENT_NONE;
    }
    return C3_SHOP_EVENT_NONE;
}

static void draw_panel(C3BottomUi *ui, int gold, const char *text, int width)
{
    c3_ui_panel(ui, (320 - width) / 2, PANEL_Y, width, PANEL_H);
    c3_ui_text(ui, (320 - width) / 2 + 14, PANEL_Y + 8, width - 24, 1, text, gold ? C3_UI_GOLD : C3_UI_WHITE);
}

void c3_shop_menu_draw(const C3ShopMenu *menu, const C3GameState *state, C3BottomUi *ui, const char *title)
{
    C3GameSummary summary;
    char value[32], money[32], line[96];
    if (!menu || !ui) return;
    if (!state || !c3_game_state_summary(state, &summary)) summary.money = 0;
    snprintf(money, sizeof money, "Money P%lu", (unsigned long)summary.money);
    c3_ui_header(ui, title ? title : "SHOP", money);
    C3ShopMenuStage stage = menu->stage == C3_SHOP_MENU_MESSAGE ? menu->after_message : menu->stage;
    int panel = menu->stage == C3_SHOP_MENU_MESSAGE || menu->stage == C3_SHOP_MENU_QUANTITY ||
                menu->stage == C3_SHOP_MENU_SELL_QUANTITY;
    unsigned rows = panel ? PANEL_ROWS : ROWS;
    if (stage == C3_SHOP_MENU_CHOICE) {
        static const char *const choices[] = {"BUY", "SELL", "QUIT"};
        for (unsigned i = 0; i < 3; ++i) c3_ui_row(ui, LIST_Y + (int)i * ROW_H, choices[i], i == menu->choice, 0);
    } else if (stage == C3_SHOP_MENU_SELL_LIST || stage == C3_SHOP_MENU_SELL_QUANTITY) {
        for (unsigned row = 0; row < rows && menu->sell_top + row < menu->sell_count; ++row) {
            const C3ShopSellEntry *e = &menu->sell[menu->sell_top + row];
            if (e->price) snprintf(value, sizeof value, "x%u  P%u", e->count, e->price);
            else snprintf(value, sizeof value, "x%u  ---", e->count);
            c3_ui_row_value(ui, LIST_Y + (int)row * ROW_H, e->name, value, menu->sell_top + row == menu->sell_cursor, !e->price);
        }
    } else {
        for (unsigned row = 0; row < rows && menu->top + row < menu->count; ++row) {
            unsigned i = menu->top + row;
            snprintf(value, sizeof value, "P%u", menu->prices[i]);
            c3_ui_row_value(ui, LIST_Y + (int)row * ROW_H, menu->names[i], value, i == menu->cursor, 0);
        }
    }
    if (menu->stage == C3_SHOP_MENU_QUANTITY) {
        snprintf(line, sizeof line, "x%u   P%lu", menu->quantity, (unsigned long)menu->prices[menu->cursor] * menu->quantity);
        draw_panel(ui, 1, line, 240);
        c3_ui_footer(ui, "Up/Down: 1   Left/Right: 10   A: buy   B: back");
    } else if (menu->stage == C3_SHOP_MENU_SELL_QUANTITY) {
        snprintf(line, sizeof line, "x%u   P%lu", menu->quantity, (unsigned long)menu->sell[menu->sell_cursor].price * menu->quantity);
        draw_panel(ui, 1, line, 240);
        c3_ui_footer(ui, "Up/Down: 1   Left/Right: 10   A: sell   B: back");
    } else if (menu->stage == C3_SHOP_MENU_MESSAGE) {
        draw_panel(ui, 0, menu->message, 280);
        c3_ui_footer(ui, "A / B: continue");
    } else if (stage == C3_SHOP_MENU_CHOICE) {
        c3_ui_footer(ui, "A: choose   B: leave");
    } else if (stage == C3_SHOP_MENU_SELL_LIST) {
        c3_ui_footer(ui, "A: sell   B: back");
    } else {
        c3_ui_footer(ui, "A: buy   B: back");
    }
}
