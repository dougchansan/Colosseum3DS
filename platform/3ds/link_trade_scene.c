/* Trading over local wireless (docs/link-play.md "Trading"): the lobby's
 * trade screen and the probe's unattended trade. The original traded with
 * Ruby/Sapphire over the GBA cable and saved in step with it; here two
 * Colosseum3DS consoles swap 312-byte party records, and only the explicit
 * Save at the end writes the card. */
#include "link_play.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "battle_general_scene.h"
#include "colosseum3ds/link_trade.h"
#include <3ds.h>
#include <string.h>
#ifdef C3_LINK_PROBE
#include "link_probe.h"
#endif

int c3_link_play_trade_available(void) { return 1; }

static void label(const C3BattlePack *pack, const uint8_t *r, char *out, size_t n)
{
    C3GeneralMon mon;
    unsigned species = (unsigned)(r[0] << 8 | r[1]);
    if (!species) { snprintf(out, n, "-"); return; }
    if (pack && c3_general_mon_from_record(pack, r, 0, &mon)) snprintf(out, n, "%s Lv%u", mon.name, r[0x60]);
    else snprintf(out, n, "#%u Lv%u%s", species, r[0x60], r[0xcb] ? " (Egg)" : "");
}

static void draw_rows(const char *title, const char *subtitle, const char rows[][40], unsigned count, unsigned selected,
                      const char *message, const char *footer)
{
    C3BottomUi *ui = c3_bottom_screen_begin();
    c3_ui_header(ui, title, subtitle);
    for (unsigned i = 0; i < count; ++i) c3_ui_row(ui, 46 + (int)i * 18, rows[i], selected == i, 0);
    if (message && message[0]) { c3_ui_panel(ui, 5, 160, 310, 50); c3_ui_text(ui, 13, 166, 294, 2, message, C3_UI_WHITE); }
    c3_ui_footer(ui, footer);
    c3_bottom_screen_present();
}

/* Returns 0 normally, -1 when the application must exit. */
int c3_link_play_trade(C3LinkSession *s, C3GameState *state, const char *save_path, FILE *trace, char *message, size_t cap)
{
    static C3LinkTrade t;
    static C3BattlePack pack;
    char rows[6][40], error[192] = "", theirs[40] = "", text[256];
    uint8_t party[6][C3_GB_POKEMON_SIZE];
    C3N3dsInput input;
    unsigned selected = 0;
    int have_pack = c3_battle_pack_load(&pack, C3_BATTLE_PACK_PATH, error);
    for (unsigned i = 0; i < 6; ++i) c3_game_state_pokemon(state, i, party[i]);
    c3_link_trade_begin(&t, s, s->self_player == 0 ? 1 : 0);
    for (;;) {
        if (!c3_n3ds_poll(&input)) { if (have_pack) c3_battle_pack_free(&pack); return -1; }
        c3_link_update(s, (uint32_t)osGetTime());
        if (!c3_link_trade_update(&t, party)) { snprintf(message, cap, "%s", t.error); break; }
        if (t.phase == C3_LT_CANCELLED) { snprintf(message, cap, "The trade was cancelled."); break; }
        if (t.phase == C3_LT_DONE) break;
        for (unsigned i = 0; i < 6; ++i) label(have_pack ? &pack : NULL, party[i], rows[i], sizeof rows[i]);
        if (t.have_offer) label(have_pack ? &pack : NULL, t.theirs, theirs, sizeof theirs);
        if (t.phase == C3_LT_CHOOSE || (t.phase == C3_LT_OFFERED && !t.have_offer)) {
            if (input.pressed & C3_PAD_UP) selected = (selected + 5) % 6;
            if (input.pressed & C3_PAD_DOWN) selected = (selected + 1) % 6;
            if (input.pressed & C3_PAD_B) { c3_link_trade_cancel(&t); snprintf(message, cap, "You cancelled the trade."); break; }
            if ((input.pressed & C3_PAD_A) && !c3_link_trade_offer(&t, (const uint8_t (*)[C3_GB_POKEMON_SIZE])party, selected)) { /* t.error says why */ }
            snprintf(text, sizeof text, "%s", t.phase == C3_LT_OFFERED ? "Waiting for your partner's offer..." : t.error);
            draw_rows("TRADE", "Choose a Pokemon to offer", (const char (*)[40])rows, 6, selected, text, "A: offer  B: cancel");
        } else {
            snprintf(text, sizeof text, "%s offers %s for your %s.%s", s->names[t.partner], theirs, rows[t.my_slot],
                     t.phase == C3_LT_CONFIRMED ? " Waiting for your partner..." : " Trade?");
            draw_rows("TRADE", "Confirm the trade", NULL, 0, 0, text, t.phase == C3_LT_CONFIRMED ? "B: cancel" : "A: trade  B: cancel");
            if (input.pressed & C3_PAD_B) { c3_link_trade_cancel(&t); snprintf(message, cap, "You cancelled the trade."); break; }
            if ((input.pressed & C3_PAD_A) && t.phase == C3_LT_OFFERED) c3_link_trade_confirm(&t);
        }
    }
    if (t.phase == C3_LT_DONE) {
        /* Memory only until the player saves (the original saved in step with the GBA). */
        int ok = c3_game_state_trade_pokemon(state, t.my_slot, party[t.my_slot]);
        if (trace) { fprintf(trace, "link_trade_done slot=%u received=%u applied=%d\n", t.my_slot, (unsigned)(party[t.my_slot][0] << 8 | party[t.my_slot][1]), ok); fflush(trace); }
        label(have_pack ? &pack : NULL, party[t.my_slot], theirs, sizeof theirs);
        static const char save_rows[2][40] = {"SAVE THE GAME", "NOT NOW"};
        unsigned choice = 0;
        for (;;) {
            if (!c3_n3ds_poll(&input)) { if (have_pack) c3_battle_pack_free(&pack); return -1; }
            c3_link_update(s, (uint32_t)osGetTime());
            if (input.pressed & (C3_PAD_UP | C3_PAD_DOWN)) choice ^= 1u;
            snprintf(text, sizeof text, "You received %s. Save now? Until you save, the card still holds the old party.", theirs);
            draw_rows("TRADE COMPLETE", "", save_rows, 2, choice, text, "A: choose");
            if (input.pressed & C3_PAD_A) break;
        }
        if (choice == 0) {
            char gerror[C3_GAME_ERROR_SIZE] = "";
            c3_bottom_screen_notice("SAVING", "Saving the game... Don't turn off the power.", "");
            int saved = c3_game_state_save(state, save_path, gerror);
            snprintf(message, cap, saved ? "The trade was saved." : "Saving failed: %s", gerror);
            if (trace) { fprintf(trace, "link_trade_save path=%s ok=%d error=%s\n", save_path, saved, gerror); fflush(trace); }
        } else snprintf(message, cap, "Traded. The next Save will keep %s.", theirs);
    }
    if (have_pack) c3_battle_pack_free(&pack);
    return 0;
}

#ifdef C3_LINK_PROBE
/* Unattended: offer the configured slot (or the first tradeable one),
 * confirm the partner's offer, log both records. Never saves. */
int c3_link_probe_trade(C3LinkSession *s, FILE *log)
{
    static C3LinkTrade t;
    uint8_t party[6][C3_GB_POKEMON_SIZE];
    if (!c3_link_probe_load_party(party, log)) return 1;
    c3_link_trade_begin(&t, s, s->self_player == 0 ? 1 : 0);
    unsigned slot = c3_link_probe_config.slot;
    if (!c3_link_trade_check((const uint8_t (*)[C3_GB_POKEMON_SIZE])party, slot, NULL, 0))
        for (slot = 0; slot < 6 && !c3_link_trade_check((const uint8_t (*)[C3_GB_POKEMON_SIZE])party, slot, NULL, 0); ++slot) {}
    if (slot >= 6 || !c3_link_trade_offer(&t, (const uint8_t (*)[C3_GB_POKEMON_SIZE])party, slot)) {
        c3_link_play_log(log, "link_trade_refused error=%s", t.error); c3_link_trade_cancel(&t); return 1; }
    uint32_t before = c3_link_hash(C3_LINK_HASH_INIT, party, sizeof party);
    c3_link_play_log(log, "link_trade_offer slot=%u species=%u level=%u record_hash=%08lX", slot,
        (unsigned)(party[slot][0] << 8 | party[slot][1]), party[slot][0x60], (unsigned long)t.my_hash);
    uint32_t start = (uint32_t)osGetTime();
    int logged = 0;
    while (t.phase != C3_LT_DONE) {
        if (!c3_link_play_pump(s)) return 1;
        if (!c3_link_trade_update(&t, party) || t.phase == C3_LT_CANCELLED) { c3_link_play_log(log, "link_trade_failed phase=%d error=%s", t.phase, t.error); return 1; }
        if (t.have_offer && t.phase == C3_LT_OFFERED) {
            if (!logged) c3_link_play_log(log, "link_trade_partner_offer slot=%u species=%u level=%u record_hash=%08lX", t.their_slot,
                (unsigned)(t.theirs[0] << 8 | t.theirs[1]), t.theirs[0x60], (unsigned long)t.their_hash);
            logged = 1;
            c3_link_trade_confirm(&t);
        }
        if ((uint32_t)osGetTime() - start > 60000u) { c3_link_play_log(log, "link_trade_timeout phase=%d", t.phase); return 1; }
    }
    c3_link_play_log(log, "link_trade_done slot=%u received_species=%u level=%u friendship=%u party_hash_before=%08lX after=%08lX save_writes=0",
        slot, (unsigned)(party[slot][0] << 8 | party[slot][1]), party[slot][0x60], (unsigned)(party[slot][0xb0] << 8 | party[slot][0xb1]),
        (unsigned long)before, (unsigned long)c3_link_hash(C3_LINK_HASH_INIT, party, sizeof party));
    char text[160];
    snprintf(text, sizeof text, "Received species %u Lv%u in slot %u (friendship %u). Not saved.",
             (unsigned)(party[slot][0] << 8 | party[slot][1]), party[slot][0x60], slot, (unsigned)(party[slot][0xb0] << 8 | party[slot][0xb1]));
    c3_bottom_screen_notice("TRADE COMPLETE", text, "probe: no save written");
    for (unsigned k = 0; k < 90; ++k) if (!c3_link_play_pump(s)) break;
    c3_link_probe_shot(log, "trade");
    return 0;
}
#endif
