#include "colosseum3ds/link_trade.h"
#include <stdio.h>
#include <string.h>

/* Trades (docs/link-play.md "Trading"): the original's checks from
 * fn_8002EE74 / fn_8002EA5C and the friendship it sets on completion. */

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int failf(char *e, size_t n, const char *m) { if (e && n) snprintf(e, n, "%s", m); return 0; }
static int is_shadow(const uint8_t *r) { return be16(r + 0xd8) && (int32_t)be32(r + 0xdc) >= 0; }
static int is_egg(const uint8_t *r) { return r[0xcb] != 0; }

/* ---- trading --------------------------------------------------------------- */

#define FRIENDSHIP_AFTER_TRADE 0x46u /* fn_80031B4C on completion */
#define IMPORTABLE_ITEM_LIMIT 0x15Du /* approximation of itemDataBiosCheckImportable */

int c3_link_trade_check(const uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot, char *e, size_t n)
{
    if (slot >= 6 || !be16(party[slot])) return failf(e, n, "There is no Pokemon in that slot.");
    const uint8_t *r = party[slot];
    if (is_egg(r)) return failf(e, n, "An Egg cannot be traded."); /* fn_8002EE74, message 0x43E1 */
    if (is_shadow(r)) return failf(e, n, "A Shadow Pokemon cannot be traded until it is purified.");
    if (be16(r + 0x88) >= IMPORTABLE_ITEM_LIMIT) return failf(e, n, "That Pokemon holds an item that cannot be traded."); /* 0x43DD */
    unsigned healthy = 0;
    for (unsigned i = 0; i < 6; ++i) if (i != slot && be16(party[i]) && !is_egg(party[i]) && be16(party[i] + 0x8a)) ++healthy;
    if (!healthy) return failf(e, n, "That is your last Pokemon able to battle."); /* fn_8002EA5C, 0x44E8 */
    return 1;
}

void c3_link_trade_apply(uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot, const uint8_t received[C3_GB_POKEMON_SIZE])
{
    if (slot >= 6) return;
    memcpy(party[slot], received, C3_GB_POKEMON_SIZE);
    put16(party[slot] + 0xb0, FRIENDSHIP_AFTER_TRADE);
}

int c3_link_trade_begin(C3LinkTrade *t, C3LinkSession *s, uint8_t partner)
{
    memset(t, 0, sizeof *t);
    t->session = s; t->partner = partner; t->phase = C3_LT_CHOOSE;
    return s && partner < C3_LINK_MAX_PLAYERS && partner != s->self_player;
}

int c3_link_trade_offer(C3LinkTrade *t, const uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot)
{
    if (t->phase != C3_LT_CHOOSE && t->phase != C3_LT_OFFERED) return 0;
    if (!c3_link_trade_check(party, slot, t->error, sizeof t->error)) return 0;
    uint8_t m[1 + C3_GB_POKEMON_SIZE];
    m[0] = (uint8_t)slot; memcpy(m + 1, party[slot], C3_GB_POKEMON_SIZE);
    if (!c3_link_send(t->session, t->partner, C3_LINK_MSG_TRADE_OFFER, m, sizeof m)) return failf(t->error, sizeof t->error, "The offer could not be sent.");
    memcpy(t->mine, party[slot], C3_GB_POKEMON_SIZE);
    t->my_slot = (uint8_t)slot; t->my_hash = c3_link_hash(C3_LINK_HASH_INIT, t->mine, C3_GB_POKEMON_SIZE);
    t->i_confirmed = 0; t->partner_confirmed = 0; t->phase = C3_LT_OFFERED;
    return 1;
}

int c3_link_trade_confirm(C3LinkTrade *t)
{
    if (t->phase != C3_LT_OFFERED || !t->have_offer) return 0;
    uint8_t m[8]; put32(m, t->my_hash); put32(m + 4, t->their_hash);
    if (!c3_link_send(t->session, t->partner, C3_LINK_MSG_TRADE_CONFIRM, m, sizeof m)) return 0;
    t->i_confirmed = 1; t->phase = C3_LT_CONFIRMED;
    return 1;
}

void c3_link_trade_cancel(C3LinkTrade *t)
{
    c3_link_send(t->session, t->partner, C3_LINK_MSG_TRADE_CANCEL, NULL, 0);
    t->phase = C3_LT_CANCELLED; t->i_confirmed = t->partner_confirmed = 0;
}

int c3_link_trade_update(C3LinkTrade *t, uint8_t party[6][C3_GB_POKEMON_SIZE])
{
    C3LinkMessage msg;
    if (t->phase == C3_LT_FAILED) return 0;
    if (t->session->state == C3_LINK_FAILED) { snprintf(t->error, sizeof t->error, "Link lost: %s.", c3_link_reason_text(t->session->reason)); t->phase = C3_LT_FAILED; return 0; }
    while (c3_link_poll(t->session, &msg)) {
        if (msg.type == C3_LINK_MSG_LEFT && msg.from == t->partner) { snprintf(t->error, sizeof t->error, "Your trading partner left."); t->phase = C3_LT_FAILED; return 0; }
        if (msg.from != t->partner) continue;
        if (msg.type == C3_LINK_MSG_TRADE_OFFER && msg.size == 1 + C3_GB_POKEMON_SIZE) {
            t->their_slot = msg.data[0]; memcpy(t->theirs, msg.data + 1, C3_GB_POKEMON_SIZE);
            t->their_hash = c3_link_hash(C3_LINK_HASH_INIT, t->theirs, C3_GB_POKEMON_SIZE);
            t->have_offer = 1; t->partner_confirmed = 0;
            if (t->phase == C3_LT_CONFIRMED) { t->i_confirmed = 0; t->phase = C3_LT_OFFERED; } /* a new offer needs a new yes */
        } else if (msg.type == C3_LINK_MSG_TRADE_CONFIRM && msg.size == 8) {
            /* The partner's view of the swap must match ours exactly. */
            if (be32(msg.data) == t->their_hash && be32(msg.data + 4) == t->my_hash) t->partner_confirmed = 1;
        } else if (msg.type == C3_LINK_MSG_TRADE_CANCEL) {
            t->phase = C3_LT_CANCELLED; t->have_offer = 0; t->i_confirmed = t->partner_confirmed = 0;
        }
    }
    if (t->phase == C3_LT_CONFIRMED && t->i_confirmed && t->partner_confirmed) {
        c3_link_trade_apply(party, t->my_slot, t->theirs);
        t->phase = C3_LT_DONE;
    }
    return 1;
}
