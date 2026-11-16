#include "colosseum3ds/link_trade.h"
#include "colosseum3ds/game_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for trading (src/link_trade.c) over the loopback transport
 * with packet loss: the swap of two 312-byte records with friendship 70,
 * the original's refusals, cancelling, a changed offer needing a new yes,
 * the partner leaving, and that nothing but the traded slot changes.
 *
 * PRIVATE, optional: with <newgame.c3g> <save A> <save B> <out.gci> it
 * trades between the two saves' parties, then performs the explicit Save
 * of side A into <out.gci> (never into a given save) and reloads it to
 * check the received Pokemon is there. The given saves are only read. */

static char error[256];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
#include "battle_pack_fixture.h"

typedef struct { C3LinkLoopback net; C3LinkLoopbackEnd end[3]; C3LinkTransport t[3]; C3LinkSession s[3]; uint32_t now; } Net;
static Net *net_new(uint32_t seed, unsigned loss)
{
    Net *w = calloc(1, sizeof *w);
    w->now = 1000;
    c3_link_loopback_init(&w->net, seed);
    w->net.drop_percent = loss; w->net.reorder_percent = loss;
    for (uint8_t i = 1; i <= 2; ++i) c3_link_loopback_transport(&w->end[i], &w->net, i, &w->t[i]);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "WES", "b", w->now) && c3_link_join(&w->s[2], &w->t[2], "RUI", "b", w->now));
    for (unsigned k = 0; k < 2000 && (w->s[2].state != C3_LINK_LOBBY || w->s[1].player_count < 2); ++k) {
        w->now += 10; c3_link_update(&w->s[1], w->now); c3_link_update(&w->s[2], w->now);
    }
    CHECK(w->s[2].state == C3_LINK_LOBBY);
    C3LinkMessage m; while (c3_link_poll(&w->s[1], &m)) {} while (c3_link_poll(&w->s[2], &m)) {}
    return w;
}
static void pump(Net *w) { w->now += 10; c3_link_update(&w->s[1], w->now); c3_link_update(&w->s[2], w->now); }

static void trade_between(const uint8_t a_party[6][0x138], const uint8_t b_party[6][0x138], unsigned a_slot, unsigned b_slot,
                          const char *label, uint8_t a_out[6][0x138])
{
    Net *w = net_new(55, 20);
    uint8_t a[6][0x138], b[6][0x138], a0[6][0x138], b0[6][0x138];
    memcpy(a, a_party, sizeof a); memcpy(b, b_party, sizeof b); memcpy(a0, a, sizeof a); memcpy(b0, b, sizeof b);
    C3LinkTrade ta, tb;
    CHECK(c3_link_trade_begin(&ta, &w->s[1], 1) && c3_link_trade_begin(&tb, &w->s[2], 0));
    CHECK(c3_link_trade_offer(&ta, (const uint8_t (*)[0x138])a, a_slot));
    CHECK(c3_link_trade_offer(&tb, (const uint8_t (*)[0x138])b, b_slot));
    int confirmed_a = 0, confirmed_b = 0;
    for (unsigned k = 0; k < 20000 && (ta.phase != C3_LT_DONE || tb.phase != C3_LT_DONE); ++k) {
        pump(w);
        c3_link_trade_update(&ta, a); c3_link_trade_update(&tb, b);
        if (ta.have_offer && !confirmed_a) confirmed_a = c3_link_trade_confirm(&ta);
        if (tb.have_offer && !confirmed_b) confirmed_b = c3_link_trade_confirm(&tb);
    }
    CHECK(ta.phase == C3_LT_DONE && tb.phase == C3_LT_DONE);
    /* The swap, friendship 70 on both received Pokemon, nothing else changed. */
    uint8_t expect[0x138];
    memcpy(expect, b0[b_slot], 0x138); p16(expect + 0xb0, 70); CHECK(!memcmp(a[a_slot], expect, 0x138));
    memcpy(expect, a0[a_slot], 0x138); p16(expect + 0xb0, 70); CHECK(!memcmp(b[b_slot], expect, 0x138));
    for (unsigned i = 0; i < 6; ++i) if (i != a_slot) CHECK(!memcmp(a[i], a0[i], 0x138));
    for (unsigned i = 0; i < 6; ++i) if (i != b_slot) CHECK(!memcmp(b[i], b0[i], 0x138));
    printf("%s: slot %u species %u <-> slot %u species %u, friendship 70, %u datagrams dropped\n", label, a_slot,
           g16(b0[b_slot]), b_slot, g16(a0[a_slot]), (unsigned)w->net.dropped);
    if (a_out) memcpy(a_out, a, sizeof a);
    free(w);
}

static void flows(void)
{
    uint8_t a[6][0x138], b[6][0x138], a0[6][0x138];
    make_party(a, 1, 30, 3, 0); make_party(b, 2, 20, 3, 0); memcpy(a0, a, sizeof a);
    /* A changed offer clears the partner's yes; then a cancel ends it with nothing swapped. */
    Net *w = net_new(9, 10);
    C3LinkTrade ta, tb;
    CHECK(c3_link_trade_begin(&ta, &w->s[1], 1) && c3_link_trade_begin(&tb, &w->s[2], 0));
    CHECK(c3_link_trade_offer(&ta, (const uint8_t (*)[0x138])a, 0) && c3_link_trade_offer(&tb, (const uint8_t (*)[0x138])b, 0));
    for (unsigned k = 0; k < 3000 && !(ta.have_offer && tb.have_offer); ++k) { pump(w); c3_link_trade_update(&ta, a); c3_link_trade_update(&tb, b); }
    CHECK(c3_link_trade_confirm(&tb));
    for (unsigned k = 0; k < 3000 && !ta.partner_confirmed; ++k) { pump(w); c3_link_trade_update(&ta, a); c3_link_trade_update(&tb, b); }
    CHECK(ta.partner_confirmed);
    CHECK(c3_link_trade_offer(&ta, (const uint8_t (*)[0x138])a, 1)); /* A changes its mind */
    for (unsigned k = 0; k < 3000 && tb.phase != C3_LT_OFFERED; ++k) { pump(w); c3_link_trade_update(&ta, a); c3_link_trade_update(&tb, b); }
    CHECK(tb.phase == C3_LT_OFFERED && !tb.i_confirmed && tb.their_slot == 1);
    c3_link_trade_cancel(&tb);
    for (unsigned k = 0; k < 3000 && ta.phase != C3_LT_CANCELLED; ++k) { pump(w); c3_link_trade_update(&ta, a); c3_link_trade_update(&tb, b); }
    CHECK(ta.phase == C3_LT_CANCELLED && !memcmp(a, a0, sizeof a));
    free(w);
    /* The partner leaves before confirming. */
    w = net_new(10, 0);
    CHECK(c3_link_trade_begin(&ta, &w->s[1], 1));
    CHECK(c3_link_trade_offer(&ta, (const uint8_t (*)[0x138])a, 0));
    c3_link_close(&w->s[2]);
    for (unsigned k = 0; k < 100 && ta.phase != C3_LT_FAILED; ++k) { pump(w); c3_link_trade_update(&ta, a); }
    CHECK(ta.phase == C3_LT_FAILED && strstr(ta.error, "left") && !memcmp(a, a0, sizeof a));
    free(w);
    puts("link trade flows: a changed offer needs a new yes, cancel, partner leaves; nothing swapped");
}

static void refusals(void)
{
    uint8_t a[6][0x138], b[6][0x138];
    make_party(a, 1, 30, 3, 0); make_party(b, 2, 20, 2, 0);
    p16(a[1] + 0x88, 0x0D); /* a held item travels with its Pokemon */
    trade_between((const uint8_t (*)[0x138])a, (const uint8_t (*)[0x138])b, 1, 0, "link trade", NULL);
    make_party(a, 1, 30, 1, 0);
    CHECK(!c3_link_trade_check((const uint8_t (*)[0x138])a, 0, error, sizeof error) && strstr(error, "last"));
    make_party(a, 1, 30, 2, 0); a[1][0xcb] = 1;
    CHECK(!c3_link_trade_check((const uint8_t (*)[0x138])a, 1, error, sizeof error) && strstr(error, "Egg"));
    make_party(a, 1, 30, 2, 1);
    CHECK(!c3_link_trade_check((const uint8_t (*)[0x138])a, 0, error, sizeof error) && strstr(error, "Shadow"));
    make_party(a, 1, 30, 2, 0); p16(a[0] + 0x88, 0x200);
    CHECK(!c3_link_trade_check((const uint8_t (*)[0x138])a, 0, error, sizeof error) && strstr(error, "item"));
    CHECK(!c3_link_trade_check((const uint8_t (*)[0x138])a, 4, error, sizeof error));
    /* A purified Pokemon (heart gauge below zero) may be traded. */
    make_party(a, 1, 30, 2, 1); p32(a[0] + 0xdc, 0xFFFFFFFFu);
    CHECK(c3_link_trade_check((const uint8_t (*)[0x138])a, 0, error, sizeof error));
    puts("link trade refusals: last healthy Pokemon, Egg, Shadow, item, empty slot; purified allowed");
}

static void private_part(const char *newgame, const char *save_a, const char *save_b, const char *out)
{
    C3GameState ga = {0}, gb = {0}, check = {0};
    char gerror[C3_GAME_ERROR_SIZE] = {0};
    uint8_t pa[6][0x138], pb[6][0x138], after[6][0x138], reread[0x138];
    CHECK(c3_game_state_load(&ga, save_a, newgame, gerror));
    CHECK(c3_game_state_load(&gb, save_b, newgame, gerror));
    for (unsigned i = 0; i < 6; ++i) { CHECK(c3_game_state_pokemon(&ga, i, pa[i])); CHECK(c3_game_state_pokemon(&gb, i, pb[i])); }
    int sa = -1, sb = -1;
    for (unsigned i = 0; i < 6 && sa < 0; ++i) if (c3_link_trade_check((const uint8_t (*)[0x138])pa, i, NULL, 0)) sa = (int)i;
    for (unsigned i = 0; i < 6 && sb < 0; ++i) if (c3_link_trade_check((const uint8_t (*)[0x138])pb, i, NULL, 0)) sb = (int)i;
    CHECK(sa >= 0 && sb >= 0);
    trade_between((const uint8_t (*)[0x138])pa, (const uint8_t (*)[0x138])pb, (unsigned)sa, (unsigned)sb, "private trade (saves)", after);
    /* The explicit Save: the traded slot into side A's state, written to out. */
    CHECK(c3_game_state_trade_pokemon(&ga, (unsigned)sa, after[sa]));
    CHECK(c3_game_state_save(&ga, out, gerror));
    CHECK(c3_game_state_load(&check, out, newgame, gerror));
    CHECK(c3_game_state_pokemon(&check, (unsigned)sa, reread));
    snprintf(error, sizeof error, "%s", gerror);
    CHECK(g16(reread) == g16(pb[sb]) && g16(reread + 0xb0) == 70);
    printf("private trade save: %s reloads with species %u in slot %d, friendship 70\n", out, g16(reread), sa);
    c3_game_state_free(&ga); c3_game_state_free(&gb); c3_game_state_free(&check);
}

int main(int argc, char **argv)
{
    refusals();
    flows();
    if (argc >= 5) private_part(argv[1], argv[2], argv[3], argv[4]);
    else puts("link trade: private part skipped (needs <newgame.c3g> <save A> <save B> <out.gci>)");
    puts("link trade: all checks passed");
    return 0;
}
