#ifndef C3_WZX_INDEX_H
#define C3_WZX_INDEX_H

#include <stddef.h>
#include <stdint.h>

/* Index of the prepared effect sequences (tools/prepare_battle_wzx.py,
 * wzx.c3n "C3WI" v1): move default roots (lbl_8036E150), PKX-keyed overrides
 * (lbl_80373750), variants (lbl_80373210), battle specials (lbl_803727C8) and
 * waza_sequence_global.fsys members. */
enum { C3_WZX_CAT_MOVE = 0, C3_WZX_CAT_OVERRIDE, C3_WZX_CAT_VARIANT, C3_WZX_CAT_SPECIAL, C3_WZX_CAT_GLOBAL };

typedef struct {
    uint8_t *bytes;
    size_t size;
    uint32_t rows, rows_at, names_at;
} C3WzxIndex;

int c3_wzx_index_decode(C3WzxIndex *index, const void *bytes, size_t size, char *error, size_t capacity);
int c3_wzx_index_load(C3WzxIndex *index, const char *path, char *error, size_t capacity);
void c3_wzx_index_free(C3WzxIndex *index);
/* The pack for a move's kind (1 attack, 2 damage, 3 special) as `pkx` uses
 * it: that PKX's override row first, else the default root. NULL: none. */
const char *c3_wzx_index_move(const C3WzxIndex *index, uint16_t move, unsigned kind, uint16_t pkx, uint16_t variant);
/* A battle special (send-out ball, status, stat change, ...), id 0..218. */
const char *c3_wzx_index_special(const C3WzxIndex *index, uint16_t special);
#endif
