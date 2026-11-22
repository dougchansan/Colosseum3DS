#include "colosseum3ds/floor_profile.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static int label_is(uint32_t floor, const char *want)
{
    const char *got = c3_floor_label(floor);
    if (got && want && !strcmp(got, want)) return 1;
    fprintf(stderr, "floor %03lx: \"%s\", want \"%s\"\n", (unsigned long)floor, got ? got : "(null)", want ? want : "(null)");
    return 0;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

/* A synthetic C3PN pack in the layout tools/prepare_place_names.py writes. */
static size_t fixture(unsigned char *out, const uint32_t *floors, const char *const *text, unsigned count)
{
    size_t at = 16u + count * 8u;
    memcpy(out, "C3PN", 4); put32(out + 4, 1); put32(out + 12, count);
    for (unsigned i = 0; i < count; ++i) {
        put32(out + 16 + i * 8, floors[i]); put32(out + 20 + i * 8, (uint32_t)at);
        size_t n = strlen(text[i]) + 1;
        memcpy(out + at, text[i], n); at += n;
    }
    put32(out + 8, (uint32_t)at);
    return at;
}

static void synthetic(void)
{
    c3_floor_names_reset();
    /* No pack: the profile labels and the surveyed-floor fallback. */
    CHECK(!c3_floor_names_load("build/missing-place-names.c3pn"));
    CHECK(!c3_floor_names_load("build/missing-place-names.c3pn"));
    CHECK(label_is(0x01, "OUTSKIRT STAND"));
    CHECK(label_is(0x03, "PHENAC CITY"));
    CHECK(label_is(0x0F, "AREA 00F"));
    CHECK(label_is(C3_FLOOR_WORLD_MAP_ID, "WORLD MAP"));
    CHECK(!c3_floor_label(0x500));

    static const uint32_t floors[] = {0x01, 0x03, 0x0F, 0x66};
    static const char *const text[] = {"FIXTURE STAND", "FIXTURE CITY", "FIXTURE TOWN", "POK\xc3\xa9 TOWER"};
    unsigned char pack[512], bad[512];
    size_t size = fixture(pack, floors, text, 4);
    CHECK(c3_floor_names_parse(pack, size));
    CHECK(label_is(0x01, "FIXTURE STAND"));
    CHECK(label_is(0x03, "FIXTURE CITY"));
    CHECK(label_is(0x0F, "FIXTURE TOWN"));
    CHECK(label_is(0x66, "POK\xc3\xa9 TOWER")); /* UTF-8 kept for the header font */
    CHECK(label_is(0x04, "PHENAC POKEMON CENTER")); /* not in the pack: profile label */
    CHECK(label_is(C3_FLOOR_WORLD_MAP_ID, "WORLD MAP"));
    CHECK(!c3_floor_label(0x500));

    /* Malformed packs are refused and leave the table as it was. */
    memcpy(bad, pack, size); bad[0] = 'X'; CHECK(!c3_floor_names_parse(bad, size));
    memcpy(bad, pack, size); put32(bad + 4, 2); CHECK(!c3_floor_names_parse(bad, size));
    CHECK(!c3_floor_names_parse(pack, size - 1));
    memcpy(bad, pack, size); put32(bad + 24, 0x01); CHECK(!c3_floor_names_parse(bad, size)); /* unsorted */
    memcpy(bad, pack, size); bad[size - 1] = 'X'; CHECK(!c3_floor_names_parse(bad, size)); /* unterminated */
    memcpy(bad, pack, size); bad[16 + 4 * 8] = '\n'; CHECK(!c3_floor_names_parse(bad, size)); /* control byte */
    memcpy(bad, pack, size); put32(bad + 20, 8); CHECK(!c3_floor_names_parse(bad, size)); /* name in the header */
    memcpy(bad, pack, size); put32(bad + 12, 0); CHECK(!c3_floor_names_parse(bad, size));
    static const char *const long_text[] = {"0123456789012345678901234567890123456789012345678"};
    size_t long_size = fixture(bad, floors, long_text, 1);
    CHECK(!c3_floor_names_parse(bad, long_size));
    CHECK(label_is(0x0F, "FIXTURE TOWN"));
    c3_floor_names_reset();
    CHECK(label_is(0x0F, "AREA 00F"));
}

/* The pack prepared from the user's disc (tools/prepare_place_names.py). */
static void original(const char *path)
{
    c3_floor_names_reset();
    CHECK(c3_floor_names_load(path));
    CHECK(label_is(0x01, "OUTSKIRT STAND"));
    CHECK(label_is(0x03, "PHENAC CITY"));
    CHECK(label_is(0x0F, "PYRITE TOWN"));
    CHECK(label_is(0x66, "REALGAM TOWER"));
    CHECK(label_is(0x2F, "THE UNDER"));
    CHECK(label_is(C3_FLOOR_WORLD_MAP_ID, "WORLD MAP")); /* the record names no place */
    c3_floor_names_reset();
}

int main(int argc, char **argv)
{
    synthetic();
    if (argc > 1) original(argv[1]);
    if (failures) return 1;
    printf("floor names: ok%s\n", argc > 1 ? " (original pack)" : "");
    return 0;
}
