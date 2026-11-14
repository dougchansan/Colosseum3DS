#include "colosseum3ds/battle3d_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 64u
#define MODEL 128u
#define FLOOR 32u

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int fail(char *e, size_t n, const char *s) { if (e && n) snprintf(e, n, "%s", s); return 0; }
static int span(size_t size, uint32_t at, uint64_t bytes) { return at >= HEADER && (uint64_t)at + bytes <= size; }
static int safe_name(const uint8_t *p, size_t n)
{
    const uint8_t *end = memchr(p, 0, n);
    if (!end) return 0;
    for (const uint8_t *c = p; c < end; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.')) return 0;
    return !strstr((const char *)p, "..");
}

int c3_battle3d_index_decode(C3Battle3dIndex *x, const void *data, size_t size, char *error, size_t cap)
{
    const uint8_t *b = data;
    if (!x || !b || size < HEADER || memcmp(b, "C3BN", 4) || le32(b + 4) != 1) return fail(error, cap, "Invalid 3D battle index header.");
    C3Battle3dIndex v; memset(&v, 0, sizeof v);
    v.species_count = le32(b + 8); v.trainer_count = le32(b + 12); v.floor_count = le32(b + 16); v.model_count = le32(b + 20);
    v.species_at = le32(b + 24); v.trainer_at = le32(b + 28); v.floor_at = le32(b + 32); v.model_at = le32(b + 36);
    if (v.species_count > 4096 || v.trainer_count > 4096 || v.floor_count > 1024 || v.model_count >= 0xFFFF ||
        !span(size, v.species_at, 2ull * v.species_count) || !span(size, v.trainer_at, 2ull * v.trainer_count) ||
        !span(size, v.floor_at, (uint64_t)FLOOR * v.floor_count) || !span(size, v.model_at, (uint64_t)MODEL * v.model_count))
        return fail(error, cap, "3D battle index tables out of bounds.");
    for (uint32_t i = 0; i < v.species_count; ++i) { unsigned m = le16(b + v.species_at + 2 * i); if (m != 0xFFFF && m >= v.model_count) return fail(error, cap, "3D battle index species model."); }
    for (uint32_t i = 0; i < v.trainer_count; ++i) { unsigned m = le16(b + v.trainer_at + 2 * i); if (m != 0xFFFF && m >= v.model_count) return fail(error, cap, "3D battle index trainer model."); }
    for (uint32_t i = 0; i < v.floor_count; ++i) if (!safe_name(b + v.floor_at + FLOOR * i, FLOOR)) return fail(error, cap, "3D battle index floor name.");
    for (uint32_t i = 0; i < v.model_count; ++i) {
        const uint8_t *r = b + v.model_at + MODEL * i;
        if (!r[0] || !safe_name(r, C3_BATTLE3D_NAME) || le32(r + 48 + 4 * C3_BATTLE3D_ENTRIES) > 1) return fail(error, cap, "3D battle index model record.");
    }
    v.bytes = (uint8_t *)data; v.size = size;
    *x = v;
    return 1;
}

int c3_battle3d_index_load(C3Battle3dIndex *x, const char *path, char *error, size_t cap)
{
    if (!x || !path) return fail(error, cap, "Invalid 3D battle index arguments.");
    memset(x, 0, sizeof *x);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(error, cap, "No 3D battle index.");
    uint8_t *data = NULL; long n = -1;
    if (!fseek(f, 0, SEEK_END)) n = ftell(f);
    if (n > 0 && n <= 4 * 1024 * 1024 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) &&
        fread(data, 1, (size_t)n, f) == (size_t)n) {
        fclose(f);
        if (c3_battle3d_index_decode(x, data, (size_t)n, error, cap)) return 1;
        free(data); return 0;
    }
    fclose(f); free(data);
    return fail(error, cap, "Unreadable 3D battle index.");
}

void c3_battle3d_index_free(C3Battle3dIndex *x)
{
    if (!x) return;
    free(x->bytes); memset(x, 0, sizeof *x);
}

static int model(const C3Battle3dIndex *x, unsigned m, C3Battle3dModel *out)
{
    if (m == 0xFFFF || m >= x->model_count) return 0;
    const uint8_t *r = x->bytes + x->model_at + MODEL * m;
    if (out) {
        memset(out, 0, sizeof *out);
        memcpy(out->file, r, C3_BATTLE3D_NAME); out->file[C3_BATTLE3D_NAME - 1] = 0;
        for (unsigned i = 0; i < C3_BATTLE3D_ENTRIES; ++i) out->entry_clip[i] = le32(r + 48 + 4 * i);
        out->kind = le32(r + 48 + 4 * C3_BATTLE3D_ENTRIES);
        uint32_t s = le32(r + 52 + 4 * C3_BATTLE3D_ENTRIES); memcpy(&out->scale, &s, 4);
        if (!(out->scale > 0.01f && out->scale < 100.f)) out->scale = 1.f;
    }
    return 1;
}

int c3_battle3d_species(const C3Battle3dIndex *x, uint16_t species, C3Battle3dModel *out)
{
    return x && x->bytes && species && species < x->species_count && model(x, le16(x->bytes + x->species_at + 2u * species), out);
}

int c3_battle3d_trainer(const C3Battle3dIndex *x, uint16_t trainer, C3Battle3dModel *out)
{
    return x && x->bytes && trainer < x->trainer_count && model(x, le16(x->bytes + x->trainer_at + 2u * trainer), out);
}

int c3_battle3d_floor(const C3Battle3dIndex *x, uint16_t floor, char name[32])
{
    if (!x || !x->bytes || floor >= x->floor_count) return 0;
    const uint8_t *r = x->bytes + x->floor_at + FLOOR * floor;
    if (!r[0]) return 0;
    if (name) { memcpy(name, r, FLOOR); name[FLOOR - 1] = 0; }
    return 1;
}

void c3_battle3d_roles(const C3Battle3dModel *m, uint32_t roles[9])
{
    for (unsigned i = 0; i < 9; ++i) roles[i] = C3_BATTLE3D_NO_CLIP;
    if (!m) return;
    uint32_t idle = m->entry_clip[0];
    roles[0] = idle;
#define PICK(role, entry) roles[role] = m->entry_clip[entry] != C3_BATTLE3D_NO_CLIP ? m->entry_clip[entry] : idle
    if (m->kind == 0) {
        PICK(1, 2); PICK(2, 1); PICK(3, 8); roles[4] = m->entry_clip[10]; roles[5] = idle;
    } else {
        PICK(7, 5); PICK(8, 10);
    }
#undef PICK
}
