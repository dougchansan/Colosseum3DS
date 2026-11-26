#ifndef COLOSSEUM3DS_FIELD_TV_H
#define COLOSSEUM3DS_FIELD_TV_H
#include <stddef.h>
#include <stdint.h>

/* A floor's field-camera texture entries (the original field televisions,
 * field_camera fn_80117E58 / fn_80117AE4 / fn_80117D14). The floor start
 * selects entry id 0; natives AB, B6 and D3 select another id. The selected
 * entry's model is drawn with its camera into a 256x256 texture that
 * replaces the screen model's placeholder texture (8x8 red on 0x1C).
 *
 * <prefix>.c3v (tools/field_tv.py, little-endian): "C3TV", version 1,
 * file size, screen map pack, screen texture index, pack count, entry
 * count, then per entry u32 id, pack (<prefix>_tv<pack>.c3t) and clip. */
#define C3_FIELD_TV_MAX_PACKS 5u /* the house floors' tables name 5 models */
#define C3_FIELD_TV_MAX_ENTRIES 16u
#define C3_FIELD_TV_NONE UINT32_MAX

typedef struct { uint32_t id, pack, clip; } C3FieldTVEntry;
typedef struct {
    uint32_t screen_pack, screen_texture, pack_count, entry_count;
    C3FieldTVEntry entries[C3_FIELD_TV_MAX_ENTRIES];
} C3FieldTV;

/* 1 and *tv filled, or 0 with error (tv unchanged). map_packs bounds the
 * screen pack; ids are unique. */
int c3_field_tv_parse(C3FieldTV *tv, const void *data, size_t size, uint32_t map_packs,
                      char *error, size_t capacity);
int c3_field_tv_load(C3FieldTV *tv, const char *path, uint32_t map_packs, char *error, size_t capacity);
/* The entry of id, or NULL: fn_80117AE4 then selects nothing and the
 * screen keeps its own texture. C3_FIELD_TV_NONE selects nothing. */
const C3FieldTVEntry *c3_field_tv_entry(const C3FieldTV *tv, uint32_t id);
#endif
