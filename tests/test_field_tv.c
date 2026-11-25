/* Field television binding (<prefix>.c3v, colosseum3ds/field_tv.h). The
 * bytes are tools/field_tv.py encode() for floor 0x1C (Pyrite Building 4F):
 * map pack 0 texture 2 is the 8x8 red placeholder of map model 2; entries
 * 0/1 are model 10331000 clips 0/1 (pack 0) and entry 2, the Nascour feed
 * of the rooftop scene, is model 10A11000 (pack 1). */
#include "colosseum3ds/field_tv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16); p[3] = (unsigned char)(value >> 24);
}

static size_t binding(unsigned char *d, uint32_t screen_pack, uint32_t packs, const uint32_t (*rows)[3], uint32_t count)
{
    size_t size = 28u + 12u * count;
    memcpy(d, "C3TV", 4);
    put32(d + 4, 1); put32(d + 8, (uint32_t)size); put32(d + 12, screen_pack); put32(d + 16, 2);
    put32(d + 20, packs); put32(d + 24, count);
    for (uint32_t i = 0; i < count; ++i)
        for (unsigned j = 0; j < 3; ++j) put32(d + 28 + i * 12 + j * 4, rows[i][j]);
    return size;
}

int main(void)
{
    static const uint32_t f01c[3][3] = {{0, 0, 0}, {1, 0, 1}, {2, 1, 0}};
    unsigned char d[256];
    char error[128];
    C3FieldTV tv;
    size_t size = binding(d, 0, 2, f01c, 3);
    assert(size == 64);
    assert(c3_field_tv_parse(&tv, d, size, 2, error, sizeof error));
    assert(tv.screen_pack == 0 && tv.screen_texture == 2 && tv.pack_count == 2 && tv.entry_count == 3);
    /* Floor start: entry 0, the animated clip of the presenter model. */
    const C3FieldTVEntry *entry = c3_field_tv_entry(&tv, 0);
    assert(entry && entry->pack == 0 && entry->clip == 0);
    /* Native AB(1), every init/pre script of the floor: the pose clip. */
    entry = c3_field_tv_entry(&tv, 1);
    assert(entry && entry->pack == 0 && entry->clip == 1);
    /* Native AB(2), bank 02040E00 function 11: Nascour on the monitor. */
    entry = c3_field_tv_entry(&tv, 2);
    assert(entry && entry->pack == 1 && entry->clip == 0);
    /* An id the table lacks selects nothing (the placeholder shows). */
    assert(!c3_field_tv_entry(&tv, 3) && !c3_field_tv_entry(&tv, C3_FIELD_TV_NONE));

    C3FieldTV kept = tv;
    /* The screen must be one of the floor's map packs. */
    assert(!c3_field_tv_parse(&tv, d, size, 0, error, sizeof error) && error[0]);
    assert(!memcmp(&tv, &kept, sizeof tv));
    /* Size, magic and version are exact. */
    assert(!c3_field_tv_parse(&tv, d, size - 1, 2, error, sizeof error));
    d[4] = 2; assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error)); d[4] = 1;
    d[0] = 'X'; assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error)); d[0] = 'C';
    /* An entry may only name a prepared pack, and ids are unique. */
    static const uint32_t missing[2][3] = {{0, 0, 0}, {1, 2, 0}};
    size = binding(d, 0, 2, missing, 2);
    assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error));
    static const uint32_t repeated[2][3] = {{0, 0, 0}, {0, 1, 0}};
    size = binding(d, 0, 2, repeated, 2);
    assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error));
    /* Pack and entry bounds. */
    size = binding(d, 0, C3_FIELD_TV_MAX_PACKS + 1u, f01c, 3);
    assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error));
    size = binding(d, 0, 2, f01c, 0);
    assert(!c3_field_tv_parse(&tv, d, size, 2, error, sizeof error));
    assert(!memcmp(&tv, &kept, sizeof tv));
    printf("field tv: ok\n");
    return 0;
}
