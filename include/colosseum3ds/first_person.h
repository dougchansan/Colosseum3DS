#ifndef COLOSSEUM3DS_FIRST_PERSON_H
#define COLOSSEUM3DS_FIRST_PERSON_H

#include "colosseum3ds/field.h"
#include <stdint.h>

/* Optional first-person field view, ported from the recomp's freecam mod
 * (pkmn-colo-recomp mods/freecam_mod.c, "Z + X"). Not original behaviour:
 * it overrides only the main field camera during free player control, and
 * scripted cameras, arrivals and menus suspend it. The eye follows the
 * player's ground transform at 14 original units up and 2 forward; looking
 * turns at 2 rad/s with a level-preserving pitch limit; the circle pad walks
 * relative to the look direction. No camera collision is added. */
#define C3_FIRST_PERSON_EYE_HEIGHT 14.0f
#define C3_FIRST_PERSON_EYE_FORWARD 2.0f
#define C3_FIRST_PERSON_LOOK_SPEED 3.5f      /* radians per second at full stick (raised from 2.0 after hardware testing) */
#define C3_FIRST_PERSON_TOUCH_SPEED 0.012f   /* radians per bottom-screen pixel */
#define C3_FIRST_PERSON_PITCH_LIMIT 1.35f    /* radians; never reaches vertical */
#define C3_FIRST_PERSON_DEADZONE 15          /* of +/-127, the mod's 12 of +/-100 */
/* Player-tuned view (port feature): field of view, eye height and view
 * distance, kept across floors until reset. fov 0 keeps the floor camera's. */
#define C3_FIRST_PERSON_FOV_MIN 30.0f        /* degrees, vertical */
#define C3_FIRST_PERSON_FOV_MAX 100.0f
#define C3_FIRST_PERSON_FOV_SPEED 30.0f      /* degrees per second held */
#define C3_FIRST_PERSON_HEIGHT_MIN 4.0f      /* original units above the feet */
#define C3_FIRST_PERSON_HEIGHT_MAX 40.0f
#define C3_FIRST_PERSON_HEIGHT_SPEED 12.0f   /* original units per second held */
#define C3_FIRST_PERSON_DISTANCE_MIN 0.25f   /* times the floor camera's far plane */
#define C3_FIRST_PERSON_DISTANCE_MAX 4.0f
#define C3_FIRST_PERSON_DISTANCE_SPEED 1.0f  /* doublings per second held */

typedef struct C3FirstPerson {
    int enabled;
    float yaw, pitch;       /* look direction: (sin yaw, cos yaw) on the ground */
    uint32_t floor;         /* floor the look was last placed on */
    int touch_active;
    uint16_t touch_x, touch_y;
    /* Tuned view; zero values mean the defaults (floor fov, EYE_HEIGHT, 1x). */
    float fov, eye_height, view_distance;
} C3FirstPerson;

/* Toggle; entering starts level, facing where the player faces. */
void c3_first_person_toggle(C3FirstPerson *view, const C3FieldPlayer *player, uint32_t floor);
/* A different floor restarts the look level and facing the spawn direction. */
void c3_first_person_arrive(C3FirstPerson *view, const C3FieldPlayer *player, uint32_t floor);
/* C-stick look (+x right, +y up) and bottom-screen drag. Positive dx turns right. */
void c3_first_person_look(C3FirstPerson *view, int stick_x, int stick_y, float seconds);
void c3_first_person_touch(C3FirstPerson *view, int held, uint16_t x, uint16_t y);
/* The camera_yaw c3_field_move expects so that circle pad up walks where the view looks. */
float c3_first_person_move_yaw(const C3FirstPerson *view);
/* The eye camera; projection settings come from the map camera, with a near
 * plane no farther than one original unit so the floor ahead is not clipped. */
void c3_first_person_camera(const C3FirstPerson *view, const C3FieldMap *map,
                            const C3FieldPlayer *player, C3TitleCamera *camera);
/* While enabled, held D-pad Up/Down narrows/widens the field of view,
 * Left/Right lowers/raises the eye, and with L held Up/Down shortens/
 * lengthens the view distance. floor_fov is the floor camera's (the start
 * of a first change). Returns the D-pad bits it used, which the caller
 * keeps from walking. */
uint16_t c3_first_person_adjust(C3FirstPerson *view, uint16_t held, float floor_fov, float seconds);
/* Back to the floor camera's field of view, the default eye and distance. */
void c3_first_person_reset_view(C3FirstPerson *view);
/* The toggle chord: (ZL/ZR or R) + X newly completed this frame. */
int c3_first_person_chord(uint16_t held, uint16_t pressed);

#endif
