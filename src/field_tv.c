#include "colosseum3ds/field_tv.h"

#include <stdio.h>
#include <string.h>

#define C3_FIELD_TV_HEADER 28u
#define C3_FIELD_TV_MAX_BYTES (C3_FIELD_TV_HEADER + C3_FIELD_TV_MAX_ENTRIES * 12u)

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return 0;
}

int c3_field_tv_parse(C3FieldTV *tv, const void *data, size_t size, uint32_t map_packs,
                      char *error, size_t capacity)
{
    const unsigned char *d = data;
    if (!tv || !d || size < C3_FIELD_TV_HEADER || memcmp(d, "C3TV", 4) || le32(d + 4) != 1u ||
        le32(d + 8) != size)
        return fail(error, capacity, "The television binding is not a C3TV version 1 pack.");
    C3FieldTV parsed = {le32(d + 12), le32(d + 16), le32(d + 20), le32(d + 24), {{0}}};
    if (parsed.screen_pack >= map_packs || parsed.screen_texture > 0xFFFFu ||
        !parsed.pack_count || parsed.pack_count > C3_FIELD_TV_MAX_PACKS ||
        !parsed.entry_count || parsed.entry_count > C3_FIELD_TV_MAX_ENTRIES ||
        size != C3_FIELD_TV_HEADER + parsed.entry_count * 12u)
        return fail(error, capacity, "The television binding exceeds its bounds.");
    for (uint32_t i = 0; i < parsed.entry_count; ++i) {
        const unsigned char *row = d + C3_FIELD_TV_HEADER + i * 12u;
        C3FieldTVEntry entry = {le32(row), le32(row + 4), le32(row + 8)};
        if (entry.id == C3_FIELD_TV_NONE || entry.pack >= parsed.pack_count || entry.clip > 0xFFFFu)
            return fail(error, capacity, "A television entry names a missing pack.");
        for (uint32_t j = 0; j < i; ++j)
            if (parsed.entries[j].id == entry.id) return fail(error, capacity, "A television entry id repeats.");
        parsed.entries[i] = entry;
    }
    *tv = parsed;
    if (error && capacity) error[0] = '\0';
    return 1;
}

int c3_field_tv_load(C3FieldTV *tv, const char *path, uint32_t map_packs, char *error, size_t capacity)
{
    unsigned char data[C3_FIELD_TV_MAX_BYTES + 1u];
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) return fail(error, capacity, "The television binding cannot be opened.");
    size_t size = fread(data, 1, sizeof data, file);
    int bad = ferror(file);
    fclose(file);
    if (bad || size > C3_FIELD_TV_MAX_BYTES) return fail(error, capacity, "The television binding cannot be read.");
    return c3_field_tv_parse(tv, data, size, map_packs, error, capacity);
}

const C3FieldTVEntry *c3_field_tv_entry(const C3FieldTV *tv, uint32_t id)
{
    if (!tv || id == C3_FIELD_TV_NONE) return NULL;
    for (uint32_t i = 0; i < tv->entry_count; ++i)
        if (tv->entries[i].id == id) return tv->entries + i;
    return NULL;
}
