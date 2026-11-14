#include "colosseum3ds/battle3d_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host checks for the 3D battle asset index (src/battle3d_index.c) on a
 * synthetic index: lookups, role derivation and bounds rejection. With a
 * path argument (PRIVATE) it also decodes a prepared index and lists it. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[160];
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void w16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int main(int argc, char **argv)
{
    enum { SPECIES = 4, TRAINERS = 3, FLOORS = 2, MODELS = 2 };
    size_t size = 64 + 2 * SPECIES + 2 * TRAINERS + 32 * FLOORS + 128 * MODELS;
    uint8_t *b = calloc(1, size);
    uint32_t sp = 64, tr = sp + 2 * SPECIES, fl = tr + 2 * TRAINERS, md = fl + 32 * FLOORS;
    memcpy(b, "C3BN", 4); w32(b + 4, 1); w32(b + 8, SPECIES); w32(b + 12, TRAINERS); w32(b + 16, FLOORS); w32(b + 20, MODELS);
    w32(b + 24, sp); w32(b + 28, tr); w32(b + 32, fl); w32(b + 36, md);
    for (unsigned i = 0; i < SPECIES; ++i) w16(b + sp + 2 * i, 0xFFFF);
    for (unsigned i = 0; i < TRAINERS; ++i) w16(b + tr + 2 * i, 0xFFFF);
    w16(b + sp + 2 * 3, 0); w16(b + tr + 2 * 1, 1);
    memcpy(b + fl + 32, "floor01.c3v", 11);
    memcpy(b + md, "pkx003.c3t", 10);
    for (unsigned e = 0; e < 17; ++e) w32(b + md + 48 + 4 * e, 0xFFFFFFFFu);
    w32(b + md + 48, 0); w32(b + md + 48 + 4 * 2, 1); w32(b + md + 48 + 4 * 10, 2);
    w32(b + md + 116, 0); w32(b + md + 120, 0x3F800000u);
    memcpy(b + md + 128, "trn001.c3t", 10);
    for (unsigned e = 0; e < 17; ++e) w32(b + md + 128 + 48 + 4 * e, 0xFFFFFFFFu);
    w32(b + md + 128 + 48, 1); w32(b + md + 128 + 48 + 4 * 5, 2); w32(b + md + 128 + 116, 1);

    C3Battle3dIndex x; C3Battle3dModel m; uint32_t roles[9]; char name[32];
    CHECK(c3_battle3d_index_decode(&x, b, size, error, sizeof error));
    CHECK(c3_battle3d_species(&x, 3, &m) && !strcmp(m.file, "pkx003.c3t") && m.kind == 0 && m.scale == 1.f);
    CHECK(!c3_battle3d_species(&x, 2, NULL) && !c3_battle3d_species(&x, 0, NULL) && !c3_battle3d_species(&x, 9, NULL));
    c3_battle3d_roles(&m, roles);
    CHECK(roles[0] == 0 && roles[1] == 1 && roles[2] == 0 && roles[3] == 0 && roles[4] == 2 && roles[5] == 0 && roles[7] == C3_BATTLE3D_NO_CLIP);
    CHECK(c3_battle3d_trainer(&x, 1, &m) && m.kind == 1);
    c3_battle3d_roles(&m, roles);
    CHECK(roles[0] == 1 && roles[7] == 2 && roles[8] == 1 && roles[1] == C3_BATTLE3D_NO_CLIP);
    CHECK(c3_battle3d_floor(&x, 1, name) && !strcmp(name, "floor01.c3v") && !c3_battle3d_floor(&x, 0, NULL));
    /* Bounds: an out-of-range model reference and an unsafe name are refused. */
    w16(b + sp, 7); CHECK(!c3_battle3d_index_decode(&x, b, size, error, sizeof error)); w16(b + sp, 0xFFFF);
    memcpy(b + md, "../x.c3t", 8); CHECK(!c3_battle3d_index_decode(&x, b, size, error, sizeof error));
    CHECK(!c3_battle3d_index_decode(&x, b, 40, error, sizeof error));
    free(b);
    puts("battle3d-index: synthetic index ok");
    if (argc > 1) {
        CHECK(c3_battle3d_index_load(&x, argv[1], error, sizeof error));
        unsigned species = 0, trainers = 0, floors = 0;
        for (unsigned i = 0; i < x.species_count; ++i) species += c3_battle3d_species(&x, (uint16_t)i, NULL);
        for (unsigned i = 0; i < x.trainer_count; ++i) trainers += c3_battle3d_trainer(&x, (uint16_t)i, NULL);
        for (unsigned i = 0; i < x.floor_count; ++i) if (c3_battle3d_floor(&x, (uint16_t)i, name)) { ++floors; printf("floor %u %s\n", i, name); }
        printf("battle3d-index: %s models=%u species=%u trainers=%u floors=%u\n", argv[1], x.model_count, species, trainers, floors);
        c3_battle3d_index_free(&x);
    }
    return 0;
}
