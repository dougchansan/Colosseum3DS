#include "colosseum3ds/battle_fight_pokemon.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void pattern(uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; i++)
        p[i] = (uint8_t)(0x59u + i * 37u);
}

#ifdef C3_TEST_FIGHT_POKEMON_INIT_FAILURE
/* Link this test variant without battle_pokemon.c. A failing dependency may
 * mutate its argument before returning; the public adapter must still roll back. */
static unsigned init_calls;
int c3_battle_pokemon_init(uint8_t p[C3_BATTLE_POKEMON_BYTES])
{
    init_calls++;
    memset(p, 0x13, C3_BATTLE_POKEMON_BYTES);
    return 0;
}

int main(void)
{
    uint8_t backing[340], before[340], source[312], source_before[312];
    pattern(backing, sizeof(backing));
    pattern(source, sizeof(source));
    memcpy(before, backing, sizeof(before));
    memcpy(source_before, source, sizeof(source));
    assert(!c3_battle_fight_pokemon_init(backing));
    assert(init_calls == 1 && !memcmp(backing, before, sizeof(backing)));
    assert(!c3_battle_fight_pokemon_create(backing, source, 0x12345678u, -7));
    assert(init_calls == 2 && !memcmp(backing, before, sizeof(backing)));
    assert(!memcmp(source, source_before, sizeof(source)));
    assert(c3_battle_fight_pokemon_init(NULL));
    assert(c3_battle_fight_pokemon_create(NULL, source, 0, 0));
    assert(c3_battle_fight_pokemon_create(backing, NULL, 0, 0));
    assert(!c3_battle_fight_pokemon_create(backing, source, 0, 0));
    assert(!c3_battle_fight_pokemon_create(backing, backing + 4, 1, 0));
    assert(init_calls == 2 && !memcmp(backing, before, sizeof(backing)));
    puts("battle_fight_pokemon: dependency failure, rollback and no-op passed");
    return 0;
}
#else
static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int status_is_written(unsigned offset)
{
    return (offset >= 0x13c && offset < 0x143) ||
           (offset >= 0x144 && offset < 0x14a) ||
           (offset >= 0x14c && offset < 0x152);
}

static void construction_write_map(void)
{
    const uint32_t references[] = {1, 0x12345678u, 0x80000000u, UINT32_MAX};
    const int16_t entries[] = {INT16_MIN, -1, 0, 1, INT16_MAX};
    struct { uint8_t left[11], body[340], right[19]; } dest;
    struct { uint8_t left[7], body[312], right[17]; } source, source_before;
    for (unsigned r = 0; r < sizeof(references) / sizeof(*references); r++) {
        for (unsigned e = 0; e < sizeof(entries) / sizeof(*entries); e++) {
            pattern((uint8_t*)&dest, sizeof(dest));
            pattern((uint8_t*)&source, sizeof(source));
            source_before = source;
            uint8_t before[sizeof(dest)];
            memcpy(before, &dest, sizeof(before));
            assert(c3_battle_fight_pokemon_create(dest.body, source.body,
                                                  references[r], entries[e]));
            assert(be32(dest.body) == references[r]);
            assert(!memcmp(dest.body + 4, source.body, 312));
            assert(be16(dest.body + 0x14c) == (uint16_t)entries[e]);
            for (unsigned i = 0x13c; i < 340; i++) {
                if (i == 0x14c || i == 0x14d)
                    continue;
                if (status_is_written(i))
                    assert(dest.body[i] == 0);
                else
                    assert(dest.body[i] == before[sizeof(dest.left) + i]);
            }
            assert(!memcmp(dest.left, before, sizeof(dest.left)));
            assert(!memcmp(dest.right, before + sizeof(dest.left) + 340, sizeof(dest.right)));
            assert(!memcmp(&source, &source_before, sizeof(source)));
        }
    }
}

static void initialization_write_map(void)
{
    struct { uint8_t left[5], body[340], right[9]; } p, before;
    pattern((uint8_t*)&p, sizeof(p));
    before = p;
    uint8_t expected_pokemon[312];
    memcpy(expected_pokemon, p.body + 4, sizeof(expected_pokemon));
    assert(c3_battle_pokemon_init(expected_pokemon));
    assert(c3_battle_fight_pokemon_init(p.body));
    assert(be32(p.body) == 0);
    assert(!memcmp(p.body + 4, expected_pokemon, 312));
    assert(be16(p.body + 0x14c) == UINT16_MAX);
    for (unsigned i = 0x13c; i < 340; i++) {
        if (i == 0x14c || i == 0x14d)
            continue;
        assert(p.body[i] == (status_is_written(i) ? 0 : before.body[i]));
    }
    assert(!memcmp(p.left, before.left, sizeof(p.left)));
    assert(!memcmp(p.right, before.right, sizeof(p.right)));
    uint8_t first[340];
    memcpy(first, p.body, sizeof(first));
    assert(c3_battle_fight_pokemon_init(p.body));
    assert(!memcmp(first, p.body, sizeof(first)));
}

static void no_op_and_rejection(void)
{
    uint8_t backing[340], before[340], source[312], source_before[312];
    pattern(backing, sizeof(backing));
    pattern(source, sizeof(source));
    memcpy(before, backing, sizeof(before));
    memcpy(source_before, source, sizeof(source));
    assert(c3_battle_fight_pokemon_init(NULL));
    assert(c3_battle_fight_pokemon_create(NULL, NULL, 0, 0));
    assert(c3_battle_fight_pokemon_create(NULL, source, UINT32_MAX, INT16_MIN));
    assert(c3_battle_fight_pokemon_create(backing, NULL, 0, INT16_MAX));
    assert(c3_battle_fight_pokemon_create(backing, NULL, 1, 0));
    assert(!memcmp(backing, before, sizeof(backing)));
    assert(!c3_battle_fight_pokemon_create(backing, source, 0, 0));
    assert(!memcmp(backing, before, sizeof(backing)));
    assert(!memcmp(source, source_before, sizeof(source)));
    /* The constructor copies an invalid/empty Pokemon too; validity belongs
     * to its caller and must not become an invented admission rule here. */
    memset(source, 0, sizeof(source));
    assert(c3_battle_fight_pokemon_create(backing, source, 1, 0));
    assert(!memcmp(backing + 4, source, sizeof(source)));

    uint8_t arena[700], arena_before[700];
    const unsigned pairs[][2] = {{0, 0}, {0, 4}, {0, 339}, {200, 0}, {311, 0}};
    for (unsigned i = 0; i < sizeof(pairs) / sizeof(*pairs); i++) {
        pattern(arena, sizeof(arena));
        memcpy(arena_before, arena, sizeof(arena));
        assert(!c3_battle_fight_pokemon_create(arena + pairs[i][0], arena + pairs[i][1], 1, 0));
        assert(!memcmp(arena, arena_before, sizeof(arena)));
    }
    pattern(arena, sizeof(arena));
    memcpy(source_before, arena + 340, sizeof(source_before));
    assert(c3_battle_fight_pokemon_create(arena, arena + 340, 1, 3));
    assert(!memcmp(arena + 340, source_before, sizeof(source_before)));
    assert(!memcmp(arena + 4, source_before, sizeof(source_before)));
    pattern(arena, sizeof(arena));
    memcpy(source_before, arena, sizeof(source_before));
    assert(c3_battle_fight_pokemon_create(arena + 312, arena, 2, -3));
    assert(!memcmp(arena, source_before, sizeof(source_before)));
    assert(!memcmp(arena + 316, source_before, sizeof(source_before)));
}

int main(void)
{
    construction_write_map();
    initialization_write_map();
    no_op_and_rejection();
    puts("battle_fight_pokemon: BE backing, source handles, status/flags, padding, canaries, overlap and no-op passed");
    return 0;
}
#endif
