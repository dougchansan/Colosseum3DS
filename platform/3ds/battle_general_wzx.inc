/* General WZX effect layer of the 3D battle stage; included by
 * battle_general_stage.c. The player core (src/wzx_player.c) schedules the
 * nodes; this file is its backend: effect models attached to the owner's
 * body-part bones, particle launches, type-0 colour keyframes on the floor
 * or the owner, and owner hide/show. Other effect types and sounds play
 * nothing yet (the node still keeps its scheduled time). */
#include "colosseum3ds/wzx_player.h"
#include "colosseum3ds/wzx_index.h"
#include "colosseum3ds/wzx_camera.h"

#define WZX_SEQUENCES 8u
#define WZX_BANKS 16u
#define WZX_SYSTEMS 6u
#define WZX_PLAYERS 4u
#define WZX_EFFECT_SLOT (UINT32_MAX - 3u)

typedef struct {
    char name[48];
    uint8_t *bytes; size_t size;
    C3WzxPack pack;
    int32_t node_model[C3_WZX_MAX_NODES]; /* stage model index per model node, -1 none */
    int32_t node_bank[C3_WZX_MAX_NODES];  /* bank index per particle node, -1 none */
} WzxSequence;
typedef struct { char name[48]; uint8_t *bytes; size_t size; C3ParticlePack pack; uint8_t registered; } WzxBank;
typedef struct {
    C3WzxPlayer player;
    WzxSequence *sequence;
    unsigned owner, target;   /* battle positions */
    int32_t node_system[C3_WZX_MAX_NODES];
    uint8_t node_live[C3_WZX_MAX_NODES];
    uint64_t units;           /* ms*60 not yet turned into ticks */
    uint8_t active, kind;
} WzxRun;
struct C3BattleWzxLayer {
    C3WzxIndex index;
    WzxSequence sequences[WZX_SEQUENCES]; unsigned sequence_count;
    WzxBank banks[WZX_BANKS]; unsigned bank_count;
    C3ParticleSystem *systems; uint8_t system_used[WZX_SYSTEMS];
    WzxRun runs[WZX_PLAYERS];
    uint32_t base_count;      /* stage models before this batch's effect models */
    int floor_tint; uint8_t floor_rgb[3];
    int owner_tint[4]; uint8_t owner_rgb[4][3];
    uint32_t started, skipped;
    /* The original battle camera (src/wzx_camera.c). */
    C3WzxCameraTables camera_tables;
    C3WzxCamera camera;
    C3WzxCameraScene scene;
    C3TitleCamera home;
    uint8_t camera_ok, sequence_camera, command_phase;
    unsigned persistent_sequences, persistent_banks; /* the Poke Ball specials, kept for the battle */
    char follow[48]; unsigned follow_owner; uint16_t follow_voice; uint8_t follow_pending;
};
/* Poke Ball specials (lbl_803727C8 rows 0x0A-0x0C, 0x40: monsterball in /
 * open / out / downin). The general engine has no ball item, so every
 * Pokemon uses the Poke Ball's. */
enum { BALL_IN = 0x0A, BALL_OPEN = 0x0B, BALL_OUT = 0x0C, BALL_DOWN = 0x40 };
/* Battle-grid order the camera uses: each trainer, then its Pokemon. */
static const unsigned grid_slot[6] = {4, 0, 1, 5, 2, 3};
static unsigned slot_actor(unsigned slot) { for (unsigned a = 0; a < 6; ++a) if (grid_slot[a] == slot) return a; return 0; }

static int read_all(const char *path, uint8_t **out, size_t *bytes)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    long n = -1; uint8_t *data = NULL;
    if (!fseek(f, 0, SEEK_END)) n = ftell(f);
    if (n > 0 && n <= 8 * 1024 * 1024 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) == (size_t)n) {
        fclose(f); *out = data; *bytes = (size_t)n; return 1;
    }
    fclose(f); free(data); return 0;
}

static unsigned slot_pkx(const C3BattleGeneralStage *s, unsigned slot)
{
    int m = model_for_slot(s, slot);
    unsigned v = 0xFFFF;
    if (m >= 0 && sscanf(s->files[m], "pkx%u.c3t", &v) != 1) v = 0xFFFF;
    return v;
}

static void wzx_release_batch(C3BattleGeneralStage *s)
{
    struct C3BattleWzxLayer *w = s->wzx;
    if (!w) return;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) if (w->runs[i].active) c3_wzx_player_stop(&w->runs[i].player);
    memset(w->runs, 0, sizeof w->runs);
    for (unsigned i = 0; i < WZX_SYSTEMS; ++i) { c3_particle_cancel(&w->systems[i]); w->system_used[i] = 0; }
    for (unsigned i = w->persistent_sequences; i < w->sequence_count; ++i) free(w->sequences[i].bytes);
    for (unsigned i = w->persistent_banks; i < w->bank_count; ++i) free(w->banks[i].bytes);
    w->sequence_count = w->persistent_sequences; w->bank_count = w->persistent_banks;
    w->follow_pending = 0;
    while (s->count > w->base_count) { c3_title_close(&s->models[s->count - 1]); memset(&s->models[s->count - 1], 0, sizeof s->models[0]); --s->count; }
    w->floor_tint = 0; memset(w->owner_tint, 0, sizeof w->owner_tint);
}

static void wzx_close(C3BattleGeneralStage *s)
{
    if (!s->wzx) return;
    s->wzx->persistent_sequences = s->wzx->persistent_banks = 0;
    s->wzx->base_count = 0;
    wzx_release_batch(s);
    c3_wzx_index_free(&s->wzx->index);
    free(s->wzx->systems); free(s->wzx); s->wzx = NULL;
}

static int wzx_open(C3BattleGeneralStage *s, const char *root)
{
    char path[512];
    struct C3BattleWzxLayer *w = calloc(1, sizeof *w);
    if (!w) return 0;
    w->systems = calloc(WZX_SYSTEMS, sizeof *w->systems);
    snprintf(path, sizeof path, "%s/wzx.c3n", root);
    if (!w->systems || !c3_wzx_index_load(&w->index, path, s->error, sizeof s->error)) { s->error[0] = 0; free(w->systems); free(w); return 0; }
    w->base_count = s->count;
    s->wzx = w;
    snprintf(path, sizeof path, "%s/wzx_camera.c3k", root);
    char error[160];
    if (c3_wzx_camera_tables_load(&w->camera_tables, path, error, sizeof error)) {
        c3_wzx_camera_init(&w->camera, &w->camera_tables, 0xCA3E0000u ^ s->cue_serial);
        c3_wzx_camera_idle_reset(&w->camera, 0);
        w->camera_ok = 1;
    } else if (s->trace) { fprintf(s->trace, "wzx_camera_unavailable reason=%s\n", error); fflush(s->trace); }
    w->home = s->camera;
    return 1;
}

static WzxSequence *wzx_sequence(C3BattleGeneralStage *s, const char *root, const char *name)
{
    struct C3BattleWzxLayer *w = s->wzx; char path[512], error[160];
    for (unsigned i = 0; i < w->sequence_count; ++i) if (!strcmp(w->sequences[i].name, name)) return &w->sequences[i];
    if (w->sequence_count >= WZX_SEQUENCES) return NULL;
    WzxSequence *q = &w->sequences[w->sequence_count];
    memset(q, 0, sizeof *q);
    snprintf(path, sizeof path, "%s/%s", root, name);
    if (!read_all(path, &q->bytes, &q->size) || !c3_wzx_pack_decode(&q->pack, q->bytes, q->size, error, sizeof error)) {
        if (s->trace) { fprintf(s->trace, "wzx_pack_unavailable name=%s\n", name); fflush(s->trace); }
        free(q->bytes); memset(q, 0, sizeof *q); return NULL;
    }
    snprintf(q->name, sizeof q->name, "%s", name);
    for (uint32_t i = 0; i < C3_WZX_MAX_NODES; ++i) { q->node_model[i] = -1; q->node_bank[i] = -1; }
    for (uint32_t i = 0; i < q->pack.node_count; ++i) {
        C3WzxNode n; c3_wzx_pack_node(&q->pack, i, &n);
        if (n.status || !n.file) continue;
        snprintf(path, sizeof path, "%s/%s", root, n.file);
        if ((n.kind == C3_WZX_MODEL || n.kind == C3_WZX_EFFECT) && strstr(n.file, ".c3t") && s->count < C3_GSTAGE_MODELS) {
            uint32_t m = s->count;
            if (c3_title_load(&s->models[m], path, error, sizeof error) && !c3_stage_renderer_validate(&s->models[m], error, sizeof error)) {
                /* A material the renderer cannot draw: the node keeps its time, unseen. */
                if (s->trace) { fprintf(s->trace, "wzx_model_unsupported file=%s reason=%s\n", n.file, error); fflush(s->trace); }
                c3_title_close(&s->models[m]); memset(&s->models[m], 0, sizeof s->models[m]);
            } else if (s->models[m].file_data) {
                s->count = m + 1; s->slots[m] = WZX_EFFECT_SLOT; s->draw[m] = &s->models[m];
                s->transforms[m][4] = 1; s->clips[m] = 0; s->clocks[m] = 0; s->appear[m] = 1;
                snprintf(s->files[m], sizeof s->files[m], "%s", n.file);
                q->node_model[i] = (int32_t)m;
            }
        } else if (n.kind == C3_WZX_PARTICLE && strstr(n.file, ".c3p")) {
            int found = -1;
            for (unsigned b = 0; b < w->bank_count; ++b) if (!strcmp(w->banks[b].name, n.file)) found = (int)b;
            if (found < 0 && w->bank_count < WZX_BANKS) {
                WzxBank *bk = &w->banks[w->bank_count]; memset(bk, 0, sizeof *bk);
                uint8_t sha[20];
                if (read_all(path, &bk->bytes, &bk->size)) {
                    c3_save_codec_sha1(sha, bk->bytes, (uint32_t)bk->size);
                    if (c3_particle_pack_decode(&bk->pack, bk->bytes, bk->size, sha, error, sizeof error)) {
                        snprintf(bk->name, sizeof bk->name, "%s", n.file); found = (int)w->bank_count++;
                    } else { free(bk->bytes); memset(bk, 0, sizeof *bk); }
                }
            }
            q->node_bank[i] = found;
        }
    }
    ++w->sequence_count;
    return q;
}

/* At stage load, after wzx_open: the Poke Ball specials stay loaded. */
static void wzx_load_persistent(C3BattleGeneralStage *s, const char *root)
{
    struct C3BattleWzxLayer *w = s->wzx;
    if (!w) return;
    static const uint16_t ids[] = {BALL_IN, BALL_OPEN, BALL_OUT, BALL_DOWN};
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        const char *name = c3_wzx_index_special(&w->index, ids[i]);
        if (name) (void)wzx_sequence(s, root, name);
    }
    w->persistent_sequences = w->sequence_count; w->persistent_banks = w->bank_count;
    w->base_count = s->count;
}

/* Load what this batch's moves need. The renderer must be stopped. */
static void wzx_prepare(C3BattleGeneralStage *s, const C3BattlePreviewEvent *e, uint32_t count, const char *root)
{
    struct C3BattleWzxLayer *w = s->wzx;
    if (!w) return;
    wzx_release_batch(s);
    for (uint32_t i = 0; i < count; ++i) {
        if (e[i].kind == C3_PREVIEW_EVENT_STATUS && e[i].effect && e[i].target < 4) {
            const char *name = c3_wzx_index_special(&w->index, e[i].effect);
            if (name) (void)wzx_sequence(s, root, name);
            continue;
        }
        if (e[i].kind != C3_PREVIEW_EVENT_MOVE || e[i].actor >= 4) continue;
        unsigned pkx = slot_pkx(s, e[i].actor);
        const char *names[3] = {
            c3_wzx_index_move(&w->index, e[i].move, 1, (uint16_t)pkx, 0),
            c3_wzx_index_move(&w->index, e[i].move, 3, (uint16_t)pkx, 0), NULL };
        for (uint32_t j = i + 1; j < count && e[j].group == e[i].group; ++j)
            if (e[j].kind == C3_PREVIEW_EVENT_HP && e[j].target < 4) {
                names[2] = c3_wzx_index_move(&w->index, e[i].move, 2, (uint16_t)slot_pkx(s, e[j].target), 0); break;
            }
        for (unsigned k = 0; k < 3; ++k) if (names[k]) (void)wzx_sequence(s, root, names[k]);
    }
    if (s->trace) { fprintf(s->trace, "wzx_prepare sequences=%u banks=%u models=%lu\n", w->sequence_count, w->bank_count,
        (unsigned long)(s->count - w->base_count)); fflush(s->trace); }
}

static int wzx_register_banks(C3BattleGeneralStage *s, char *error, size_t cap)
{
    if (!s->wzx) return 1;
    for (unsigned b = 0; b < s->wzx->bank_count; ++b)
        if (c3_stage_renderer_particle_pack(&s->wzx->banks[b].pack, error, cap) != C3_RENDER_OK) {
            if (s->trace) { fprintf(s->trace, "wzx_bank_not_registered name=%s error=%s\n", s->wzx->banks[b].name, error); fflush(s->trace); }
            error[0] = 0; s->wzx->bank_count = b; break; /* the renderer's bank limit: later banks stay unbound */
        }
    return 1;
}

/* The owner's body-map part in native world units, from its retained part bone. */
static void wzx_part_position(const C3BattleGeneralStage *s, unsigned slot, unsigned attach, float out[3])
{
    int m = model_for_slot(s, slot);
    const float *t = m >= 0 ? s->transforms[m] : NULL;
    out[0] = out[1] = out[2] = 0;
    if (!t) return;
    unsigned bone = attach < 17 ? s->part_bone[slot < 6 ? slot : 0][attach] : 0xFF;
    const C3Title *model = &s->models[m];
    float local[3] = {0, 0, 0};
    if (bone != 0xFF && bone < model->bone_count) {
        const float *b = model->pose.matrices[bone]; local[0] = b[3]; local[1] = b[7]; local[2] = b[11];
    }
    float co = cosf(t[3]), si = sinf(t[3]);
    out[0] = t[0] + t[4] * (co * local[0] + si * local[2]);
    out[1] = t[1] + t[4] * local[1];
    out[2] = t[2] + t[4] * (-si * local[0] + co * local[2]);
}

static WzxRun *run_of(void *user) { return user; }
static C3BattleGeneralStage *wzx_stage; /* the single live stage (backend callbacks) */

static void colour_at(const C3WzxNode *n, int32_t local, uint8_t rgb[3], int *hold)
{
    /* Type-0 keys: from RGBA, to RGBA, duration (-1 holds), interpolation. */
    uint32_t mode = c3_wzx_payload_u32(n, 0x14), count = c3_wzx_payload_u32(n, 0x10);
    uint32_t at = mode == 1 || mode == 2 ? 0x18 : 0x1C;
    int32_t t = local; *hold = 0;
    for (uint32_t k = 0; k < count && k < 32; ++k) {
        uint32_t from = c3_wzx_payload_u32(n, at + 16 * k), to = c3_wzx_payload_u32(n, at + 16 * k + 4);
        int32_t span = (int32_t)c3_wzx_payload_u32(n, at + 16 * k + 8);
        if (span < 0 || t < span || k + 1 == count) {
            float f = span > 0 ? (float)(t < span ? t : span) / (float)span : 1.f;
            if (span < 0) { f = 0; *hold = 1; }
            for (unsigned c = 0; c < 3; ++c) {
                float a = (float)(from >> (24 - 8 * c) & 255), b = (float)(to >> (24 - 8 * c) & 255);
                rgb[c] = (uint8_t)(a + (b - a) * f + .5f);
            }
            if (span >= 0 && t >= span && k + 1 == count) *hold = 0;
            return;
        }
        t -= span;
    }
    rgb[0] = rgb[1] = rgb[2] = 128;
}

static int be_start(void *user, uint32_t i, const C3WzxNode *n)
{
    WzxRun *r = run_of(user); C3BattleGeneralStage *s = wzx_stage; struct C3BattleWzxLayer *w = s->wzx;
    char error[160];
    r->node_system[i] = -1;
    switch (n->kind) {
    case C3_WZX_MODEL: {
        int32_t m = r->sequence->node_model[i];
        if (m < 0) return 0;
        wzx_part_position(s, r->owner, n->attach, s->transforms[m]);
        int om = model_for_slot(s, r->owner);
        s->transforms[m][3] = om >= 0 ? s->transforms[om][3] : 0;
        s->transforms[m][4] = om >= 0 ? s->transforms[om][4] : 1;
        s->clocks[m] = 0; s->clips[m] = 0; r->node_live[i] = 1;
        return 1; }
    case C3_WZX_PARTICLE: {
        int32_t b = r->sequence->node_bank[i];
        if (b < 0 || b >= (int32_t)w->bank_count) return 0;
        int sys = -1;
        for (unsigned k = 0; k < WZX_SYSTEMS; ++k) if (!w->system_used[k]) { sys = (int)k; break; }
        if (sys < 0) return 0;
        float p[3]; wzx_part_position(s, r->owner, n->attach, p);
        float src[3] = {10 * p[0], 10 * p[1], 10 * p[2]};
        int om = model_for_slot(s, r->owner);
        uint32_t root = c3_wzx_payload_u32(n, 0);
        if (!c3_particle_begin_position_flags(&w->systems[sys], &w->banks[b].pack.bank, root, 0x57A70000u ^ (i * 977u) ^ s->cue_serial,
                                              src, om >= 0 ? s->transforms[om][3] : 0, 1.f, 1.f, 0, error, sizeof error)) {
            if (s->trace) { fprintf(s->trace, "wzx_particle_refused node=%lu root=%lu error=%s\n", (unsigned long)i, (unsigned long)root, error); fflush(s->trace); }
            return 0;
        }
        w->system_used[sys] = 1; r->node_system[i] = sys; r->node_live[i] = 1;
        return 1; }
    case C3_WZX_EFFECT: {
        uint32_t type = c3_wzx_payload_u32(n, 0);
        if (type != 0) return 1; /* scheduled, not drawn yet */
        r->node_live[i] = 1; return 1; }
    case C3_WZX_SOUND:
        return sound(s, c3_wzx_payload_u32(n, 0), error, sizeof error) || 1;
    case C3_WZX_ACTION: {
        uint32_t action = c3_wzx_payload_u32(n, 0);
        if (r->owner < 4 && (action == 2 || action == 3)) s->presentation.visible[r->owner] = action == 3;
        return 1; }
    default: return 0;
    }
}

static int be_update(void *user, uint32_t i, const C3WzxNode *n, uint32_t ticks, int32_t local)
{
    WzxRun *r = run_of(user); C3BattleGeneralStage *s = wzx_stage; struct C3BattleWzxLayer *w = s->wzx;
    char error[160];
    switch (n->kind) {
    case C3_WZX_MODEL: {
        int32_t m = r->sequence->node_model[i];
        if (m < 0) return 0;
        if (n->postype) wzx_part_position(s, r->owner, n->attach, s->transforms[m]);
        C3Title *t = &s->models[m];
        float length = t->clip_count ? (float)t->clips[0].frame_count / t->fps : 0;
        s->clocks[m] = (float)local / 60.f;
        uint32_t mflags = c3_wzx_payload_u32(n, 0x18);
        if (s->clocks[m] >= length) { if (mflags & 2) { s->clocks[m] = length > 0 ? length - 1.f / t->fps : 0; return n->tidx ? -1 : 1; } return n->tidx ? -1 : 0; }
        return 1; }
    case C3_WZX_PARTICLE: {
        int sys = r->node_system[i];
        if (sys < 0) return 0;
        if (n->postype) { float p[3]; wzx_part_position(s, r->owner, n->attach, p); float src[3] = {10 * p[0], 10 * p[1], 10 * p[2]};
            c3_particle_position(&w->systems[sys], src, error, sizeof error); }
        if (!c3_particle_step(&w->systems[sys], ticks * 1000u / 60u, 0, error, sizeof error)) return 0;
        return !c3_particle_finished(&w->systems[sys]); }
    case C3_WZX_EFFECT: {
        if (c3_wzx_payload_u32(n, 0) != 0) return -1;
        uint32_t target = c3_wzx_payload_u32(n, 0xC);
        uint8_t rgb[3]; int hold;
        colour_at(n, local, rgb, &hold);
        if (target == 1) { w->floor_tint = 1; memcpy(w->floor_rgb, rgb, 3); }
        else if (target == 2 && r->owner < 4) { w->owner_tint[r->owner] = 1; memcpy(w->owner_rgb[r->owner], rgb, 3); }
        int32_t frames = (int32_t)c3_wzx_payload_u32(n, 4);
        if (hold) return 1;
        return frames > 0 ? local < frames : -1; }
    default: return -1;
    }
}

static void be_stop(void *user, uint32_t i, const C3WzxNode *n, int forced)
{
    WzxRun *r = run_of(user); C3BattleGeneralStage *s = wzx_stage; struct C3BattleWzxLayer *w = s->wzx;
    (void)forced;
    r->node_live[i] = 0;
    if (n->kind == C3_WZX_PARTICLE && r->node_system[i] >= 0) {
        c3_particle_cancel(&w->systems[r->node_system[i]]); w->system_used[r->node_system[i]] = 0; r->node_system[i] = -1;
    }
    if (n->kind == C3_WZX_EFFECT && c3_wzx_payload_u32(n, 0) == 0) {
        uint32_t target = c3_wzx_payload_u32(n, 0xC);
        if (target == 1) w->floor_tint = 0; else if (target == 2 && r->owner < 4) w->owner_tint[r->owner] = 0;
    }
}

static const C3WzxBackend wzx_backend = {NULL, be_start, be_update, be_stop};

static int camera_part(void *user, unsigned actor, unsigned body_slot, float out[3])
{
    C3BattleGeneralStage *s = user;
    if (actor >= 6) return 0;
    unsigned slot = grid_slot[actor];
    if (model_for_slot(s, slot) < 0) return 0;
    if (slot >= 4 || body_slot >= 17 || s->part_bone[slot][body_slot] == 0xFF) {
        int m = model_for_slot(s, slot);
        out[0] = s->transforms[m][0]; out[1] = s->transforms[m][1] + .8f * s->transforms[m][4]; out[2] = s->transforms[m][2];
        return 1;
    }
    wzx_part_position(s, slot, body_slot, out);
    return 1;
}

/* Fill the camera's actor view of the stage (root, yaw, bounds, timing). */
static void camera_scene(C3BattleGeneralStage *s)
{
    struct C3BattleWzxLayer *w = s->wzx;
    C3WzxCameraScene *c = &w->scene;
    memset(c, 0, sizeof *c);
    c->count = 6; c->part_world = camera_part; c->user = s;
    c->grid_scale[0] = c->grid_scale[1] = c->grid_scale[2] = 1;
    c->base = w->home;
    for (unsigned a = 0; a < 6; ++a) {
        unsigned slot = grid_slot[a];
        int m = model_for_slot(s, slot);
        C3WzxCameraActor *x = &c->actors[a];
        x->is_trainer = slot >= 4;
        x->pokemon_count = (uint16_t)(s->cast.active_per_side ? s->cast.active_per_side : 1);
        x->source_facing = slot == 4 || slot < 2 ? 1 : -1;
        x->scale_selector = 1;
        if (m < 0) continue;
        const float *t = s->transforms[m];
        x->present = slot >= 4 || s->presentation.visible[slot];
        memcpy(x->position, t, 12); x->yaw = t[3];
        /* No sampled model bound: span of the retained parts, else a body-sized box. */
        float lo[3] = {-.4f, 0, -.4f}, hi[3] = {.4f, 1.2f, .4f};
        if (slot < 4) {
            int any = 0;
            for (unsigned k = 0; k < 17; ++k) {
                unsigned bone = s->part_bone[slot][k];
                if (bone == 0xFF || bone >= s->models[m].bone_count) continue;
                const float *b = s->models[m].pose.matrices[bone];
                float v[3] = {b[3], b[7], b[11]};
                for (unsigned j = 0; j < 3; ++j) { if (!any || v[j] < lo[j]) lo[j] = v[j]; if (!any || v[j] > hi[j]) hi[j] = v[j]; }
                any = 1;
            }
            if (any) { lo[1] = 0; for (unsigned j = 0; j < 3; ++j) { float pad = .15f * (hi[j] - lo[j]) + .05f; lo[j] -= j == 1 ? 0 : pad; hi[j] += pad; } }
        }
        for (unsigned j = 0; j < 3; ++j) { x->bound_min[j] = lo[j] * t[4]; x->bound_max[j] = hi[j] * t[4]; }
        if (slot < 4) for (unsigned e = 0; e < C3_WZX_CAMERA_ENTRIES && e < C3_BATTLE3D_ENTRIES; ++e)
            for (unsigned k = 0; k < 4; ++k) x->timing[e][k] = (int32_t)s->timing[slot][e][k];
    }
}

static void camera_sequence(C3BattleGeneralStage *s, const WzxSequence *q, unsigned kind, unsigned id, unsigned owner)
{
    struct C3BattleWzxLayer *w = s->wzx; char error[160];
    if (!w->camera_ok || w->command_phase) return;
    camera_scene(s);
    C3WzxCameraSequence seq = {q->pack.flags, q->pack.part_sel, q->pack.entry, kind, id,
                               q->pack.camera_at ? q->pack.bytes + q->pack.camera_at : NULL, q->pack.camera_count};
    if (c3_wzx_camera_begin_sequence(&w->camera, &w->scene, slot_actor(owner), &seq, &s->camera, error, sizeof error)) w->sequence_camera = 1;
    else if (s->trace) { fprintf(s->trace, "wzx_camera_refused name=%s reason=%s\n", q->name, error); fflush(s->trace); }
}

static void camera_step(C3BattleGeneralStage *s, uint32_t ms)
{
    struct C3BattleWzxLayer *w = s->wzx; char error[160];
    if (!w || !w->camera_ok) return;
    int running = 0;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) running |= w->runs[i].active;
    if (w->sequence_camera && !running) { c3_wzx_camera_end(&w->camera); w->sequence_camera = 0; c3_wzx_camera_idle_reset(&w->camera, w->command_phase); }
    camera_scene(s);
    if (!c3_wzx_camera_step(&w->camera, &w->scene, ms, error, sizeof error)) {
        if (s->trace) { fprintf(s->trace, "wzx_camera_failed reason=%s\n", error); fflush(s->trace); }
        w->camera_ok = 0; s->camera = w->home; return;
    }
    s->camera = !w->command_phase && c3_wzx_camera_active(&w->camera) ? w->camera.camera : w->home;
}

/* Start a prepared sequence on an owner; the owner's clip comes from the
 * sequence's PKX entry, delayed by the player's motion delay. */
static WzxRun *wzx_begin_id(C3BattleGeneralStage *s, const char *name, unsigned kind, unsigned owner, unsigned target, unsigned id)
{
    struct C3BattleWzxLayer *w = s->wzx; char error[160];
    if (!w || !name || owner >= 4) return NULL;
    WzxSequence *q = NULL;
    for (unsigned i = 0; i < w->sequence_count; ++i) if (!strcmp(w->sequences[i].name, name)) q = &w->sequences[i];
    if (!q) return NULL;
    WzxRun *r = NULL;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) if (!w->runs[i].active) { r = &w->runs[i]; break; }
    if (!r) return NULL;
    memset(r, 0, sizeof *r);
    r->sequence = q; r->owner = owner; r->target = target; r->kind = (uint8_t)kind;
    int32_t timing[16] = {0};
    unsigned entry = q->pack.entry < C3_BATTLE3D_ENTRIES ? q->pack.entry : 0;
    for (unsigned k = 0; k < 4; ++k) timing[k] = (int32_t)s->timing[owner][entry][k];
    C3WzxBackend backend = wzx_backend; backend.user = r;
    wzx_stage = s;
    if (!c3_wzx_player_begin(&r->player, &q->pack, kind, timing, &backend, error, sizeof error)) return NULL;
    r->active = 1;
    int m = model_for_slot(s, owner);
    if (m >= 0 && s->entry_clip[owner][entry] != C3_BATTLE3D_NO_CLIP && s->entry_clip[owner][entry] < s->models[m].clip_count) {
        s->clips[m] = s->entry_clip[owner][entry];
        s->clocks[m] = -(float)r->player.motion_delay / 60.f; s->hold[m] = 0;
    }
    camera_sequence(s, q, kind == 3 && id >= 0x1000 ? 4 : kind, id & 0xFFF, owner);
    if (s->trace) { fprintf(s->trace, "wzx_begin name=%s kind=%u owner=%u entry=%u nodes=%lu delay=%ld end=%ld damage=%ld\n", name, kind, owner, entry,
        (unsigned long)q->pack.node_count, (long)r->player.motion_delay, (long)c3_wzx_player_mark(&r->player, C3_WZX_TIME_END),
        (long)c3_wzx_player_mark(&r->player, C3_WZX_TIME_DAMAGE)); fflush(s->trace); }
    return r;
}

#define wzx_begin(s, name, kind, owner, target) wzx_begin_id(s, name, kind, owner, target, s->wzx_id)

/* A battle special on a Pokemon; `then` (0: none) follows when it ends,
 * with `voice` (a cry) at its start. */
static WzxRun *wzx_special(C3BattleGeneralStage *s, uint16_t special, unsigned owner, uint16_t then, uint16_t voice)
{
    struct C3BattleWzxLayer *w = s->wzx;
    if (!w || owner >= 4) return NULL;
    const char *name = c3_wzx_index_special(&w->index, special);
    s->wzx_id = 0x1000u | special;
    WzxRun *r = name ? wzx_begin(s, name, 4, owner, owner) : NULL;
    const char *next = then ? c3_wzx_index_special(&w->index, then) : NULL;
    if (r && next) { snprintf(w->follow, sizeof w->follow, "%s", next); w->follow_owner = owner; w->follow_voice = voice; w->follow_pending = 1; }
    return r;
}

static void wzx_step(C3BattleGeneralStage *s, uint32_t ms)
{
    struct C3BattleWzxLayer *w = s->wzx;
    if (!w) return;
    wzx_stage = s;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) {
        WzxRun *r = &w->runs[i];
        if (!r->active) continue;
        r->units += (uint64_t)ms * 60u;
        uint32_t ticks = (uint32_t)(r->units / 1000u); r->units -= (uint64_t)ticks * 1000u;
        if (!c3_wzx_player_step(&r->player, ticks)) {
            r->active = 0;
            w->started += r->player.started; w->skipped += r->player.skipped;
            if (s->trace) { fprintf(s->trace, "wzx_end name=%s owner=%u ticks=%ld started=%lu skipped=%lu\n", r->sequence->name, r->owner,
                (long)r->player.time, (unsigned long)r->player.started, (unsigned long)r->player.skipped); fflush(s->trace); }
        }
    }
    if (w->follow_pending) {
        int busy = 0;
        for (unsigned i = 0; i < WZX_PLAYERS; ++i) busy |= w->runs[i].active && w->runs[i].owner == w->follow_owner;
        if (!busy) {
            char error[160];
            w->follow_pending = 0;
            if (w->follow_voice) (void)sound(s, w->follow_voice, error, sizeof error);
            s->wzx_id = 0x1000u | BALL_OUT;
            if (wzx_begin(s, w->follow, 4, w->follow_owner, w->follow_owner)) s->wzx_attack_wait = 1;
        }
    }
    camera_step(s, ms);
}

static int wzx_running(const C3BattleGeneralStage *s, unsigned kind)
{
    if (kind == 3 && s->wzx && s->wzx->follow_pending) return 1;
    if (!s->wzx) return 0;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) if (s->wzx->runs[i].active && (!kind || s->wzx->runs[i].kind == kind)) return 1;
    return 0;
}

static int wzx_model_visible(const C3BattleGeneralStage *s, uint32_t model)
{
    if (!s->wzx) return 0;
    for (unsigned i = 0; i < WZX_PLAYERS; ++i) {
        const WzxRun *r = &s->wzx->runs[i];
        if (!r->active) continue;
        for (uint32_t k = 0; k < r->sequence->pack.node_count; ++k)
            if (r->node_live[k] && r->sequence->node_model[k] == (int32_t)model) return 1;
    }
    return 0;
}

/* The renderer binds one particle system: the earliest live launch. */
static const C3ParticleSystem *wzx_live_system(const C3BattleGeneralStage *s)
{
    if (!s->wzx) return NULL;
    for (unsigned k = 0; k < WZX_SYSTEMS; ++k) if (s->wzx->system_used[k]) return &s->wzx->systems[k];
    return NULL;
}
