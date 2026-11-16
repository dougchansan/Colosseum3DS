#ifndef C3_LINK_PROTOCOL_H
#define C3_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Link session for local multiplayer (docs/link-play.md).
 *
 * The original game linked players through extra GameCube controllers on
 * one console and a GBA link cable. On 3DS every player is a separate
 * console (or Azahar instance) on local wireless, so the port needs a small
 * network layer the original never had. This module is that layer, kept
 * free of libctru so the host tests can run it over a loopback transport:
 *
 * - a star of up to four nodes: the host (player 0) and up to three
 *   clients; clients talk only to the host, which relays messages addressed
 *   to another player;
 * - reliable, ordered messages over an unreliable datagram transport
 *   (sequence numbers, cumulative acknowledgements, retransmission and an
 *   in-order receive window), with messages up to C3_LINK_MAX_MESSAGE bytes
 *   split into fragments;
 * - a HELLO/WELCOME handshake that checks the protocol version and the
 *   build ID, so two different builds never battle each other;
 * - keepalives, a timeout and a BYE, so a dropped player is reported once
 *   (C3_LINK_MSG_LEFT) and a lost host fails the session cleanly.
 *
 * Datagrams carry no game state streaming: battles stay in lockstep by
 * exchanging commands and one shared seed (src/link_battle.c). */

#define C3_LINK_MAX_PLAYERS 4u
#define C3_LINK_PROTOCOL_VERSION 1u
#define C3_LINK_BUILD_SIZE 16u
#define C3_LINK_NAME_SIZE 12u
#define C3_LINK_MAX_MESSAGE 2048u
#define C3_LINK_FRAGMENT 480u
#define C3_LINK_PACKET_MAX (C3_LINK_FRAGMENT + 16u)
#define C3_LINK_WINDOW 32u
#define C3_LINK_RESEND_MS 120u
#define C3_LINK_PING_MS 250u
#define C3_LINK_TIMEOUT_MS 6000u
#define C3_LINK_BROADCAST 0xFFu
#define C3_LINK_HOST_NODE 1u

/* Unreliable datagrams between transport nodes. On UDS the host is node 1
 * and clients are 2..; the loopback transport uses the same numbering.
 * send: node 0xFF broadcasts; returns 1 when queued, 0 on failure.
 * recv: one datagram; returns its size, 0 when none waits, -1 when the
 * transport itself is gone. */
typedef struct C3LinkTransport {
    void *ctx;
    int (*send)(void *ctx, uint8_t node, const void *data, size_t size);
    int (*recv)(void *ctx, uint8_t *node, void *data, size_t capacity);
} C3LinkTransport;

/* Message types below 16 belong to the session; games use 16 and up. */
enum {
    C3_LINK_MSG_HELLO = 1, C3_LINK_MSG_WELCOME, C3_LINK_MSG_REJECT, C3_LINK_MSG_ROSTER,
    C3_LINK_MSG_BYE, C3_LINK_MSG_LOCK,
    C3_LINK_MSG_JOINED = 8, /* delivered locally: a player joined (from = player) */
    C3_LINK_MSG_LEFT = 9,   /* delivered locally: a player left or timed out */
    C3_LINK_MSG_USER = 16
};

typedef enum {
    C3_LINK_OFF = 0, C3_LINK_JOINING, C3_LINK_LOBBY, C3_LINK_FAILED, C3_LINK_CLOSED
} C3LinkState;

typedef enum {
    C3_LINK_REASON_NONE = 0, C3_LINK_REASON_VERSION, C3_LINK_REASON_BUILD, C3_LINK_REASON_FULL,
    C3_LINK_REASON_LOCKED, C3_LINK_REASON_TIMEOUT, C3_LINK_REASON_HOST_LEFT, C3_LINK_REASON_TRANSPORT,
    C3_LINK_REASON_PROTOCOL
} C3LinkReason;

typedef struct C3LinkMessage {
    uint8_t type, from, to;
    uint16_t size;
    uint8_t data[C3_LINK_MAX_MESSAGE];
} C3LinkMessage;

typedef struct C3LinkPacketSlot {
    uint8_t used;
    uint16_t seq, size;
    uint32_t sent_ms;
    uint8_t data[C3_LINK_PACKET_MAX];
} C3LinkPacketSlot;

typedef struct C3LinkPeer {
    uint8_t used, node, player, welcomed;
    uint16_t send_seq, send_base, recv_next;
    uint32_t last_heard_ms, last_sent_ms;
    int ack_due;
    C3LinkPacketSlot out[C3_LINK_WINDOW], in[C3_LINK_WINDOW];
    uint8_t asm_type, asm_from, asm_to, asm_active;
    uint16_t asm_size;
    uint8_t asm_buf[C3_LINK_MAX_MESSAGE];
} C3LinkPeer;

#define C3_LINK_QUEUE 24u
typedef struct C3LinkSession {
    C3LinkTransport transport;
    C3LinkState state;
    C3LinkReason reason;
    int is_host, locked;
    uint8_t self_player;
    uint8_t player_count;                       /* roster size including the host */
    uint8_t present[C3_LINK_MAX_PLAYERS];       /* roster: player slot in use */
    char names[C3_LINK_MAX_PLAYERS][C3_LINK_NAME_SIZE];
    char build[C3_LINK_BUILD_SIZE], remote_build[C3_LINK_BUILD_SIZE];
    uint32_t now_ms, started_ms;
    C3LinkPeer peers[C3_LINK_MAX_PLAYERS - 1];  /* host: one per client; client: [0] is the host */
    unsigned queue_head, queue_count;
    C3LinkMessage queue[C3_LINK_QUEUE];
    uint32_t stats_sent, stats_resent, stats_received, stats_dropped;
} C3LinkSession;

/* Start a session. build: the C3_BUILD_ID string both sides must share. */
int c3_link_host(C3LinkSession *s, const C3LinkTransport *t, const char *name, const char *build, uint32_t now_ms);
int c3_link_join(C3LinkSession *s, const C3LinkTransport *t, const char *name, const char *build, uint32_t now_ms);
/* Pump the transport: receive, acknowledge, retransmit, keepalive, timeouts. */
void c3_link_update(C3LinkSession *s, uint32_t now_ms);
/* Queue a reliable message to a player (C3_LINK_BROADCAST: every other
 * player). Clients send to anyone through the host. 1 queued, 0 not. */
int c3_link_send(C3LinkSession *s, uint8_t to, uint8_t type, const void *data, size_t size);
/* Next delivered message, in order per sender. 1 when one was copied. */
int c3_link_poll(C3LinkSession *s, C3LinkMessage *out);
/* Host: refuse further joins (the game has started). */
void c3_link_lock(C3LinkSession *s);
/* Whether every queued message has been acknowledged by every peer. */
int c3_link_flushed(const C3LinkSession *s);
/* Send BYE and close. */
void c3_link_close(C3LinkSession *s);
const char *c3_link_reason_text(C3LinkReason reason);

/* In-memory transport for host tests: up to four endpoints exchanging
 * datagrams through one shared queue, with optional loss, duplication and
 * reordering driven by a fixed seed so failures are reproducible. */
#define C3_LINK_LOOPBACK_QUEUE 1024u
typedef struct C3LinkLoopbackPacket { uint8_t from, to; uint16_t size; uint8_t data[C3_LINK_PACKET_MAX]; } C3LinkLoopbackPacket;
typedef struct C3LinkLoopback {
    unsigned count;
    C3LinkLoopbackPacket packets[C3_LINK_LOOPBACK_QUEUE];
    uint32_t seed;
    unsigned drop_percent, duplicate_percent, reorder_percent;
    uint8_t alive[C3_LINK_MAX_PLAYERS + 1]; /* by node 1..4 */
    uint32_t delivered, dropped;
} C3LinkLoopback;
typedef struct C3LinkLoopbackEnd { C3LinkLoopback *net; uint8_t node; } C3LinkLoopbackEnd;
void c3_link_loopback_init(C3LinkLoopback *net, uint32_t seed);
/* end->node: 1 host, 2..4 clients. */
void c3_link_loopback_transport(C3LinkLoopbackEnd *end, C3LinkLoopback *net, uint8_t node, C3LinkTransport *out);

/* FNV-1a, used for state hashes in traces. */
uint32_t c3_link_hash(uint32_t hash, const void *data, size_t size);
#define C3_LINK_HASH_INIT 2166136261u

#endif
