/* Host checks for the general WZX battle camera (synthetic tables/actors;
 * a prepared wzx_camera.c3k as argument is decoded and exercised too). */
#include "colosseum3ds/wzx_camera.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "wzx camera check %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t fnv1a(const uint8_t *b, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; } return h; }

/* Synthetic pattern rows: segment {mode 3, duration mode 5, thresholds .5,
 * initial mask, mask, timing mode}; weights cover the table. */
static void synthetic_row(uint8_t *p, float weight, unsigned seed)
{
    put32(p, fbits(weight));
    for (unsigned s = 0; s < 2; ++s) {
        uint8_t *q = p + 4 + 36 * s;
        put32(q, (seed + s) % 3 ? 3u : 1u);
        put32(q + 4, (uint32_t[]){ 5, 12, 13, 7, 2 }[(seed + s) % 5]);
        for (unsigned k = 0; k < 4; ++k) put32(q + 8 + 4 * k, fbits(.25f + .15f * (float)k));
        put32(q + 24, 1u + (seed % 7));
        put32(q + 28, 1u + ((seed + s) % 7));
        put32(q + 32, (seed + s) & 1 ? 2u : 1u);
    }
}
static void synthetic_bytes(uint8_t b[C3_WZX_CAMERA_FILE_BYTES], float gate)
{
    memset(b, 0, C3_WZX_CAMERA_FILE_BYTES);
    memcpy(b, "C3WK", 4);
    put32(b + 4, 1); put32(b + 8, C3_WZX_CAMERA_FILE_BYTES); put32(b + 12, 8); put32(b + 16, 13); put32(b + 20, 19);
    for (unsigned r = 0; r < 8; ++r) synthetic_row(b + 32 + 76 * r, .125f, r);
    for (unsigned r = 0; r < 13; ++r) synthetic_row(b + 32 + 76 * (8 + r), r < 9 ? 1.0f / 9.0f : 0.0f, 3 * r + 1);
    const float scale[6] = { .8f, .875f, 1.4f, 1.8f, 3.0f, 1.0f };
    for (unsigned i = 0; i < 6; ++i) put32(b + 1628 + 4 * i, fbits(scale[i]));
    put32(b + 1652, fbits(gate));
    put32(b + 1656, (uint32_t)-1); put32(b + 1660, UINT32_MAX); put32(b + 1664, 200);
    put32(b + 1668, fnv1a(b, 1668));
}

typedef struct { float part_offset[C3_WZX_CAMERA_MAX_ACTORS][3]; unsigned calls; int missing; } Parts;
static C3WzxCameraScene *part_scene;
static int part_world(void *user, unsigned actor, unsigned slot, float out[3])
{
    Parts *p = user;
    ++p->calls;
    if (p->missing || slot > 16) return 0;
    for (unsigned k = 0; k < 3; ++k) out[k] = part_scene->actors[actor].position[k] + p->part_offset[actor][k];
    return 1;
}

static const C3TitleCamera base = { { 0, 11, 9 }, { 0, 0, 0 }, 0, 30, .01f, 3276.8f, 1.18f };

static void actor(C3WzxCameraActor *a, float x, float z, float yaw, int facing, int trainer, unsigned count)
{
    memset(a, 0, sizeof *a);
    a->position[0] = x; a->position[2] = z; a->yaw = yaw; a->source_facing = facing;
    a->bound_min[0] = a->bound_min[2] = -.3f; a->bound_max[0] = a->bound_max[2] = .3f; a->bound_max[1] = 1.0f;
    a->scale_selector = trainer ? 0 : -1;
    a->present = 1; a->is_trainer = (uint8_t)trainer; a->pokemon_count = (uint16_t)count;
    const int32_t rows[3][4] = { { 0, 118, 0, 0 }, { 0, 62, 130, 240 }, { 0, 170, 298, 0 } };
    memcpy(a->timing[0], rows[0], sizeof rows[0]); a->timing_count[0] = 2;
    memcpy(a->timing[2], rows[1], sizeof rows[1]); a->timing_count[2] = 4;
    memcpy(a->timing[8], rows[2], sizeof rows[2]); a->timing_count[8] = 3;
    memcpy(a->timing[1], rows[1], sizeof rows[1]); /* count derived: 4 */
}
static void make_scene(C3WzxCameraScene *s, Parts *parts)
{
    memset(s, 0, sizeof *s);
    memset(parts, 0, sizeof *parts);
    actor(&s->actors[0], -6.0f, -1.0f, 1.5707964f, 1, 1, 1);
    actor(&s->actors[1], -3.5f, -2.625f, 1.5707964f, 1, 0, 0);
    actor(&s->actors[2], 6.0f, 1.0f, -1.5707964f, -1, 1, 2);
    actor(&s->actors[3], 3.5f, 2.625f, -1.5707964f, -1, 0, 0);
    s->count = 4;
    s->part_world = part_world; s->user = parts;
    s->grid_scale[0] = s->grid_scale[1] = s->grid_scale[2] = 1.0f;
    s->base = base;
    for (unsigned i = 0; i < 4; ++i) parts->part_offset[i][1] = .55f;
    part_scene = s;
}
static C3WzxCameraSequence sequence(uint32_t flags, uint32_t entry, uint32_t kind)
{
    C3WzxCameraSequence q;
    memset(&q, 0, sizeof q);
    q.flags = flags; q.part_sel = 2; q.entry = entry; q.kind = kind; q.id = 33;
    return q;
}
static int camera_finite(const C3TitleCamera *c)
{
    for (unsigned k = 0; k < 3; ++k) if (!isfinite(c->eye[k]) || !isfinite(c->target[k])) return 0;
    return isfinite(c->fov_y_degrees) && c->near_plane == base.near_plane && c->far_plane == base.far_plane && c->aspect == base.aspect;
}

static void tables_checks(const C3WzxCameraTables *t)
{
    uint8_t b[C3_WZX_CAMERA_FILE_BYTES];
    char error[128];
    C3WzxCameraTables u, before;
    synthetic_bytes(b, .5f);
    CHECK(c3_wzx_camera_tables_decode(&u, b, sizeof b, error, sizeof error));
    CHECK(!memcmp(&u, t, sizeof u));
    before = u;
    b[100] ^= 1;
    CHECK(!c3_wzx_camera_tables_decode(&u, b, sizeof b, error, sizeof error) && !memcmp(&u, &before, sizeof u));
    b[100] ^= 1;
    CHECK(!c3_wzx_camera_tables_decode(&u, b, sizeof b - 1, error, sizeof error));
    put32(b + 24, 1); put32(b + 1668, fnv1a(b, 1668));
    CHECK(!c3_wzx_camera_tables_decode(&u, b, sizeof b, error, sizeof error));
    CHECK(!c3_wzx_camera_tables_load(&u, "build/no-such-wzx-camera.c3k", error, sizeof error));
}

static void motion_checks(const C3WzxCameraTables *t)
{
    static const uint32_t flags[6] = { 0x08, 0x10, 0x20, 0x40, 0x01, 0x78 };
    static const unsigned expect[6] = { 3, 0, 1, 2, 5, 99 };
    char error[160];
    C3WzxCameraScene scene; Parts parts;
    make_scene(&scene, &parts);
    for (unsigned f = 0; f < 6; ++f)
        for (unsigned seed = 1; seed <= 48; ++seed)
            for (unsigned owner = 0; owner < 4; ++owner) {
                C3WzxCamera c;
                c3_wzx_camera_init(&c, t, seed * 7919u + f);
                C3WzxCameraSequence q = sequence(flags[f], owner & 1 ? 2 : 0, 1);
                CHECK(c3_wzx_camera_begin_sequence(&c, &scene, owner, &q, &base, error, sizeof error));
                CHECK(c.mode == C3_WZX_CAMERA_PROCEDURAL && c.idle_disabled && camera_finite(&c.camera));
                if (expect[f] != 99) CHECK(c.motion == expect[f]);
                const C3WzxCameraActor *a = &scene.actors[owner];
                CHECK(c.reverse == (a->source_facing < 0));
                if (a->is_trainer) CHECK(c.shift == 1 && (c.params_flags & 0x1F) == (a->pokemon_count == 1 ? 0x10u : 1u));
                else CHECK(c.shift == 0);
                unsigned motion = c.motion;
                float dmin = c.distance_min * .1f - 1e-3f, dmax = c.distance_max * .1f + 1e-3f;
                float hmin = c.height_min * .1f - 1e-3f, hmax = c.height_max * .1f + 1e-3f;
                float p[3] = { c.position[0] * .1f, c.position[1] * .1f, c.position[2] * .1f };
                float start_x = c.camera.eye[0];
                for (unsigned tick = 0; tick < 700; ++tick) {
                    CHECK(camera_finite(&c.camera));
                    CHECK(c.camera.fov_y_degrees >= 15.0f - 1e-3f && c.camera.fov_y_degrees <= 85.0f + 1e-3f);
                    float dx = c.camera.eye[0] - p[0], dy = c.camera.eye[1] - p[1], dz = c.camera.eye[2] - p[2];
                    float horizontal = sqrtf(dx * dx + dz * dz);
                    if (motion == 0 || motion == 2 || motion == 3) {
                        CHECK(horizontal >= dmin && horizontal <= dmax);
                        CHECK(dy >= hmin && dy <= hmax);
                    } else if (motion == 4) CHECK(fabsf(horizontal - 11.0f) < 1e-3f && fabsf(dy - 2.5f) < 1e-3f);
                    else if (motion == 5) CHECK(fabsf(horizontal - 5.0f * (a->scale_selector == -1 ? .875f : 1.0f)) < 1e-3f);
                    /* The snap tick keeps the previous tick's yaw (camera.c derives yaw only
                     * while the position move is active), so the endpoint turns slightly. */
                    else if (motion == 1) CHECK(dy >= .1f - 1e-3f && dy <= 1.0f + 1e-3f && fabsf(c.camera.eye[2] - p[2]) <= 5.05f);
                    CHECK(c3_wzx_camera_step_units(&c, &scene, 1000, error, sizeof error));
                }
                if (motion == 1) { /* translated by the signed 10 + offset (25..35) along world x */
                    float moved = c.camera.eye[0] - start_x;
                    CHECK(a->source_facing < 0 ? moved >= 2.4f && moved <= 3.6f : moved <= -2.4f && moved >= -3.6f);
                }
                CHECK(!c.eye_move.active && !c.yaw_move.active);
                /* The target follows the body-map part (0.2 s moves renewed each tick). */
                CHECK(fabsf(c.camera.target[1] - (a->position[1] + .55f)) < 1e-3f);
                CHECK(fabsf(c.camera.target[0] - a->position[0]) < 1e-3f);
                c3_wzx_camera_end(&c);
                CHECK(!c3_wzx_camera_active(&c) && !memcmp(&c.camera, &base, sizeof base));
            }
    /* 0x13A forces motion 4; trainer durations are doubled. */
    {
        C3WzxCamera c;
        c3_wzx_camera_init(&c, t, 3);
        scene.actors[1].model_id = 0x13A;
        C3WzxCameraSequence q = sequence(0x78, 2, 1);
        CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error) && c.motion == 4);
        CHECK(c.eye_move.duration == 130);
        scene.actors[1].model_id = 0;
        q = sequence(0x08, 2, 1);
        CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 0, &q, &base, error, sizeof error) && c.motion == 3);
        CHECK(c.eye_move.duration == 260 && c.rotation_min == 1.2566371f);
        q = sequence(0x08, 8, 2);
        CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && c.eye_move.duration == 170);
    }
    /* Consecutive cuts with several options never repeat the motion. */
    {
        C3WzxCamera c;
        c3_wzx_camera_init(&c, t, 99);
        unsigned previous = 99;
        for (unsigned i = 0; i < 200; ++i) {
            C3WzxCameraSequence q = sequence(0x78, 2, 1);
            CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1 + 2 * (i & 1), &q, &base, error, sizeof error));
            CHECK(c.motion != previous);
            previous = c.motion;
        }
    }
}

static void determinism_checks(const C3WzxCameraTables *t)
{
    char error[160];
    C3WzxCameraScene scene; Parts parts;
    make_scene(&scene, &parts);
    for (unsigned seed = 1; seed <= 32; ++seed) {
        C3WzxCamera a, b, c;
        c3_wzx_camera_init(&a, t, seed); c3_wzx_camera_init(&b, t, seed); c3_wzx_camera_init(&c, t, seed);
        C3WzxCameraSequence q = sequence(seed & 1 ? 0x78 : 0x200858, seed & 2 ? 8 : 2, seed & 2 ? 2 : 1);
        unsigned owner = seed % 4;
        CHECK(c3_wzx_camera_begin_sequence(&a, &scene, owner, &q, &base, error, sizeof error));
        CHECK(c3_wzx_camera_begin_sequence(&b, &scene, owner, &q, &base, error, sizeof error));
        CHECK(c3_wzx_camera_begin_sequence(&c, &scene, owner, &q, &base, error, sizeof error));
        CHECK(!memcmp(&a, &b, sizeof a));
        for (unsigned i = 0; i < 300; ++i) CHECK(c3_wzx_camera_step_units(&a, &scene, 1000, error, sizeof error));
        CHECK(c3_wzx_camera_step_units(&b, &scene, 300000, error, sizeof error));
        for (unsigned i = 0; i < 300; ++i) CHECK(c3_wzx_camera_step(&c, &scene, 16, error, sizeof error)); /* 960 units */
        CHECK(c3_wzx_camera_step_units(&c, &scene, 12000, error, sizeof error));
        CHECK(!memcmp(&a, &b, sizeof a) && !memcmp(&a, &c, sizeof a) && a.ticks == 300 && a.remainder == 0);
        /* Fractions are retained across calls. */
        CHECK(c3_wzx_camera_step_units(&a, &scene, 400, error, sizeof error) && a.ticks == 300 && a.remainder == 400);
        CHECK(c3_wzx_camera_step_units(&a, &scene, 600, error, sizeof error) && a.ticks == 301 && a.remainder == 0);
    }
    /* Failures leave the camera unchanged. */
    C3WzxCamera c, old;
    c3_wzx_camera_init(&c, t, 5);
    C3WzxCameraSequence q = sequence(0x78, 2, 1);
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error));
    old = c;
    CHECK(!c3_wzx_camera_begin_sequence(&c, &scene, 4, &q, &base, error, sizeof error) && !memcmp(&c, &old, sizeof c));
    C3TitleCamera bad = base; bad.eye[1] = NAN;
    CHECK(!c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &bad, error, sizeof error) && !memcmp(&c, &old, sizeof c));
    q.part_sel = 17;
    CHECK(!c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error) && !memcmp(&c, &old, sizeof c));
    q.part_sel = 2;
    scene.actors[3].source_facing = 0;
    CHECK(!c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && !memcmp(&c, &old, sizeof c));
    scene.actors[3].source_facing = -1;
    CHECK(!c3_wzx_camera_step_units(&c, &scene, 3601000ull, error, sizeof error) && !memcmp(&c, &old, sizeof c));
    /* A cut while running keeps the first inherited camera. */
    C3TitleCamera other = base; other.eye[0] = 5;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &other, error, sizeof error));
    c3_wzx_camera_end(&c);
    CHECK(!memcmp(&c.camera, &base, sizeof base));
    /* Missing part: the view offset starts from the inherited target and holds. */
    parts.missing = 1;
    c3_wzx_camera_init(&c, t, 6);
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error));
    CHECK(fabsf(c.camera.target[0] - base.target[0]) < 1e-4f && fabsf(c.camera.target[2] - base.target[2]) < 1e-4f);
    CHECK(c3_wzx_camera_step_units(&c, &scene, 50000, error, sizeof error) && camera_finite(&c.camera));
    parts.missing = 0;
}

static void put_sample(uint8_t *s, const float eye[3], const float interest[3], float fov)
{
    for (unsigned k = 0; k < 3; ++k) { put32(s + 4 * k, fbits(eye[k])); put32(s + 12 + 4 * k, fbits(interest[k])); }
    put32(s + 24, fbits(.3f)); put32(s + 28, fbits(fov)); put32(s + 32, 0);
}
static int near3(const float a[3], float x, float y, float z) { return fabsf(a[0] - x) < 1e-4f && fabsf(a[1] - y) < 1e-4f && fabsf(a[2] - z) < 1e-4f; }

static void dat_checks(const C3WzxCameraTables *t)
{
    char error[160];
    C3WzxCameraScene scene; Parts parts;
    make_scene(&scene, &parts);
    uint8_t samples[3 * 36];
    put_sample(samples, (float[]){ 0, 0, 10 }, (float[]){ 0, 5, 0 }, 40);
    put_sample(samples + 36, (float[]){ 10, 0, 0 }, (float[]){ 0, 5, 0 }, 41);
    put_sample(samples + 72, (float[]){ 0, 20, 10 }, (float[]){ 0, 5, 0 }, 42);
    C3WzxCameraActor *a = &scene.actors[1];
    a->position[0] = 1; a->position[1] = 0; a->position[2] = 2; a->yaw = 0; a->scale_selector = 0;
    C3WzxCamera c;
    c3_wzx_camera_init(&c, t, 1);
    C3WzxCameraSequence q = sequence(0x78, 2, 1);
    q.camera = samples; q.camera_count = 3;
    uint32_t draws = c.random_draws;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error));
    CHECK(c.mode == C3_WZX_CAMERA_DAT && c.random_draws == draws && c.idle_disabled);
    CHECK(near3(c.camera.eye, 1, 0, 3) && near3(c.camera.target, 1, .5f, 2) && c.camera.fov_y_degrees == 40 && c.camera.roll == 0);
    CHECK(camera_finite(&c.camera));
    CHECK(c3_wzx_camera_step_units(&c, &scene, 1000, error, sizeof error) && near3(c.camera.eye, 2, 0, 2) && c.camera.fov_y_degrees == 41);
    CHECK(c3_wzx_camera_step_units(&c, &scene, 5000, error, sizeof error) && near3(c.camera.eye, 1, 2, 3) && c.camera.fov_y_degrees == 42);
    c3_wzx_camera_end(&c);
    CHECK(!memcmp(&c.camera, &base, sizeof base));
    /* Yaw rotation, selector scale, bound centre (0x4000) and the damage root null. */
    a->yaw = 1.5707964f; a->scale_selector = -1; a->root_null = 1; a->root_null_y = .25f;
    q.flags = 0x4000; q.kind = 2;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error));
    CHECK(near3(c.camera.eye, 1 + .875f, .5f + .25f, 2) && near3(c.camera.target, 1, .5f + .25f + .5f * .875f, 2));
    q.kind = 1;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error) && near3(c.camera.eye, 1 + .875f, .5f, 2));
    c3_wzx_camera_end(&c);
    /* Negative-facing owner: mirrored z unless 0x80. */
    C3WzxCameraActor *o = &scene.actors[3];
    o->position[0] = 1; o->position[1] = 0; o->position[2] = 2; o->yaw = 0; o->scale_selector = 0;
    q.flags = 0;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && c.dat_mirror && near3(c.camera.eye, 1, 0, 1));
    q.flags = 0x80;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && !c.dat_mirror && near3(c.camera.eye, 1, 0, 3));
    /* World space (4): no offset; pi yaw for 0x02000000; grid scale 0x00800000 (y=1 with 0x01000000). */
    q.flags = 4 | 0x80;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && near3(c.camera.eye, 0, 0, 1));
    q.flags = 4 | 0x80 | 0x02000000;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && near3(c.camera.eye, 0, 0, -1));
    q.flags = 4;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && near3(c.camera.eye, 0, 0, -1));
    scene.grid_scale[0] = scene.grid_scale[1] = scene.grid_scale[2] = 2;
    q.flags = 4 | 0x80 | 0x00800000 | 0x01000000;
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error));
    CHECK(c3_wzx_camera_step_units(&c, &scene, 2000, error, sizeof error) && near3(c.camera.eye, 0, 2, 2));
    /* Invalid samples are rejected without a change. */
    C3WzxCamera old = c;
    put32(samples + 72 + 28, fbits(0));
    CHECK(!c3_wzx_camera_begin_sequence(&c, &scene, 3, &q, &base, error, sizeof error) && !memcmp(&c, &old, sizeof c));
}

static void idle_checks(const C3WzxCameraTables *always, const C3WzxCameraTables *never)
{
    char error[160];
    C3WzxCameraScene scene; Parts parts;
    make_scene(&scene, &parts);
    C3WzxCamera c;
    c3_wzx_camera_init(&c, always, 77);
    c3_wzx_camera_idle_reset(&c, 0);
    unsigned free_ticks = 0, cuts = 0, last_owner = 99;
    uint32_t previous_cuts = 0;
    for (unsigned tick = 0; tick < 20000; ++tick) {
        int was_active = c.mode != C3_WZX_CAMERA_NONE;
        CHECK(c3_wzx_camera_step_units(&c, &scene, 1000, error, sizeof error));
        if (c.cuts != previous_cuts) {
            /* A cut starts exactly 200 ticks after the last camera stopped. */
            CHECK(!was_active && free_ticks + 1 == 200);
            CHECK(c.mode == C3_WZX_CAMERA_PROCEDURAL && c.idle_cut && c.params_flags != 0);
            CHECK(c.owner != last_owner && c.owner < 4);
            if (!scene.actors[c.owner].is_trainer) CHECK(c.params_flags == 4 && c.part == 2 && c.entry == 0);
            CHECK(camera_finite(&c.camera));
            last_owner = c.owner;
            previous_cuts = c.cuts;
            ++cuts;
            free_ticks = 0;
        } else if (c.mode == C3_WZX_CAMERA_NONE) {
            if (!was_active) ++free_ticks;
            CHECK(c3_wzx_camera_active(&c) == (cuts > 0)); /* held pose */
        }
    }
    CHECK(cuts > 20);
    /* Holding the last pose: the output stops changing once the cut ends. */
    while (c.mode != C3_WZX_CAMERA_NONE) CHECK(c3_wzx_camera_step_units(&c, &scene, 1000, error, sizeof error));
    C3TitleCamera held = c.camera;
    CHECK(c3_wzx_camera_step_units(&c, &scene, 150000, error, sizeof error) && !memcmp(&held, &c.camera, sizeof held));
    /* A sequence disables the scheduler until reset; simple mode suppresses it. */
    C3WzxCameraSequence q = sequence(0x78, 2, 1);
    CHECK(c3_wzx_camera_begin_sequence(&c, &scene, 1, &q, &base, error, sizeof error));
    c3_wzx_camera_end(&c);
    uint32_t before = c.cuts;
    CHECK(c3_wzx_camera_step_units(&c, &scene, 3600000, error, sizeof error) && c.cuts == before && c.idle_timer == 200);
    c3_wzx_camera_idle_reset(&c, 1);
    CHECK(c3_wzx_camera_step_units(&c, &scene, 3600000, error, sizeof error) && c.cuts == before);
    c3_wzx_camera_idle_reset(&c, 0);
    CHECK(c3_wzx_camera_step_units(&c, &scene, 200000, error, sizeof error) && c.cuts == before + 1);
    /* Absent slots are skipped by the ordinal walk. */
    scene.actors[0].present = scene.actors[2].present = 0;
    c3_wzx_camera_end(&c);
    for (unsigned i = 0; i < 40; ++i) {
        uint32_t n = c.cuts;
        CHECK(c3_wzx_camera_step_units(&c, &scene, 1000000, error, sizeof error));
        if (c.cuts != n) CHECK(c.owner == 1 || c.owner == 3);
    }
    /* A closed gate never cuts. */
    C3WzxCamera d;
    c3_wzx_camera_init(&d, never, 1);
    c3_wzx_camera_idle_reset(&d, 0);
    CHECK(c3_wzx_camera_step_units(&d, &scene, 3600000, error, sizeof error) && d.cuts == 0);
}

/* The base camera pack: a synthetic 4-sample circle. */
static void home_checks(void)
{
    enum { COUNT = 4, BYTES = 32 + 32 * COUNT + 4 };
    uint8_t b[BYTES]; char error[160];
    memset(b, 0, sizeof b);
    memcpy(b, "C3WH", 4); put32(b + 4, 1); put32(b + 8, BYTES); put32(b + 12, COUNT);
    const float scale[4] = { 1.1f, 1.2f, 1.4f, 1.0f };
    for (unsigned i = 0; i < 4; ++i) put32(b + 16 + 4 * i, fbits(scale[i]));
    for (unsigned i = 0; i < COUNT; ++i) {
        float a = (float)i * 1.5707964f, row[8] = { 10 * sinf(a), 7, 10 * cosf(a), 9 * sinf(a), 6.8f, 9 * cosf(a), .01f, 30 + 2 * (float)i };
        for (unsigned k = 0; k < 8; ++k) put32(b + 32 + 32 * i + 4 * k, fbits(row[k]));
    }
    put32(b + BYTES - 4, fnv1a(b, BYTES - 4));
    C3WzxHomeCamera h;
    CHECK(c3_wzx_home_camera_decode(&h, b, sizeof b, error, sizeof error) && h.count == COUNT && h.scale[2] == 1.4f);
    C3TitleCamera c = base;
    c3_wzx_home_camera_sample(&h, 0, 1.0f, &c);
    CHECK(fabsf(c.eye[2] - 10) < 1e-4f && fabsf(c.eye[1] - 7) < 1e-4f && c.roll == 0 && c.fov_y_degrees == 30);
    CHECK(c.near_plane == base.near_plane && c.far_plane == base.far_plane && c.aspect == base.aspect);
    c3_wzx_home_camera_sample(&h, 1500, 1.0f, &c); /* halfway between samples 1 and 2 */
    CHECK(fabsf(c.eye[0] - 5) < 1e-4f && fabsf(c.eye[2] + 5) < 1e-4f && fabsf(c.fov_y_degrees - 33) < 1e-4f);
    c3_wzx_home_camera_sample(&h, 3500, 1.0f, &c); /* the loop wraps to sample 0 */
    CHECK(fabsf(c.eye[0] + 5) < 1e-4f && fabsf(c.eye[2] - 5) < 1e-4f && fabsf(c.fov_y_degrees - 33) < 1e-4f);
    c3_wzx_home_camera_sample(&h, 4000 * 5, 1.2f, &c);
    CHECK(fabsf(c.eye[2] - 12) < 1e-4f && fabsf(c.target[1] - 6.8f * 1.2f) < 1e-4f);
    c3_wzx_home_camera_free(&h);
    CHECK(!h.bytes && !h.count);
    /* Damaged packs are refused. */
    b[40] ^= 1;
    CHECK(!c3_wzx_home_camera_decode(&h, b, sizeof b, error, sizeof error) && !h.bytes);
    b[40] ^= 1;
    CHECK(!c3_wzx_home_camera_decode(&h, b, sizeof b - 1, error, sizeof error));
    put32(b + 32 + 28, fbits(180.0f)); put32(b + BYTES - 4, fnv1a(b, BYTES - 4));
    CHECK(!c3_wzx_home_camera_decode(&h, b, sizeof b, error, sizeof error));
}

int main(int argc, char **argv)
{
    home_checks();
    uint8_t b[C3_WZX_CAMERA_FILE_BYTES];
    char error[160];
    C3WzxCameraTables tables, always, never;
    synthetic_bytes(b, .5f);
    CHECK(c3_wzx_camera_tables_decode(&tables, b, sizeof b, error, sizeof error));
    synthetic_bytes(b, 1.0f);
    CHECK(c3_wzx_camera_tables_decode(&always, b, sizeof b, error, sizeof error));
    synthetic_bytes(b, -1.0f);
    CHECK(c3_wzx_camera_tables_decode(&never, b, sizeof b, error, sizeof error));
    tables_checks(&tables);
    motion_checks(&tables);
    determinism_checks(&tables);
    dat_checks(&tables);
    idle_checks(&always, &never);
    if (argc > 1) { /* the prepared private tables */
        C3WzxCameraTables real;
        if (!c3_wzx_camera_tables_load(&real, argv[1], error, sizeof error)) { fprintf(stderr, "%s: %s\n", argv[1], error); return 1; }
        motion_checks(&real);
        determinism_checks(&real);
        C3WzxCameraTables gate = real; gate.idle_gate = 1.0f;
        C3WzxCameraTables closed = real; closed.idle_gate = -1.0f;
        idle_checks(&gate, &closed);
        printf("real tables: gate %.3f scale %.3f %.3f %.3f %.3f %.3f %.3f previous %d last %u timer %d\n", real.idle_gate,
               real.scale[0], real.scale[1], real.scale[2], real.scale[3], real.scale[4], real.scale[5],
               real.initial_previous_motion, real.initial_idle_last, real.initial_idle_timer);
    }
    printf("wzx camera: %u checks passed\n", checks);
    return 0;
}
