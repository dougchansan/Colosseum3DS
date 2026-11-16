#ifndef C3_DEBUG_OVERLAY_H
#define C3_DEBUG_OVERLAY_H
#include "n3ds_platform.h"
#include "title_renderer.h"
#include "colosseum3ds/title.h"

/* Development overlay on the top screen for tuning the look of a scene:
 * frame rate, frame time, CPU draw preparation, GPU time, draw counts,
 * the camera (eye, target, distance, field of view and zoom against the
 * authored field of view) and up to three scene lines. Drawn through the
 * stage renderer's development HUD (libctru font), never in release looks
 * unless toggled.
 *
 * Toggle: hold L and R and press START (the START press is consumed).
 * Starts enabled when sdmc:/3ds/Colosseum3DS/debug-overlay exists. */

/* Call once per polled frame, before the scene reads input. */
void c3_debug_overlay_input(C3N3dsInput *input);
int c3_debug_overlay_enabled(void);
/* Call after each submitted frame of a stage-renderer scene. camera and
 * stats may be NULL. authored_fov is the scene's reference field of view
 * (0 when there is none); zoom is authored_fov / camera fov. extra lines
 * may be NULL; each is cut at 48 characters. */
void c3_debug_overlay_frame(const char *scene, const C3TitleCamera *camera, float authored_fov,
                            const C3TitleRenderStats *stats, const char *extra1, const char *extra2,
                            const char *extra3);
#endif
