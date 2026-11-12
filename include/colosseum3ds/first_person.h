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
#define C3_FIRST_PERSON_LOOK_SPEED 2.0f      /* radians per second at full stick */
#define C3_FIRST_PERSON_TOUCH_SPEED 0.012f   /* radians per bottom-screen pixel */
#define C3_FIRST_PERSON_PITCH_LIMIT 1.35f    /* radians; never reaches vertical */
#define C3_FIRST_PERSON_DEADZONE 15          /* of +/-127, the mod's 12 of +/-100 */

typedef struct C3FirstPerson {
    int enabled;
    float yaw, pitch;       /* look direction: (sin yaw, cos yaw) on the ground */
    uint32_t floor;         /* floor the look was last placed on */
    int touch_active;
    uint16_t touch_x, touch_y;
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
/* The toggle chord: (ZL/ZR or R) + X newly completed this frame. */
int c3_first_person_chord(uint16_t held, uint16_t pressed);

#endif
