/* Kind-4 effect nodes other than type 0 (colour keys), included by
 * battle_general_wzx.inc. Types on disc: 12 distortion, 11 billboard /
 * banner (+DAT), 8 owner shine, 9 owner blur / after-image, 5 leaf clones of
 * a DAT, 1 lightning. A node's DAT (types 5/11) is loaded as
 * r->sequence->node_model[i] (a stage model, slot WZX_EFFECT_SLOT).
 * start: 1 started (0 skipped); update: 1 running, 0 done, -1 the node's
 * duration mark; frame: once per stage step after the nodes ran.
 *
 * Payloads are the original big-endian kind-4 payloads; the parse offsets
 * are tracefx.c fn_801364A8 (the type's parameters start at payload +0xC,
 * written q+N below). Positions in the source are GC units, native = 0.1x.
 * Each live node has a slot here; the frame hook turns every slot into the
 * stage renderer's effect passes (effect_pass_renderer.inc): instances
 * (8 shine, 9 after-images, 5 leaves), a model matrix (11 billboards),
 * world triangles (1 lightning) and the type-12 capture composite.
 * Approximations against the original GX renderer are marked APPROX. */

#define FX_SLOTS 8u
#define FX_LEAVES 32u          /* leaffx caps a node at 0x20 clones */
#define FX_LEAF_NODES 2u       /* Razor Leaf/Magical Leaf run two leaf nodes at once */
#define FX_HISTORY 48u
#define FX_GHOSTS 4u
#define FX_BOLTS 6u
#define FX_BOLT_POINTS 200u    /* fn_8013A1D4 stops at 0xC8 points per bolt */
#define FX_SEQUENCE_SCALE .75f /* the effect nodes' sequence scale (0x8047E354) */

typedef struct { float p[4][3]; float q0[4], q1[4]; float scale, t; uint8_t alive; } FxLeaf;
typedef struct { float pos[3]; int16_t prev; float w0, w1; } FxPoint;
typedef struct { FxPoint pts[FX_BOLT_POINTS]; float origin[3], end[3]; uint16_t count, start, stop; } FxBolt;
typedef struct {
    uint8_t used, type;
    const WzxRun *run; uint32_t node;
    const C3WzxNode *n;
    int32_t model, local, frames;
    uint32_t rng;
    float value;                 /* 8/9: keyed blend */
    uint8_t capture, hold;
    /* 9: owner positions per tick */
    struct { int32_t tick; float pos[3]; } hist[FX_HISTORY]; unsigned hist_head, hist_count;
    /* 11 */ float bound[2];
    /* 5 */ int leaf_set;
    /* 12 */ float center[3];
} FxSlot;

static FxSlot fx_slot[FX_SLOTS];
static FxLeaf fx_leaves[FX_LEAF_NODES][FX_LEAVES];
static uint8_t fx_leaf_used[FX_LEAF_NODES];
static FxBolt fx_bolts[FX_BOLTS];
static C3WzxConfusionProgram fx_distortion_program; /* borrowed by the renderer: static storage */
static C3ConfusionType12 fx_distortion;
static C3StageInstance fx_instances[C3_STAGE_MAX_INSTANCES];
static C3StageEffectVertex fx_triangles[C3_STAGE_MAX_EFFECT_VERTICES];
static uint8_t fx_passes_live;

static float fx_f32(const C3WzxNode *n, uint32_t off) { uint32_t u = c3_wzx_payload_u32(n, off); float f; memcpy(&f, &u, 4); return f; }
static float fx_rand01(FxSlot *x) { x->rng ^= x->rng << 13; x->rng ^= x->rng >> 17; x->rng ^= x->rng << 5; return (float)(x->rng >> 8) / 16777216.f; }
static float fx_rand11(FxSlot *x) { return 2.f * fx_rand01(x) - 1.f; }
static void fx_vec(const C3WzxNode *n, uint32_t off, float v[3]) { for (unsigned k = 0; k < 3; ++k) v[k] = fx_f32(n, off + 4 * k); }
static float fx_dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void fx_cross(float o[3], const float a[3], const float b[3])
{ o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
static float fx_normalize(float v[3])
{ float l = sqrtf(fx_dot(v, v)); if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; } return l; }
static int fx_owner_visible(const C3BattleGeneralStage *s, unsigned owner) { return owner >= 4 || s->presentation.visible[owner]; }

/* Camera basis of the stage's current camera (roll ignored: APPROX). */
static int fx_camera(const C3BattleGeneralStage *s, float fwd[3], float right[3], float up[3])
{
    static const float world_up[3] = {0, 1, 0};
    for (unsigned k = 0; k < 3; ++k) fwd[k] = s->camera.target[k] - s->camera.eye[k];
    if (fx_normalize(fwd) <= 1e-6f) return 0;
    fx_cross(right, fwd, world_up);
    if (fx_normalize(right) <= 1e-6f) { right[0] = 1; right[1] = right[2] = 0; }
    fx_cross(up, right, fwd);
    return 1;
}

static FxSlot *fx_find(const WzxRun *r, uint32_t i)
{
    for (unsigned k = 0; k < FX_SLOTS; ++k) if (fx_slot[k].used && fx_slot[k].run == r && fx_slot[k].node == i) return &fx_slot[k];
    return NULL;
}

/* Blend keys (from f32, to f32, frames; 0xFFFFFFFF holds on `from`), as
 * fn_8013D984 / fn_8013E470 walk them: 1 running, 2 holding, 0 done. */
static int fx_keys(const C3WzxNode *n, uint32_t at, uint32_t count, int32_t t, float *v)
{
    for (uint32_t k = 0; k < count && k < 32; ++k) {
        float from = fx_f32(n, at + 16 * k), to = fx_f32(n, at + 16 * k + 4);
        uint32_t span = c3_wzx_payload_u32(n, at + 16 * k + 8);
        if (span == 0xFFFFFFFFu) { *v = from; return 2; }
        if ((uint32_t)t < span) { *v = from + (to - from) * (float)t / (float)span; return 1; }
        t -= (int32_t)span;
    }
    return 0;
}
/* Mode 1 (one value, three thirds of the node's frames): 0->v, v, then 0. */
static int fx_thirds(float value, int32_t frames, int32_t t, float *v)
{
    int32_t third = frames / 3;
    if (third <= 0) return 0;
    if (t < third) { *v = value * (float)t / (float)third; return 1; }
    if (t < 2 * third) { *v = value; return 1; }
    if (t < 3 * third) { *v = 0; return 1; }
    return 0;
}
/* Type 8 (fn_801364A8 case 8): q+8 count, q+0xC mode (2: keys at q+0x10;
 * 1: the count word is the value). Type 9 (fn_80137114): q+0 count, q+4
 * mode (3: q+8 capture flag, keys q+0xC; 2: capture, keys q+8; 1: the count
 * word is the value, capture). */
static int fx_blend(FxSlot *x, int32_t t)
{
    const C3WzxNode *n = x->n;
    if (x->type == 8) {
        uint32_t count = c3_wzx_payload_u32(n, 0x14), mode = c3_wzx_payload_u32(n, 0x18);
        if (mode == 2) return fx_keys(n, 0x1C, count, t, &x->value);
        if (mode == 1) return fx_thirds(fx_f32(n, 0x14), x->frames, t, &x->value);
        return 0;
    }
    uint32_t count = c3_wzx_payload_u32(n, 0xC), mode = c3_wzx_payload_u32(n, 0x10);
    if (mode == 3) return fx_keys(n, 0x18, count, t, &x->value);
    if (mode == 2) return fx_keys(n, 0x14, count, t, &x->value);
    if (mode == 1) return fx_thirds(fx_f32(n, 0xC), x->frames, t, &x->value);
    return 0;
}

/* ---- 12: distortion (fn_8013F114 / fn_8013F80C / fn_8013F410) ----------
 * q+0 particles in the capture, q+4 follow the part, q+8 radius per tick,
 * q+0xC indirect base, q+0x10 mask bytes, q+0x14 mode (1: pivot 1, the
 * payload 4 bytes shorter; 2: pivot q+0x18), then the GS I8 mask (32-byte
 * aligned in the original; the pack copy keeps no alignment, so the header
 * is searched). The CPU indirect grid of the encounter-10 Confusion path
 * (confusion_renderer.inc) draws it from the scene capture. APPROX: masks of
 * 256x256 are box-filtered to that path's 128x128; particles are never in
 * the capture (that path's documented limit). */
static int fx_distortion_mask(const C3WzxNode *n)
{
    uint32_t mode = c3_wzx_payload_u32(n, 0x20), base = mode == 1 ? 0x24 : 0x28;
    for (uint32_t at = base; at < base + 40 && at + 12 <= n->payload_bytes; at += 4) {
        const uint8_t *h = n->payload + at;
        unsigned w = (unsigned)h[0] << 8 | h[1], hgt = (unsigned)h[2] << 8 | h[3];
        if (w != hgt || (w != 128 && w != 256) || c3_wzx_payload_u32(n, at + 8) != 0x42 ||
            (uint64_t)at + 128 + (uint64_t)w * w > n->payload_bytes) continue;
        const uint8_t *d = h + 128;
        unsigned step = w / 128;
        for (unsigned y = 0; y < 128; ++y) for (unsigned x = 0; x < 128; ++x) {
            unsigned sum = 0;
            for (unsigned dy = 0; dy < step; ++dy) for (unsigned dx = 0; dx < step; ++dx) {
                unsigned sx = x * step + dx, sy = y * step + dy;
                sum += d[((sy / 4) * (w / 8) + sx / 8) * 32 + (sy % 4) * 8 + sx % 8]; /* GX I8: 8x4 tiles */
            }
            fx_distortion_program.intensity[y * 128 + x] = (uint8_t)(sum / (step * step));
        }
        return 1;
    }
    return 0;
}

static int fx_start_distortion(C3BattleGeneralStage *s, FxSlot *x)
{
    char error[160];
    for (unsigned k = 0; k < FX_SLOTS; ++k) if (fx_slot[k].used && fx_slot[k].type == 12) return 0; /* one capture */
    /* The capture pass refuses (fails the frame) on this HSD class. */
    for (uint32_t m = 0; m < s->count; ++m)
        for (uint32_t b = 0; b < s->models[m].batch_count; ++b)
            if ((s->models[m].batches[b].render_mode & 0x60000000u) == 0x20000000u) return 0;
    if (x->frames <= 0 || !fx_distortion_mask(x->n)) return 0;
    if (!c3_stage_renderer_distortion_program(&fx_distortion_program, error, sizeof error)) {
        if (s->trace) { fprintf(s->trace, "wzx_effect_refused type=12 reason=%s\n", error); fflush(s->trace); }
        return 0;
    }
    /* c3_confusion_type12_frame admits only the qualified encounter-10
     * profiles; the general curve below sets radius/indirect directly and
     * the profile is never stepped. */
    static const C3ConfusionType12Profile profile = {0, 44, 90, 0, 145, 62, .15f, 0, .25f};
    memset(&fx_distortion, 0, sizeof fx_distortion);
    fx_distortion.profile = profile;
    wzx_part_position(s, x->run->owner, 2, x->center); /* EffectStart: the idle body map [2] */
    return 1;
}

static void fx_frame_distortion(C3BattleGeneralStage *s, FxSlot *x)
{
    const C3WzxNode *n = x->n;
    if (c3_wzx_payload_u32(n, 0x10)) wzx_part_position(s, x->run->owner, 2, x->center);
    float duration = (float)x->frames, frame = (float)(x->local < x->frames ? x->local : x->frames - 1);
    float progress = frame / duration, pivot = c3_wzx_payload_u32(n, 0x20) == 1 ? 1.f : fx_f32(n, 0x24);
    float envelope = pivot != 0 && progress <= pivot ? progress / pivot : pivot < 1 ? (1 - progress) / (1 - pivot) : 0;
    float radius = fx_f32(n, 0x14) * duration * envelope * FX_SEQUENCE_SCALE * .1f;
    float indirect = fx_f32(n, 0x18) * (1 - progress);
    if (indirect < 0) indirect = 0; else if (indirect > .25f) indirect = .25f;
    /* Guard the capture quad's projection: a centre at or behind the near
     * plane would fail the frame, so it is hidden instead. */
    float fwd[3], right[3], up[3], rel[3];
    for (unsigned k = 0; k < 3; ++k) rel[k] = x->center[k] - s->camera.eye[k];
    int ok = fx_camera(s, fwd, right, up) && isfinite(radius) && radius > 0 && radius < 50 &&
             fx_dot(rel, fwd) > s->camera.near_plane * 2 + radius;
    /* ... and its projected corners must stay near the screen (the grid
     * admits capture coordinates within +-8 screens). */
    float th = tanf(s->camera.fov_y_degrees * .5f * 0.01745329f);
    for (int sgn = -1; ok && sgn <= 1; sgn += 2) {
        float c[3];
        for (unsigned k = 0; k < 3; ++k) c[k] = rel[k] + (float)sgn * radius * 1.5f * (right[k] + up[k]);
        float depth = fx_dot(c, fwd);
        if (depth <= s->camera.near_plane || th <= 0) { ok = 0; break; }
        float sx = fx_dot(c, right) / (depth * th * (400.f / 240.f)), sy = fx_dot(c, up) / (depth * th);
        if (!(fabsf(sx) < 6) || !(fabsf(sy) < 6)) ok = 0;
    }
    memcpy(fx_distortion.center, x->center, sizeof x->center);
    fx_distortion.radius = ok ? radius : 0; fx_distortion.indirect = indirect;
    fx_distortion.frame = (uint32_t)frame; fx_distortion.active = (uint8_t)ok;
    c3_stage_renderer_confusion(&fx_distortion);
}

/* ---- 11: billboard (patchiruTextureStart / fn_8013FF0C) ----------------
 * The node's DAT is put at the camera, near * 1.1 ahead, and scaled to
 * exactly fill the view (2 near' tan(fov/2), times aspect) over its bound.
 * It runs until its animation ends. APPROX: the display aspect (Hor+) and
 * the camera without roll; the original particle resource (q+0xC) is not
 * spawned. */
static int fx_start_billboard(C3BattleGeneralStage *s, FxSlot *x)
{
    if (x->model < 0) return 0;
    C3Title *t = &s->models[x->model];
    t->placement.enabled = 0; t->hidden = 0;
    if (!c3_title_animate(t, 0, 0)) return 0;
    float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
    for (uint32_t b = 0; b < t->batch_count; ++b)
        for (uint32_t v = t->batches[b].first; v < t->batches[b].first + t->batches[b].count && v < t->vertex_count; ++v)
            for (unsigned k = 0; k < 2; ++k) { float p = t->draw_vertices[v].pos[k]; if (p < lo[k]) lo[k] = p; if (p > hi[k]) hi[k] = p; }
    x->bound[0] = hi[0] - lo[0]; x->bound[1] = hi[1] - lo[1];
    if (!(x->bound[0] > 1e-4f) || !(x->bound[1] > 1e-4f) || !isfinite(x->bound[0]) || !isfinite(x->bound[1])) return 0;
    float *tr = s->transforms[x->model];
    tr[0] = tr[1] = tr[2] = tr[3] = 0; tr[4] = 1; /* placement off: the model matrix places it */
    s->clips[x->model] = 0; s->clocks[x->model] = 0;
    return 1;
}

static void fx_frame_billboard(C3BattleGeneralStage *s, FxSlot *x)
{
    float fwd[3], right[3], up[3], m[12];
    if (!fx_camera(s, fwd, right, up)) return;
    float d = s->camera.near_plane * 1.1f;
    float h = 2.f * d * tanf(s->camera.fov_y_degrees * .5f * 0.01745329f), w = h * (400.f / 240.f);
    float sx = w / x->bound[0], sy = h / x->bound[1], sz = .001f;
    for (unsigned r = 0; r < 3; ++r) {
        m[r * 4 + 0] = right[r] * sx; m[r * 4 + 1] = up[r] * sy; m[r * 4 + 2] = -fwd[r] * sz;
        m[r * 4 + 3] = s->camera.eye[r] + fwd[r] * d;
    }
    c3_stage_renderer_set_model_matrix(x->model, m);
}

/* ---- 5: leaf clones (leaffxStartEffect / fn_80138CCC / fn_80138DE4) ----
 * q+0/4 scale base/range, q+8/0xC height base/range, q+0x10/0x14 radius
 * base/range, q+0x18 respawn lift, q+0x1C path speed per tick, q+0x20/0x24
 * height multiplier base/range, q+0x28 clones (<=32), q+0x2C fade start,
 * q+0x30 source part. Each clone flies a cubic Bezier from the owner's part
 * (P0, P1 = e+0x18, P2 = e+0x24, P3 = e+0x0C), re-seeds a new arc when t
 * passes 1, slerps between two random orientations, dies below the floor,
 * and all fade out after the fade start. APPROX: the clone scale factor at
 * work+0x14 (not in the payload) is taken as the sequence scale .75; the
 * source part (a GSmodel joint index) is read as the owner's body-map slot
 * (also for types 1 and 12); the clones are instances of the one DAT (its
 * own draw collapsed to a point) without its animation. */
static void fx_dir(FxSlot *x, float pitch_range, float out[3])
{
    float yaw = 6.2831853f * fx_rand01(x), pitch = pitch_range * fx_rand01(x);
    out[0] = sinf(pitch) * cosf(yaw); out[1] = cosf(pitch); out[2] = sinf(pitch) * sinf(yaw);
}
static void fx_axis_angle(FxSlot *x, float q[4])
{
    float axis[3]; fx_dir(x, 3.1415927f, axis);
    float a = 6.2831853f * fx_rand01(x) * .5f, sn = sinf(a);
    q[0] = axis[0] * sn; q[1] = axis[1] * sn; q[2] = axis[2] * sn; q[3] = cosf(a);
}
static void fx_leaf_seed(C3BattleGeneralStage *s, FxSlot *x, FxLeaf *l)
{
    const C3WzxNode *n = x->n;
    float rb = fx_f32(n, 0x1C) * .1f, rr = fx_f32(n, 0x20) * .1f, hb = fx_f32(n, 0x14) * .1f, hr = fx_f32(n, 0x18) * .1f;
    int32_t part = (int16_t)(c3_wzx_payload_u32(n, 0x3C) & 0xFFFF);
    float pos[3], d[3];
    wzx_part_position(s, x->run->owner, part >= 0 && part < 17 ? (unsigned)part : 0, pos);
    l->scale = (fx_f32(n, 0x10) * fx_rand11(x) + fx_f32(n, 0xC)) * FX_SEQUENCE_SCALE;
    l->t = 0;
    fx_axis_angle(x, l->q0); fx_axis_angle(x, l->q1);
    memcpy(l->p[0], pos, 12);
    float r1 = (rr * fx_rand11(x) + rb) * l->scale; fx_dir(x, 3.1415927f, d);
    for (unsigned k = 0; k < 3; ++k) l->p[1][k] = pos[k] + d[k] * r1;
    float h = (hr * fx_rand11(x) + hb) * (fx_f32(n, 0x30) * fx_rand11(x) + fx_f32(n, 0x2C)) * l->scale;
    fx_dir(x, 1.5707963f, d);
    for (unsigned k = 0; k < 3; ++k) l->p[3][k] = pos[k] + d[k] * h;
    float r2 = (rr * fx_rand11(x) + rb) * l->scale; fx_dir(x, 3.1415927f, d);
    for (unsigned k = 0; k < 3; ++k) l->p[2][k] = l->p[3][k] + d[k] * r2;
    l->alive = 1;
}
/* fn_80139074: continue from the old end along its tangent. */
static void fx_leaf_respawn(FxSlot *x, FxLeaf *l)
{
    const C3WzxNode *n = x->n;
    float fb = fx_f32(n, 0x1C) * .1f, fr = fx_f32(n, 0x20) * .1f, nb = fx_f32(n, 0x14) * .1f, nr = fx_f32(n, 0x18) * .1f;
    float lift = fx_f32(n, 0x24) * .1f, tangent[3], d[3];
    l->t -= 1.f;
    memcpy(l->q0, l->q1, sizeof l->q0); fx_axis_angle(x, l->q1);
    float old_end[3], old_c2[3]; memcpy(old_end, l->p[3], 12); memcpy(old_c2, l->p[2], 12);
    memcpy(l->p[0], old_end, 12);
    for (unsigned k = 0; k < 3; ++k) tangent[k] = old_end[k] - old_c2[k];
    fx_normalize(tangent);
    float d0 = (fr * fx_rand11(x) + fb) * l->scale;
    for (unsigned k = 0; k < 3; ++k) l->p[1][k] = l->p[0][k] + tangent[k] * d0;
    float d1 = (nr * fx_rand11(x) + nb) * l->scale; fx_dir(x, 3.1415927f, d);
    for (unsigned k = 0; k < 3; ++k) l->p[3][k] = l->p[0][k] + d[k] * d1 + (k == 1 ? lift : 0);
    float d2 = (fr * fx_rand11(x) + fb) * l->scale; fx_dir(x, 3.1415927f, d);
    for (unsigned k = 0; k < 3; ++k) l->p[2][k] = l->p[3][k] + d[k] * d2;
}
static void fx_bezier(const FxLeaf *l, float t, float out[3])
{
    float u = 1 - t, b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    for (unsigned k = 0; k < 3; ++k) out[k] = b0 * l->p[0][k] + b1 * l->p[1][k] + b2 * l->p[2][k] + b3 * l->p[3][k];
}
static void fx_slerp_matrix(const float a[4], const float b0[4], float t, float scale, float m[12])
{
    float b[4] = {b0[0], b0[1], b0[2], b0[3]}, c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], q[4];
    if (c < 0) { for (unsigned k = 0; k < 4; ++k) b[k] = -b[k]; c = -c; }
    float wa = 1 - t, wb = t;
    if (c < .9995f) { float th = acosf(c), sn = sinf(th); wa = sinf((1 - t) * th) / sn; wb = sinf(t * th) / sn; }
    float len = 0;
    for (unsigned k = 0; k < 4; ++k) { q[k] = wa * a[k] + wb * b[k]; len += q[k] * q[k]; }
    len = len > 1e-12f ? 1.f / sqrtf(len) : 0;
    float x = q[0] * len, y = q[1] * len, z = q[2] * len, w = q[3] * len;
    m[0] = (1 - 2 * (y * y + z * z)) * scale; m[1] = 2 * (x * y - z * w) * scale; m[2] = 2 * (x * z + y * w) * scale;
    m[4] = 2 * (x * y + z * w) * scale; m[5] = (1 - 2 * (x * x + z * z)) * scale; m[6] = 2 * (y * z - x * w) * scale;
    m[8] = 2 * (x * z - y * w) * scale; m[9] = 2 * (y * z + x * w) * scale; m[10] = (1 - 2 * (x * x + y * y)) * scale;
}

static int fx_start_leaves(C3BattleGeneralStage *s, FxSlot *x)
{
    if (x->model < 0 || x->frames <= 0) return 0;
    int set = -1;
    for (unsigned k = 0; k < FX_LEAF_NODES; ++k) if (!fx_leaf_used[k]) { set = (int)k; break; }
    if (set < 0) return 0;
    uint32_t count = c3_wzx_payload_u32(x->n, 0x34);
    if (count > FX_LEAVES) count = FX_LEAVES;
    memset(fx_leaves[set], 0, sizeof fx_leaves[set]);
    for (uint32_t k = 0; k < count; ++k) fx_leaf_seed(s, x, &fx_leaves[set][k]);
    fx_leaf_used[set] = 1; x->leaf_set = set;
    float *tr = s->transforms[x->model];
    tr[0] = tr[1] = tr[2] = tr[3] = 0; tr[4] = 1;
    s->clips[x->model] = 0; s->clocks[x->model] = 0;
    return 1;
}

static void fx_update_leaves(FxSlot *x, uint32_t ticks)
{
    const C3WzxNode *n = x->n;
    uint32_t fade = c3_wzx_payload_u32(n, 0x38);
    float delta = fx_f32(n, 0x28) * (float)ticks;
    if ((uint32_t)x->local < fade && fade) delta *= 2.f - (float)x->local / (float)fade;
    for (unsigned k = 0; k < FX_LEAVES; ++k) {
        FxLeaf *l = &fx_leaves[x->leaf_set][k];
        if (!l->alive) continue;
        l->t += delta;
        for (unsigned guard = 0; l->t >= 1.f && guard < 4; ++guard) fx_leaf_respawn(x, l);
        float p[3]; fx_bezier(l, l->t, p);
        if (p[1] < 0) l->alive = 0;
    }
}

/* ---- 1: lightning (electronStartEffect / fn_80139D10 / fn_8013A1D4) -----
 * q+0 per-tick jitter, q+0xC step spread, q+0x24 step, q+0x30 height,
 * q+0x34 width, q+0x38 branch chance, q+0x3C bolts, q+0x40 branch depth,
 * q+0x44 colour (R in the low byte), q+0x48 owner part. Bolts start in turn
 * over the node's frames; each grows down from its top over the first half
 * of its span and fades over the second, and every point random-walks by
 * the jitter each tick. APPROX: drawn as untextured additive ribbons (a soft
 * wide pass and a bright core) instead of the payload's glow texture, and
 * the bolt's lowest point is placed on the owner's part instead of the
 * original's q+0x18 interpolation. */
static void fx_bolt_branch(FxSlot *x, FxBolt *b, int16_t origin, unsigned depth, float weight)
{
    const C3WzxNode *n = x->n;
    float spread[3], step[3];
    fx_vec(n, 0x18, spread); fx_vec(n, 0x30, step);
    unsigned start = b->count;
    const float *opos = origin < 0 ? b->origin : b->pts[origin].pos;
    float stop_height = depth ? .25f * opos[1] : 0, spawn = 1.f / (float)(depth + 1);
    int16_t prev = origin;
    for (unsigned i = start; i < FX_BOLT_POINTS; ++i) {
        FxPoint *c = &b->pts[i];
        const float *pp = prev < 0 ? b->origin : b->pts[prev].pos;
        for (unsigned k = 0; k < 3; ++k) c->pos[k] = spread[k] * fx_rand11(x) + step[k] + pp[k];
        c->prev = prev; prev = (int16_t)i; ++b->count;
        if (c->pos[1] < stop_height || spawn < fx_rand01(x)) { if (!depth) memcpy(b->end, c->pos, 12); break; }
    }
    if (!depth && b->count == FX_BOLT_POINTS) memcpy(b->end, b->pts[b->count - 1].pos, 12);
    unsigned max_depth = c3_wzx_payload_u32(n, 0x4C), count = b->count;
    if (max_depth > 6) max_depth = 6; /* recursion bound; the disc uses 3 */
    if (depth >= max_depth) return;  /* as the original: the deepest branch keeps zero width */
    float chance = fx_f32(n, 0x44) * (float)(max_depth - depth) / (float)max_depth;
    float decay = depth && count > start ? weight / (float)(count - start) : 0;
    for (unsigned j = start; j < count; ++j) {
        b->pts[j].w0 = weight; weight = b->pts[j].w1 = weight - decay;
        if (chance > fx_rand01(x) && b->count < FX_BOLT_POINTS) fx_bolt_branch(x, b, b->pts[j].prev, depth + 1, .5f * weight);
    }
}

static int fx_start_lightning(FxSlot *x)
{
    for (unsigned k = 0; k < FX_SLOTS; ++k) if (fx_slot[k].used && fx_slot[k].type == 1) return 0; /* one bolt buffer */
    const C3WzxNode *n = x->n;
    uint32_t count = c3_wzx_payload_u32(n, 0x48);
    if (!count || x->frames <= 0 || n->payload_bytes < 0x58) return 0;
    if (count > FX_BOLTS) count = FX_BOLTS;
    uint32_t total = (uint32_t)x->frames, span, step;
    if (count > 1) { span = total / (count - 1); step = (total - span) / count; } else { step = 0; span = total; }
    float height = fx_f32(n, 0x3C);
    for (uint32_t i = 0; i < FX_BOLTS; ++i) {
        FxBolt *b = &fx_bolts[i];
        memset(b, 0, sizeof *b);
        if (i >= count) continue;
        b->origin[1] = height;
        fx_bolt_branch(x, b, -1, 0, fx_f32(n, 0x40));
        b->start = (uint16_t)(i * step); b->stop = (uint16_t)(b->start + span);
    }
    return 1;
}

static void fx_update_lightning(FxSlot *x, uint32_t ticks)
{
    float jitter[3]; fx_vec(x->n, 0xC, jitter);
    for (uint32_t i = 0; i < FX_BOLTS && ticks; ++i) {
        FxBolt *b = &fx_bolts[i];
        if (!b->count || x->local < b->start || x->local >= b->stop) continue;
        for (uint32_t t = 0; t < ticks && t < 8; ++t)
            for (unsigned p = 0; p < b->count; ++p) for (unsigned k = 0; k < 3; ++k) b->pts[p].pos[k] += jitter[k] * fx_rand11(x);
    }
}

static unsigned fx_frame_lightning(C3BattleGeneralStage *s, FxSlot *x, unsigned used)
{
    const C3WzxNode *n = x->n;
    uint32_t c = c3_wzx_payload_u32(n, 0x50);
    float rgba[4] = {(float)(c & 255) / 255.f, (float)(c >> 8 & 255) / 255.f, (float)(c >> 16 & 255) / 255.f, (float)(c >> 24) / 255.f};
    float target[3], height = fx_f32(n, 0x3C);
    uint32_t part = c3_wzx_payload_u32(n, 0x54);
    wzx_part_position(s, x->run->owner, part < 17 ? part : 0, target);
    for (uint32_t i = 0; i < FX_BOLTS; ++i) {
        FxBolt *b = &fx_bolts[i];
        if (!b->count || x->local < b->start || x->local >= b->stop) continue;
        unsigned current = (unsigned)(x->local - b->start), half = (unsigned)(b->stop - b->start) >> 1;
        float threshold = current < half && half ? height * (1.f - (float)current / (float)half) : 0;
        float fade = current > half && half ? (float)(b->stop - b->start - current) / (float)half : 1;
        if (fade > 1) fade = 1;
        for (unsigned p = 0; p < b->count; ++p) {
            const FxPoint *pt = &b->pts[p];
            const float *pp = pt->prev < 0 ? b->origin : b->pts[pt->prev].pos;
            if (pp[1] <= threshold) continue;
            float a[3], e[3], dir[3], view[3], side[3], mid[3];
            for (unsigned k = 0; k < 3; ++k) {
                a[k] = target[k] + (pp[k] - b->end[k]) * .1f; e[k] = target[k] + (pt->pos[k] - b->end[k]) * .1f;
                dir[k] = e[k] - a[k]; mid[k] = .5f * (a[k] + e[k]); view[k] = s->camera.eye[k] - mid[k];
            }
            fx_normalize(dir); fx_normalize(view); fx_cross(side, view, dir);
            if (fx_normalize(side) <= .001f) continue;
            for (unsigned layer = 0; layer < 2; ++layer) {
                if (used + 6 > C3_STAGE_MAX_EFFECT_VERTICES) return used;
                float width = layer ? .3f : 1.f, alpha = (layer ? 1.f : .35f) * rgba[3] * fade;
                float w0 = .5f * pt->w0 * .1f * width, w1 = .5f * pt->w1 * .1f * width;
                float q[4][3];
                for (unsigned k = 0; k < 3; ++k) {
                    q[0][k] = a[k] + side[k] * w0; q[1][k] = a[k] - side[k] * w0;
                    q[2][k] = e[k] - side[k] * w1; q[3][k] = e[k] + side[k] * w1;
                }
                static const unsigned order[6] = {0, 1, 2, 0, 2, 3};
                for (unsigned v = 0; v < 6; ++v) {
                    C3StageEffectVertex *o = &fx_triangles[used++];
                    memcpy(o->pos, q[order[v]], 12);
                    for (unsigned k = 0; k < 3; ++k) o->rgba[k] = layer ? .5f + .5f * rgba[k] : rgba[k];
                    o->rgba[3] = alpha;
                }
            }
        }
    }
    return used;
}

/* ---- hooks ---------------------------------------------------------------- */
static int wzx_effect_start(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n)
{
    FxSlot *x = NULL;
    for (unsigned k = 0; k < FX_SLOTS; ++k) if (!fx_slot[k].used) { x = &fx_slot[k]; break; }
    uint32_t type = c3_wzx_payload_u32(n, 0);
    if (!x || r->owner >= 6) return 0;
    memset(x, 0, sizeof *x);
    x->type = (uint8_t)type; x->run = r; x->node = i; x->n = &r->player.nodes[i];
    x->model = r->sequence->node_model[i]; x->frames = (int32_t)c3_wzx_payload_u32(n, 4);
    x->rng = 0x9E3779B9u ^ (i * 2654435761u) ^ s->cue_serial ^ (r->owner << 24); if (!x->rng) x->rng = 1;
    x->leaf_set = -1;
    int ok;
    switch (type) {
    case 12: ok = fx_start_distortion(s, x); break;
    case 11: ok = fx_start_billboard(s, x); break;
    case 8: ok = model_for_slot(s, r->owner) >= 0 && fx_blend(x, 0) != 0; break;
    case 9:
        x->capture = c3_wzx_payload_u32(n, 0x10) != 3 || c3_wzx_payload_u32(n, 0x14) != 0;
        ok = model_for_slot(s, r->owner) >= 0 && fx_blend(x, 0) != 0; break;
    case 5: ok = fx_start_leaves(s, x); break;
    case 1: ok = fx_start_lightning(x); break;
    default: ok = 0; break;
    }
    if (s->trace) { fprintf(s->trace, "wzx_effect_start type=%lu node=%lu owner=%u frames=%ld model=%ld started=%d\n", (unsigned long)type,
        (unsigned long)i, r->owner, (long)x->frames, (long)x->model, ok); fflush(s->trace); }
    if (!ok) { memset(x, 0, sizeof *x); return 0; }
    x->used = 1;
    if (type == 11 || type == 5) r->node_live[i] = 1;
    return 1;
}

static int wzx_effect_update(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n, uint32_t ticks, int32_t local)
{
    FxSlot *x = fx_find(r, i);
    (void)n;
    if (!x) return 0;
    x->local = local;
    switch (x->type) {
    case 12: return local < x->frames;
    case 11: {
        C3Title *t = &s->models[x->model];
        float length = t->clip_count && t->fps > 0 ? (float)t->clips[0].frame_count / t->fps : 0;
        s->clocks[x->model] = (float)local / 60.f;
        if (t->clip_count && t->clips[0].loop) return -1;
        return s->clocks[x->model] < length; }
    case 8: case 9: {
        int k = fx_blend(x, local);
        if (x->type == 9) {
            /* Owner position history for the after-images (part 2, the body
             * centre the camera uses; the origin without retained parts). */
            float p[3]; wzx_part_position(s, r->owner, 2, p);
            unsigned at = (x->hist_head + x->hist_count) % FX_HISTORY;
            if (x->hist_count == FX_HISTORY) { x->hist_head = (x->hist_head + 1) % FX_HISTORY; at = (x->hist_head + FX_HISTORY - 1) % FX_HISTORY; }
            else ++x->hist_count;
            x->hist[at].tick = local; memcpy(x->hist[at].pos, p, 12);
        }
        return k != 0; }
    case 5:
        fx_update_leaves(x, ticks);
        return local < x->frames;
    case 1:
        fx_update_lightning(x, ticks);
        return local < x->frames;
    default: return 0;
    }
}

static void wzx_effect_stop(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n, int forced)
{
    FxSlot *x = fx_find(r, i);
    (void)n; (void)forced;
    if (!x) return;
    if ((x->type == 11 || x->type == 5) && x->model >= 0) {
        c3_stage_renderer_set_model_matrix((size_t)x->model, NULL);
        s->transforms[x->model][4] = 1;
    }
    if (x->type == 12) { c3_confusion_type12_cancel(&fx_distortion); c3_stage_renderer_confusion(NULL); }
    if (x->type == 5 && x->leaf_set >= 0) fx_leaf_used[x->leaf_set] = 0;
    memset(x, 0, sizeof *x);
}

/* 8: the owner redrawn additively over itself at the keyed blend (APPROX of
 * the GX environment map, which reflects a capture of the other models).
 * 9: up to FX_GHOSTS translucent copies of the owner at its positions of the
 * last 60 * blend ticks, alpha 0.5 (j / copies)^2 as fn_8013DE6C (APPROX:
 * past positions with the current pose; the capture variant, a full-screen
 * feedback blend of the previous frame, uses the same after-images). */
static void wzx_effect_frame(C3BattleGeneralStage *s)
{
    unsigned instances = 0, triangles = 0, live = 0;
    for (unsigned k = 0; k < FX_SLOTS; ++k) {
        FxSlot *x = &fx_slot[k];
        if (!x->used) continue;
        ++live;
        unsigned owner = x->run->owner;
        int om = model_for_slot(s, owner);
        switch (x->type) {
        case 12: fx_frame_distortion(s, x); break;
        case 11: fx_frame_billboard(s, x); break;
        case 8: {
            if (om < 0 || !fx_owner_visible(s, owner) || x->value <= 0) break;
            float v = x->value > 1 ? 1 : x->value;
            for (unsigned layer = 0; layer < 2 && instances < C3_STAGE_MAX_INSTANCES; ++layer) {
                /* Its own texture brightened, plus a flat cool sheen. */
                C3StageInstance *o = &fx_instances[instances++];
                memset(o, 0, sizeof *o);
                o->model = (uint32_t)om; o->matrix[0] = o->matrix[5] = o->matrix[10] = 1; o->additive = (uint8_t)(1 + layer);
                float k = layer ? .28f : .5f;
                o->rgba[0] = o->rgba[1] = k * v; o->rgba[2] = (k + .05f) * v; o->rgba[3] = 1;
            }
            break; }
        case 9: {
            if (om < 0 || !fx_owner_visible(s, owner) || !x->hist_count) break;
            int copies = (int)(60.f * x->value + .5f);
            if (copies <= 0) break;
            const float *now = x->hist[(x->hist_head + x->hist_count - 1) % FX_HISTORY].pos;
            for (unsigned j = 1; j <= FX_GHOSTS && instances < C3_STAGE_MAX_INSTANCES; ++j) {
                int32_t back = (int32_t)((float)copies * (float)(FX_GHOSTS - j + 1) / FX_GHOSTS + .5f);
                int32_t want = x->local - back;
                const float *at = x->hist[x->hist_head].pos;
                for (unsigned h = 0; h < x->hist_count; ++h) {
                    unsigned e = (x->hist_head + h) % FX_HISTORY;
                    if (x->hist[e].tick <= want) at = x->hist[e].pos; else break;
                }
                float d[3] = {at[0] - now[0], at[1] - now[1], at[2] - now[2]};
                if (fx_dot(d, d) < 1e-4f) continue; /* no motion: the copy would hide in the owner */
                float f = (float)j / FX_GHOSTS;
                C3StageInstance *o = &fx_instances[instances++];
                memset(o, 0, sizeof *o);
                o->model = (uint32_t)om; o->matrix[0] = o->matrix[5] = o->matrix[10] = 1;
                o->matrix[3] = d[0]; o->matrix[7] = d[1]; o->matrix[11] = d[2];
                o->rgba[0] = o->rgba[1] = o->rgba[2] = 1; o->rgba[3] = .5f * f * f;
            }
            break; }
        case 5: {
            if (x->model < 0 || x->leaf_set < 0) break;
            /* The DAT's own draw collapses to a point; every clone is an instance. */
            static const float collapsed[12] = {0};
            c3_stage_renderer_set_model_matrix((size_t)x->model, collapsed);
            uint32_t fade_start = c3_wzx_payload_u32(x->n, 0x38);
            float alpha = 1;
            if ((uint32_t)x->local >= fade_start && x->frames > (int32_t)fade_start)
                alpha = 1.f - (float)(x->local - (int32_t)fade_start) / (float)(x->frames - (int32_t)fade_start);
            if (alpha <= 0) break;
            for (unsigned l = 0; l < FX_LEAVES && instances < C3_STAGE_MAX_INSTANCES; ++l) {
                const FxLeaf *leaf = &fx_leaves[x->leaf_set][l];
                if (!leaf->alive) continue;
                C3StageInstance *o = &fx_instances[instances++];
                memset(o, 0, sizeof *o);
                o->model = (uint32_t)x->model;
                fx_slerp_matrix(leaf->q0, leaf->q1, leaf->t, leaf->scale, o->matrix);
                float p[3]; fx_bezier(leaf, leaf->t, p);
                o->matrix[3] = p[0]; o->matrix[7] = p[1]; o->matrix[11] = p[2];
                o->rgba[0] = o->rgba[1] = o->rgba[2] = 1; o->rgba[3] = alpha;
            }
            break; }
        case 1: triangles = fx_frame_lightning(s, x, triangles); break;
        default: break;
        }
    }
    if (!live && !fx_passes_live) return;
    if (!c3_stage_renderer_set_instances(fx_instances, instances)) c3_stage_renderer_set_instances(NULL, 0);
    if (!c3_stage_renderer_set_effect_triangles(fx_triangles, triangles)) c3_stage_renderer_set_effect_triangles(NULL, 0);
    fx_passes_live = (uint8_t)(live != 0);
}
