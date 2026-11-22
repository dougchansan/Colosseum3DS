/* Private gameplay-harness UDP link; see agent_link.h. Compiled only into
 * C3_AGENT_HARNESS builds (Makefile.3ds filters it out otherwise). */
#ifdef C3_AGENT_HARNESS
#include "agent_link.h"
#include "colosseum3ds/battle_general.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef C3_AGENT_HOST
#define C3_AGENT_HOST "192.168.50.47"
#endif
#ifndef C3_AGENT_PORT
#define C3_AGENT_PORT 4951
#endif
#ifndef C3_AGENT_HZ
#define C3_AGENT_HZ 10
#endif
/* SOC shared buffer: one small datagram at a time needs little. */
#ifndef C3_AGENT_SOC_BYTES
#define C3_AGENT_SOC_BYTES 0x20000u
#endif
#define AGENT_CONFIG "sdmc:/3ds/Colosseum3DS/agent.cfg"

static C3AgentFeed feed;
static int link_state; /* 0 not tried, 1 ready, -1 off or failed */
static int sock = -1;
static u32 *soc_buffer;
static struct sockaddr_in target;
static uint64_t interval_us = 1000000u / C3_AGENT_HZ, next_us;
static uint32_t frame, claimed_frame, sent_frame;

static void configure(char *host, size_t capacity, unsigned *port, int *enabled)
{
    FILE *file = fopen(AGENT_CONFIG, "r");
    char line[96];
    if (!file) return;
    while (fgets(line, sizeof line, file)) {
        char *end = line + strcspn(line, "\r\n#");
        *end = 0;
        if (!strncmp(line, "host=", 5)) {
            size_t n = strlen(line + 5);
            if (n >= capacity) n = capacity - 1;
            memcpy(host, line + 5, n); host[n] = 0;
        }
        else if (!strncmp(line, "port=", 5)) *port = (unsigned)strtoul(line + 5, NULL, 10);
        else if (!strncmp(line, "hz=", 3)) {
            unsigned long hz = strtoul(line + 3, NULL, 10);
            if (hz >= 1 && hz <= 30) interval_us = 1000000u / hz;
        } else if (!strcmp(line, "off")) *enabled = 0;
    }
    fclose(file);
}

static int open_link(void)
{
    char host[64] = C3_AGENT_HOST;
    unsigned port = C3_AGENT_PORT;
    int enabled = 1;
    if (link_state) return link_state > 0;
    link_state = -1;
    configure(host, sizeof host, &port, &enabled);
    if (!enabled || !port || port > 65535u) return 0;
    soc_buffer = memalign(0x1000, C3_AGENT_SOC_BYTES);
    if (!soc_buffer) return 0;
    if (R_FAILED(socInit(soc_buffer, C3_AGENT_SOC_BYTES))) { free(soc_buffer); soc_buffer = NULL; return 0; }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { socExit(); free(soc_buffer); soc_buffer = NULL; return 0; }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    memset(&target, 0, sizeof target);
    target.sin_family = AF_INET;
    target.sin_port = htons((uint16_t)port);
    target.sin_addr.s_addr = inet_addr(host);
    link_state = 1;
    return 1;
}

void c3_agent_link_poll(const C3N3dsInput *input)
{
    /* The previous frame had no scene loop claiming it: say so rather than
     * repeat a stale mode. */
    if (claimed_frame != frame && feed.mode) {
        feed.mode = C3_AGENT_MODE_UNKNOWN;
        feed.has_field = feed.has_battle = 0; feed.actor_count = 0; feed.text[0] = 0;
    }
    ++frame;
    feed.frame = frame;
    if (input) {
        feed.held = input->held; feed.stick_x = input->stick_x; feed.stick_y = input->stick_y;
        feed.touch_held = input->touch_held; feed.touch_x = input->touch_x; feed.touch_y = input->touch_y;
        if ((input->held || input->stick_x || input->stick_y || input->touch_held) && feed.input_frames < 255u) ++feed.input_frames;
        feed.held_latch |= input->held;
        if (abs(input->stick_x) + abs(input->stick_y) > abs(feed.latch_stick_x) + abs(feed.latch_stick_y)) {
            feed.latch_stick_x = input->stick_x; feed.latch_stick_y = input->stick_y;
        }
    }
    if (!open_link()) return;
    uint64_t now = c3_n3ds_time_us();
    if (now < next_us) return;
    next_us = now + interval_us;
    ++feed.sequence;
    feed.time_ms = (uint32_t)(now / 1000u);
    feed.frames_since = (uint16_t)(frame - sent_frame > 65535u ? 65535u : frame - sent_frame);
    uint8_t packet[C3_AGENT_PACKET_MAX];
    size_t bytes = c3_agent_feed_encode(&feed, packet, sizeof packet);
    if (bytes) (void)sendto(sock, packet, bytes, 0, (struct sockaddr *)&target, sizeof target);
    sent_frame = frame;
    feed.input_frames = 0; feed.held_latch = 0; feed.latch_stick_x = feed.latch_stick_y = 0;
}

void c3_agent_link_scene(uint8_t mode)
{
    claimed_frame = frame;
    if (feed.mode == mode) return;
    int field_mode = mode == C3_AGENT_MODE_FIELD || mode == C3_AGENT_MODE_DIALOGUE || mode == C3_AGENT_MODE_MENU ||
                     mode == C3_AGENT_MODE_PDA || mode == C3_AGENT_MODE_EVENT || mode == C3_AGENT_MODE_LOADING;
    if (!field_mode) { feed.has_field = 0; feed.actor_count = 0; feed.text[0] = 0; }
    if (mode != C3_AGENT_MODE_BATTLE) feed.has_battle = 0;
    feed.mode = mode;
}

int c3_agent_link_due(void)
{
    /* One frame ahead (30 fps): the next poll sends. */
    return link_state >= 0 && c3_n3ds_time_us() + 40000u >= next_us;
}

void c3_agent_link_battle(const C3GeneralBattle *b, uint8_t ui_page)
{
    c3_agent_link_scene(C3_AGENT_MODE_BATTLE);
    if (!b || !c3_agent_link_due()) return;
    C3AgentBattle *o = &feed.battle;
    memset(o, 0, sizeof *o);
    o->encounter = b->encounter; o->turn = (uint16_t)b->turn; o->outcome = (uint8_t)b->outcome; o->ui_page = ui_page;
    o->active_count = b->active_count; o->snag = b->snag;
    memcpy(o->active, b->active, sizeof o->active);
    for (unsigned side = 0; side < 2; ++side) {
        unsigned count = b->party_count[side] < C3_AGENT_PARTY ? b->party_count[side] : C3_AGENT_PARTY;
        o->party_count[side] = (uint8_t)count;
        for (unsigned i = 0; i < count; ++i) {
            const C3GeneralMon *m = &b->party[side][i];
            C3AgentMon *t = &o->party[side][i];
            t->species = m->species; t->hp = m->hp; t->max_hp = m->max_hp;
            for (unsigned j = 0; j < 5; ++j) t->stats[j] = m->stats[j + 1];
            memcpy(t->moves, m->moves, sizeof t->moves); memcpy(t->pp, m->pp, sizeof t->pp);
            t->level = m->level; t->types[0] = m->types[0]; t->types[1] = m->types[1]; t->status = m->status;
        }
    }
    feed.has_battle = 1;
}

C3AgentFeed *c3_agent_link_feed(void)
{
    return &feed;
}

void c3_agent_link_close(void)
{
    if (link_state > 0) {
        close(sock); sock = -1;
        socExit();
        free(soc_buffer); soc_buffer = NULL;
    }
    link_state = -1;
}
#endif
