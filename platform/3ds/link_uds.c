#include "link_uds.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Communication ID and passphrase shared by every Colosseum3DS build; the
 * build ID is checked by the session handshake, not here, so a mismatched
 * build gets a clear refusal instead of an invisible network. */
#define WLAN_COMM_ID 0xC3C01105u
#define WLAN_ID8 0x01u
#define DATA_CHANNEL 1u
static const char passphrase[] = "Colosseum3DS link play";

_Static_assert(sizeof(udsBindContext) <= sizeof(((C3LinkUds *)0)->bind), "udsBindContext fits");

static udsBindContext *bindctx(C3LinkUds *u) { return (udsBindContext *)(void *)u->bind; }

static int uds_fail(char *e, size_t n, const char *what, Result r)
{
    if (e && n) snprintf(e, n, "%s failed (0x%08lX)", what, (unsigned long)r);
    return 0;
}

static int init(C3LinkUds *u, const char *name, char *e, size_t n)
{
    memset(u, 0, sizeof *u);
    char user[11] = {0};
    strncpy(user, name && name[0] ? name : "C3", sizeof user - 1);
    Result r = udsInit(0x3000, user);
    if (R_FAILED(r)) return uds_fail(e, n, "Local wireless (udsInit)", r);
    u->initialised = 1;
    return 1;
}

int c3_link_uds_host(C3LinkUds *u, const char *name, char *e, size_t n)
{
    if (!init(u, name, e, n)) return 0;
    udsNetworkStruct net;
    udsGenerateDefaultNetworkStruct(&net, WLAN_COMM_ID, WLAN_ID8, C3_LINK_MAX_PLAYERS);
    Result r = udsCreateNetwork(&net, passphrase, sizeof passphrase, bindctx(u), DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(r)) { c3_link_uds_close(u); return uds_fail(e, n, "Creating the local network", r); }
    u->connected = 1; u->host = 1;
    return 1;
}

int c3_link_uds_join(C3LinkUds *u, const char *name, unsigned timeout_ms, char *e, size_t n)
{
    if (!init(u, name, e, n)) return 0;
    size_t size = 0x4000;
    void *buffer = malloc(size);
    if (!buffer) { c3_link_uds_close(u); if (e && n) snprintf(e, n, "Out of memory for the network scan."); return 0; }
    uint64_t start = osGetTime();
    Result r = 0;
    int joined = 0;
    while (!joined && osGetTime() - start < timeout_ms && aptMainLoop()) {
        udsNetworkScanInfo *networks = NULL;
        size_t total = 0;
        memset(buffer, 0, size);
        r = udsScanBeacons(buffer, size, &networks, &total, WLAN_COMM_ID, WLAN_ID8, NULL, false);
        if (R_SUCCEEDED(r) && total && networks) {
            for (int attempt = 0; attempt < 5 && !joined; ++attempt) {
                r = udsConnectNetwork(&networks[0].network, passphrase, sizeof passphrase, bindctx(u),
                                      UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
                if (R_SUCCEEDED(r)) joined = 1; else svcSleepThread(200000000LL);
            }
        }
        free(networks);
        if (!joined) svcSleepThread(250000000LL);
    }
    free(buffer);
    if (!joined) {
        c3_link_uds_close(u);
        if (e && n) { if (R_FAILED(r)) uds_fail(e, n, "Joining the local network", r); else snprintf(e, n, "No Colosseum3DS host was found nearby."); }
        return 0;
    }
    u->connected = 1;
    return 1;
}

static int uds_send(void *ctx, uint8_t node, const void *data, size_t size)
{
    C3LinkUds *u = ctx;
    if (!u->connected || size > UDS_DATAFRAME_MAXSIZE) return 0;
    u16 dest = node == C3_LINK_BROADCAST ? UDS_BROADCAST_NETWORKNODEID : node;
    Result r = udsSendTo(dest, DATA_CHANNEL, UDS_SENDFLAG_Default, data, size);
    if (R_FAILED(r) && (u32)r != 0xC86113F0u) { ++u->send_errors; return 0; } /* UDS_CHECK_SENDTO_FATALERROR */
    ++u->packets_out;
    return 1;
}

static int uds_recv(void *ctx, uint8_t *node, void *data, size_t capacity)
{
    C3LinkUds *u = ctx;
    if (!u->connected) return -1;
    size_t got = 0; u16 src = 0;
    Result r = udsPullPacket(bindctx(u), data, capacity, &got, &src);
    if (R_FAILED(r)) return -1;
    if (!got) return 0;
    *node = (uint8_t)src;
    ++u->packets_in;
    return (int)got;
}

void c3_link_uds_transport(C3LinkUds *u, C3LinkTransport *out)
{
    out->ctx = u; out->send = uds_send; out->recv = uds_recv;
}

void c3_link_uds_block_joins(C3LinkUds *u)
{
    if (u && u->host && u->connected) udsSetNewConnectionsBlocked(true, true, false);
}

void c3_link_uds_close(C3LinkUds *u)
{
    if (!u || !u->initialised) return;
    if (u->connected) {
        udsUnbind(bindctx(u));
        if (u->host) udsDestroyNetwork(); else udsDisconnectNetwork();
    }
    udsExit();
    u->initialised = u->connected = 0;
}
