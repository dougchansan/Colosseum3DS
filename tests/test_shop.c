/* Shop stock, prices, purchases and the menu (native 19). Synthetic data. */
#define main c3_shop_game_state_fixture_main
#include "test_game_state.c"
#undef main
#include "colosseum3ds/shop.h"
#include "colosseum3ds/inventory.h"

static void leput32(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void leput16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint8_t assets_memory[4096];
static unsigned entries, asset_end;
static uint8_t *entry(unsigned kind, unsigned key, unsigned size)
{
    uint8_t *e = assets_memory + 64 + entries++ * 16;
    leput32(e, kind); leput32(e + 4, key); leput32(e + 8, asset_end); leput32(e + 12, size);
    uint8_t *d = assets_memory + asset_end; asset_end += size; return d;
}
/* Items 1 (price 300) and 2 (price 200), both medicine (kind 2, pocket 0);
 * item 3 has no price; item 4 is a key item (kind 5, pocket 1). */
static C3PdaAssets shop_assets(void)
{
    memset(assets_memory, 0, sizeof assets_memory); entries = 0; asset_end = 64 + 8 * 16;
    uint8_t *d;
    d = entry(7, 1, 40); d[0] = 2; d[6] = 0x01; d[7] = 0x2c;
    d = entry(7, 2, 40); d[0] = 2; d[7] = 200;
    d = entry(7, 3, 40); d[0] = 2;
    d = entry(7, 4, 40); d[0] = 5; d[1] = 1; d[7] = 100;   /* a key item with a price */
    return (C3PdaAssets){assets_memory, asset_end, entries};
}
/* C3H1: shop 0 (kind 0) sells 1, 2, 3; shop 1 is a coupon counter. */
static size_t shop_pack(uint8_t *p)
{
    const unsigned list[] = {1, 2, 3, 0, 1, 0};
    size_t size = 20 + 2 * 4 + 6 * 2;
    memset(p, 0, size); memcpy(p, "C3H1", 4); leput32(p + 4, 1); leput32(p + 8, (unsigned)size);
    leput32(p + 12, 2); leput32(p + 16, 6);
    p[20] = 0; p[21] = 0; leput16(p + 22, 0);
    p[24] = 1; p[25] = 1; leput16(p + 26, 4);
    for (unsigned i = 0; i < 6; ++i) leput16(p + 28 + i * 2, list[i]);
    return size;
}
static unsigned money(C3GameState *state) { C3GameSummary s; CHECK(c3_game_state_summary(state, &s)); return s.money; }
static unsigned held(C3GameState *state, unsigned item)
{
    const uint8_t *b = c3_game_state_body(state, NULL); unsigned total = 0;
    for (unsigned i = 0; i < c3_inventory_capacity(0); ++i)
        if (r16(b + 0x7f0 + i * 4) == item) total += r16(b + 0x7f2 + i * 4);
    return total;
}

int main(void)
{
    char directory[] = "/tmp/c3-shop-XXXXXX", path[1024], error[192];
    CHECK(mkdtemp(directory) != NULL);
    uint8_t *game = synthetic_pack();
    snprintf(path, sizeof path, "%s/game.c3g", directory); write_file(path, game, PACK_SIZE);
    C3GameState state = {0}; CHECK(c3_game_state_new(&state, "WES", 7, path, error));
    C3PdaAssets assets = shop_assets();
    uint8_t pack[64]; size_t size = shop_pack(pack);
    C3ShopTable table = {0}; uint16_t items[C3_SHOP_MAX_STOCK];

    /* Pack checks: header, extent, offsets and terminators. */
    CHECK(c3_shop_table_open(&table, pack, size) && table.shops == 2 && table.words == 6);
    CHECK(!c3_shop_table_open(&table, pack, size - 2));
    { uint8_t bad[64]; memcpy(bad, pack, size); leput16(bad + 22, 6); CHECK(!c3_shop_table_open(&table, bad, size)); }
    { uint8_t bad[64]; memcpy(bad, pack, size); leput16(bad + 28 + 5 * 2, 7); leput16(bad + 28 + 3 * 2, 7); CHECK(!c3_shop_table_open(&table, bad, size)); }
    CHECK(c3_shop_table_open(&table, pack, size));
    CHECK(c3_shop_stock(&table, 0, items) == 3 && items[0] == 1 && items[1] == 2 && items[2] == 3);
    CHECK(c3_shop_stock(&table, 1, items) == 0);   /* coupon counter: not sold here */
    CHECK(c3_shop_stock(&table, 2, items) == 0);   /* unknown shop */
    CHECK(c3_shop_price(&assets, 1) == 300 && c3_shop_price(&assets, 2) == 200 && !c3_shop_price(&assets, 3) && !c3_shop_price(&assets, 9));

    /* Money: a checked signed change within 0..9,999,999. */
    unsigned start = money(&state);
    CHECK(c3_game_state_add_money(&state, 1000) && money(&state) == start + 1000);
    CHECK(!c3_game_state_add_money(&state, -(int64_t)(start + 1001)) && money(&state) == start + 1000);
    CHECK(!c3_game_state_add_money(&state, 9999999) && money(&state) == start + 1000);
    CHECK(c3_game_state_add_money(&state, -(int64_t)(start + 1000)) && money(&state) == 0);
    CHECK(c3_game_state_add_money(&state, 1000));

    /* Purchases: the money is checked, then the bag; a refusal changes nothing. */
    memset((uint8_t *)c3_game_state_body(&state, NULL) + 0x7f0, 0, c3_inventory_capacity(0) * 4);
    CHECK(c3_shop_buy(&state, &assets, 1, 3) == C3_SHOP_BOUGHT && money(&state) == 100 && held(&state, 1) == 3);
    CHECK(c3_shop_buy(&state, &assets, 2, 1) == C3_SHOP_NO_MONEY && money(&state) == 100 && held(&state, 2) == 0);
    CHECK(c3_shop_buy(&state, &assets, 3, 1) == C3_SHOP_INVALID && c3_shop_buy(&state, &assets, 1, 0) == C3_SHOP_INVALID);
    CHECK(c3_game_state_add_money(&state, 5000));
    {   /* Every pocket-0 slot holds another item: no room for item 2. */
        uint8_t *b = (uint8_t *)c3_game_state_body(&state, NULL);
        for (unsigned i = 0; i < c3_inventory_capacity(0); ++i) { w16(b + 0x7f0 + i * 4, 100 + i); w16(b + 0x7f2 + i * 4, 1); }
        unsigned before = money(&state);
        CHECK(c3_shop_buy(&state, &assets, 2, 1) == C3_SHOP_BAG_FULL && money(&state) == before && held(&state, 2) == 0);
        memset(b + 0x7f0, 0, c3_inventory_capacity(0) * 4);
    }

    /* Menu: BUY / SELL / QUIT first; buying with a quantity within what the
     * money covers, a message, and B stepping back a level each time. */
    C3ShopMenu menu;
    CHECK(c3_shop_menu_open(&menu, &table, &assets, 0) && menu.count == 2 && menu.stage == C3_SHOP_MENU_CHOICE);
    CHECK(menu.items[0] == 1 && menu.prices[0] == 300 && menu.items[1] == 2);   /* item 3 has no price */
    CHECK(!c3_shop_menu_open(&menu, &table, &assets, 1));
    CHECK(c3_shop_menu_open(&menu, &table, &assets, 0));
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 8) == C3_SHOP_EVENT_CHANGED && menu.choice == 2);   /* Up wraps to QUIT */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.choice == 0);   /* Down wraps to BUY */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_LIST);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 8) == C3_SHOP_EVENT_CHANGED && menu.cursor == 1);   /* Up wraps */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.cursor == 0);   /* Down wraps */
    unsigned cash = money(&state);                                    /* 5100: 17 of item 1 at 300 */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_QUANTITY && menu.quantity == 1);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.quantity == cash / 300); /* 1 - 1 wraps to the most */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 8) == C3_SHOP_EVENT_CHANGED && menu.quantity == 1);          /* most + 1 wraps to 1 */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 2) == C3_SHOP_EVENT_CHANGED && menu.quantity == 11);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 2) == C3_SHOP_EVENT_CHANGED && menu.quantity == cash / 300); /* clamps to the most */
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_LIST);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 8) == C3_SHOP_EVENT_CHANGED && menu.quantity == 2);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_MESSAGE);
    CHECK(money(&state) == cash - 600 && held(&state, 1) == 2 && strstr(menu.message, "bought 2"));
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_LIST);
    {   unsigned all = money(&state);
        CHECK(c3_game_state_add_money(&state, -(int64_t)all));
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_MESSAGE && strstr(menu.message, "enough money"));
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_LIST);
        CHECK(c3_game_state_add_money(&state, (int64_t)all)); }
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_CHOICE);
    CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CLOSE);

    /* Selling (fn_800181C4): price / 2 each; price 0 and key items refuse;
     * the money limit refuses; a refusal changes nothing. */
    CHECK(c3_shop_sell_price(&assets, 1) == 150 && c3_shop_sell_price(&assets, 2) == 100 && !c3_shop_sell_price(&assets, 3));
    {
        uint8_t *b = (uint8_t *)c3_game_state_body(&state, NULL);
        memset(b + 0x7f0, 0, c3_inventory_capacity(0) * 4);
        w16(b + 0x7f0, 1); w16(b + 0x7f2, 5);        /* 5 x item 1 */
        w16(b + 0x7f4, 3); w16(b + 0x7f6, 2);        /* 2 x item 3 (no price) */
        w16(b + 0x840, 4); w16(b + 0x842, 1);        /* key item 4 in pocket 1 */
        unsigned before = money(&state);
        CHECK(c3_shop_sell(&state, &assets, 0, 0, 2) == C3_SHOP_SOLD && money(&state) == before + 300 && held(&state, 1) == 3);
        CHECK(c3_shop_sell(&state, &assets, 0, 0, 4) == C3_SHOP_INVALID && held(&state, 1) == 3);   /* more than held */
        CHECK(c3_shop_sell(&state, &assets, 0, 1, 1) == C3_SHOP_CANNOT_SELL && held(&state, 3) == 2);
        CHECK(c3_shop_sell(&state, &assets, 1, 0, 1) == C3_SHOP_CANNOT_SELL && money(&state) == before + 300);
        CHECK(c3_game_state_add_money(&state, (int64_t)(9999999u - money(&state)) - 100));
        unsigned full = money(&state);
        CHECK(c3_shop_sell(&state, &assets, 0, 0, 1) == C3_SHOP_MONEY_FULL && money(&state) == full && held(&state, 1) == 3);
        CHECK(c3_game_state_add_money(&state, -(int64_t)full + 1000));

        /* Through the menu: SELL lists the bag (not the key pocket), marks
         * the unsellable item, sells up to the stack and updates the list. */
        CHECK(c3_shop_menu_open(&menu, &table, &assets, 0));
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.choice == 1);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_SELL_LIST);
        CHECK(menu.sell_count == 2 && menu.sell[0].item == 1 && menu.sell[0].count == 3 && menu.sell[0].price == 150);
        CHECK(menu.sell[1].item == 3 && menu.sell[1].price == 0);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.sell_cursor == 1);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_MESSAGE && strstr(menu.message, "cannot be sold"));
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_SELL_LIST);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 8) == C3_SHOP_EVENT_CHANGED && menu.sell_cursor == 0);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_SELL_QUANTITY);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.quantity == 3);   /* wraps to the stack */
        unsigned cash2 = money(&state);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_MESSAGE);
        CHECK(strstr(menu.message, "sold 3") && money(&state) == cash2 + 450 && held(&state, 1) == 0);
        CHECK(menu.sell_count == 1 && menu.sell[0].item == 3);                     /* the sold stack is gone */
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_SELL_LIST);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_CHOICE);
        memset(b + 0x7f0, 0, c3_inventory_capacity(0) * 4);                         /* an empty sellable bag */
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CHANGED && strstr(menu.message, "nothing to sell"));
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x200) == C3_SHOP_EVENT_CHANGED && menu.stage == C3_SHOP_MENU_CHOICE);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 4) == C3_SHOP_EVENT_CHANGED && menu.choice == 2);
        CHECK(c3_shop_menu_update(&menu, &state, &assets, 0x100) == C3_SHOP_EVENT_CLOSE);   /* QUIT */
    }
    c3_game_state_free(&state); free(game);
    printf("shop: %u checks passed\n", checks);
    return 0;
}
