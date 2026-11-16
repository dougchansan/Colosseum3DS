#ifdef C3_LINK_PROBE
/* Unattended link-play probe (-DC3_LINK_PROBE, docs/link-play.md "Testing").
 *
 * Every Azahar instance runs the same build. Its own SD card holds
 * sdmc:/3ds/Colosseum3DS/link-role.txt, one "key value" per line:
 *   role host|join      players N (host: how many to wait for)
 *   name P1             mode echo|battle-now|colosseum|trade
 *   type single|double  rule 0|1|2     slot N (trade: party slot offered)
 * and the probe writes link-probe.log (synchronous fopen, survives a kill)
 * plus link-probe_*.bmp frames. It never writes a save. */
#include "link_probe.h"
#include "link_uds.h"
#include "n3ds_platform.h"
#include "colosseum3ds/link_protocol.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif
#define ROOT "sdmc:/3ds/Colosseum3DS/"

C3LinkProbeConfig c3_link_probe_config;

static uint32_t ms_now(void) { return (uint32_t)osGetTime(); }

void c3_link_probe_log(FILE *log, const char *fmt, ...)
{
    va_list ap;
    if (log) { fprintf(log, "[%lu] ", (unsigned long)ms_now()); va_start(ap, fmt); vfprintf(log, fmt, ap); va_end(ap); fputc('\n', log); fflush(log); }
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n");
}

static void read_config(C3LinkProbeConfig *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->role, "join"); strcpy(c->name, "C3"); strcpy(c->mode, "echo"); strcpy(c->type, "single");
    c->players = 2; c->rule = 1; c->slot = 0; c->turn_limit = 60;
    FILE *f = fopen(ROOT "link-role.txt", "r");
    if (!f) return;
    char key[32], value[64];
    while (fscanf(f, "%31s %63s", key, value) == 2) {
        if (!strcmp(key, "role")) snprintf(c->role, sizeof c->role, "%.*s", (int)sizeof c->role - 1, value);
        else if (!strcmp(key, "name")) snprintf(c->name, sizeof c->name, "%.*s", (int)sizeof c->name - 1, value);
        else if (!strcmp(key, "mode")) snprintf(c->mode, sizeof c->mode, "%.*s", (int)sizeof c->mode - 1, value);
        else if (!strcmp(key, "type")) snprintf(c->type, sizeof c->type, "%.*s", (int)sizeof c->type - 1, value);
        else if (!strcmp(key, "players")) c->players = (unsigned)atoi(value);
        else if (!strcmp(key, "rule")) c->rule = (unsigned)atoi(value);
        else if (!strcmp(key, "slot")) c->slot = (unsigned)atoi(value);
        else if (!strcmp(key, "turns")) c->turn_limit = (unsigned)atoi(value);
        else if (!strcmp(key, "seed")) c->seed = (uint32_t)strtoul(value, NULL, 0);
    }
    fclose(f);
    if (c->players < 2) c->players = 2;
    if (c->players > C3_LINK_MAX_PLAYERS) c->players = C3_LINK_MAX_PLAYERS;
}

/* Keep the session pumped while waiting; returns 0 when the app must exit. */
int c3_link_probe_pump(C3LinkSession *s)
{
    C3N3dsInput input;
    if (!c3_n3ds_poll(&input) || !aptMainLoop()) return 0;
    c3_link_update(s, ms_now());
    gspWaitForVBlank();
    return 1;
}

int c3_link_probe_open(C3LinkUds *uds, C3LinkSession *s, C3LinkTransport *t, FILE *log)
{
    C3LinkProbeConfig *c = &c3_link_probe_config;
    char error[160] = "";
    int host = !strcmp(c->role, "host");
    c3_link_probe_log(log, "link_probe_start build=%s role=%s name=%s mode=%s players=%u", C3_BUILD_ID, c->role, c->name, c->mode, c->players);
    if (host ? !c3_link_uds_host(uds, c->name, error, sizeof error) : !c3_link_uds_join(uds, c->name, 300000, error, sizeof error)) {
        c3_link_probe_log(log, "link_uds_failed error=%s", error);
        return 0;
    }
    c3_link_probe_log(log, "link_uds_ready host=%d", host);
    c3_link_uds_transport(uds, t);
    if (host ? !c3_link_host(s, t, c->name, C3_BUILD_ID, ms_now()) : !c3_link_join(s, t, c->name, C3_BUILD_ID, ms_now())) return 0;
    uint32_t start = ms_now();
    C3LinkMessage m;
    for (;;) {
        if (!c3_link_probe_pump(s)) return 0;
        while (c3_link_poll(s, &m))
            if (m.type == C3_LINK_MSG_ROSTER || m.type == C3_LINK_MSG_JOINED || m.type == C3_LINK_MSG_LEFT)
                c3_link_probe_log(log, "link_roster type=%u from=%u players=%u self=%u", m.type, m.from, s->player_count, s->self_player);
        if (s->state == C3_LINK_FAILED) { c3_link_probe_log(log, "link_failed reason=%s", c3_link_reason_text(s->reason)); return 0; }
        if (host && s->player_count >= c->players) break;
        if (!host && s->state == C3_LINK_LOBBY && s->locked) break;
        if (ms_now() - start > 600000u) { c3_link_probe_log(log, "link_lobby_timeout players=%u", s->player_count); return 0; }
    }
    if (host) {
        /* The session LOCK refuses late joiners. udsSetNewConnectionsBlocked
         * is not used: under Azahar the host's udsPullPacket fails after it. */
        c3_link_lock(s); (void)uds;
        /* Let the LOCK reach everyone before the game starts. */
        while (!c3_link_flushed(s)) if (!c3_link_probe_pump(s)) return 0;
    }
    c3_link_probe_log(log, "link_lobby_ready self=%u players=%u names=%s,%s,%s,%s", s->self_player, s->player_count,
                      s->names[0], s->names[1], s->names[2], s->names[3]);
    return 1;
}

void c3_link_probe_shot(FILE *log, const char *tag)
{
    char path[96];
    snprintf(path, sizeof path, ROOT "link-probe_%s_top.bmp", tag);
    int top = c3_n3ds_capture_top(path) == 0;
    snprintf(path, sizeof path, ROOT "link-probe_%s_bottom.bmp", tag);
    int bottom = c3_n3ds_capture_bottom(path) == 0;
    c3_link_probe_log(log, "link_shot tag=%s top=%d bottom=%d", tag, top, bottom);
}

/* mode echo: every player sends 50 numbered messages to everyone; each
 * checks it got 50 in order from every other player. */
static C3LinkUds *probe_uds;
static int echo(C3LinkSession *s, FILE *log)
{
    unsigned sent = 0, got[C3_LINK_MAX_PLAYERS] = {0}, want = 50, others = s->player_count - 1u;
    uint32_t start = ms_now();
    C3LinkMessage m;
    for (;;) {
        if (sent < want) { uint8_t b[64] = {(uint8_t)sent, s->self_player}; if (c3_link_send(s, C3_LINK_BROADCAST, C3_LINK_MSG_USER, b, sizeof b)) ++sent; }
        if (!c3_link_probe_pump(s)) return 1;
        while (c3_link_poll(s, &m)) if (m.type == C3_LINK_MSG_USER && m.from < C3_LINK_MAX_PLAYERS) {
            if (m.data[0] != got[m.from]) { c3_link_probe_log(log, "link_echo_out_of_order from=%u got=%u want=%u", m.from, m.data[0], got[m.from]); return 1; }
            ++got[m.from];
        }
        unsigned done = 0;
        for (unsigned p = 0; p < C3_LINK_MAX_PLAYERS; ++p) if (p != s->self_player && s->present[p] && got[p] >= want) ++done;
        if (sent >= want && done >= others && c3_link_flushed(s)) break;
        if (s->state == C3_LINK_FAILED || ms_now() - start > 60000u) { c3_link_probe_log(log, "link_echo_failed state=%d reason=%s got=%u,%u,%u,%u dropped=%lu pull_error=%08lX pull_errors=%u send_error=%08lX send_errors=%u in=%u out=%u", s->state, c3_link_reason_text(s->reason), got[0], got[1], got[2], got[3], (unsigned long)s->stats_dropped,
            probe_uds->last_pull_error, probe_uds->pull_errors, probe_uds->last_send_error, probe_uds->send_errors, probe_uds->packets_in, probe_uds->packets_out); return 1; }
    }
    c3_link_probe_log(log, "link_uds in=%u out=%u send_errors=%u last_pull_error=%08lX", probe_uds->packets_in, probe_uds->packets_out, probe_uds->send_errors, probe_uds->last_pull_error);
    c3_link_probe_log(log, "link_echo_ok ms=%lu got=%u,%u,%u,%u sent=%lu resent=%lu received=%lu",
                      (unsigned long)(ms_now() - start), got[0], got[1], got[2], got[3],
                      (unsigned long)s->stats_sent, (unsigned long)s->stats_resent, (unsigned long)s->stats_received);
    c3_link_probe_shot(log, "echo");
    return 0;
}

__attribute__((weak)) int c3_link_probe_game(C3LinkSession *s, const char *mode, FILE *log)
{ (void)s; c3_link_probe_log(log, "link_probe_mode_unknown mode=%s", mode); return 1; }

int c3_link_probe_run(void)
{
    static C3LinkSession session;
    C3LinkUds uds; C3LinkTransport t;
    int result = 1;
    read_config(&c3_link_probe_config);
    FILE *log = fopen(ROOT "link-probe.log", "w");
    probe_uds = &uds;
    if (c3_link_probe_open(&uds, &session, &t, log)) {
        const char *mode = c3_link_probe_config.mode;
        if (!strcmp(mode, "echo")) result = echo(&session, log);
        else result = c3_link_probe_game(&session, mode, log);
        /* Give the last acknowledgements a moment, then leave cleanly. */
        /* Linger: keep acknowledging for a few seconds so the other
         * consoles can finish before this one leaves. */
        for (unsigned k = 0; k < 600 && !c3_link_flushed(&session); ++k) if (!c3_link_probe_pump(&session)) break;
        for (unsigned k = 0; k < 300; ++k) if (!c3_link_probe_pump(&session)) break;
        c3_link_close(&session);
    }
    c3_link_uds_close(&uds);
    c3_link_probe_log(log, "link_probe_end result=%d", result);
    if (log) fclose(log);
    return result;
}
#endif
