#include "colosseum3ds/wzx_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for the general WZX player (src/wzx_player.c) on a synthetic
 * C3WX pack: link timing, the negative-start shift (owner motion delay),
 * start order, control-node end, hit/finish marks and backend calls. With a
 * directory argument (PRIVATE) every wx_*.c3w there is decoded and played
 * against a neutral owner row. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[160];
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void b32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

typedef struct { int starts[8], updates[8], stops[8], forced; int32_t start_time[8]; int32_t *clock; } Log;
static int be_start(void *u, uint32_t i, const C3WzxNode *n) { Log *l = u; (void)n; ++l->starts[i]; l->start_time[i] = *l->clock; return 1; }
static int be_update(void *u, uint32_t i, const C3WzxNode *n, uint32_t t, int32_t local) { Log *l = u; (void)n; (void)t; (void)local; ++l->updates[i]; return -1; }
static void be_stop(void *u, uint32_t i, const C3WzxNode *n, int forced) { Log *l = u; (void)n; ++l->stops[i]; l->forced += forced; }

static void node(uint8_t *b, unsigned at, uint32_t key, uint32_t kind, uint32_t link, uint32_t src, uint32_t tgt, uint32_t tidx, const int32_t *t)
{
    uint8_t *n = b + at;
    w32(n, key); w32(n + 4, kind); w32(n + 16, link); w32(n + 20, src); w32(n + 24, tgt); w32(n + 28, tidx);
    for (unsigned k = 0; k < 16; ++k) w32(n + 32 + 4 * k, (uint32_t)t[k]);
}

static void synthetic(void)
{
    enum { NODES = 4, PAYLOAD = 64 + NODES * 160 };
    static uint8_t b[PAYLOAD + 16];
    memcpy(b, "C3WX", 4); w32(b + 4, 1); w32(b + 8, sizeof b); w32(b + 12, NODES);
    w32(b + 16, 3); w32(b + 32, 64);
    int32_t t0[16] = {0, 10, 40}, t1[16] = {30, 0, 25}, t2[16] = {0}, t3[16] = {0};
    /* 0 particle: anchors on owner value 1 (20) at its mark 1 (10) -> start 10, runs to its mark 2 (40) */
    node(b, 64, 1, 3, 0, 1, 1, 3, t0);
    /* 1 model linked to node 1 at node1.timing[2]=40 minus own mark 0 (30) -> 10+40-30 = 20, duration mark 3 (25) */
    node(b, 64 + 160, 2, 2, 1, 0, 2, 3, t1);
    /* 2 sound at owner value 0 (5) minus 0 -> 5; done at once */
    node(b, 64 + 320, 3, 5, 0, 0, 0, 0, t2);
    /* 3 control: owner value 3 (80) -> ends the sequence 200 ticks later */
    node(b, 64 + 480, 4, 1, 0, 0, 3, 0, t3);
    w32(b + 64 + 480 + 112, PAYLOAD); w32(b + 64 + 480 + 116, 12);
    b32(b + PAYLOAD, 0); b32(b + PAYLOAD + 4, 200);
    C3WzxPack pack; C3WzxPlayer pl; Log log; memset(&log, 0, sizeof log); log.clock = &pl.time;
    CHECK(c3_wzx_pack_decode(&pack, b, sizeof b, error, sizeof error));
    int32_t owner[16] = {5, 20, 60, 80};
    C3WzxBackend be = {&log, be_start, be_update, be_stop};
    CHECK(c3_wzx_player_begin(&pl, &pack, 1, owner, &be, error, sizeof error));
    CHECK(pl.start[0] == 10 && pl.start[1] == 20 && pl.start[2] == 5 && pl.start[3] == 80 && pl.motion_delay == 0);
    CHECK(pl.order[0] == 2 && pl.order[1] == 0 && pl.order[2] == 1 && pl.order[3] == 3);
    /* Entry 3 attack: hit (type 5) reads the first value, end the third. */
    CHECK(c3_wzx_player_mark(&pl, C3_WZX_TIME_DAMAGE) == 5 && c3_wzx_player_mark(&pl, C3_WZX_TIME_END) == 60);
    int steps = 0; while (c3_wzx_player_step(&pl, 1)) ++steps;
    CHECK(log.starts[0] == 1 && log.start_time[0] == 10 && log.starts[1] == 1 && log.start_time[1] == 20 && log.starts[2] == 1);
    CHECK(log.stops[0] == 1 && log.stops[1] == 1 && log.stops[2] == 1);
    /* The owner's end mark (60) ends it before the control node runs out. */
    CHECK(pl.time == 61 && !pl.active);
    /* Negative earliest start shifts everything and delays the owner. */
    int32_t early[16] = {0, 0, 0, 500};
    t0[1] = 30; node(b, 64, 1, 3, 0, 1, 1, 3, t0);
    memset(&log, 0, sizeof log); log.clock = &pl.time;
    CHECK(c3_wzx_player_begin(&pl, &pack, 1, early, &be, error, sizeof error));
    CHECK(pl.motion_delay == 30 && pl.start[0] == 0 && pl.start[2] == 30);
    CHECK(c3_wzx_player_mark(&pl, C3_WZX_TIME_END) == 30);
    /* Bounds: a bad timing index is refused. */
    w32(b + 64 + 20, 16); CHECK(!c3_wzx_pack_decode(&pack, b, sizeof b, error, sizeof error));
    puts("wzx-player: synthetic pack ok");
}

int main(int argc, char **argv)
{
    synthetic();
    if (argc > 1) {
        char cmd[512]; snprintf(cmd, sizeof cmd, "ls %s/wx_*.c3w", argv[1]);
        FILE *list = popen(cmd, "r"); char path[400]; unsigned ok = 0, bad = 0, nodes = 0, skipped = 0;
        while (list && fgets(path, sizeof path, list)) {
            path[strcspn(path, "\n")] = 0;
            FILE *f = fopen(path, "rb"); if (!f) { ++bad; continue; }
            static uint8_t data[1 << 20]; size_t n = fread(data, 1, sizeof data, f); fclose(f);
            C3WzxPack pack; C3WzxPlayer pl; int32_t owner[16] = {30, 60, 90, 120};
            C3WzxBackend be = {0};
            if (!c3_wzx_pack_decode(&pack, data, n, error, sizeof error) || !c3_wzx_player_begin(&pl, &pack, 1, owner, &be, error, sizeof error)) {
                printf("bad %s: %s\n", path, error); ++bad; continue;
            }
            for (unsigned t = 0; t < 6000 && c3_wzx_player_step(&pl, 1); ++t) {}
            ++ok; nodes += pack.node_count; skipped += pl.skipped;
        }
        if (list) pclose(list);
        printf("wzx-player: %u packs played, %u failed, %u nodes, %u skipped without a backend\n", ok, bad, nodes, skipped);
        if (bad) return 1;
    }
    return 0;
}
