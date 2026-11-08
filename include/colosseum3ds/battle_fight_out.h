#ifndef C3_BATTLE_FIGHT_OUT_H
#define C3_BATTLE_FIGHT_OUT_H

#include "colosseum3ds/battle_status.h"

/* Original species rows (PokemonData, 0x11C bytes each) for the type and
 * ability lookups: zokuseiDataId[2] at +0x30, tokuseiDataId[2] at +0x32. */
#define C3_SPECIES_ROW_BYTES 0x11Cu

typedef struct {
    const uint8_t *rows;
    uint32_t count;
} C3BattleSpeciesTable;

/* Original fightOutPokemonCreate (fight_out_pokemon.c, 0x802068C8):
 *   fightOutPokemonInit(fo); 0xD5 = 0xD6 = fp; if sequence: 0xEE = sequence
 *   (fightFloorSetShadow is empty); types 0xFF[0..1] from the species row;
 *   ability 0x100 = pokemonGetTokuseiDataId(fp's PokemonBios).
 * fo and fp are arena guest addresses; zero for either is the source no-op.
 * Returns 0 (record unchanged where possible) if fo/fp lie outside the arena
 * or the shared initializer is busy. */
int c3_battle_fight_out_pokemon_create(C3BattleArena *arena, uint32_t fight_out,
                                       uint32_t fight_pokemon, uint32_t sequence,
                                       const C3BattleSpeciesTable *species);

/* pokemonGetTokuseiDataId over a PokemonBios record (arena guest address). */
int c3_battle_pokemon_ability(const C3BattleArena *arena, uint32_t pokemon,
                              const C3BattleSpeciesTable *species, uint16_t *ability);
#endif
