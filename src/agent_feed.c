/* Private gameplay-harness state feed encoder; see colosseum3ds/agent_feed.h.
 * Compiled only into C3_AGENT_HARNESS builds (Makefile.3ds filters it out of
 * every other build, and the body is guarded as well). */
#ifdef C3_AGENT_HARNESS
#include "colosseum3ds/agent_feed.h"

#include <string.h>

typedef struct { uint8_t *p; size_t at, cap; int ok; } Out;

static void put(Out *o, const void *data, size_t n)
{
    if (!o->ok || o->at + n > o->cap) { o->ok = 0; return; }
    memcpy(o->p + o->at, data, n); o->at += n;
}
static size_t bounded_length(const char *s, size_t limit)
{ size_t n = 0; while (n < limit && s[n]) ++n; return n; }
static void u8(Out *o, uint8_t v) { put(o, &v, 1); }
static void u16(Out *o, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; put(o, b, 2); }
static void u32(Out *o, uint32_t v)
{ uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; put(o, b, 4); }
static void f32(Out *o, float v) { uint32_t w; memcpy(&w, &v, 4); u32(o, w); }
static void patch16(Out *o, size_t at, uint16_t v)
{ if (o->ok && at + 2 <= o->cap) { o->p[at] = (uint8_t)v; o->p[at + 1] = (uint8_t)(v >> 8); } }

/* Opens a section and returns the offset of its length field. */
static size_t section(Out *o, uint8_t type)
{ u8(o, type); u8(o, 1); size_t at = o->at; u16(o, 0); return at; }
static void close_section(Out *o, size_t length_at)
{ patch16(o, length_at, (uint16_t)(o->at - length_at - 2)); }

static void field(Out *o, const C3AgentField *f)
{
    size_t at = section(o, C3_AGENT_SECTION_FIELD);
    u16(o, f->floor); u16(o, f->flags);
    u8(o, f->phase); u8(o, f->pause_page); u8(o, f->ingame_page); u8(o, f->motion);
    u8(o, f->collisions); u8(o, 0); u16(o, f->object_count);
    u32(o, f->message_id); u32(o, f->battle_id); u32(o, f->menu_floor);
    char prefix[16] = {0};
    memcpy(prefix, f->prefix, bounded_length(f->prefix, sizeof prefix - 1));
    put(o, prefix, sizeof prefix);
    for (unsigned i = 0; i < 3; ++i) f32(o, f->wes[i]);
    f32(o, f->wes_yaw); f32(o, f->move_yaw); f32(o, f->camera_yaw);
    for (unsigned i = 0; i < 3; ++i) f32(o, f->partner[i]);
    f32(o, f->partner_yaw); u16(o, f->partner_clip); u16(o, 0);
    put(o, f->disabled_objects, sizeof f->disabled_objects);
    close_section(o, at);
}

static void actors(Out *o, const C3AgentFeed *feed)
{
    uint32_t count = feed->actor_count < C3_AGENT_MAX_ACTORS ? feed->actor_count : C3_AGENT_MAX_ACTORS;
    size_t at = section(o, C3_AGENT_SECTION_ACTORS);
    u8(o, (uint8_t)count); u8(o, 0); u16(o, 0);
    for (uint32_t i = 0; i < count; ++i) {
        const C3AgentActor *a = feed->actors + i;
        u16(o, a->index); u8(o, a->flags); u8(o, 0);
        for (unsigned j = 0; j < 3; ++j) f32(o, a->position[j]);
        f32(o, a->yaw);
    }
    close_section(o, at);
}

static void battle(Out *o, const C3AgentBattle *b)
{
    size_t at = section(o, C3_AGENT_SECTION_BATTLE);
    u32(o, b->encounter); u16(o, b->turn);
    u8(o, b->outcome); u8(o, b->ui_page); u8(o, b->active_count);
    u8(o, b->party_count[0]); u8(o, b->party_count[1]); u8(o, b->snag);
    for (unsigned s = 0; s < 2; ++s) for (unsigned i = 0; i < 2; ++i) u8(o, b->active[s][i]);
    for (unsigned s = 0; s < 2; ++s) for (unsigned i = 0; i < C3_AGENT_PARTY; ++i) {
        const C3AgentMon *m = &b->party[s][i];
        u16(o, m->species); u16(o, m->hp); u16(o, m->max_hp);
        for (unsigned j = 0; j < 5; ++j) u16(o, m->stats[j]);
        for (unsigned j = 0; j < 4; ++j) u16(o, m->moves[j]);
        put(o, m->pp, 4); u8(o, m->level); u8(o, m->types[0]); u8(o, m->types[1]); u8(o, m->status);
    }
    close_section(o, at);
}

size_t c3_agent_feed_encode(const C3AgentFeed *feed, uint8_t *out, size_t capacity)
{
    Out o = {out, 0, capacity < C3_AGENT_PACKET_MAX ? capacity : C3_AGENT_PACKET_MAX, out != NULL};
    if (!feed) return 0;
    uint8_t sections = 0;
    size_t text = bounded_length(feed->text, C3_AGENT_TEXT_MAX);
    put(&o, "C3AG", 4); u16(&o, C3_AGENT_FEED_VERSION); u16(&o, 0);
    u32(&o, feed->sequence); u32(&o, feed->frame); u32(&o, feed->time_ms);
    u8(&o, feed->mode); u8(&o, 0);
    u16(&o, feed->held); u8(&o, (uint8_t)feed->stick_x); u8(&o, (uint8_t)feed->stick_y);
    u8(&o, feed->touch_held); u8(&o, feed->input_frames); u16(&o, feed->touch_x); u16(&o, feed->touch_y);
    u16(&o, feed->held_latch); u8(&o, (uint8_t)feed->latch_stick_x); u8(&o, (uint8_t)feed->latch_stick_y);
    u16(&o, feed->frames_since); u16(&o, 0);
    if (feed->has_field) { field(&o, &feed->field); actors(&o, feed); sections += 2; }
    if (feed->has_battle) { battle(&o, &feed->battle); ++sections; }
    if (text) {
        size_t at = section(&o, C3_AGENT_SECTION_TEXT);
        put(&o, feed->text, text);
        close_section(&o, at); ++sections;
    }
    if (!o.ok) return 0;
    out[21] = sections;
    patch16(&o, 6, (uint16_t)o.at);
    return o.at;
}
#endif
