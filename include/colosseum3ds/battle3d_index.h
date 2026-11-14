#ifndef C3_BATTLE3D_INDEX_H
#define C3_BATTLE3D_INDEX_H

#include <stddef.h>
#include <stdint.h>

/* Index of the private 3D battle assets (tools/prepare_battle_stages.py):
 * which stage each battle floor uses and which model pack each internal
 * species and trainer uses, with the pack clip of every original PKX
 * animation entry. Little-endian "C3BN" version 1. */
#define C3_BATTLE3D_ENTRIES 17u
#define C3_BATTLE3D_NO_CLIP 0xFFFFFFFFu
#define C3_BATTLE3D_NAME 48u

typedef struct {
    char file[C3_BATTLE3D_NAME];
    uint32_t entry_clip[C3_BATTLE3D_ENTRIES];
    uint32_t kind; /* 0 Pokemon, 1 trainer */
    float scale;
} C3Battle3dModel;

typedef struct {
    uint8_t *bytes;
    size_t size;
    uint32_t species_count, trainer_count, floor_count, model_count;
    uint32_t species_at, trainer_at, floor_at, model_at;
} C3Battle3dIndex;

int c3_battle3d_index_decode(C3Battle3dIndex *index, const void *bytes, size_t size, char *error, size_t capacity);
int c3_battle3d_index_load(C3Battle3dIndex *index, const char *path, char *error, size_t capacity);
void c3_battle3d_index_free(C3Battle3dIndex *index);
/* 1 and *out when the species/trainer/floor has prepared assets. */
int c3_battle3d_species(const C3Battle3dIndex *index, uint16_t species, C3Battle3dModel *out);
int c3_battle3d_trainer(const C3Battle3dIndex *index, uint16_t trainer, C3Battle3dModel *out);
int c3_battle3d_floor(const C3Battle3dIndex *index, uint16_t floor, char name[32]);
/* The battle stage's nine presentation roles from the PKX entries:
 * Pokemon idle(0) physical(2) special(1) hit(8) faint(10) send-out(0);
 * trainer idle(0), win(5) and loss(10) at roles 7 and 8. A missing entry
 * falls back to idle except faint, which stays missing (the actor then
 * just disappears). */
void c3_battle3d_roles(const C3Battle3dModel *model, uint32_t roles[9]);
#endif
