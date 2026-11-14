#include "colosseum3ds/orbit_camera.h"
#include <math.h>
#include <string.h>

#define PAD_DOWN 0x0004u
#define PAD_UP 0x0008u
#define PAD_Z 0x0010u
#define PAD_R 0x0020u
#define PAD_L 0x0040u
#define PAD_Y 0x0800u

static float stick(int value)
{
    if (value > -C3_ORBIT_DEADZONE && value < C3_ORBIT_DEADZONE) return 0.0f;
    float v = (float)value / 127.0f;
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

void c3_orbit_reset(C3OrbitCamera *orbit)
{
    if (!orbit) return;
    memset(orbit, 0, sizeof *orbit);
    orbit->zoom = 1.0f;
}

int c3_orbit_input(C3OrbitCamera *orbit, int stick_x, int stick_y, uint16_t held, float seconds)
{
    if (!orbit || orbit->free || !(seconds > 0.0f)) return 0;
    if (orbit->zoom <= 0.0f) orbit->zoom = 1.0f;
    float x = stick(stick_x), y = stick(stick_y);
    int acted = 0;
    if (x != 0.0f || y != 0.0f) {
        /* Reversed after hardware testing (2026-11-13): right swings the
         * camera to the hero's left, up lowers the view toward the horizon. */
        orbit->yaw += x * C3_ORBIT_LOOK_SPEED * seconds;
        orbit->pitch -= y * C3_ORBIT_LOOK_SPEED * seconds;
        acted = 1;
    }
    if (!(held & (PAD_Z | PAD_R))) {
        if (held & PAD_UP) { orbit->zoom *= expf(-C3_ORBIT_ZOOM_SPEED * seconds); acted = 1; }
        if (held & PAD_DOWN) { orbit->zoom *= expf(C3_ORBIT_ZOOM_SPEED * seconds); acted = 1; }
    }
    if (orbit->zoom < C3_ORBIT_ZOOM_MIN) orbit->zoom = C3_ORBIT_ZOOM_MIN;
    if (orbit->zoom > C3_ORBIT_ZOOM_MAX) orbit->zoom = C3_ORBIT_ZOOM_MAX;
    if (acted) orbit->active = 1;
    return acted;
}

int c3_orbit_free_chord(uint16_t held, uint16_t pressed)
{
    if (!(held & PAD_Y) || !(held & (PAD_Z | PAD_R))) return 0;
    return (pressed & (PAD_Y | PAD_Z | PAD_R)) != 0;
}

int c3_orbit_recenter_chord(uint16_t held, uint16_t pressed)
{
    if (!(held & PAD_DOWN) || !(held & (PAD_Z | PAD_R))) return 0;
    return (pressed & (PAD_DOWN | PAD_Z | PAD_R)) != 0;
}

void c3_orbit_free_enter(C3OrbitCamera *orbit, const C3TitleCamera *current)
{
    if (!orbit || !current) return;
    float d[3] = {current->target[0] - current->eye[0], current->target[1] - current->eye[1],
                  current->target[2] - current->eye[2]};
    float flat = sqrtf(d[0] * d[0] + d[2] * d[2]);
    memcpy(orbit->free_eye, current->eye, sizeof orbit->free_eye);
    orbit->free_yaw = atan2f(d[0], d[2]);
    orbit->free_pitch = atan2f(d[1], flat > 1e-6f ? flat : 1e-6f);
    orbit->free = 1;
}

void c3_orbit_free_update(C3OrbitCamera *orbit, int move_x, int move_y, int look_x, int look_y,
                          uint16_t held, float scale, float seconds)
{
    if (!orbit || !orbit->free || !(seconds > 0.0f)) return;
    orbit->free_yaw -= stick(look_x) * C3_ORBIT_LOOK_SPEED * seconds;
    orbit->free_pitch += stick(look_y) * C3_ORBIT_LOOK_SPEED * seconds;
    if (orbit->free_pitch > C3_ORBIT_PITCH_MAX) orbit->free_pitch = C3_ORBIT_PITCH_MAX;
    if (orbit->free_pitch < -C3_ORBIT_PITCH_MAX) orbit->free_pitch = -C3_ORBIT_PITCH_MAX;
    float step = C3_ORBIT_FREE_SPEED * scale * seconds;
    float cy = cosf(orbit->free_yaw), sy = sinf(orbit->free_yaw), cp = cosf(orbit->free_pitch), sp = sinf(orbit->free_pitch);
    float forward[3] = {sy * cp, sp, cy * cp}, right[3] = {-cy, 0.0f, sy};
    float f = stick(move_y), r = stick(move_x), u = (held & PAD_R ? 1.0f : 0.0f) - (held & PAD_L ? 1.0f : 0.0f);
    for (int i = 0; i < 3; ++i) orbit->free_eye[i] += (forward[i] * f + right[i] * r) * step;
    orbit->free_eye[1] += u * step;
}

void c3_orbit_apply(C3OrbitCamera *orbit, const C3FieldMap *map, const C3FieldPlayer *player,
                    C3TitleCamera *camera)
{
    if (!orbit || !map || !player || !camera) return;
    if (orbit->free) {
        float cp = cosf(orbit->free_pitch);
        memcpy(camera->eye, orbit->free_eye, sizeof camera->eye);
        camera->target[0] = camera->eye[0] + sinf(orbit->free_yaw) * cp;
        camera->target[1] = camera->eye[1] + sinf(orbit->free_pitch);
        camera->target[2] = camera->eye[2] + cosf(orbit->free_yaw) * cp;
        return;
    }
    if (!orbit->active) return;
    float pivot[3] = {player->position[0], player->position[1] + C3_ORBIT_TARGET_HEIGHT * map->scale, player->position[2]};
    if (!orbit->based || orbit->floor != map->floor) {
        /* Snapshot the authored framing around the hero on this floor. */
        float v[3] = {camera->eye[0] - pivot[0], camera->eye[1] - pivot[1], camera->eye[2] - pivot[2]};
        float distance = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (distance < 1e-3f) return;
        orbit->base_distance = distance;
        orbit->base_yaw = atan2f(v[0], v[2]);
        orbit->base_pitch = asinf(fmaxf(-1.0f, fminf(1.0f, v[1] / distance)));
        orbit->based = 1; orbit->floor = map->floor;
    }
    float pitch = orbit->base_pitch + orbit->pitch;
    if (pitch < C3_ORBIT_PITCH_MIN) { orbit->pitch += C3_ORBIT_PITCH_MIN - pitch; pitch = C3_ORBIT_PITCH_MIN; }
    if (pitch > C3_ORBIT_PITCH_MAX) { orbit->pitch -= pitch - C3_ORBIT_PITCH_MAX; pitch = C3_ORBIT_PITCH_MAX; }
    float yaw = orbit->base_yaw + orbit->yaw, distance = orbit->base_distance * orbit->zoom;
    camera->eye[0] = pivot[0] + sinf(yaw) * cosf(pitch) * distance;
    camera->eye[1] = pivot[1] + sinf(pitch) * distance;
    camera->eye[2] = pivot[2] + cosf(yaw) * cosf(pitch) * distance;
    memcpy(camera->target, pivot, sizeof camera->target);
}

float c3_orbit_move_yaw(const C3TitleCamera *camera)
{
    return atan2f(camera->eye[0] - camera->target[0], camera->eye[2] - camera->target[2]);
}
