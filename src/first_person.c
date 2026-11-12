#include "colosseum3ds/first_person.h"
#include <math.h>

#define PAD_Z 0x0010u
#define PAD_R 0x0020u
#define PAD_X 0x0400u
#define TWO_PI 6.2831853f
#define PI 3.14159265f

static float wrap(float angle)
{
    if (!isfinite(angle)) return 0.0f;
    angle = fmodf(angle, TWO_PI);
    return angle < 0.0f ? angle + TWO_PI : angle;
}

static void face(C3FirstPerson *view, const C3FieldPlayer *player, uint32_t floor)
{
    view->yaw = player ? wrap(player->yaw) : 0.0f;
    view->pitch = 0.0f;
    view->floor = floor;
    view->touch_active = 0;
}

void c3_first_person_toggle(C3FirstPerson *view, const C3FieldPlayer *player, uint32_t floor)
{
    if (!view) return;
    view->enabled = !view->enabled;
    if (view->enabled) face(view, player, floor);
    view->touch_active = 0;
}

void c3_first_person_arrive(C3FirstPerson *view, const C3FieldPlayer *player, uint32_t floor)
{
    if (view && view->enabled && floor != view->floor) face(view, player, floor);
}

static void turn(C3FirstPerson *view, float yaw, float pitch)
{
    view->yaw = wrap(view->yaw + yaw);
    view->pitch = fmaxf(-C3_FIRST_PERSON_PITCH_LIMIT, fminf(C3_FIRST_PERSON_PITCH_LIMIT, view->pitch + pitch));
}

static float axis(int value)
{
    if (value > 127) value = 127;
    if (value < -127) value = -127;
    if (value > -C3_FIRST_PERSON_DEADZONE && value < C3_FIRST_PERSON_DEADZONE) return 0.0f;
    float sign = value < 0 ? -1.0f : 1.0f;
    return sign * (fabsf((float)value) - C3_FIRST_PERSON_DEADZONE) / (127.0f - C3_FIRST_PERSON_DEADZONE);
}

void c3_first_person_look(C3FirstPerson *view, int stick_x, int stick_y, float seconds)
{
    if (!view || !view->enabled || !isfinite(seconds) || seconds <= 0.0f) return;
    seconds = fminf(seconds, 0.25f);
    /* Ground forward is (sin yaw, cos yaw) and right is (-cos yaw, sin yaw),
     * so turning right decreases yaw. */
    turn(view, -axis(stick_x) * C3_FIRST_PERSON_LOOK_SPEED * seconds,
         axis(stick_y) * C3_FIRST_PERSON_LOOK_SPEED * seconds);
}

void c3_first_person_touch(C3FirstPerson *view, int held, uint16_t x, uint16_t y)
{
    if (!view) return;
    if (!view->enabled || !held) { view->touch_active = 0; return; }
    if (view->touch_active)
        turn(view, -((float)x - view->touch_x) * C3_FIRST_PERSON_TOUCH_SPEED,
             -((float)y - view->touch_y) * C3_FIRST_PERSON_TOUCH_SPEED);
    view->touch_active = 1; view->touch_x = x; view->touch_y = y;
}

float c3_first_person_move_yaw(const C3FirstPerson *view)
{
    /* c3_field_move walks circle-pad up at camera_yaw + pi. */
    return view ? wrap(view->yaw - PI) : 0.0f;
}

void c3_first_person_camera(const C3FirstPerson *view, const C3FieldMap *map,
                            const C3FieldPlayer *player, C3TitleCamera *camera)
{
    if (!view || !map || !player || !camera) return;
    float scale = map->scale > 0.0f ? map->scale : 1.0f;
    *camera = map->camera;
    camera->eye[0] = player->position[0] + sinf(player->yaw) * C3_FIRST_PERSON_EYE_FORWARD * scale;
    camera->eye[1] = player->position[1] + C3_FIRST_PERSON_EYE_HEIGHT * scale;
    camera->eye[2] = player->position[2] + cosf(player->yaw) * C3_FIRST_PERSON_EYE_FORWARD * scale;
    float c = cosf(view->pitch), distance = 10.0f * scale;
    camera->target[0] = camera->eye[0] + sinf(view->yaw) * c * distance;
    camera->target[1] = camera->eye[1] + sinf(view->pitch) * distance;
    camera->target[2] = camera->eye[2] + cosf(view->yaw) * c * distance;
    camera->roll = 0.0f;
    if (!(camera->near_plane > 0.0f) || camera->near_plane > scale) camera->near_plane = scale;
}

int c3_first_person_chord(uint16_t held, uint16_t pressed)
{
    /* Either half completing the chord counts, so press order does not matter. */
    if (!(held & PAD_X) || !(held & (PAD_Z | PAD_R))) return 0;
    return (pressed & (PAD_X | PAD_Z | PAD_R)) != 0;
}
