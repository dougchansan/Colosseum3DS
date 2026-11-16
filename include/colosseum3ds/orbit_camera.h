#ifndef COLOSSEUM3DS_ORBIT_CAMERA_H
#define COLOSSEUM3DS_ORBIT_CAMERA_H

#include "colosseum3ds/field.h"
#include <stdint.h>

/* Optional third-person orbit and detached free camera, ported from the
 * recomp's C-stick camera mod (pkmn-colosseum-recomp mods/freecam_mod.c,
 * FREECAM.md). Not original behaviour; only the main field camera during
 * free player control is changed, and scripted cameras suspend it.
 *
 *   C-stick              orbit around the hero (first person: look)
 *   Circle pad           walk relative to the view
 *   Hold D-pad Up/Down   zoom in / out (35%..250% of the authored distance)
 *   ZL/ZR (or R) + Y     toggle the detached free camera
 *     free camera:       circle pad flies, C-stick looks, L/R down/up
 *   ZL/ZR (or R) + Down  leave the free camera and recenter the view
 *
 * Until the C-stick or zoom is used, the floor's authored camera is shown
 * unchanged. The first use snapshots the authored view's angle and distance
 * around the hero, so a fixed room camera becomes a follow camera that keeps
 * that framing; a new floor takes its own authored framing again while the
 * chosen offsets and zoom carry over. */
#define C3_ORBIT_LOOK_SPEED 2.0f       /* radians per second at full stick */
#define C3_ORBIT_ZOOM_SPEED 1.0f       /* e-folds per second while held */
#define C3_ORBIT_ZOOM_MIN 0.35f
#define C3_ORBIT_ZOOM_MAX 2.5f
#define C3_ORBIT_PITCH_MIN 0.05f       /* radians above the hero's eye line */
#define C3_ORBIT_PITCH_MAX 1.45f
#define C3_ORBIT_TARGET_HEIGHT 10.0f   /* original units above the feet */
#define C3_ORBIT_FREE_SPEED 250.0f     /* original units per second */
#define C3_ORBIT_DEADZONE 15

typedef struct C3OrbitCamera {
    int active;                 /* the view was moved since the last recenter */
    float yaw, pitch, zoom;     /* offsets from the authored framing */
    int based; uint32_t floor;  /* authored framing snapshot for `floor` */
    float base_yaw, base_pitch, base_distance;
    int free;                   /* detached free flight */
    float free_eye[3], free_yaw, free_pitch;
} C3OrbitCamera;

void c3_orbit_reset(C3OrbitCamera *orbit);
/* C-stick orbit and D-pad zoom (third person). Returns 1 when it acted. */
int c3_orbit_input(C3OrbitCamera *orbit, int stick_x, int stick_y, uint16_t held, float seconds);
/* Chords: (ZL/ZR or R) + Y toggles free flight; (ZL/ZR or R) + Down recenters. */
int c3_orbit_free_chord(uint16_t held, uint16_t pressed);
int c3_orbit_recenter_chord(uint16_t held, uint16_t pressed);
/* Free flight starts where `current` looks from. */
void c3_orbit_free_enter(C3OrbitCamera *orbit, const C3TitleCamera *current);
void c3_orbit_free_update(C3OrbitCamera *orbit, int move_x, int move_y, int look_x, int look_y,
                          uint16_t held, float scale, float seconds);
/* Rewrites the authored field camera for the orbit or free view. */
void c3_orbit_apply(C3OrbitCamera *orbit, const C3FieldMap *map, const C3FieldPlayer *player,
                    C3TitleCamera *camera);
/* The camera_yaw c3_field_move expects so circle pad up walks into the view. */
float c3_orbit_move_yaw(const C3TitleCamera *camera);

#endif
