#ifndef C3_LINK_UDS_H
#define C3_LINK_UDS_H
#include "colosseum3ds/link_protocol.h"
#include <stddef.h>

/* libctru UDS (3DS local wireless) under the link session
 * (src/link_protocol.c). One network per session: the host creates it
 * (node 1, up to C3_LINK_MAX_PLAYERS nodes), clients scan for the
 * Colosseum3DS communication ID and join the first network they find.
 * Azahar carries the same calls through its multiplayer room. */
typedef struct C3LinkUds {
    int initialised, connected, host;
    unsigned char bind[64]; /* udsBindContext, kept opaque for the header */
    unsigned packets_in, packets_out, send_errors, pull_errors;
    unsigned long last_pull_error, last_send_error;
    size_t last_pull_size;
} C3LinkUds;

int c3_link_uds_host(C3LinkUds *uds, const char *name, char *error, size_t capacity);
/* Scans for up to timeout_ms; 1 joined, 0 nothing found or failure. */
int c3_link_uds_join(C3LinkUds *uds, const char *name, unsigned timeout_ms, char *error, size_t capacity);
void c3_link_uds_transport(C3LinkUds *uds, C3LinkTransport *out);
/* Host: stop accepting new nodes (the game has started). */
void c3_link_uds_block_joins(C3LinkUds *uds);
void c3_link_uds_close(C3LinkUds *uds);
#endif
