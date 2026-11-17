#ifndef C3_MAIL_ALERT_H
#define C3_MAIL_ALERT_H
#include <stddef.h>
#include <stdint.h>

/* Receiving P*DA mail (mailMain.c), separated from the screen and audio.
 *
 * mailMainSendByScrpt (native AD) adds a mail to the mailbox once and, if
 * no alert is running, starts one (mailMainStartSe): the selected receive
 * tone plays and fn_801D2404 blinks the new-mail envelope (texture
 * 0x0C881200) for 600 frames, visible while frames % 60 < 30. The alert
 * ends once 600 frames have passed and the tone has stopped.
 * mailMainReceiveTerminate jumps to frame 600 and stops the tone. */
#define C3_MAIL_ALERT_FRAMES 600u
typedef struct {
    int active;
    unsigned tone;      /* mailbox +0x443 selection the tone was started with */
    uint32_t frames;    /* lbl_80467390[1] */
    int tone_playing;   /* lbl_80467390[2] != 0 */
    uint32_t started;   /* alerts started, for traces and tests */
} C3MailAlert;

/* 1 when a new alert starts (the caller plays `tone`); 0 while one runs,
 * as the original does not restart the tone. */
int c3_mail_alert_start(C3MailAlert *alert, unsigned tone);
/* Advance by `frames` 60 Hz frames. tone_playing is the audio's status;
 * returns the envelope's visibility for this frame. */
int c3_mail_alert_tick(C3MailAlert *alert, uint32_t frames, int tone_playing);
int c3_mail_alert_visible(const C3MailAlert *alert);
/* mailMainReceiveTerminate: the envelope stops; returns 1 when the caller
 * must stop the tone. */
int c3_mail_alert_terminate(C3MailAlert *alert);

/* chkMailSend (mailMain.c) on one 44-byte original mail record (common REL
 * mail data +0x138, P*DA pack kind 3): +0 condition type, +1 comparison,
 * +4 stage compared with mailbox +0x444, +6 send rate, +0x1C condition.
 * step is the hero-step random value in [0,1) (cbStep) or -1 for
 * mailMainSendAllMail. flag answers condition type 1 (GSflagGet), script
 * type 2 (fn_800F7434, nonzero eligible); a missing callback is ineligible.
 * Returns 1 when the mail would be sent now. */
typedef int (*C3MailFlagGet)(void *context, uint32_t flag);
typedef int (*C3MailScript)(void *context, uint32_t script);
int c3_mail_send_eligible(const uint8_t record[44], int received, unsigned stage, float step,
                          C3MailFlagGet flag, C3MailScript script, void *context);

#endif
