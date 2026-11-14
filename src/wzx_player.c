#include "colosseum3ds/wzx_player.h"
#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int fail(char *e, size_t n, const char *s) { if (e && n) snprintf(e, n, "%s", s); return 0; }
static int span(size_t size, uint64_t at, uint64_t bytes) { return at <= size && bytes <= size - at; }

int c3_wzx_pack_decode(C3WzxPack *pack, const void *data, size_t size, char *error, size_t cap)
{
    const uint8_t *b = data;
    if (!pack || !b || size < C3_WZX_HEADER_BYTES || memcmp(b, "C3WX", 4) || le32(b + 4) != 1 || le32(b + 8) != size)
        return fail(error, cap, "Invalid WZX pack header.");
    C3WzxPack p; memset(&p, 0, sizeof p);
    p.bytes = b; p.size = size;
    p.node_count = le32(b + 12); p.entry = le32(b + 16); p.flags = le32(b + 20); p.part_sel = le32(b + 24);
    p.seq_version = le32(b + 28); p.nodes_at = le32(b + 32); p.strings_at = le32(b + 36);
    p.camera_at = le32(b + 40); p.camera_count = le32(b + 44);
    if (p.node_count > C3_WZX_MAX_NODES || p.entry > 16 ||
        !span(size, p.nodes_at, (uint64_t)p.node_count * C3_WZX_NODE_BYTES) || p.strings_at > size ||
        (p.camera_at && !span(size, p.camera_at, (uint64_t)p.camera_count * 36u)))
        return fail(error, cap, "WZX pack tables out of bounds.");
    for (uint32_t i = 0; i < p.node_count; ++i) {
        const uint8_t *n = b + p.nodes_at + (size_t)i * C3_WZX_NODE_BYTES;
        uint32_t kind = le32(n + 4), payload = le32(n + 112), bytes = le32(n + 116), name = le32(n + 120);
        uint32_t map = le32(n + 136), maps = le32(n + 140);
        if (kind < C3_WZX_CONTROL || kind > C3_WZX_ACTION || le32(n + 20) >= 16 || le32(n + 24) >= 16 || le32(n + 28) > 16 ||
            (bytes && !span(size, payload, bytes)) || (maps && !span(size, map, 2ull * maps)) ||
            (name && (!p.strings_at || (uint64_t)p.strings_at + name >= size || !memchr(b + p.strings_at + name, 0, size - p.strings_at - name))))
            return fail(error, cap, "WZX pack node out of bounds.");
    }
    *pack = p;
    return 1;
}

int c3_wzx_pack_node(const C3WzxPack *p, uint32_t i, C3WzxNode *o)
{
    if (!p || !o || i >= p->node_count) return 0;
    const uint8_t *n = p->bytes + p->nodes_at + (size_t)i * C3_WZX_NODE_BYTES;
    memset(o, 0, sizeof *o);
    o->key = le32(n); o->kind = le32(n + 4); o->attach = le32(n + 8); o->postype = le32(n + 12);
    o->link = le32(n + 16); o->src = le32(n + 20); o->tgt = le32(n + 24); o->tidx = le32(n + 28);
    for (unsigned k = 0; k < 16; ++k) o->timing[k] = (int32_t)le32(n + 32 + 4 * k);
    o->flags = le32(n + 96); o->part = le32(n + 100); o->node_ver = le32(n + 104); o->state = le32(n + 108);
    o->payload_bytes = le32(n + 116); o->payload = o->payload_bytes ? p->bytes + le32(n + 112) : NULL;
    uint32_t name = le32(n + 120); o->file = name ? (const char *)p->bytes + p->strings_at + name : NULL; /* relative to strings_at */
    o->status = le32(n + 124); o->model_clips = le32(n + 128); o->model_bones = le32(n + 132);
    o->joint_map_count = le32(n + 140);
    o->joint_map = o->joint_map_count ? (const uint16_t *)(p->bytes + le32(n + 136)) : NULL;
    return 1;
}

unsigned c3_wzx_node_joint(const C3WzxNode *n, uint32_t joint)
{
    if (!n || !n->joint_map || joint >= n->joint_map_count) return 0xFFFF;
    const uint8_t *m = (const uint8_t *)n->joint_map + 2 * joint;
    return (unsigned)m[0] | (unsigned)m[1] << 8;
}

uint32_t c3_wzx_payload_u32(const C3WzxNode *n, uint32_t off)
{
    if (!n || !n->payload || off + 4 > n->payload_bytes) return 0;
    const uint8_t *p = n->payload + off;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int c3_wzx_pack_camera(const C3WzxPack *p, uint32_t tick, float out[8])
{
    if (!p || !p->camera_at || !p->camera_count || !out) return 0;
    if (tick >= p->camera_count) tick = p->camera_count - 1;
    const uint8_t *s = p->bytes + p->camera_at + (size_t)tick * 36u;
    for (unsigned k = 0; k < 8; ++k) { uint32_t u = le32(s + 4 * k); memcpy(&out[k], &u, 4); }
    return 1;
}

/* wazaSequenceSysGetWazaTime: which of the owner row's first four timing
 * values a mark reads, by sequence type (animationMode) and PKX entry. */
static int mark_value(unsigned mode, uint32_t entry, unsigned type)
{
    int idle = entry == 0 || entry == 0xB || entry == 0x10, hit = entry >= 8 && entry <= 10;
    if (type == C3_WZX_TIME_START || type == 7) return -1;
    if (mode == 1 && hit) return -2;
    if (mode == 2 && !idle && !hit) return -2;
    if (idle) return type >= 3 && type <= 5 ? 0 : 1;
    if (hit) return type >= 3 && type <= 5 ? 0 : type == 1 ? 1 : 2;
    /* Other entries: 3 and 5 the first mark, 4 the second, 1 the third, 2/6 the fourth. */
    if (mode == 2) return -2;
    return type == 3 || type == 5 ? 0 : type == 4 ? 1 : type == 1 ? 2 : 3;
}

int32_t c3_wzx_player_mark(const C3WzxPlayer *pl, unsigned type)
{
    if (!pl || !pl->pack) return 0;
    int v = mark_value(pl->kind, pl->pack->entry, type);
    return pl->motion_delay + (v >= 0 ? pl->owner_timing[v] : 0);
}

int c3_wzx_player_reached(const C3WzxPlayer *pl, unsigned type)
{
    return pl && pl->time >= c3_wzx_player_mark(pl, type);
}

int c3_wzx_player_begin(C3WzxPlayer *pl, const C3WzxPack *pack, unsigned kind,
                        const int32_t owner_timing[16], const C3WzxBackend *backend, char *error, size_t cap)
{
    if (!pl || !pack || !owner_timing || !backend) return fail(error, cap, "Invalid WZX player arguments.");
    memset(pl, 0, sizeof *pl);
    pl->pack = pack; pl->backend = *backend; pl->kind = (uint8_t)(kind ? kind : 1);
    memcpy(pl->owner_timing, owner_timing, sizeof pl->owner_timing);
    uint32_t n = pack->node_count;
    for (uint32_t i = 0; i < n; ++i) if (!c3_wzx_pack_node(pack, i, &pl->nodes[i])) return fail(error, cap, "WZX node decode failed.");
    /* wazaSequenceEntryLink, in file order: a linked node anchors on the
     * linked node's start plus its mark tgt, else on the owner's PKX row
     * value tgt; the node's own mark src is subtracted. */
    for (uint32_t i = 0; i < n; ++i) {
        const C3WzxNode *e = &pl->nodes[i];
        int32_t anchor = owner_timing[e->tgt];
        if (e->link) {
            for (uint32_t j = 0; j < i; ++j)
                if (pl->nodes[j].key == e->link) { anchor = pl->start[j] + pl->nodes[j].timing[e->tgt]; break; }
        }
        pl->start[i] = anchor - e->timing[e->src];
    }
    int32_t earliest = 0;
    for (uint32_t i = 0; i < n; ++i) if (pl->start[i] < earliest) earliest = pl->start[i];
    if (earliest < 0) { for (uint32_t i = 0; i < n; ++i) pl->start[i] -= earliest; pl->motion_delay = -earliest; }
    /* Start order: by start time; an owner action wins a tie, as the
     * original's insertion places kind 6 before equal starts. */
    for (uint32_t i = 0; i < n; ++i) pl->order[i] = (uint8_t)i;
    for (uint32_t i = 1; i < n; ++i) {
        uint8_t v = pl->order[i]; uint32_t j = i;
        while (j > 0) {
            uint8_t u = pl->order[j - 1];
            int before = pl->start[v] < pl->start[u] || (pl->start[v] == pl->start[u] && pl->nodes[v].kind == C3_WZX_ACTION && pl->nodes[u].kind != C3_WZX_ACTION);
            if (!before) break;
            pl->order[j] = u; --j;
        }
        pl->order[j] = v;
    }
    pl->end_time = c3_wzx_player_mark(pl, C3_WZX_TIME_END);
    pl->control_left = -1;
    pl->active = 1;
    c3_wzx_player_step(pl, 0);
    return 1;
}

static int default_running(const C3WzxNode *e, int32_t local)
{
    return e->tidx && local < e->timing[e->tidx - 1];
}

int c3_wzx_player_step(C3WzxPlayer *pl, uint32_t ticks)
{
    if (!pl || !pl->active) return 0;
    pl->time += (int32_t)ticks;
    int pending = c3_wzx_player_mark(pl, 2) > pl->time, running = 0;
    /* wazaSequenceUpdate: once the owner's motion is past its end mark the
     * sequence is over, whatever is still running. */
    if (pl->end_time > 0 && pl->time > pl->end_time) { c3_wzx_player_stop(pl); return 0; }
    for (uint32_t k = 0; k < pl->pack->node_count; ++k) {
        uint32_t i = pl->order[k];
        const C3WzxNode *e = &pl->nodes[i];
        if (pl->state[i] == 0) {
            if (pl->start[i] > pl->time) { ++running; continue; }
            if (e->kind == C3_WZX_CONTROL) {
                /* EntryUpdate case 1, sub 0: end the sequence after value ticks. */
                pl->control_left = (int32_t)c3_wzx_payload_u32(e, 4);
                pl->state[i] = 1; ++pending; ++pl->started; continue;
            }
            int started = !e->status && pl->backend.start ? pl->backend.start(pl->backend.user, i, e) : 0;
            if (!started) { pl->state[i] = 2; ++pl->skipped; continue; }
            pl->state[i] = 1; ++pending; ++pl->started;
            continue; /* a node's first update happens on the next step */
        }
        if (pl->state[i] == 1) {
            int32_t local = pl->time - pl->start[i];
            int live;
            if (e->kind == C3_WZX_CONTROL) {
                live = pl->control_left > local;
                if (!live) pl->stopping = 1;
            } else {
                live = pl->backend.update ? pl->backend.update(pl->backend.user, i, e, ticks, local) : -1;
                if (live < 0) live = default_running(e, local);
            }
            if (live) ++pending;
            else { if (e->kind != C3_WZX_CONTROL && pl->backend.stop) pl->backend.stop(pl->backend.user, i, e, 0); pl->state[i] = 2; }
        }
    }
    if (pl->stopping || pending + running == 0) { c3_wzx_player_stop(pl); return 0; }
    return 1;
}

void c3_wzx_player_stop(C3WzxPlayer *pl)
{
    if (!pl || !pl->active) return;
    for (uint32_t i = 0; i < pl->pack->node_count; ++i) {
        if (pl->state[i] == 1 && pl->nodes[i].kind != C3_WZX_CONTROL && pl->backend.stop)
            pl->backend.stop(pl->backend.user, i, &pl->nodes[i], 1);
        pl->state[i] = 2;
    }
    pl->active = 0; pl->stopping = 0;
}
