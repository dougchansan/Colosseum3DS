/* The private gameplay-harness feed encoder (src/agent_feed.c), built only
 * with -DC3_AGENT_HARNESS. Pins the datagram layout the PC bridge decodes;
 * `test_agent_feed <field.bin> <battle.bin>` also writes both golden packets. */
#include "colosseum3ds/agent_feed.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float rdf(const uint8_t *p) { uint32_t w = rd32(p); float f; memcpy(&f, &w, 4); return f; }

static void field_feed(C3AgentFeed *f)
{
    memset(f, 0, sizeof *f);
    f->sequence = 7; f->frame = 1234; f->time_ms = 99000; f->mode = C3_AGENT_MODE_FIELD;
    f->held = 0x0100; f->stick_x = -64; f->stick_y = 127; f->touch_held = 1; f->touch_x = 193; f->touch_y = 150;
    f->input_frames = 3; f->held_latch = 0x0101; f->latch_stick_x = -90; f->latch_stick_y = 127; f->frames_since = 3;
    f->has_field = 1;
    C3AgentField *d = &f->field;
    d->floor = 0x003; d->flags = C3_AGENT_FIELD_CONTROL | C3_AGENT_FIELD_PARTNER | C3_AGENT_FIELD_PARTNER_SEEN;
    d->object_count = 42; d->phase = 4; d->pause_page = 0; d->ingame_page = 0; d->motion = 1; d->collisions = 2;
    d->message_id = 0x2ef5; d->battle_id = 10; d->menu_floor = 0;
    snprintf(d->prefix, sizeof d->prefix, "M1_out");
    d->wes[0] = 1.5f; d->wes[1] = 2.0f; d->wes[2] = -3.25f; d->wes_yaw = 0.5f; d->move_yaw = 0.0f; d->camera_yaw = 0.0f;
    d->partner[0] = 1.0f; d->partner[1] = 2.0f; d->partner[2] = -4.0f; d->partner_yaw = 1.25f; d->partner_clip = 3;
    d->disabled_objects[5] = 0x01; /* object 40 off */
    d->disabled_objects[5] |= 0x02; /* object 41 off */
    f->actor_count = 2;
    f->actors[0] = (C3AgentActor){12, C3_AGENT_ACTOR_VISIBLE | C3_AGENT_ACTOR_TALKABLE, {10.0f, 0.0f, 20.0f}, 3.0f};
    f->actors[1] = (C3AgentActor){101, C3_AGENT_ACTOR_VISIBLE, {-1.0f, 2.0f, 0.5f}, 0.0f};
    snprintf(f->text, sizeof f->text, "Welcome to Phenac City!");
}

static void battle_feed(C3AgentFeed *f)
{
    memset(f, 0, sizeof *f);
    f->sequence = 8; f->frame = 2000; f->time_ms = 100000; f->mode = C3_AGENT_MODE_BATTLE; f->has_battle = 1;
    C3AgentBattle *b = &f->battle;
    b->encounter = 10; b->turn = 2; b->outcome = 1; b->ui_page = 3; b->active_count = 2;
    b->party_count[0] = 2; b->party_count[1] = 2; b->active[0][0] = 0; b->active[0][1] = 1; b->active[1][0] = 0; b->active[1][1] = 1;
    b->party[0][0] = (C3AgentMon){197, 87, 87, {60, 70, 50, 90, 55}, {44, 290, 269, 289}, {25, 20, 20, 10}, 26, {17, 17}, 0};
    b->party[1][0] = (C3AgentMon){288, 40, 52, {30, 30, 30, 30, 40}, {33, 39, 0, 0}, {35, 30, 0, 0}, 24, {0, 0}, 0};
}

static void write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, n, file) == n);
    fclose(file);
}

int main(int argc, char **argv)
{
    static C3AgentFeed feed;
    uint8_t packet[C3_AGENT_PACKET_MAX];
    field_feed(&feed);
    size_t n = c3_agent_feed_encode(&feed, packet, sizeof packet);
    /* header 40 + field 4+116 + actors 4+4+2*20 + text 4+23 */
    assert(n == 40u + 120u + 48u + 27u);
    assert(!memcmp(packet, "C3AG", 4) && rd16(packet + 4) == 1 && rd16(packet + 6) == n);
    assert(rd32(packet + 8) == 7 && rd32(packet + 12) == 1234 && rd32(packet + 16) == 99000);
    assert(packet[20] == C3_AGENT_MODE_FIELD && packet[21] == 3);
    assert(rd16(packet + 22) == 0x0100 && (int8_t)packet[24] == -64 && (int8_t)packet[25] == 127);
    assert(packet[26] == 1 && packet[27] == 3 && rd16(packet + 28) == 193 && rd16(packet + 30) == 150);
    assert(rd16(packet + 32) == 0x0101 && (int8_t)packet[34] == -90 && (int8_t)packet[35] == 127 && rd16(packet + 36) == 3);
    const uint8_t *s = packet + 40;
    assert(s[0] == C3_AGENT_SECTION_FIELD && s[1] == 1 && rd16(s + 2) == 116);
    const uint8_t *d = s + 4;
    assert(rd16(d) == 3 && rd16(d + 2) == feed.field.flags && d[4] == 4 && d[7] == 1 && d[8] == 2 && rd16(d + 10) == 42);
    assert(rd32(d + 12) == 0x2ef5 && rd32(d + 16) == 10 && !strcmp((const char *)d + 24, "M1_out"));
    assert(rdf(d + 40) == 1.5f && rdf(d + 44) == 2.0f && rdf(d + 48) == -3.25f && rdf(d + 52) == 0.5f);
    assert(rdf(d + 64) == 1.0f && rdf(d + 72) == -4.0f && rdf(d + 76) == 1.25f && rd16(d + 80) == 3);
    assert(d[84 + 5] == 0x03);
    s += 4 + 116;
    assert(s[0] == C3_AGENT_SECTION_ACTORS && rd16(s + 2) == 44 && s[4] == 2);
    assert(rd16(s + 8) == 12 && s[10] == 3 && rdf(s + 12) == 10.0f && rdf(s + 20) == 20.0f && rdf(s + 24) == 3.0f);
    assert(rd16(s + 28) == 101 && s[30] == 1 && rdf(s + 36) == 2.0f);
    s += 4 + 44;
    assert(s[0] == C3_AGENT_SECTION_TEXT && rd16(s + 2) == 23 && !memcmp(s + 4, "Welcome to Phenac City!", 23));
    /* Too small a buffer never yields a partial datagram. */
    assert(c3_agent_feed_encode(&feed, packet, 100) == 0);
    /* Every actor slot still fits one datagram. */
    feed.actor_count = C3_AGENT_MAX_ACTORS;
    memset(feed.text, 'x', C3_AGENT_TEXT_MAX); feed.text[C3_AGENT_TEXT_MAX] = 0;
    size_t full = c3_agent_feed_encode(&feed, packet, sizeof packet);
    assert(full == 40u + 120u + 4u + 4u + 20u * C3_AGENT_MAX_ACTORS + 4u + C3_AGENT_TEXT_MAX && full <= C3_AGENT_PACKET_MAX);
    field_feed(&feed);
    n = c3_agent_feed_encode(&feed, packet, sizeof packet);
    if (argc > 2) write_file(argv[1], packet, n);

    battle_feed(&feed);
    n = c3_agent_feed_encode(&feed, packet, sizeof packet);
    assert(n == 40u + 4u + 400u && packet[20] == C3_AGENT_MODE_BATTLE && packet[21] == 1);
    s = packet + 40;
    assert(s[0] == C3_AGENT_SECTION_BATTLE && rd16(s + 2) == 400);
    const uint8_t *b = s + 4;
    assert(rd32(b) == 10 && rd16(b + 4) == 2 && b[6] == 1 && b[7] == 3 && b[8] == 2 && b[9] == 2 && b[10] == 2);
    assert(b[12] == 0 && b[13] == 1);
    const uint8_t *m = b + 16;
    assert(rd16(m) == 197 && rd16(m + 2) == 87 && rd16(m + 4) == 87 && rd16(m + 6) == 60 && rd16(m + 16) == 44);
    assert(m[24] == 25 && m[28] == 26 && m[29] == 17 && m[30] == 17);
    m = b + 16 + 6 * 32;
    assert(rd16(m) == 288 && rd16(m + 2) == 40 && m[28] == 24);
    if (argc > 2) write_file(argv[2], packet, n);
    puts("agent feed: ok");
    return 0;
}
