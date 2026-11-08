#ifndef COLOSSEUM3DS_RENDER_RECOVERY_H
#define COLOSSEUM3DS_RENDER_RECOVERY_H

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    C3_RECOVERY_RESUME,
    C3_RECOVERY_RESUME_PAUSED,
    C3_RECOVERY_DRAINED,
    C3_RECOVERY_EXIT
} C3RenderRecoveryResult;

typedef enum {
    C3_RECOVER_WAIT,
    C3_RECOVER_DRAIN,
    C3_RECOVER_EXIT
} C3RenderRecoveryMode;

/* Optional scene-owned hook: 1 at the watchdog pause, 0 before returning.
 * Clearing/replacing a paused hook first releases the previous owner. */
void c3_render_recovery_set_pause_callback(void (*callback)(int));

/* Cooperatively wait for a busy renderer, or safely drain it for exit. Keeps
 * polling input while APT/HID return. No gameplay or save writes occur here.
 * RESUME_PAUSED means reset the scene's delta-time baseline. DRAINED and EXIT
 * mean GPU teardown completed and scene-owned assets may now be released;
 * EXIT additionally means a latched application-exit request. DRAIN is for
 * scene transitions and promotes a fresh exit request to EXIT.
 * A true GPU/system wedge can still require physical recovery. */
C3RenderRecoveryResult c3_render_recover(FILE *trace, const char *scene,
                                        unsigned submitted_frames, C3RenderRecoveryMode mode);

#endif
