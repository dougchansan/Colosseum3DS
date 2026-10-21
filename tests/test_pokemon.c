#include "colosseum3ds/pokemon.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* Synthetic parameters, not a species-data export. */
static C3Pokemon fixture(void)
{
    C3Pokemon pokemon = {0};
    pokemon.species_id = 1;
    for (unsigned i = 0; i < C3_STAT_COUNT; ++i) {
        pokemon.base_stats[i] = 100;
        pokemon.iv[i] = 31;
    }
    pokemon.ev[C3_STAT_ATTACK] = 252;
    pokemon.ev[C3_STAT_SPEED] = 252;
    pokemon.ev[C3_STAT_SP_DEFENSE] = 6;
    c3_pokemon_neutral_nature(&pokemon);
    return pokemon;
}

int main(void)
{
    C3Pokemon pokemon = fixture();
    C3Pokemon before;
    /* Hand-calculated level-50 outputs in the API's named stat order. */
    const uint16_t expected50[] = {175, 152, 120, 152, 120, 121};
    const uint16_t expected100[] = {341, 299, 236, 299, 236, 237};
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_OK);
    CHECK(pokemon.level == 50);
    CHECK(pokemon.current_hp == 175);
    CHECK(memcmp(pokemon.stats, expected50, sizeof(expected50)) == 0);

    /* Nature applies integer truncation after the raw level stat. */
    pokemon.nature[C3_STAT_ATTACK] = (C3NatureRate){11, 10};
    pokemon.nature[C3_STAT_SPEED] = (C3NatureRate){9, 10};
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_OK);
    CHECK(pokemon.stats[C3_STAT_ATTACK] == 167);
    CHECK(pokemon.stats[C3_STAT_SPEED] == 136);
    CHECK(pokemon.stats[C3_STAT_HP] == 175);

    /* Raising level adds the max-HP difference, retaining 50 HP lost. */
    c3_pokemon_neutral_nature(&pokemon);
    pokemon.current_hp = 125;
    CHECK(c3_pokemon_recalculate(&pokemon, 100) == C3_POKEMON_OK);
    CHECK(memcmp(pokemon.stats, expected100, sizeof(expected100)) == 0);
    CHECK(pokemon.current_hp == 291);

    /* A fainted existing Pokemon stays fainted during a stat recalculation. */
    pokemon.current_hp = 0;
    CHECK(c3_pokemon_recalculate(&pokemon, 100) == C3_POKEMON_OK);
    CHECK(pokemon.current_hp == 0);

    /* Retail's Shedinja test uses Colosseum's internal species number. */
    pokemon = fixture();
    pokemon.species_id = 0x12F;
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_OK);
    CHECK(pokemon.stats[C3_STAT_HP] == 1);
    CHECK(pokemon.current_hp == 1);
    pokemon.current_hp = 0;
    CHECK(c3_pokemon_recalculate(&pokemon, 100) == C3_POKEMON_OK);
    CHECK(pokemon.stats[C3_STAT_HP] == 1 && pokemon.current_hp == 0);

    /* High damage + a large level decrease must not publish wrapped HP. */
    pokemon = fixture();
    CHECK(c3_pokemon_recalculate(&pokemon, 100) == C3_POKEMON_OK);
    pokemon.current_hp = 1;
    before = pokemon;
    CHECK(c3_pokemon_recalculate(&pokemon, 1) == C3_POKEMON_RESULT_RANGE);
    CHECK(memcmp(&pokemon, &before, sizeof(pokemon)) == 0);

    /* Bad public inputs fail without changing the supplied record. */
    CHECK(c3_pokemon_recalculate(NULL, 50) == C3_POKEMON_INVALID_ARGUMENT);
    pokemon = fixture();
    before = pokemon;
    CHECK(c3_pokemon_recalculate(&pokemon, 0) == C3_POKEMON_INVALID_ARGUMENT);
    CHECK(c3_pokemon_recalculate(&pokemon, 101) == C3_POKEMON_INVALID_ARGUMENT);
    CHECK(memcmp(&pokemon, &before, sizeof(pokemon)) == 0);
    pokemon.iv[C3_STAT_ATTACK] = 32;
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_INVALID_ARGUMENT);
    pokemon = fixture();
    pokemon.ev[C3_STAT_HP] = 1; /* Existing fixture totals 510. */
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_INVALID_ARGUMENT);
    pokemon = fixture();
    pokemon.nature[C3_STAT_ATTACK] = (C3NatureRate){11, 10};
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_INVALID_ARGUMENT);
    pokemon = fixture();
    pokemon.nature[C3_STAT_HP] = (C3NatureRate){11, 10};
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_INVALID_ARGUMENT);
    pokemon = fixture();
    pokemon.nature[C3_STAT_ATTACK].denominator = 0;
    CHECK(c3_pokemon_recalculate(&pokemon, 50) == C3_POKEMON_INVALID_ARGUMENT);
    printf("Pokemon native source slice: %u checks passed\n", checks);
    return 0;
}
