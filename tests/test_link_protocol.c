#include "colosseum3ds/link_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for the link session (src/link_protocol.c) over the loopback
 * transport: the version/build handshake, four players, a fifth refused,
 * reliable ordered delivery of fragmented messages through loss,
 * duplication and reordering, relaying between clients, a clean BYE and a
 * timed-out player. No original data. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct { C3LinkLoopback net; C3LinkLoopbackEnd end[5]; C3LinkTransport t[5]; C3LinkSession s[5]; uint32_t now; } World;

static World *world_new(uint32_t seed)
{
    World *w = calloc(1, sizeof *w);
    c3_link_loopback_init(&w->net, seed);
    for (uint8_t n = 1; n <= 4; ++n) c3_link_loopback_transport(&w->end[n], &w->net, n, &w->t[n]);
    w->now = 1000;
    return w;
}
static void step(World *w, unsigned ms, unsigned nodes)
{
    for (unsigned k = 0; k < ms; k += 10) {
        w->now += 10;
        for (unsigned n = 1; n <= nodes; ++n) c3_link_update(&w->s[n], w->now);
    }
}
static int drain(C3LinkSession *s, uint8_t type, C3LinkMessage *out)
{
    static C3LinkMessage m;
    while (c3_link_poll(s, &m)) if (m.type == type) { if (out) *out = m; return 1; }
    return 0;
}

static void handshake_and_roster(void)
{
    World *w = world_new(1);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "WES", "build-a", w->now));
    for (unsigned n = 2; n <= 4; ++n) { char name[8]; snprintf(name, sizeof name, "P%u", n); CHECK(c3_link_join(&w->s[n], &w->t[n], name, "build-a", w->now)); }
    step(w, 500, 4);
    CHECK(w->s[1].player_count == 4);
    for (unsigned n = 2; n <= 4; ++n) {
        CHECK(w->s[n].state == C3_LINK_LOBBY);
        CHECK(w->s[n].player_count == 4);
        CHECK(!strcmp(w->s[n].names[0], "WES"));
    }
    /* Join order sets the player index. */
    CHECK(w->s[2].self_player == 1 && w->s[3].self_player == 2 && w->s[4].self_player == 3);
    CHECK(!strcmp(w->s[1].names[3], "P4"));
    free(w);
    printf("link handshake: 4 players, roster on every node\n");
}

static void refusals(void)
{
    World *w = world_new(2);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "build-a", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "OLD", "build-b", w->now));
    step(w, 400, 2);
    CHECK(w->s[2].state == C3_LINK_FAILED && w->s[2].reason == C3_LINK_REASON_BUILD);
    CHECK(w->s[1].player_count == 1);
    /* A matching build joins; then the host locks and a late joiner is refused. */
    CHECK(c3_link_join(&w->s[3], &w->t[3], "OK", "build-a", w->now));
    step(w, 400, 3);
    CHECK(w->s[3].state == C3_LINK_LOBBY);
    c3_link_lock(&w->s[1]);
    CHECK(c3_link_join(&w->s[4], &w->t[4], "LATE", "build-a", w->now));
    step(w, 400, 4);
    CHECK(w->s[4].state == C3_LINK_FAILED && w->s[4].reason == C3_LINK_REASON_LOCKED);
    CHECK(w->s[3].locked);
    free(w);
    /* Version mismatch: forge a HELLO with version 99 straight onto the wire. */
    w = world_new(3);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "build-a", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "V99", "build-a", w->now));
    w->s[2].peers[0].out[0].data[14] = 0; w->s[2].peers[0].out[0].data[15] = 99;
    step(w, 400, 2);
    CHECK(w->s[2].state == C3_LINK_FAILED && w->s[2].reason == C3_LINK_REASON_VERSION);
    free(w);
    printf("link refusals: build, version and locked session\n");
}

static void fifth_player(void)
{
    /* Four nodes fill the session; a fifth endpoint on node 4 is impossible
     * with the loopback numbering, so check FULL by filling slots by hand. */
    World *w = world_new(4);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "b", w->now));
    w->s[1].present[1] = w->s[1].present[2] = w->s[1].present[3] = 1;
    CHECK(c3_link_join(&w->s[2], &w->t[2], "FIVE", "b", w->now));
    step(w, 400, 2);
    CHECK(w->s[2].state == C3_LINK_FAILED && w->s[2].reason == C3_LINK_REASON_FULL);
    free(w);
    printf("link refusals: a full session\n");
}

static void reliable_under_loss(void)
{
    World *w = world_new(5);
    w->net.drop_percent = 25; w->net.duplicate_percent = 10; w->net.reorder_percent = 20;
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "b", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "A", "b", w->now));
    CHECK(c3_link_join(&w->s[3], &w->t[3], "B", "b", w->now));
    step(w, 3000, 3);
    CHECK(w->s[2].state == C3_LINK_LOBBY && w->s[3].state == C3_LINK_LOBBY);
    while (drain(&w->s[1], 0xEE, NULL) || drain(&w->s[2], 0xEE, NULL) || drain(&w->s[3], 0xEE, NULL)) {}
    /* 40 messages of varying size, some spanning several fragments, each way. */
    uint8_t payload[C3_LINK_MAX_MESSAGE];
    unsigned relay_sent = 0, sent_up = 0, sent_down = 0, got_up = 0, got_down = 0, got_relay = 0;
    C3LinkMessage m;
    for (unsigned round = 0; round < 4000 && (got_up < 40 || got_down < 40 || got_relay < 40); ++round) {
        if (sent_up < 40) {
            size_t n = 1 + (sent_up * 97u) % C3_LINK_MAX_MESSAGE;
            for (size_t i = 0; i < n; ++i) payload[i] = (uint8_t)(sent_up + i);
            if (c3_link_send(&w->s[2], 0, C3_LINK_MSG_USER, payload, n)) ++sent_up;
        }
        if (sent_down < 40) {
            size_t n = 1 + (sent_down * 131u) % C3_LINK_MAX_MESSAGE;
            for (size_t i = 0; i < n; ++i) payload[i] = (uint8_t)(sent_down * 3 + i);
            /* Host broadcast: both clients; client 3 also relays one to client 2 below. */
            if (c3_link_send(&w->s[1], C3_LINK_BROADCAST, C3_LINK_MSG_USER + 1, payload, n)) ++sent_down;
        }
        if (relay_sent < 40 && round % 2 == 0) {
            uint8_t r[3] = {(uint8_t)relay_sent, 0xAB, 0xCD};
            if (c3_link_send(&w->s[3], 1, C3_LINK_MSG_USER + 2, r, 3)) ++relay_sent;
        }
        step(w, 10, 3);
        while (c3_link_poll(&w->s[1], &m)) if (m.type == C3_LINK_MSG_USER) {
            size_t n = 1 + (got_up * 97u) % C3_LINK_MAX_MESSAGE;
            CHECK(m.size == n && m.from == 1);
            for (size_t i = 0; i < n; ++i) CHECK(m.data[i] == (uint8_t)(got_up + i));
            ++got_up;
        }
        while (c3_link_poll(&w->s[2], &m)) {
            if (m.type == C3_LINK_MSG_USER + 1) {
                size_t n = 1 + (got_down * 131u) % C3_LINK_MAX_MESSAGE;
                CHECK(m.size == n && m.from == 0);
                for (size_t i = 0; i < n; ++i) CHECK(m.data[i] == (uint8_t)(got_down * 3 + i));
                ++got_down;
            } else if (m.type == C3_LINK_MSG_USER + 2) {
                CHECK(m.from == 2 && m.to == 1 && m.size == 3 && m.data[0] == (uint8_t)got_relay);
                ++got_relay;
            }
        }
        while (c3_link_poll(&w->s[3], &m)) {}
    }
    CHECK(got_up == 40 && got_down == 40 && got_relay == 40);
    CHECK(w->net.dropped > 0 && w->s[1].stats_resent > 0);
    printf("link reliability: 120 messages in order through %u dropped datagrams (%u resent by host)\n",
           (unsigned)w->net.dropped, (unsigned)w->s[1].stats_resent);
    free(w);
}

static void disconnects(void)
{
    World *w = world_new(6);
    C3LinkMessage m;
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "b", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "A", "b", w->now));
    CHECK(c3_link_join(&w->s[3], &w->t[3], "B", "b", w->now));
    step(w, 500, 3);
    while (c3_link_poll(&w->s[1], &m)) {}
    while (c3_link_poll(&w->s[2], &m)) {}
    /* Player 2 (node 3) says goodbye. */
    c3_link_close(&w->s[3]);
    step(w, 200, 2);
    CHECK(drain(&w->s[1], C3_LINK_MSG_LEFT, &m) && m.from == 2);
    CHECK(drain(&w->s[2], C3_LINK_MSG_LEFT, &m) && m.from == 2);
    CHECK(w->s[1].player_count == 2 && w->s[2].player_count == 2);
    /* Player 1 (node 2) vanishes without a word: a timeout. */
    w->net.alive[2] = 0;
    step(w, C3_LINK_TIMEOUT_MS + 500, 1);
    CHECK(drain(&w->s[1], C3_LINK_MSG_LEFT, &m) && m.from == 1);
    CHECK(w->s[1].player_count == 1);
    free(w);
    /* The host vanishes: the client fails with a timeout. */
    w = world_new(7);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "b", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "A", "b", w->now));
    step(w, 500, 2);
    w->net.alive[1] = 0;
    for (unsigned k = 0; k < C3_LINK_TIMEOUT_MS + 500; k += 10) { w->now += 10; c3_link_update(&w->s[2], w->now); }
    CHECK(w->s[2].state == C3_LINK_FAILED && w->s[2].reason == C3_LINK_REASON_TIMEOUT);
    CHECK(drain(&w->s[2], C3_LINK_MSG_LEFT, &m) && m.from == 0);
    /* A host BYE is reported as the host leaving. */
    free(w);
    w = world_new(8);
    CHECK(c3_link_host(&w->s[1], &w->t[1], "HOST", "b", w->now));
    CHECK(c3_link_join(&w->s[2], &w->t[2], "A", "b", w->now));
    step(w, 500, 2);
    c3_link_close(&w->s[1]);
    for (unsigned k = 0; k < 100; k += 10) { w->now += 10; c3_link_update(&w->s[2], w->now); }
    CHECK(w->s[2].state == C3_LINK_FAILED && w->s[2].reason == C3_LINK_REASON_HOST_LEFT);
    free(w);
    printf("link disconnects: BYE, a silent client and a lost host\n");
}

int main(void)
{
    CHECK(c3_link_hash(C3_LINK_HASH_INIT, "a", 1) == 0xE40C292Cu);
    handshake_and_roster();
    refusals();
    fifth_player();
    reliable_under_loss();
    disconnects();
    printf("link protocol: all checks passed\n");
    return 0;
}
