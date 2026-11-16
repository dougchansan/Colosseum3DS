#include "colosseum3ds/link_protocol.h"
#include <string.h>

/* Wire format, big-endian (docs/link-play.md "Wire format"):
 *   0 'C' 'L'   2 kind   3 flags   4 seq   6 ack
 *   DATA only: 8 type  9 origin  10 dest  11 reserved  12 payload length  14 payload
 * kind 1 DATA (reliable, ordered), 2 ACK, 3 PING, 4 REJECT (reason in byte 8,
 * unreliable), 5 BYE (unreliable, sent three times).
 * DATA flags: bit 0 first fragment of a message, bit 1 last fragment. */
enum { K_DATA = 1, K_ACK, K_PING, K_REJECT, K_BYE };
enum { F_FIRST = 1, F_LAST = 2 };
#define HEADER 14u
#define PEERS (C3_LINK_MAX_PLAYERS - 1u)

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

uint32_t c3_link_hash(uint32_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

const char *c3_link_reason_text(C3LinkReason r)
{
    switch (r) {
    case C3_LINK_REASON_NONE: return "no error";
    case C3_LINK_REASON_VERSION: return "the other system runs a different link protocol version";
    case C3_LINK_REASON_BUILD: return "the other system runs a different build of Colosseum3DS";
    case C3_LINK_REASON_FULL: return "the session already has four players";
    case C3_LINK_REASON_LOCKED: return "the session has already started";
    case C3_LINK_REASON_TIMEOUT: return "the connection timed out";
    case C3_LINK_REASON_HOST_LEFT: return "the host left the session";
    case C3_LINK_REASON_TRANSPORT: return "local wireless stopped";
    case C3_LINK_REASON_PROTOCOL: return "a damaged link message arrived";
    }
    return "unknown link error";
}

/* Control datagrams are padded to 32 bytes: under Azahar's UDS a 10-byte
 * frame makes the receiver's udsPullPacket fail and is lost
 * (docs/link-play.md, "Azahar notes"). */
#define RAW_SIZE 32u
static void raw(C3LinkSession *s, uint8_t node, unsigned kind, unsigned ack, unsigned extra)
{
    uint8_t p[RAW_SIZE] = {0};
    p[0] = 'C'; p[1] = 'L'; p[2] = (uint8_t)kind; p[3] = 0; put16(p + 4, 0); put16(p + 6, ack);
    p[8] = (uint8_t)extra; p[9] = 0;
    s->transport.send(s->transport.ctx, node, p, sizeof p);
}

static C3LinkPeer *peer_by_node(C3LinkSession *s, uint8_t node)
{
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].node == node) return &s->peers[i];
    return NULL;
}
static C3LinkPeer *peer_by_player(C3LinkSession *s, uint8_t player)
{
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].welcomed && s->peers[i].player == player) return &s->peers[i];
    return NULL;
}
static void peer_reset(C3LinkPeer *p, uint8_t node, uint8_t player, uint32_t now)
{
    memset(p, 0, sizeof *p);
    p->used = 1; p->node = node; p->player = player; p->last_heard_ms = now; p->last_sent_ms = now;
}

static int deliver(C3LinkSession *s, uint8_t type, uint8_t from, uint8_t to, const void *data, size_t size)
{
    if (s->queue_count >= C3_LINK_QUEUE || size > C3_LINK_MAX_MESSAGE) return 0;
    C3LinkMessage *m = &s->queue[(s->queue_head + s->queue_count++) % C3_LINK_QUEUE];
    m->type = type; m->from = from; m->to = to; m->size = (uint16_t)size;
    if (size) memcpy(m->data, data, size);
    return 1;
}

/* Queue every fragment of one message to one peer, all or nothing. */
static int queue_to_peer(C3LinkSession *s, C3LinkPeer *p, uint8_t type, uint8_t origin, uint8_t dest,
                         const uint8_t *data, size_t size)
{
    unsigned fragments = size ? (unsigned)((size + C3_LINK_FRAGMENT - 1) / C3_LINK_FRAGMENT) : 1u;
    (void)s;
    if (size > C3_LINK_MAX_MESSAGE) return 0;
    if ((uint16_t)(p->send_seq - p->send_base) + fragments > C3_LINK_WINDOW) return 0;
    for (unsigned f = 0; f < fragments; ++f) {
        size_t at = (size_t)f * C3_LINK_FRAGMENT, n = size - at < C3_LINK_FRAGMENT ? size - at : C3_LINK_FRAGMENT;
        if (!size) n = 0;
        C3LinkPacketSlot *slot = &p->out[p->send_seq % C3_LINK_WINDOW];
        uint8_t *b = slot->data;
        b[0] = 'C'; b[1] = 'L'; b[2] = K_DATA;
        b[3] = (uint8_t)((f == 0 ? F_FIRST : 0) | (f + 1 == fragments ? F_LAST : 0));
        put16(b + 4, p->send_seq); put16(b + 6, p->recv_next);
        b[8] = type; b[9] = origin; b[10] = dest; b[11] = 0; put16(b + 12, (unsigned)n);
        if (n) memcpy(b + HEADER, data + at, n);
        slot->used = 1; slot->seq = p->send_seq; slot->size = (uint16_t)(HEADER + n); slot->sent_ms = 0;
        ++p->send_seq;
    }
    return 1;
}

static void transmit_pending(C3LinkSession *s, C3LinkPeer *p)
{
    for (uint16_t seq = p->send_base; seq != p->send_seq; ++seq) {
        C3LinkPacketSlot *slot = &p->out[seq % C3_LINK_WINDOW];
        if (!slot->used) continue;
        if (slot->sent_ms && s->now_ms - slot->sent_ms < C3_LINK_RESEND_MS) continue;
        if (slot->sent_ms) ++s->stats_resent;
        put16(slot->data + 6, p->recv_next);
        s->transport.send(s->transport.ctx, p->node, slot->data, slot->size);
        slot->sent_ms = s->now_ms ? s->now_ms : 1u;
        p->last_sent_ms = s->now_ms; p->ack_due = 0;
        ++s->stats_sent;
    }
}

static void broadcast_roster(C3LinkSession *s)
{
    uint8_t m[1 + C3_LINK_MAX_PLAYERS * (1 + C3_LINK_NAME_SIZE)];
    m[0] = (uint8_t)C3_LINK_MAX_PLAYERS;
    for (unsigned i = 0; i < C3_LINK_MAX_PLAYERS; ++i) {
        m[1 + i * (1 + C3_LINK_NAME_SIZE)] = s->present[i];
        memcpy(m + 2 + i * (1 + C3_LINK_NAME_SIZE), s->names[i], C3_LINK_NAME_SIZE);
    }
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].welcomed)
        queue_to_peer(s, &s->peers[i], C3_LINK_MSG_ROSTER, 0, s->peers[i].player, m, sizeof m);
    deliver(s, C3_LINK_MSG_ROSTER, 0, 0, m, sizeof m);
}

static void count_players(C3LinkSession *s)
{
    s->player_count = 0;
    for (unsigned i = 0; i < C3_LINK_MAX_PLAYERS; ++i) s->player_count += s->present[i] ? 1u : 0u;
}

static void drop_peer(C3LinkSession *s, C3LinkPeer *p, C3LinkReason why)
{
    if (!s->is_host) {
        if (s->state != C3_LINK_FAILED && s->state != C3_LINK_CLOSED) {
            s->state = C3_LINK_FAILED; s->reason = why;
            deliver(s, C3_LINK_MSG_LEFT, 0, s->self_player, NULL, 0);
        }
        p->used = 0;
        return;
    }
    uint8_t player = p->player; int welcomed = p->welcomed;
    p->used = 0;
    if (!welcomed) return;
    s->present[player] = 0; memset(s->names[player], 0, C3_LINK_NAME_SIZE);
    count_players(s);
    uint8_t who = player;
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].welcomed)
        queue_to_peer(s, &s->peers[i], C3_LINK_MSG_LEFT, player, s->peers[i].player, &who, 1);
    deliver(s, C3_LINK_MSG_LEFT, player, 0, &who, 1);
    broadcast_roster(s);
}

static void handle_hello(C3LinkSession *s, C3LinkPeer *p, const uint8_t *d, size_t n)
{
    C3LinkReason why = C3_LINK_REASON_NONE;
    if (n < 2 + C3_LINK_BUILD_SIZE + C3_LINK_NAME_SIZE) why = C3_LINK_REASON_PROTOCOL;
    else if (get16(d) != C3_LINK_PROTOCOL_VERSION) why = C3_LINK_REASON_VERSION;
    else if (memcmp(d + 2, s->build, C3_LINK_BUILD_SIZE) != 0) why = C3_LINK_REASON_BUILD;
    else if (s->locked) why = C3_LINK_REASON_LOCKED;
    if (p->welcomed) return; /* a retransmitted HELLO */
    unsigned player = 0;
    if (!why) { for (player = 1; player < C3_LINK_MAX_PLAYERS && s->present[player]; ++player) {}
        if (player >= C3_LINK_MAX_PLAYERS) why = C3_LINK_REASON_FULL; }
    if (why) {
        raw(s, p->node, K_REJECT, 0, why);
        p->used = 0;
        return;
    }
    p->player = (uint8_t)player; p->welcomed = 1;
    s->present[player] = 1;
    memcpy(s->names[player], d + 2 + C3_LINK_BUILD_SIZE, C3_LINK_NAME_SIZE);
    s->names[player][C3_LINK_NAME_SIZE - 1] = 0;
    count_players(s);
    uint8_t w[1 + C3_LINK_BUILD_SIZE];
    w[0] = (uint8_t)player; memcpy(w + 1, s->build, C3_LINK_BUILD_SIZE);
    queue_to_peer(s, p, C3_LINK_MSG_WELCOME, 0, (uint8_t)player, w, sizeof w);
    deliver(s, C3_LINK_MSG_JOINED, (uint8_t)player, 0, NULL, 0);
    broadcast_roster(s);
}

/* A whole message arrived from a peer. Returns 0 when it could not be taken
 * yet (local queue full), so the packet is not acknowledged. */
static int message_complete(C3LinkSession *s, C3LinkPeer *p, uint8_t type, uint8_t origin, uint8_t dest,
                            const uint8_t *d, size_t n)
{
    if (s->is_host) {
        if (type == C3_LINK_MSG_HELLO) { handle_hello(s, p, d, n); return 1; }
        if (!p->welcomed) return 1;
        origin = p->player; /* clients cannot speak for anyone else */
        if (dest != 0 && dest != C3_LINK_BROADCAST) {
            C3LinkPeer *q = peer_by_player(s, dest);
            if (!q) return 1; /* addressed to a player who left */
            return queue_to_peer(s, q, type, origin, dest, d, n);
        }
        if (dest == C3_LINK_BROADCAST) {
            if (s->queue_count >= C3_LINK_QUEUE) return 0;
            for (unsigned i = 0; i < PEERS; ++i)
                if (&s->peers[i] != p && s->peers[i].used && s->peers[i].welcomed)
                    queue_to_peer(s, &s->peers[i], type, origin, dest, d, n);
        }
        return deliver(s, type, origin, dest, d, n);
    }
    switch (type) {
    case C3_LINK_MSG_WELCOME:
        if (n >= 1 + C3_LINK_BUILD_SIZE && d[0] && d[0] < C3_LINK_MAX_PLAYERS) {
            s->self_player = d[0]; memcpy(s->remote_build, d + 1, C3_LINK_BUILD_SIZE);
            s->state = C3_LINK_LOBBY;
        }
        return 1;
    case C3_LINK_MSG_ROSTER:
        if (n >= 1 + C3_LINK_MAX_PLAYERS * (1 + C3_LINK_NAME_SIZE)) {
            if (s->queue_count >= C3_LINK_QUEUE) return 0;
            for (unsigned i = 0; i < C3_LINK_MAX_PLAYERS; ++i) {
                s->present[i] = d[1 + i * (1 + C3_LINK_NAME_SIZE)];
                memcpy(s->names[i], d + 2 + i * (1 + C3_LINK_NAME_SIZE), C3_LINK_NAME_SIZE);
                s->names[i][C3_LINK_NAME_SIZE - 1] = 0;
            }
            count_players(s);
        }
        return deliver(s, type, origin, dest, d, n);
    case C3_LINK_MSG_LOCK: s->locked = 1; return 1;
    default: return deliver(s, type, origin, dest, d, n);
    }
}

static int accept_data(C3LinkSession *s, C3LinkPeer *p, const uint8_t *b, size_t size)
{
    unsigned flags = b[3], n = get16(b + 12);
    if (HEADER + n > size) return -1; /* transports may pad a datagram */
    if (flags & F_FIRST) { p->asm_active = 1; p->asm_size = 0; p->asm_type = b[8]; p->asm_from = b[9]; p->asm_to = b[10]; }
    if (!p->asm_active || p->asm_size + n > C3_LINK_MAX_MESSAGE) return -1;
    if (flags & F_LAST) {
        uint8_t tmp[C3_LINK_MAX_MESSAGE];
        memcpy(tmp, p->asm_buf, p->asm_size); memcpy(tmp + p->asm_size, b + HEADER, n);
        if (!message_complete(s, p, p->asm_type, p->asm_from, p->asm_to, tmp, p->asm_size + n)) return 0;
        p->asm_active = 0;
        return 1;
    }
    memcpy(p->asm_buf + p->asm_size, b + HEADER, n); p->asm_size = (uint16_t)(p->asm_size + n);
    return 1;
}

static void release_acked(C3LinkPeer *p, unsigned ack)
{
    uint16_t a = (uint16_t)ack;
    if ((uint16_t)(a - p->send_base) > (uint16_t)(p->send_seq - p->send_base)) return; /* stale */
    while (p->send_base != a) { p->out[p->send_base % C3_LINK_WINDOW].used = 0; ++p->send_base; }
}

static void receive(C3LinkSession *s, uint8_t node, const uint8_t *b, size_t size)
{
    if (size < 8 || b[0] != 'C' || b[1] != 'L') { ++s->stats_dropped; return; }
    unsigned kind = b[2];
    C3LinkPeer *p = peer_by_node(s, node);
    if (!p) {
        if (!s->is_host || kind != K_DATA) return;
        for (unsigned i = 0; i < PEERS; ++i) if (!s->peers[i].used) { p = &s->peers[i]; break; }
        if (!p) { raw(s, node, K_REJECT, 0, s->locked ? C3_LINK_REASON_LOCKED : C3_LINK_REASON_FULL); return; }
        peer_reset(p, node, 0xFF, s->now_ms);
    }
    p->last_heard_ms = s->now_ms;
    ++s->stats_received;
    if (kind == K_REJECT) {
        if (!s->is_host) { s->state = C3_LINK_FAILED; s->reason = size > 8 ? (C3LinkReason)b[8] : C3_LINK_REASON_PROTOCOL; p->used = 0; }
        return;
    }
    if (kind == K_BYE) { drop_peer(s, p, C3_LINK_REASON_HOST_LEFT); return; }
    release_acked(p, get16(b + 6));
    if (kind != K_DATA) return;
    if (size < HEADER) { ++s->stats_dropped; return; }
    uint16_t seq = (uint16_t)get16(b + 4);
    if (s->is_host && !p->welcomed && p->recv_next == 0 && seq >= C3_LINK_WINDOW) {
        /* Mid-stream data from a client this host already dropped. */
        raw(s, node, K_REJECT, 0, C3_LINK_REASON_TIMEOUT); p->used = 0; return;
    }
    uint16_t ahead = (uint16_t)(seq - p->recv_next);
    p->ack_due = 1;
    if (ahead >= C3_LINK_WINDOW) return; /* duplicate of an old packet, or far future */
    C3LinkPacketSlot *slot = &p->in[seq % C3_LINK_WINDOW];
    if (!slot->used || slot->seq != seq) {
        if (size > C3_LINK_PACKET_MAX) { ++s->stats_dropped; return; }
        slot->used = 1; slot->seq = seq; slot->size = (uint16_t)size; memcpy(slot->data, b, size);
    }
    /* Consume every in-order packet. */
    for (;;) {
        C3LinkPacketSlot *next = &p->in[p->recv_next % C3_LINK_WINDOW];
        if (!next->used || next->seq != p->recv_next) break;
        int r = accept_data(s, p, next->data, next->size);
        if (r == 0) break; /* local queue full: keep it for later */
        next->used = 0; ++p->recv_next;
        if (r < 0) { if (s->is_host) drop_peer(s, p, C3_LINK_REASON_PROTOCOL); else { s->state = C3_LINK_FAILED; s->reason = C3_LINK_REASON_PROTOCOL; } return; }
        if (!p->used) return; /* rejected during HELLO */
    }
}

static void session_init(C3LinkSession *s, const C3LinkTransport *t, const char *name, const char *build, uint32_t now)
{
    memset(s, 0, sizeof *s);
    s->transport = *t; s->now_ms = now; s->started_ms = now;
    strncpy(s->build, build ? build : "", C3_LINK_BUILD_SIZE - 1);
    strncpy(s->names[0], name ? name : "", C3_LINK_NAME_SIZE - 1);
}

int c3_link_host(C3LinkSession *s, const C3LinkTransport *t, const char *name, const char *build, uint32_t now)
{
    if (!s || !t || !t->send || !t->recv) return 0;
    session_init(s, t, name, build, now);
    s->is_host = 1; s->state = C3_LINK_LOBBY; s->self_player = 0; s->present[0] = 1; s->player_count = 1;
    return 1;
}

int c3_link_join(C3LinkSession *s, const C3LinkTransport *t, const char *name, const char *build, uint32_t now)
{
    if (!s || !t || !t->send || !t->recv) return 0;
    char own[C3_LINK_NAME_SIZE] = {0};
    strncpy(own, name ? name : "", C3_LINK_NAME_SIZE - 1);
    session_init(s, t, name, build, now);
    memset(s->names[0], 0, C3_LINK_NAME_SIZE);
    s->state = C3_LINK_JOINING; s->self_player = 0xFF;
    peer_reset(&s->peers[0], C3_LINK_HOST_NODE, 0, now);
    s->peers[0].welcomed = 1;
    uint8_t hello[2 + C3_LINK_BUILD_SIZE + C3_LINK_NAME_SIZE] = {0};
    put16(hello, C3_LINK_PROTOCOL_VERSION);
    memcpy(hello + 2, s->build, C3_LINK_BUILD_SIZE);
    memcpy(hello + 2 + C3_LINK_BUILD_SIZE, own, C3_LINK_NAME_SIZE);
    return queue_to_peer(s, &s->peers[0], C3_LINK_MSG_HELLO, 0xFF, 0, hello, sizeof hello);
}

void c3_link_update(C3LinkSession *s, uint32_t now)
{
    if (!s || s->state == C3_LINK_OFF || s->state == C3_LINK_CLOSED) return;
    s->now_ms = now;
    uint8_t buffer[C3_LINK_PACKET_MAX + 64];
    for (unsigned guard = 0; guard < 256; ++guard) {
        uint8_t node = 0;
        int n = s->transport.recv(s->transport.ctx, &node, buffer, sizeof buffer);
        if (n < 0) {
            if (s->state != C3_LINK_FAILED) { s->state = C3_LINK_FAILED; s->reason = C3_LINK_REASON_TRANSPORT; }
            return;
        }
        if (n == 0) break;
        receive(s, node, buffer, (size_t)n);
    }
    if (s->state == C3_LINK_FAILED) return;
    for (unsigned i = 0; i < PEERS; ++i) {
        C3LinkPeer *p = &s->peers[i];
        if (!p->used) continue;
        if (now - p->last_heard_ms > C3_LINK_TIMEOUT_MS) {
            drop_peer(s, p, s->is_host ? C3_LINK_REASON_TIMEOUT : C3_LINK_REASON_TIMEOUT);
            continue;
        }
        transmit_pending(s, p);
        if (p->ack_due || now - p->last_sent_ms >= C3_LINK_PING_MS) {
            raw(s, p->node, p->ack_due ? K_ACK : K_PING, p->recv_next, 0);
            p->ack_due = 0; p->last_sent_ms = now;
        }
    }
}

int c3_link_send(C3LinkSession *s, uint8_t to, uint8_t type, const void *data, size_t size)
{
    if (!s || (s->state != C3_LINK_LOBBY) || size > C3_LINK_MAX_MESSAGE || (size && !data)) return 0;
    if (to == s->self_player) return 0;
    if (!s->is_host) return queue_to_peer(s, &s->peers[0], type, s->self_player, to, data, size);
    if (to != C3_LINK_BROADCAST) { C3LinkPeer *p = peer_by_player(s, to); return p && queue_to_peer(s, p, type, 0, to, data, size); }
    unsigned need = size ? (unsigned)((size + C3_LINK_FRAGMENT - 1) / C3_LINK_FRAGMENT) : 1u;
    for (unsigned i = 0; i < PEERS; ++i) {
        C3LinkPeer *p = &s->peers[i];
        if (p->used && p->welcomed && (uint16_t)(p->send_seq - p->send_base) + need > C3_LINK_WINDOW) return 0;
    }
    for (unsigned i = 0; i < PEERS; ++i)
        if (s->peers[i].used && s->peers[i].welcomed) queue_to_peer(s, &s->peers[i], type, 0, to, data, size);
    return 1;
}

int c3_link_poll(C3LinkSession *s, C3LinkMessage *out)
{
    if (!s || !out || !s->queue_count) return 0;
    *out = s->queue[s->queue_head];
    s->queue_head = (s->queue_head + 1) % C3_LINK_QUEUE; --s->queue_count;
    return 1;
}

void c3_link_lock(C3LinkSession *s)
{
    if (!s || !s->is_host) return;
    s->locked = 1;
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].welcomed)
        queue_to_peer(s, &s->peers[i], C3_LINK_MSG_LOCK, 0, s->peers[i].player, NULL, 0);
}

int c3_link_flushed(const C3LinkSession *s)
{
    if (!s) return 1;
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used && s->peers[i].send_base != s->peers[i].send_seq) return 0;
    return 1;
}

void c3_link_close(C3LinkSession *s)
{
    if (!s || s->state == C3_LINK_OFF || s->state == C3_LINK_CLOSED) return;
    for (unsigned i = 0; i < PEERS; ++i) if (s->peers[i].used)
        for (unsigned k = 0; k < 3; ++k) raw(s, s->peers[i].node, K_BYE, s->peers[i].recv_next, 0);
    s->state = C3_LINK_CLOSED;
}

/* ---- loopback transport -------------------------------------------------- */

static unsigned lb_rand(C3LinkLoopback *net, unsigned n)
{
    net->seed = net->seed * 1103515245u + 12345u;
    return n ? (net->seed >> 16) % n : 0;
}

void c3_link_loopback_init(C3LinkLoopback *net, uint32_t seed)
{
    memset(net, 0, sizeof *net);
    net->seed = seed;
    for (unsigned i = 1; i <= C3_LINK_MAX_PLAYERS; ++i) net->alive[i] = 1;
}

static void lb_push(C3LinkLoopback *net, uint8_t from, uint8_t to, const void *data, size_t size)
{
    if (!net->alive[to] || net->count >= C3_LINK_LOOPBACK_QUEUE || size > C3_LINK_PACKET_MAX) { ++net->dropped; return; }
    if (net->drop_percent && lb_rand(net, 100) < net->drop_percent) { ++net->dropped; return; }
    unsigned copies = net->duplicate_percent && lb_rand(net, 100) < net->duplicate_percent ? 2u : 1u;
    for (unsigned c = 0; c < copies && net->count < C3_LINK_LOOPBACK_QUEUE; ++c) {
        unsigned at = net->count;
        if (net->reorder_percent && net->count && lb_rand(net, 100) < net->reorder_percent) {
            at = lb_rand(net, net->count);
            memmove(&net->packets[at + 1], &net->packets[at], (net->count - at) * sizeof net->packets[0]);
        }
        C3LinkLoopbackPacket *p = &net->packets[at];
        p->from = from; p->to = to; p->size = (uint16_t)size; memcpy(p->data, data, size);
        ++net->count;
    }
}

static int lb_send(void *ctx, uint8_t node, const void *data, size_t size)
{
    C3LinkLoopbackEnd *e = ctx;
    if (!e->net->alive[e->node]) return 0;
    if (node == C3_LINK_BROADCAST) { for (uint8_t n = 1; n <= C3_LINK_MAX_PLAYERS; ++n) if (n != e->node) lb_push(e->net, e->node, n, data, size); }
    else if (node >= 1 && node <= C3_LINK_MAX_PLAYERS) lb_push(e->net, e->node, node, data, size);
    return 1;
}

static int lb_recv(void *ctx, uint8_t *node, void *data, size_t capacity)
{
    C3LinkLoopbackEnd *e = ctx;
    C3LinkLoopback *net = e->net;
    if (!net->alive[e->node]) return -1;
    for (unsigned i = 0; i < net->count; ++i) {
        C3LinkLoopbackPacket *p = &net->packets[i];
        if (p->to != e->node) continue;
        int n = p->size <= capacity ? p->size : 0;
        if (n) { memcpy(data, p->data, (size_t)n); *node = p->from; }
        memmove(&net->packets[i], &net->packets[i + 1], (net->count - i - 1) * sizeof net->packets[0]);
        --net->count; ++net->delivered;
        return n;
    }
    return 0;
}

void c3_link_loopback_transport(C3LinkLoopbackEnd *end, C3LinkLoopback *net, uint8_t node, C3LinkTransport *out)
{
    end->net = net; end->node = node;
    out->ctx = end; out->send = lb_send; out->recv = lb_recv;
}
