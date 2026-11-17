#include "colosseum3ds/mail_alert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (24 - 8 * i)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct { uint32_t flag_asked, script_asked; int flag_value, script_value; } Probe;
static int probe_flag(void *c, uint32_t id) { Probe *p = c; p->flag_asked = id; return p->flag_value; }
static int probe_script(void *c, uint32_t id) { Probe *p = c; p->script_asked = id; return p->script_value; }

static void record(uint8_t r[44], unsigned type, unsigned comparison, unsigned stage, unsigned rate, uint32_t condition)
{
    memset(r, 0, 44); r[0] = (uint8_t)type; r[1] = (uint8_t)comparison;
    put16(r + 4, stage); put16(r + 6, rate); put32(r + 0x1c, condition);
}

static void alert_lifecycle(void)
{
    C3MailAlert a = {0};
    CHECK(!c3_mail_alert_visible(&a) && !c3_mail_alert_tick(&a, 1, 0));
    CHECK(c3_mail_alert_start(&a, 4) && a.tone == 4 && a.started == 1);
    /* A second mail while the alert runs neither restarts the tone nor the blink. */
    CHECK(!c3_mail_alert_start(&a, 7) && a.tone == 4 && a.started == 1);
    /* Visible on frames 0-29 of each second, hidden 30-59. */
    unsigned shown = 0;
    for (unsigned f = 0; f < 60; ++f) { int v = c3_mail_alert_tick(&a, 1, 1); CHECK(v == (f < 30)); shown += (unsigned)v; }
    CHECK(shown == 30 && a.frames == 60);
    for (unsigned f = 60; f < 600; ++f) c3_mail_alert_tick(&a, 1, 1);
    /* Frame 600: hidden, but still active while the tone plays. */
    CHECK(a.frames == 600 && a.active && !c3_mail_alert_visible(&a));
    CHECK(!c3_mail_alert_tick(&a, 1, 1) && a.active);
    CHECK(!c3_mail_alert_tick(&a, 1, 0) && !a.active);
    CHECK(c3_mail_alert_start(&a, 2) && a.started == 2 && a.frames == 0);
    /* A tone that ends early leaves the envelope blinking to frame 600. */
    c3_mail_alert_tick(&a, 10, 0); CHECK(a.active && !a.tone_playing);
    c3_mail_alert_tick(&a, 589, 0); CHECK(a.active && a.frames == 599);
    c3_mail_alert_tick(&a, 1, 0); CHECK(!a.active);
    /* Large frame steps (a slow frame) do not wrap the counter. */
    CHECK(c3_mail_alert_start(&a, 0));
    c3_mail_alert_tick(&a, UINT32_MAX, 1); CHECK(a.frames == UINT32_MAX && a.active);
    /* Terminate hides the envelope and asks for the tone to stop. */
    a = (C3MailAlert){0}; CHECK(c3_mail_alert_start(&a, 1));
    c3_mail_alert_tick(&a, 5, 1);
    CHECK(c3_mail_alert_terminate(&a) && a.frames == 600 && !c3_mail_alert_visible(&a));
    CHECK(!c3_mail_alert_tick(&a, 1, 0) && !a.active && !c3_mail_alert_terminate(&a));
}

static void send_checks(void)
{
    uint8_t r[44]; Probe p = {0};
    /* Already in the mailbox: never sent again. */
    record(r, 0, 6, 0, 64000, 0);
    CHECK(c3_mail_send_eligible(r, 0, 0, 0.5f, NULL, NULL, NULL));
    CHECK(!c3_mail_send_eligible(r, 1, 0, -1.0f, NULL, NULL, NULL));
    /* Rate: per step the chance is rate/64000; 0xFFFF never. */
    record(r, 0, 6, 0, 640, 0);              /* threshold 0.01 */
    CHECK(c3_mail_send_eligible(r, 0, 0, 0.0099f, NULL, NULL, NULL));
    CHECK(!c3_mail_send_eligible(r, 0, 0, 0.0101f, NULL, NULL, NULL));
    record(r, 0, 6, 0, 0, 0);                /* rate 0: only mailMainSendAllMail */
    CHECK(!c3_mail_send_eligible(r, 0, 0, 0.0f, NULL, NULL, NULL));
    CHECK(c3_mail_send_eligible(r, 0, 0, -1.0f, NULL, NULL, NULL));
    record(r, 0, 6, 0, 0xffff, 0);
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, NULL, NULL, NULL));
    /* Stage comparisons against mailbox +0x444. */
    static const struct { unsigned cmp, stage; int want; } cases[] = {
        {0, 5, 1}, {0, 4, 0}, {1, 6, 1}, {1, 5, 0}, {2, 4, 1}, {2, 5, 0}, {3, 5, 1}, {3, 4, 0},
        {4, 5, 1}, {4, 6, 0}, {5, 4, 1}, {5, 5, 0}, {6, 0, 1}, {7, 5, 0}, {0xff, 5, 0},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        record(r, 0, cases[i].cmp, 5, 0, 0);
        CHECK(c3_mail_send_eligible(r, 0, cases[i].stage, -1.0f, NULL, NULL, NULL) == cases[i].want);
    }
    record(r, 0, 6, 0xffff, 0, 0);
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, NULL, NULL, NULL));
    /* Condition type 1: a flag; type 2: a script returning nonzero. */
    record(r, 1, 6, 0, 0, 0xa7c);
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, NULL, NULL, NULL));
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p) && p.flag_asked == 0xa7c);
    p.flag_value = 1; CHECK(c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p));
    record(r, 1, 6, 0, 0, UINT32_MAX); p.flag_asked = 0;
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p) && !p.flag_asked);
    record(r, 2, 6, 0, 0, 0x5960018);
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p) && p.script_asked == 0x5960018);
    p.script_value = 3; CHECK(c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p));
    record(r, 2, 6, 0, 0, 0); p.script_asked = 0;
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p) && !p.script_asked);
    record(r, 3, 6, 0, 0, 0);
    CHECK(!c3_mail_send_eligible(r, 0, 0, -1.0f, probe_flag, probe_script, &p));
}

/* Optional PRIVATE pack: every original record, as the US release ships it.
 * All rates are 0, so the hero-step callback never sends mail; story mail
 * arrives only from scripts (native AD). */
static void original_records(const char *path)
{
    FILE *f = fopen(path, "rb"); CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END)); long size = ftell(f); CHECK(size > 64 && !fseek(f, 0, SEEK_SET));
    uint8_t *d = malloc((size_t)size); CHECK(d && fread(d, 1, (size_t)size, f) == (size_t)size); fclose(f);
    CHECK(!memcmp(d, "C3PD", 4));
    uint32_t n = le32(d + 12); unsigned found = 0, by_step = 0, sent_all = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *e = d + 64 + i * 16;
        if (le32(e) != 3) continue;
        CHECK(le32(e + 12) == 44 && le32(e + 8) + 44 <= (uint32_t)size);
        const uint8_t *r = d + le32(e + 8); ++found;
        for (unsigned k = 0; k < 64; ++k) by_step += (unsigned)c3_mail_send_eligible(r, 0, 0, (float)k / 64.0f, NULL, NULL, NULL);
        sent_all += (unsigned)c3_mail_send_eligible(r, 0, 0, -1.0f, NULL, NULL, NULL);
    }
    free(d);
    CHECK(found == 32 && by_step == 0);
    printf("mail original records=%u step_sends=%u send_all=%u\n", found, by_step, sent_all);
}

int main(int argc, char **argv)
{
    alert_lifecycle();
    send_checks();
    if (argc > 1) original_records(argv[1]);
    printf("mail alert: %u checks passed\n", checks);
    return 0;
}
