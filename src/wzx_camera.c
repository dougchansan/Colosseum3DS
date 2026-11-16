#include "colosseum3ds/wzx_camera.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Original GC units per native unit and back (the stage's 0.1 basis). */
#define TO_GC 10.0f
#define TO_NATIVE 0.1f
/* cameraMoveTargetOfs(7, ofs, 0.2f) in fn_801D2D28: 0.2 s at 60 Hz. */
#define TARGET_MOVE_TICKS 12u

static int fail(char *e, size_t n, const char *s) { if (e && n) snprintf(e, n, "%s", s); return 0; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float word_float(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
static float le_float(const uint8_t *p) { return word_float(le32(p)); }
static uint32_t fnv1a(const uint8_t *b, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}

/* ---- wzx_camera.c3k (tools/prepare_wzx_camera.py) ---------------------- */

int c3_wzx_camera_tables_decode(C3WzxCameraTables *out, const void *bytes, size_t size, char *e, size_t n)
{
    const uint8_t *b = bytes;
    if (!out || !b || size != C3_WZX_CAMERA_FILE_BYTES || memcmp(b, "C3WK", 4) || le32(b + 4) != 1 ||
        le32(b + 8) != size || le32(b + 12) != C3_WZX_CAMERA_SHORT_ROWS || le32(b + 16) != C3_WZX_CAMERA_LONG_ROWS ||
        le32(b + 20) != C3_WZX_CAMERA_ROW_WORDS || le32(b + 24) || le32(b + 28))
        return fail(e, n, "Unsupported WZX camera table header.");
    if (le32(b + 1668) != fnv1a(b, 1668)) return fail(e, n, "WZX camera table checksum mismatch.");
    C3WzxCameraTables t;
    memset(&t, 0, sizeof t);
    for (unsigned r = 0; r < C3_WZX_CAMERA_SHORT_ROWS + C3_WZX_CAMERA_LONG_ROWS; ++r) {
        uint32_t *row = r < C3_WZX_CAMERA_SHORT_ROWS ? t.short_rows[r] : t.long_rows[r - C3_WZX_CAMERA_SHORT_ROWS];
        for (unsigned w = 0; w < C3_WZX_CAMERA_ROW_WORDS; ++w) row[w] = le32(b + 32 + 76 * r + 4 * w);
        float weight = word_float(row[0]);
        if (!isfinite(weight) || weight < 0 || weight > 1) return fail(e, n, "Invalid WZX camera pattern weight.");
        for (unsigned s = 0; s < 2; ++s)
            for (unsigned k = 0; k < 4; ++k)
                if (!isfinite(word_float(row[1 + 9 * s + 2 + k]))) return fail(e, n, "Invalid WZX camera pattern threshold.");
    }
    for (unsigned i = 0; i < 6; ++i) {
        t.scale[i] = le_float(b + 1628 + 4 * i);
        if (!isfinite(t.scale[i]) || t.scale[i] <= 0 || t.scale[i] > 100) return fail(e, n, "Invalid WZX camera scale.");
    }
    t.idle_gate = le_float(b + 1652);
    if (!isfinite(t.idle_gate)) return fail(e, n, "Invalid idle camera gate.");
    t.initial_previous_motion = (int32_t)le32(b + 1656);
    t.initial_idle_last = le32(b + 1660);
    t.initial_idle_timer = (int32_t)le32(b + 1664);
    *out = t;
    if (e && n) e[0] = 0;
    return 1;
}

int c3_wzx_camera_tables_load(C3WzxCameraTables *out, const char *path, char *e, size_t n)
{
    uint8_t bytes[C3_WZX_CAMERA_FILE_BYTES + 1];
    if (!out || !path) return fail(e, n, "Missing WZX camera table path.");
    FILE *f = fopen(path, "rb");
    if (!f) return fail(e, n, "Cannot open WZX camera tables.");
    size_t count = fread(bytes, 1, sizeof bytes, f);
    int io = ferror(f);
    if (fclose(f)) io = 1;
    if (io) return fail(e, n, "Cannot read WZX camera tables.");
    return c3_wzx_camera_tables_decode(out, bytes, count, e, n);
}

/* ---- cosmetic random stream (fn_800E0BE4 / _fadeEffectGetRandom) ----- */

static uint32_t random_word(C3WzxCamera *c)
{
    uint32_t x = c->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->rng = x;
    ++c->random_draws;
    return x;
}
static float random_unit(C3WzxCamera *c) { return (float)(random_word(c) >> 8) * (1.0f / 16777216.0f); }
static uint32_t random_below(C3WzxCamera *c, uint32_t bound)
{
    uint32_t a = random_word(c) & 65535u, b = random_word(c) & 65535u;
    return bound ? ((b << 16) | a) % bound : 0;
}

/* ---- PKX timing row (owner table + entry * 0xD4) ---------------------- */

static uint32_t row_count(const C3WzxCameraActor *a, unsigned entry)
{
    if (a->timing_count[entry]) return a->timing_count[entry];
    for (int i = 3; i >= 0; --i) if (a->timing[entry][i]) return (uint32_t)i + 1;
    return 1;
}
/* Word index from the row start: words 3..6 are the marks at +0x0C..+0x18.
 * Other words are fields this module does not have; they read as 0. */
static int32_t row_word(const C3WzxCameraActor *a, unsigned entry, uint32_t word)
{
    return word >= 3 && word <= 6 ? a->timing[entry][word - 3] : 0;
}
/* _DoPosition: duration = row[count + 1], else row[count + 2]. */
static int32_t row_duration(const C3WzxCameraActor *a, unsigned entry)
{
    uint32_t count = row_count(a, entry);
    int32_t d = row_word(a, entry, count + 1);
    return d ? d : row_word(a, entry, count + 2);
}
static int32_t shifted(int32_t v, unsigned shift) { return (int32_t)((uint32_t)v << shift); }

/* ---- camera.c, mode 7 (_cameraDynamicUpdate) --------------------------- */

static void move_start(C3WzxCameraMove *m, const float *from, const float *to, unsigned n, int32_t duration)
{
    memcpy(m->start, from, n * sizeof(float));
    memcpy(m->end, to, n * sizeof(float));
    m->ticks = 0;
    m->duration = duration > 0 ? (uint32_t)duration : 0;
    m->active = 1;
}
static void move_advance(C3WzxCameraMove *m, float *out, unsigned n)
{
    ++m->ticks;
    if (m->ticks >= m->duration) {
        memcpy(out, m->end, n * sizeof(float));
        m->ticks = 0;
        m->active = 0;
        return;
    }
    float t = (float)m->ticks / (float)m->duration;
    for (unsigned k = 0; k < n; ++k) out[k] = m->start[k] + (m->end[k] - m->start[k]) * t;
}

/* cameraUpdate 0x80177A64, mode 7: advance the active moves (time += 1/60),
 * derive height/distance (and yaw during a pure position move) from the
 * eye, or refresh the target base from the owner when nothing moves; then
 * eye = position + RotY(yaw) (0, height, distance). On the tick a position
 * move snaps, the move is already inactive, so the yaw of the previous tick
 * is kept with the final distance (a slight turn the source also makes). */
static void camera_update(C3WzxCamera *c, const C3WzxCameraActor *a)
{
    if (c->moving) {
        if (c->target_move.active) move_advance(&c->target_move, c->view, 3);
        if (c->eye_move.active) move_advance(&c->eye_move, c->eye, 3);
        if (c->yaw_move.active) move_advance(&c->yaw_move, &c->yaw, 1);
        float d[3] = { c->eye[0] - c->position[0], c->eye[1] - c->position[1], c->eye[2] - c->position[2] };
        c->cam_height = d[1];
        c->cam_distance = sqrtf(d[0] * d[0] + d[2] * d[2]);
        if (!c->yaw_move.active && c->eye_move.active) c->yaw = (float)atan2((double)d[0], (double)d[2]);
        if (!c->target_move.active && !c->eye_move.active && !c->yaw_move.active) c->moving = 0;
    } else {
        for (unsigned k = 0; k < 3; ++k) c->position[k] = a->position[k] * TO_GC; /* cameraRefreshTargetPos, no part */
    }
    c->eye[0] = c->position[0] + sinf(c->yaw) * c->cam_distance;
    c->eye[1] = c->position[1] + c->cam_height;
    c->eye[2] = c->position[2] + cosf(c->yaw) * c->cam_distance;
}
/* cameraSetDistance/Height/RotY then cameraUpdate. */
static void set_orbit(C3WzxCamera *c, const C3WzxCameraActor *a, float distance, float height, float yaw)
{
    c->cam_distance = distance;
    c->cam_height = height;
    c->yaw = yaw;
    camera_update(c, a);
}
static void move_eye(C3WzxCamera *c, const float to[3], int32_t duration)
{
    move_start(&c->eye_move, c->eye, to, 3, duration);
    c->moving = 1;
}
static void set_fov(C3WzxCamera *c, float fov)
{
    c->fov = fov < 3.0f ? 3.0f : fov > 120.0f ? 120.0f : fov; /* cameraSetFov */
}
static void publish(C3WzxCamera *c)
{
    c->camera = c->restored;
    for (unsigned k = 0; k < 3; ++k) {
        c->camera.eye[k] = c->eye[k] * TO_NATIVE;
        c->camera.target[k] = (c->position[k] + c->view[k]) * TO_NATIVE;
    }
    c->camera.roll = 0;
    c->camera.fov_y_degrees = c->fov;
}

/* ---- _CalculateParams 0x801D4FB8 -------------------------------------- */

static void calculate_params(C3WzxCamera *c, const C3WzxCameraActor *a, uint32_t flags)
{
    float size_scale, distance_scale, lower, upper;
    int32_t mode = a->scale_selector;
    c->rotation_base = a->yaw;
    switch (mode) {
    case -2: case -1: size_scale = 1.1f; distance_scale = 0.875f; break;
    case 1: size_scale = 1.25f; distance_scale = 1.4f; break;
    case 2: size_scale = 1.3f; distance_scale = 1.8f; break;
    case 3: size_scale = 1.5f; distance_scale = 3.0f; break;
    default: size_scale = 1.2f; distance_scale = 1.0f; break;
    }
    if (flags & 1) { c->rotation_min = 0.0f; c->rotation_max = 0.5237035f; }
    else if (flags & 2) { c->rotation_min = 0.31415927f; c->rotation_max = 0.62831855f; }
    else if (flags & 4) {
        if (mode > 0) { c->rotation_min = 0.31415927f; c->rotation_max = 0.62831855f; }
        else { c->rotation_min = 0.62831855f; c->rotation_max = 0.9424779f; }
    } else if (flags & 8) {
        if (mode > 0) { c->rotation_min = 0.62831855f; c->rotation_max = 0.9424779f; }
        else { c->rotation_min = 0.9424779f; c->rotation_max = 1.2566371f; }
    } else if (flags & 0x10) { c->rotation_min = 1.2566371f; c->rotation_max = 1.5707964f; }
    else if (mode > 0) { c->rotation_min = 0.31415927f; c->rotation_max = 0.9424779f; }
    else { c->rotation_min = 0.31415927f; c->rotation_max = 1.0995574f; }

    if (flags & 0x20) { lower = 6.0f; upper = 8.0f; c->distance_min = 25.0f; c->distance_max = 35.0f; }
    else if (flags & 0x40) { c->distance_min = 35.0f; c->distance_max = 50.0f; lower = 6.0f; upper = 11.0f; }
    else if (flags & 0x80) { c->distance_min = 50.0f; c->distance_max = 60.0f; lower = 6.0f; upper = 20.0f; }
    else { c->distance_min = upper = 20.0f; c->distance_max = 60.0f; lower = 6.0f; }

    /* GSmodelGetBound: min +0x10, max +0x1C, size +0x28 (= max - min). */
    float extent[3];
    for (unsigned k = 0; k < 3; ++k) extent[k] = (a->bound_max[k] - a->bound_min[k]) * TO_GC;
    c->distance_min *= distance_scale;
    c->distance_max *= distance_scale;
    c->size = 0.57735026f * sqrtf(extent[0] * extent[0] + extent[1] * extent[1] + extent[2] * extent[2]);
    c->height = extent[1];
    c->height_min = a->bound_min[1] * TO_GC;
    c->height_max = a->bound_max[1] * TO_GC;
    if (c->height_min < lower) { c->height_min = lower; if (c->height_max < lower) c->height_max = 1.5f * lower; }
    if (c->height_max > upper) { c->height_max = upper; if (c->height_min > upper) c->height_min = 0.8f * upper; }
    c->size *= size_scale;
    c->height *= size_scale;
    if (c->distance_min < 20.0f) c->distance_min = 20.0f;
    if (c->distance_max < 40.0f) c->distance_max = 40.0f;
    if (c->distance_min > 48.0f) c->distance_min = 48.0f;
    if (c->distance_max > 60.0f) c->distance_max = 60.0f;
}

/* ---- _SelectMotion 0x801D4DA0 ----------------------------------------- */

static void select_motion(C3WzxCamera *c, const C3WzxCameraActor *a, const C3WzxCameraSequence *seq)
{
    uint8_t o0 = 0, o1 = 0, o2 = 0, o3 = 0;
    int count = 0;
    if (a->model_id == 0x13A) { c->motion = 4; return; }
    if (seq) {
        uint32_t flags = seq->flags;
        if (flags & 1) { c->motion = 5; return; }
        o0 = (flags >> 3) & 1; o1 = (flags >> 4) & 1; o2 = (flags >> 5) & 1; o3 = (flags >> 6) & 1;
        count = o0 + o1 + o2 + o3;
    }
    if (count == 0) { count = 4; o0 = o1 = o2 = o3 = 1; }
    else if (count == 1) {
        if (o0) c->motion = 3;
        if (o1) c->motion = 0;
        if (o2) c->motion = 1;
        if (o3) c->motion = 2;
        c->previous_motion = (int32_t)c->motion;
        return;
    }
    float interval = 1.0f / (float)count;
    /* The source loops until a draw lands on a bin other than the previous
     * motion; a repeat falls through to the next enabled option. Bounded. */
    for (unsigned guard = 0; guard < 4096; ++guard) {
        float r = random_unit(c), limit = interval;
        if (o0) { if (limit > r) { c->motion = 3; if (c->previous_motion != 3) break; } limit += interval; }
        if (o1) { if (limit > r) { c->motion = 0; if (c->previous_motion != 0) break; } limit += interval; }
        if (o2) { if (limit > r) { c->motion = 1; if (c->previous_motion != 1) break; } limit += interval; }
        if (o3 && limit > r) { c->motion = 2; if (c->previous_motion != 2) break; }
    }
    c->previous_motion = (int32_t)c->motion;
}

/* ---- _DoDollyPosition 0x801D3F7C / _DoPosition 0x801D349C ------------- */

static void do_dolly(C3WzxCamera *c, const C3WzxCameraActor *a, int32_t duration)
{
    float threshold0 = 0.5f, threshold1 = 0.75f;
    int32_t delay;
    int alternate;
    duration = shifted(duration, c->shift);
    if (random_unit(c) <= 0.7f) { threshold0 = 0.3f; threshold1 = 0.65f; alternate = 1; }
    else alternate = 0;
    float chance = random_unit(c);
    if (chance < threshold0) delay = 0;
    else if (chance < threshold1) {
        int32_t full = duration;
        delay = 0;
        duration = (int32_t)random_below(c, (uint32_t)duration);
        if (duration < (full >> 1)) duration = full >> 1;
    } else {
        delay = (int32_t)random_below(c, (uint32_t)duration);
        if (delay > (duration >> 1)) delay = duration >> 1;
    }
    float d0 = c->distance_min + (c->distance_max - c->distance_min) * random_unit(c), near = d0;
    float d1 = c->distance_min + (c->distance_max - c->distance_min) * random_unit(c), far = d1;
    if (alternate) { if (d0 < d1) { far = d0; near = d1; } }
    else if (d0 > d1) { far = d0; near = d1; }
    float height = c->height_min + (c->height_max - c->height_min) * random_unit(c);
    float rotation = c->reverse
        ? (c->rotation_base - c->rotation_min) - (c->rotation_max - c->rotation_min) * random_unit(c)
        : (c->rotation_max - c->rotation_min) * random_unit(c) + (c->rotation_base + c->rotation_min);
    set_orbit(c, a, near, height, rotation);
    float to[3] = { c->eye[0] - c->position[0], 0.0f, c->eye[2] - c->position[2] };
    float scale = far / near;
    to[0] *= scale; to[2] *= scale;
    to[1] = height;
    for (unsigned k = 0; k < 3; ++k) to[k] += c->position[k];
    move_eye(c, to, duration - delay);
    c->range[0] = sqrtf(near * near + height * height);
    float middle = 0.5f * (near + far);
    c->range[1] = sqrtf(middle * middle + height * height);
    c->range[2] = sqrtf(far * far + height * height);
}

static void still_ranges(C3WzxCamera *c, float distance, float height)
{
    float length = sqrtf(distance * distance + height * height);
    c->range[0] = c->range[1] = c->range[2] = length;
}

static float rotation_draw(C3WzxCamera *c)
{
    return c->reverse
        ? (c->rotation_base - c->rotation_min) - (c->rotation_max - c->rotation_min) * random_unit(c)
        : (c->rotation_max - c->rotation_min) * random_unit(c) + (c->rotation_base + c->rotation_min);
}

static void do_position(C3WzxCamera *c, const C3WzxCameraActor *a)
{
    int32_t duration = row_duration(a, c->entry);
    switch (c->motion) {
    case 0:
        do_dolly(c, a, duration);
        break;
    case 1: { /* translation along world x, yaw 0 */
        duration = shifted(duration, c->shift);
        float offset = 10.0f * random_unit(c) + 25.0f;
        float height = 9.0f * random_unit(c) + 1.0f;
        float distance = 30.0f * random_unit(c) + 20.0f;
        set_orbit(c, a, distance, height, 0.0f);
        float to[3] = { c->eye[0], c->eye[1], c->eye[2] };
        to[0] += c->reverse ? 10.0f : -10.0f;
        memcpy(c->eye, to, sizeof to); /* GSscene_SetCameraDirectionVector */
        to[0] += c->reverse ? offset : -offset;
        move_eye(c, to, duration);
        float square = offset * offset + height * height;
        c->range[0] = sqrtf(square);
        c->range[2] = sqrtf(distance * distance + square);
        c->range[1] = 0.5f * (c->range[0] + c->range[2]);
        break;
    }
    case 2: { /* orbit */
        duration = shifted(duration, c->shift);
        float distance = c->distance_min + (c->distance_max - c->distance_min) * random_unit(c);
        float height = c->height_min + (c->height_max - c->height_min) * random_unit(c);
        float a0 = rotation_draw(c), a1 = rotation_draw(c);
        if (a0 > a1) { float t = a0; a0 = a1; a1 = t; }
        set_orbit(c, a, distance, height, a0);
        move_start(&c->yaw_move, &c->yaw, &a1, 1, duration); /* cameraMoveRotationXYZ(0, a1, 0) */
        c->moving = 1;
        still_ranges(c, distance, height);
        break;
    }
    case 3: { /* still */
        duration = shifted(duration, c->shift);
        float distance = c->distance_min + (c->distance_max - c->distance_min) * random_unit(c);
        float height = c->height_min + (c->height_max - c->height_min) * random_unit(c);
        float rotation = rotation_draw(c);
        set_orbit(c, a, distance, height, rotation);
        float to[3] = { c->eye[0], c->eye[1], c->eye[2] };
        move_eye(c, to, duration);
        still_ranges(c, distance, height);
        break;
    }
    case 4: { /* fixed far view; retail does not shift this duration */
        float rotation = c->reverse ? c->rotation_base - 0.47123894f : 0.47123894f + c->rotation_base;
        set_orbit(c, a, 110.0f, 25.0f, rotation);
        float to[3] = { c->eye[0], c->eye[1], c->eye[2] };
        move_eye(c, to, duration);
        still_ranges(c, 110.0f, 25.0f);
        break;
    }
    case 5: { /* flags & 1 */
        float scale;
        duration = shifted(duration, c->shift);
        switch (a->scale_selector) {
        case -2: case -1: scale = 0.875f; break;
        case 1: scale = 1.4f; break;
        case 2: scale = 1.8f; break;
        case 3: scale = 3.0f; break;
        default: scale = 1.0f; break;
        }
        float distance = 50.0f * scale;
        float height = c->height_min + (c->height_max - c->height_min) * random_unit(c);
        float rotation = c->reverse ? c->rotation_base - 0.7853982f : 0.7853982f + c->rotation_base;
        set_orbit(c, a, distance, height, rotation);
        float to[3] = { c->eye[0], c->eye[1], c->eye[2] };
        move_eye(c, to, duration);
        still_ranges(c, distance, height);
        break;
    }
    }
}

/* ---- _DoFOV 0x801D44C4, GetPattern 0x801D2B4C, _SelectDuration 0x801D49D8 */

static void fov_range(const C3WzxCamera *c, float radius, float *low, float *high)
{
    float span = 0.75f * c->size;
    if (c->height > span) span = c->height;
    *low = 57.29578f * (2.0f * (float)atan2((double)(0.5f * span), (double)radius));
    *high = 57.29578f * (2.0f * (float)atan2((double)(2.0f * span), (double)radius));
    if (*low < 15.0f) *low = 15.0f;
    if (*high < 15.0f) *high = 15.0f;
    if (*low > 85.0f) *low = 85.0f;
    if (*high > 85.0f) *high = 85.0f;
}
/* Locals persist across calls inside one _DoFOV, as in the source. */
static void fov_mix(uint32_t choice, float *start, float *end)
{
    int first = 0, second = 0;
    if ((choice & 1) == 1) { *start = 0.0f; first = 1; *end = 0.2f; }
    if ((choice & 2) == 2) { if (!first) *start = 0.35f; *end = 0.6f; second = 1; }
    if ((choice & 4) == 4) { if (!second) *start = 0.75f; *end = 1.0f; }
}
static const uint32_t *get_pattern(C3WzxCamera *c, int short_table, uint32_t flags)
{
    float frame = random_unit(c), end = 0.0f;
    const uint32_t (*table)[C3_WZX_CAMERA_ROW_WORDS] = short_table ? c->tables->short_rows : c->tables->long_rows;
    unsigned count = short_table ? C3_WZX_CAMERA_SHORT_ROWS : C3_WZX_CAMERA_LONG_ROWS;
    if (flags & 0x20) return table[short_table ? 4 : 9];
    if (flags & 0x40) return table[short_table ? 5 : 10];
    if (flags & 0x80) return table[short_table ? 6 : 11];
    for (unsigned i = 0; i < count; ++i) {
        end += word_float(table[i][0]);
        if (frame < end) return table[i];
    }
    return table[random_below(c, count)];
}
static int32_t cap(int32_t d, int32_t limit) { return d < limit ? d : limit; }
static int32_t select_duration(C3WzxCamera *c, uint32_t mode, const uint32_t *th, int32_t d)
{
    float r;
    switch (mode) {
    case 1: return cap(d, 12);
    case 2: return cap(d, 20);
    case 4: return cap(d, 45);
    case 8: return d;
    case 3: r = random_unit(c); return r < word_float(th[0]) ? cap(d, 12) : cap(d, 20);
    case 5: r = random_unit(c); return r < word_float(th[0]) ? cap(d, 12) : cap(d, 45);
    case 9: r = random_unit(c); return r < word_float(th[0]) ? cap(d, 12) : d;
    case 6: r = random_unit(c); return r < word_float(th[1]) ? cap(d, 20) : cap(d, 45);
    case 10: r = random_unit(c); return r < word_float(th[1]) ? cap(d, 20) : d;
    case 12: r = random_unit(c); return r < word_float(th[2]) ? cap(d, 45) : d;
    case 7: r = random_unit(c);
        return r < word_float(th[0]) ? cap(d, 12) : r < word_float(th[1]) ? cap(d, 20) : cap(d, 45);
    case 11: r = random_unit(c);
        return r < word_float(th[0]) ? cap(d, 12) : r < word_float(th[1]) ? cap(d, 20) : d;
    case 13: r = random_unit(c);
        return r < word_float(th[0]) ? cap(d, 12) : r < word_float(th[2]) ? cap(d, 45) : d;
    case 14: r = random_unit(c);
        return r < word_float(th[1]) ? cap(d, 12) : r < word_float(th[2]) ? cap(d, 20) : d;
    case 15: r = random_unit(c);
        if (r < word_float(th[0]) || r < word_float(th[1])) return cap(d, 12);
        return r < word_float(th[2]) ? cap(d, 20) : d;
    default: return d;
    }
}
static void key_set(C3WzxCamera *c, unsigned i, int32_t begin, int32_t finish, float start, float end)
{
    c->key_begin[i] = (uint32_t)shifted(begin, c->shift);
    c->key_finish[i] = (uint32_t)shifted(finish, c->shift);
    c->key_start[i] = start;
    c->key_end[i] = end;
}
static void do_fov(C3WzxCamera *c, const C3WzxCameraActor *a)
{
    const int32_t *frames = a->timing[c->entry];
    uint32_t count = row_count(a, c->entry);
    float mix_start = 0.0f, mix_end = 1.0f, low, high, fov;
    fov_range(c, c->range[0], &low, &high);
    const uint32_t *row = NULL;
    if (c->motion == 0 || c->motion == 4 || c->motion == 5) {
        fov_mix(c->params_flags & 0x20 ? 1u : c->params_flags & 0x80 ? 4u : 2u, &mix_start, &mix_end);
    } else {
        row = get_pattern(c, c->entry != 8 && c->entry != 9, c->params_flags);
        fov_mix(row[1 + 6], &mix_start, &mix_end); /* segment 0's initial mask */
    }
    fov = mix_start + (mix_end - mix_start) * random_unit(c);
    fov = fov * (high - low) + low;
    key_set(c, 0, frames[0], frames[0], fov, fov);
    key_set(c, 1, frames[0], frames[0], fov, fov);
    c->fov_clock = (uint32_t)shifted(frames[0], c->shift);
    set_fov(c, fov);
    if (!row || count <= 2) return;
    int32_t next = frames[0];
    float current = fov;
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t *s = row + 1 + 9 * i;
        int32_t previous = next;
        next = frames[i + 1];
        if (previous == next) key_set(c, i, previous, previous, current, current);
        else if (s[0] == 3 || s[0] == 4) {
            int32_t duration = select_duration(c, s[1], s + 2, next - previous);
            fov_range(c, c->range[i + 1], &low, &high);
            fov_mix(s[7], &mix_start, &mix_end);
            fov = mix_start + (mix_end - mix_start) * random_unit(c);
            fov = fov * (high - low) + low;
            if (s[8] == 2) key_set(c, i, next - duration, next, current, fov);
            else key_set(c, i, previous, previous + duration, current, fov);
            current = fov;
        } else key_set(c, i, previous, next, current, current);
    }
}

/* ---- validation -------------------------------------------------------- */

static int finite3(const float v[3], float limit)
{
    for (unsigned k = 0; k < 3; ++k) if (!isfinite(v[k]) || fabsf(v[k]) > limit) return 0;
    return 1;
}
static int actor_ok(const C3WzxCameraActor *a)
{
    if (!finite3(a->position, 10000) || !isfinite(a->yaw) || fabsf(a->yaw) > 1000 || !finite3(a->bound_min, 10000) ||
        !finite3(a->bound_max, 10000) || (a->source_facing != 1 && a->source_facing != -1) ||
        !isfinite(a->root_null_y) || fabsf(a->root_null_y) > 10000 || a->entry >= C3_WZX_CAMERA_ENTRIES)
        return 0;
    for (unsigned k = 0; k < 3; ++k) if (a->bound_min[k] > a->bound_max[k]) return 0;
    for (unsigned e = 0; e < C3_WZX_CAMERA_ENTRIES; ++e) {
        if (a->timing_count[e] > 16) return 0;
        for (unsigned k = 0; k < 4; ++k) if (a->timing[e][k] < -100000 || a->timing[e][k] > 100000) return 0;
    }
    return 1;
}
static int camera_ok(const C3TitleCamera *b)
{
    return b && finite3(b->eye, 1e6f) && finite3(b->target, 1e6f) && isfinite(b->roll) && isfinite(b->fov_y_degrees) &&
           b->fov_y_degrees > 0 && b->fov_y_degrees < 180 && isfinite(b->aspect) && b->aspect > 0 &&
           isfinite(b->near_plane) && b->near_plane > 0 && isfinite(b->far_plane) && b->far_plane > b->near_plane;
}

/* ---- begin: battleCameraStartWaza 0x801D30BC -------------------------- */

static void cut_common(C3WzxCamera *c, unsigned owner, const C3TitleCamera *inherited, int mode)
{
    if (!c->mode && !c->holding) c->restored = *inherited;
    c->mode = (uint8_t)mode;
    c->holding = 0;
    c->owner = owner;
    c->ticks = 0;
    ++c->cuts;
    c->target_move.active = c->eye_move.active = c->yaw_move.active = 0; /* cameraMoveStop */
    c->moving = 0;
}

static float dat_scale(const C3WzxCamera *c, int32_t selector)
{
    switch (selector) { /* fn_801DABAC */
    case -2: return c->tables->scale[0];
    case -1: return c->tables->scale[1];
    case 1: return c->tables->scale[2];
    case 2: return c->tables->scale[3];
    case 3: return c->tables->scale[4];
    default: return c->tables->scale[5];
    }
}

/* _cameraOffsetAnimeUpdate 0x801786F4: scale, rotate, mirror (flags[2] & 4 negates z),
 * then offset. The animation's own up vector (roll) is replaced by
 * (0,1,0) in the source, so roll stays 0. */
static void dat_publish(C3WzxCamera *c)
{
    const uint8_t *s = c->samples + (size_t)c->sample * 36u;
    float co = cosf(c->dat_yaw), si = sinf(c->dat_yaw), out[2][3];
    for (unsigned v = 0; v < 2; ++v) {
        float p[3];
        for (unsigned k = 0; k < 3; ++k) p[k] = le_float(s + 12 * v + 4 * k) * c->dat_scale[k];
        float x = co * p[0] + si * p[2], z = -si * p[0] + co * p[2];
        if (c->dat_mirror) z = -z;
        out[v][0] = x + c->dat_offset[0];
        out[v][1] = p[1] + c->dat_offset[1];
        out[v][2] = z + c->dat_offset[2];
    }
    c->camera = c->restored;
    for (unsigned k = 0; k < 3; ++k) {
        c->camera.eye[k] = out[0][k] * TO_NATIVE;
        c->camera.target[k] = out[1][k] * TO_NATIVE;
        c->eye[k] = out[0][k];
    }
    c->camera.roll = 0;
    c->camera.fov_y_degrees = c->fov = le_float(s + 28);
}

static int begin_dat(C3WzxCamera *c, const C3WzxCameraScene *scene, unsigned owner, const C3WzxCameraSequence *seq,
                     const C3TitleCamera *inherited, char *e, size_t n)
{
    const C3WzxCameraActor *a = &scene->actors[owner];
    if (seq->camera_count > 1u << 20) return fail(e, n, "WZX camera DAT too long.");
    for (uint32_t i = 0; i < seq->camera_count; ++i) {
        const uint8_t *s = seq->camera + (size_t)i * 36u;
        for (unsigned k = 0; k < 7; ++k)
            if (!isfinite(le_float(s + 4 * k)) || fabsf(le_float(s + 4 * k)) > 1e6f) return fail(e, n, "Nonfinite WZX camera DAT sample.");
        float fov = le_float(s + 28);
        if (!isfinite(fov) || fov <= 0 || fov >= 180) return fail(e, n, "Invalid WZX camera DAT field of view.");
    }
    if (seq->flags & 0x00800000u)
        for (unsigned k = 0; k < 3; ++k)
            if (!isfinite(scene->grid_scale[k]) || scene->grid_scale[k] <= 0 || scene->grid_scale[k] > 100)
                return fail(e, n, "Invalid battle grid scale.");
    cut_common(c, owner, inherited, C3_WZX_CAMERA_DAT);
    c->idle_cut = 0;
    c->samples = seq->camera;
    c->sample_count = seq->camera_count;
    c->sample = 0;
    c->dat_mirror = c->reverse; /* fn_801765F4(reverse ? 4 : 0) */
    if (seq->flags & 4) { /* world space */
        memset(c->dat_offset, 0, sizeof c->dat_offset);
        c->dat_yaw = a->source_facing < 0 && (seq->flags & 0x02000000u) ? 3.1415927f : 0.0f;
        if (seq->flags & 0x00800000u) {
            memcpy(c->dat_scale, scene->grid_scale, sizeof c->dat_scale);
            if (seq->flags & 0x01000000u) c->dat_scale[1] = 1.0f;
        } else c->dat_scale[0] = c->dat_scale[1] = c->dat_scale[2] = 1.0f;
    } else {
        for (unsigned k = 0; k < 3; ++k) c->dat_offset[k] = a->position[k] * TO_GC;
        if (seq->flags & 0x4000u)
            for (unsigned k = 0; k < 3; ++k) c->dat_offset[k] += 0.5f * (a->bound_min[k] + a->bound_max[k]) * TO_GC;
        if (seq->kind == 2 && a->root_null) c->dat_offset[1] += a->root_null_y * TO_GC;
        c->dat_yaw = a->yaw;
        c->dat_scale[0] = c->dat_scale[1] = c->dat_scale[2] = dat_scale(c, a->scale_selector);
    }
    dat_publish(c);
    return 1;
}

static int begin_camera(C3WzxCamera *c, const C3WzxCameraScene *scene, unsigned owner, const C3WzxCameraSequence *seq,
                        const C3TitleCamera *inherited, char *e, size_t n)
{
    if (!c || !c->tables || !scene || scene->count > C3_WZX_CAMERA_MAX_ACTORS || owner >= scene->count)
        return fail(e, n, "Invalid WZX camera owner or scene.");
    const C3WzxCameraActor *a = &scene->actors[owner];
    if (!a->present || !actor_ok(a)) return fail(e, n, "Invalid WZX camera owner actor.");
    if (!camera_ok(inherited)) return fail(e, n, "Invalid inherited camera.");
    if (seq && (seq->part_sel >= 17 || seq->entry >= C3_WZX_CAMERA_ENTRIES || (seq->camera_count && !seq->camera)))
        return fail(e, n, "Invalid WZX camera sequence header.");
    c->reverse = a->source_facing < 0 && (!seq || !(seq->flags & 0x80));
    if (seq) {
        c->idle_disabled = 1; /* battleCameraDisable */
        if (seq->camera && seq->camera_count) return begin_dat(c, scene, owner, seq, inherited, e, n);
    }
    uint32_t pf = 0;
    if (seq) {
        uint32_t f = seq->flags;
        if (f & 0x00200000u) pf |= 2;
        else if (f & 0x200u) pf |= 4;
        else if (f & 0x00400000u) pf |= a->position[2] < 0.0f ? 4u : 8u;
        if (f & 0x400u) pf |= 0x20;
        else if (f & 0x800u) pf |= 0x40;
        else if (f & 0x1000u) pf |= 0x80;
    } else pf = 4;
    if (a->is_trainer) {
        if (a->pokemon_count == 1) pf = (pf & ~0x1Fu) | 0x10;
        else if (a->pokemon_count > 1) pf = (pf & ~0x1Fu) | 1;
    }
    int was_running = c->mode || c->holding;
    cut_common(c, owner, inherited, C3_WZX_CAMERA_PROCEDURAL);
    c->idle_cut = seq == NULL;
    c->params_flags = pf;
    c->entry = seq ? seq->entry : a->entry;
    c->part = seq ? seq->part_sel : 2;
    c->shift = a->is_trainer ? 1 : 0;
    c->special_origin = seq && seq->kind == 3 && (seq->id == 0x154 || seq->id == 0x6E);
    /* cameraSetTargetExt(owner, part) + cameraRefreshTargetPos. Without the
     * part the source keeps camera.c's previous view offset; a fresh camera
     * uses the inherited target instead. */
    float part[3];
    for (unsigned k = 0; k < 3; ++k) c->position[k] = a->position[k] * TO_GC;
    if (scene->part_world && scene->part_world(scene->user, owner, c->part, part) && finite3(part, 10000)) {
        for (unsigned k = 0; k < 3; ++k) c->view[k] = part[k] * TO_GC - c->position[k];
    } else if (!was_running) {
        for (unsigned k = 0; k < 3; ++k) c->view[k] = inherited->target[k] * TO_GC - c->position[k];
    }
    calculate_params(c, a, pf);
    select_motion(c, a, seq);
    do_position(c, a);
    do_fov(c, a);
    camera_update(c, a); /* the final cameraUpdate() in battleCameraStartWaza */
    publish(c);
    return 1;
}

int c3_wzx_camera_begin_sequence(C3WzxCamera *camera, const C3WzxCameraScene *scene, unsigned owner,
                                 const C3WzxCameraSequence *sequence, const C3TitleCamera *inherited, char *e, size_t n)
{
    if (!camera || !sequence) return fail(e, n, "Missing WZX camera or sequence.");
    C3WzxCamera next = *camera;
    if (!begin_camera(&next, scene, owner, sequence, inherited, e, n)) return 0;
    *camera = next;
    if (e && n) e[0] = 0;
    return 1;
}

int c3_wzx_camera_begin_idle(C3WzxCamera *camera, const C3WzxCameraScene *scene, unsigned owner,
                             const C3TitleCamera *inherited, char *e, size_t n)
{
    if (!camera) return fail(e, n, "Missing WZX camera.");
    C3WzxCamera next = *camera;
    if (!begin_camera(&next, scene, owner, NULL, inherited, e, n)) return 0;
    *camera = next;
    if (e && n) e[0] = 0;
    return 1;
}

/* ---- per tick ----------------------------------------------------------- */

/* fn_801D2D28 then cameraUpdate (assumed: battle update before the scene
 * camera update within a frame). */
static void procedural_tick(C3WzxCamera *c, const C3WzxCameraScene *scene)
{
    const C3WzxCameraActor *a = &scene->actors[c->owner];
    float part[3];
    if (scene->part_world && scene->part_world(scene->user, c->owner, c->part, part) && finite3(part, 10000)) {
        float ofs[3];
        for (unsigned k = 0; k < 3; ++k)
            ofs[k] = part[k] * TO_GC - (c->special_origin ? c->position[k] : a->position[k] * TO_GC);
        move_start(&c->target_move, c->view, ofs, 3, (int32_t)TARGET_MOVE_TICKS);
        c->moving = 1;
    }
    ++c->fov_clock;
    float fov = c->fov;
    for (unsigned i = 0; i < 2; ++i) {
        if (c->fov_clock <= c->key_begin[i]) { fov = c->key_start[i]; break; }
        if (c->fov_clock <= c->key_finish[i]) {
            float t = (float)(c->fov_clock - c->key_begin[i]) / (float)(c->key_finish[i] - c->key_begin[i]);
            fov = c->key_start[i] + (c->key_end[i] - c->key_start[i]) * t;
            break;
        }
        fov = c->key_end[i];
    }
    set_fov(c, fov);
    if (c->idle_cut && !c->eye_move.active && !c->yaw_move.active) {
        /* cameraMoveEndCheckSpecial == 0 -> wazaCameraStop: scene mode 8
         * without an animation leaves the last pose on screen. */
        c->mode = C3_WZX_CAMERA_NONE;
        c->holding = 1;
        c->idle_cut = 0;
        return;
    }
    camera_update(c, a);
    publish(c);
}

/* battleCameraStartRandom 0x801C2D80. Returns 1 when it started a cut. */
static int idle_tick(C3WzxCamera *c, const C3WzxCameraScene *scene)
{
    if (c->idle_simple || c->idle_disabled || c->mode) { c->idle_timer = C3_WZX_CAMERA_IDLE_TICKS; return 0; }
    c->idle_timer -= 1;
    if (c->idle_timer > 0) return 0;
    uint32_t total = scene->count;
    if (!total) { c->idle_timer = C3_WZX_CAMERA_IDLE_TICKS; return 0; }
    if (random_unit(c) > c->tables->idle_gate) { c->idle_timer = C3_WZX_CAMERA_IDLE_TICKS; return 0; }
    uint32_t candidate = random_below(c, total);
    /* The source redraws without bound; with one candidate equal to the
     * last it would never return, so a lone candidate is accepted. */
    for (unsigned guard = 0; candidate == c->idle_last && total > 1 && guard < 1024; ++guard)
        candidate = random_below(c, total);
    c->idle_last = candidate;
    uint32_t ordinal = 0;
    int started = 0;
    for (unsigned i = 0; i < total; ++i) {
        if (!scene->actors[i].present) continue;
        if (ordinal == candidate) {
            if (begin_camera(c, scene, i, NULL, &scene->base, NULL, 0)) started = 1;
            c->idle_timer = C3_WZX_CAMERA_IDLE_TICKS;
        }
        ++ordinal;
    }
    return started;
}

int c3_wzx_camera_step_units(C3WzxCamera *camera, const C3WzxCameraScene *scene, uint64_t units, char *e, size_t n)
{
    if (!camera || !camera->tables || !scene || scene->count > C3_WZX_CAMERA_MAX_ACTORS)
        return fail(e, n, "Invalid WZX camera step.");
    if (camera->remainder >= 1000 || units > (uint64_t)C3_WZX_CAMERA_MAX_STEP_TICKS * 1000u)
        return fail(e, n, "WZX camera step exceeds 3600 ticks.");
    uint64_t total = camera->remainder + units;
    uint32_t ticks = (uint32_t)(total / 1000u);
    if (ticks > C3_WZX_CAMERA_MAX_STEP_TICKS) return fail(e, n, "WZX camera step exceeds 3600 ticks.");
    if (camera->mode) {
        if (camera->owner >= scene->count || !actor_ok(&scene->actors[camera->owner]))
            return fail(e, n, "Invalid active WZX camera owner.");
        if (camera->mode == C3_WZX_CAMERA_DAT && !camera->samples) return fail(e, n, "WZX camera DAT samples missing.");
    }
    C3WzxCamera next = *camera;
    for (uint32_t t = 0; t < ticks; ++t) {
        int started = idle_tick(&next, scene);
        if (!started && next.mode == C3_WZX_CAMERA_PROCEDURAL) procedural_tick(&next, scene);
        else if (!started && next.mode == C3_WZX_CAMERA_DAT) {
            if (next.sample + 1 < next.sample_count) ++next.sample;
            dat_publish(&next);
        }
        if (next.mode) ++next.ticks;
    }
    next.remainder = (uint32_t)(total % 1000u);
    *camera = next;
    if (e && n) e[0] = 0;
    return 1;
}

int c3_wzx_camera_step(C3WzxCamera *camera, const C3WzxCameraScene *scene, uint32_t ms, char *e, size_t n)
{
    return c3_wzx_camera_step_units(camera, scene, (uint64_t)ms * 60u, e, n);
}

/* ---- lifecycle ---------------------------------------------------------- */

void c3_wzx_camera_init(C3WzxCamera *c, const C3WzxCameraTables *tables, uint32_t seed)
{
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->tables = tables;
    c->rng = seed ? seed : 1;
    c->previous_motion = tables ? tables->initial_previous_motion : -1;
    c->idle_last = tables ? tables->initial_idle_last : UINT32_MAX;
    c->idle_timer = tables ? tables->initial_idle_timer : C3_WZX_CAMERA_IDLE_TICKS;
}

void c3_wzx_camera_end(C3WzxCamera *c)
{
    if (!c) return;
    if (c->mode || c->holding) c->camera = c->restored;
    c->mode = C3_WZX_CAMERA_NONE;
    c->holding = 0;
    c->idle_cut = 0;
    c->target_move.active = c->eye_move.active = c->yaw_move.active = 0;
    c->moving = 0;
    c->samples = NULL;
    c->sample_count = 0;
}

void c3_wzx_camera_idle_reset(C3WzxCamera *c, int simple)
{
    if (!c) return;
    c->idle_simple = (uint8_t)(simple != 0);
    c->idle_disabled = 0;
    c->idle_timer = C3_WZX_CAMERA_IDLE_TICKS;
}

int c3_wzx_camera_active(const C3WzxCamera *c) { return c && (c->mode || c->holding); }
