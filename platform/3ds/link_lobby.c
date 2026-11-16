/* The title menu's COLOSSEUM BATTLE and BATTLE NOW over local wireless
 * (docs/link-play.md). The original chose players by plugging in
 * controllers on one GameCube; here each player hosts or joins a session on
 * their own console, and the host picks the battle type (and, for
 * Colosseum, the rule preset) once everyone is in. */
#include "link_play.h"
#include "link_uds.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include <3ds.h>
#include <string.h>

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif

/* Trading (stage 5) provides the real one. */
__attribute__((weak)) int c3_link_play_trade(C3LinkSession *s, C3GameState *state, const char *save_path, FILE *trace,
                                             char *message, size_t capacity)
{ (void)s; (void)state; (void)save_path; (void)trace; snprintf(message, capacity, "Trading is not available in this build."); return 1; }
__attribute__((weak)) int c3_link_play_trade_available(void) { return 0; }

static const char *const rule_names[3] = {"FREE: Lv1-100, whole party", "Lv50: levels up to 50", "OPEN: Lv1-100, no repeats"};

static void draw(const char *title, const char *subtitle, const char *const *rows, unsigned count, unsigned selected,
                 unsigned disabled, const char *message, const char *footer)
{
    C3BottomUi *ui = c3_bottom_screen_begin();
    c3_ui_header(ui, title, subtitle);
    for (unsigned i = 0; i < count; ++i) c3_ui_row(ui, 46 + (int)i * 22, rows[i], selected == i, (int)(disabled >> i & 1u));
    if (message && message[0]) { c3_ui_panel(ui, 5, 160, 310, 50); c3_ui_text(ui, 13, 166, 294, 2, message, C3_UI_WHITE); }
    c3_ui_footer(ui, footer);
    c3_bottom_screen_present();
}

static void roster_text(const C3LinkSession *s, char *out, size_t n)
{
    size_t used = (size_t)snprintf(out, n, "Players %u:", s->player_count);
    for (unsigned p = 0; p < C3_LINK_MAX_PLAYERS && used < n; ++p)
        if (s->present[p]) used += (size_t)snprintf(out + used, n - used, " %s", s->names[p][0] ? s->names[p] : "?");
}

/* Wait for A (or B) on a notice; 0 when the application must exit. */
static int notice(const char *title, const char *text)
{
    C3N3dsInput input;
    c3_bottom_screen_notice(title, text, "A: OK");
    while (c3_n3ds_poll(&input)) if (input.pressed & (C3_PAD_A | C3_PAD_B)) return 1;
    return 0;
}

static C3LinkSession session;
int c3_link_lobby_single_session; /* probe: back to the caller after one game */
static int game_started;

/* host: 1 host, 0 join; trade: the session is for a trade. */
static int play(int mode, int host, int trade, C3GameState *state, const char *save_path, FILE *trace, char *message, size_t cap)
{
    C3LinkUds uds; C3LinkTransport t; C3N3dsInput input;
    char name[C3_LINK_NAME_SIZE] = "WES", error[160] = "", line[96];
    C3GameSummary summary;
    uint8_t party[6][C3_GB_POKEMON_SIZE];
    int have_party = state && state->impl;
    int result = 0;
    memset(party, 0, sizeof party);
    if (have_party && c3_game_state_summary(state, &summary) && summary.name[0]) snprintf(name, sizeof name, "%.*s", (int)sizeof name - 1, summary.name);
    for (unsigned i = 0; have_party && i < 6; ++i) c3_game_state_pokemon(state, i, party[i]);
    if ((mode == C3_LINK_MODE_COLOSSEUM || trade) && !have_party) { snprintf(message, cap, "This needs a Story Mode save."); return 0; }
    c3_bottom_screen_notice(trade ? "TRADE" : mode == C3_LINK_MODE_BATTLE_NOW ? "BATTLE NOW" : "COLOSSEUM BATTLE",
                            host ? "Opening a local wireless session..." : "Looking for a host nearby...", "");
    if (host ? !c3_link_uds_host(&uds, name, error, sizeof error) : !c3_link_uds_join(&uds, name, 20000, error, sizeof error)) {
        snprintf(message, cap, "%s", error); return 0;
    }
    c3_link_uds_transport(&uds, &t);
    if (host ? !c3_link_host(&session, &t, name, C3_BUILD_ID, (uint32_t)osGetTime()) : !c3_link_join(&session, &t, name, C3_BUILD_ID, (uint32_t)osGetTime())) {
        c3_link_uds_close(&uds); snprintf(message, cap, "The link session could not start."); return 0;
    }
    if (trace) { fprintf(trace, "link_lobby mode=%d host=%d trade=%d name=%s build=%s\n", mode, host, trade, name, C3_BUILD_ID); fflush(trace); }
    static const char *const types[3] = {"SINGLE BATTLE", "DOUBLE BATTLE", "MULTI BATTLE (4 PLAYERS)"};
    static const char *const trade_rows[1] = {"START THE TRADE"};
    unsigned selected = 0, rule = 0, started = 0;
    C3LinkMessage m;
    while (!started) {
        if (!c3_n3ds_poll(&input)) { result = 1; goto done; }
        c3_link_update(&session, (uint32_t)osGetTime());
        while (c3_link_poll(&session, &m)) {}
        if (session.state == C3_LINK_FAILED) { snprintf(message, cap, "Link ended: %s.", c3_link_reason_text(session.reason)); goto done; }
        roster_text(&session, line, sizeof line);
        if (host) {
            unsigned n = session.player_count, count = trade ? 1u : 3u;
            unsigned disabled = trade ? (n == 2 ? 0u : 1u) : (n == 2 ? 4u : n == 4 ? 3u : 7u);
            char sub[96];
            snprintf(sub, sizeof sub, "%s%s", mode == C3_LINK_MODE_COLOSSEUM && !trade ? "Rule (L/R): " : "",
                     mode == C3_LINK_MODE_COLOSSEUM && !trade ? rule_names[rule] : "Waiting for players to join");
            if (input.pressed & C3_PAD_UP) selected = (selected + count - 1) % count;
            if (input.pressed & C3_PAD_DOWN) selected = (selected + 1) % count;
            if (!trade && mode == C3_LINK_MODE_COLOSSEUM && (input.pressed & (C3_PAD_LEFT | C3_PAD_RIGHT)))
                rule = (rule + ((input.pressed & C3_PAD_LEFT) ? 2u : 1u)) % 3u;
            draw(trade ? "HOST A TRADE" : "HOST A BATTLE", sub, trade ? trade_rows : types, count, selected, disabled, line, "A: start  B: leave");
            if (input.pressed & C3_PAD_B) goto done;
            if ((input.pressed & C3_PAD_A) && !(disabled >> selected & 1u)) started = 1;
        } else {
            draw("JOINED", session.state == C3_LINK_LOBBY ? "Waiting for the host to start" : "Connecting...", NULL, 0, 0, 0, line, "B: leave");
            if (input.pressed & C3_PAD_B) goto done;
            if (session.state == C3_LINK_LOBBY && session.locked) started = 1;
        }
    }
    game_started = 1;
    if (host) {
        c3_link_lock(&session);
        for (unsigned k = 0; k < 300 && !c3_link_flushed(&session); ++k) if (!c3_link_play_pump(&session)) { result = 1; goto done; }
    }
    if (trade) result = c3_link_play_trade(&session, state, save_path, trace, message, cap) < 0;
    else {
        C3LinkBattleType type = (C3LinkBattleType)selected;
        int r = c3_link_play_battle(&session, (C3LinkMode)mode, type, rule, 0, have_party ? (const uint8_t (*)[C3_GB_POKEMON_SIZE])party : NULL,
                                    0, trace, message, cap);
        if (!r && !message[0]) snprintf(message, cap, "The battle is over.");
        if (!aptMainLoop()) result = 1;
    }
    /* Keep acknowledging briefly so the others can finish too. */
    for (unsigned k = 0; k < 120 && session.state == C3_LINK_LOBBY; ++k) if (!c3_link_play_pump(&session)) { result = 1; break; }
done:
    c3_link_close(&session);
    c3_link_uds_close(&uds);
    return result;
}

int c3_link_lobby_run(int mode, C3GameState *state, const char *save_path, FILE *trace)
{
    C3N3dsInput input;
    char message[192] = "";
    const char *battle_rows[5] = {"HOST A BATTLE", "JOIN A BATTLE", "HOST A TRADE", "JOIN A TRADE", "BACK"};
    int trades = mode == C3_LINK_MODE_COLOSSEUM && c3_link_play_trade_available();
    unsigned count = trades ? 5u : 3u, selected = 0;
    const char *rows[5];
    for (unsigned i = 0; i < count; ++i) rows[i] = battle_rows[trades || i < 2 ? i : 4];
    for (;;) {
        if (!c3_n3ds_poll(&input)) return 1;
        if (input.pressed & C3_PAD_UP) selected = (selected + count - 1) % count;
        if (input.pressed & C3_PAD_DOWN) selected = (selected + 1) % count;
        draw(mode == C3_LINK_MODE_BATTLE_NOW ? "BATTLE NOW" : "COLOSSEUM BATTLE", "Local wireless: one system per player",
             rows, count, selected, 0, message, "A: choose  B: back");
        if ((input.pressed & C3_PAD_B) || ((input.pressed & C3_PAD_A) && selected == count - 1)) return 0;
        if (input.pressed & C3_PAD_A) {
            message[0] = 0;
            int host = selected == 0 || selected == 2, trade = selected >= 2;
            if (play(mode, host, trade, state, save_path, trace, message, sizeof message)) return 1;
            if (trace) { fprintf(trace, "link_lobby_session_end message=%s\n", message); fflush(trace); }
            if (c3_link_lobby_single_session && game_started) return 0;
            if (message[0] && !notice(trade ? "TRADE" : "BATTLE", message)) return 1;
        }
    }
}
