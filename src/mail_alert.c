#include "colosseum3ds/mail_alert.h"

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int c3_mail_alert_start(C3MailAlert *a, unsigned tone)
{
    if (!a || a->active) return 0;
    a->active = 1; a->tone = tone; a->frames = 0; a->tone_playing = 1; ++a->started;
    return 1;
}

int c3_mail_alert_visible(const C3MailAlert *a)
{
    return a && a->active && a->frames < C3_MAIL_ALERT_FRAMES && a->frames % 60u < 30u;
}

/* fn_801D2404: draw with the current frame, then advance; the tone's end
 * releases it; frame 600 with no tone ends the alert. */
int c3_mail_alert_tick(C3MailAlert *a, uint32_t frames, int tone_playing)
{
    if (!a || !a->active) return 0;
    int visible = c3_mail_alert_visible(a);
    a->frames = a->frames > UINT32_MAX - frames ? UINT32_MAX : a->frames + frames;
    if (a->tone_playing && !tone_playing) a->tone_playing = 0;
    if (a->frames >= C3_MAIL_ALERT_FRAMES && !a->tone_playing) a->active = 0;
    return visible;
}

int c3_mail_alert_terminate(C3MailAlert *a)
{
    if (!a || !a->active) return 0;
    if (a->frames < C3_MAIL_ALERT_FRAMES) a->frames = C3_MAIL_ALERT_FRAMES;
    return a->tone_playing;
}

int c3_mail_send_eligible(const uint8_t r[44], int received, unsigned stage, float step,
                          C3MailFlagGet flag, C3MailScript script, void *context)
{
    if (!r || received) return 0;
    unsigned rate = be16(r + 6);
    if (rate == 0xffffu) return 0;
    if (step >= (0.015625f * (float)rate) / 1000.0f) return 0;
    unsigned comparison = r[1];
    if (comparison == 0xffu) return 0;
    unsigned expected = be16(r + 4);
    if (expected == 0xffffu) return 0;
    stage &= 0xffffu;
    int eligible = 0;
    switch (comparison) {
    case 0: eligible = stage == expected; break;
    case 1: eligible = stage > expected; break;
    case 2: eligible = stage < expected; break;
    case 3: eligible = stage >= expected; break;
    case 4: eligible = stage <= expected; break;
    case 5: eligible = stage != expected; break;
    case 6: eligible = 1; break;
    default: break;
    }
    if (!eligible) return 0;
    uint32_t condition = be32(r + 0x1c);
    switch (r[0]) {
    case 0: return 1;
    case 1: return condition != UINT32_MAX && flag && flag(context, condition) != 0;
    case 2: return condition != UINT32_MAX && condition != 0 && script && script(context, condition) != 0;
    default: return 0;
    }
}
