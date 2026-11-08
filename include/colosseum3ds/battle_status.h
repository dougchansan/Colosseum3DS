#ifndef C3_BATTLE_STATUS_H
#define C3_BATTLE_STATUS_H

#include <stddef.h>
#include <stdint.h>

/* Battle records live in an arena addressed by synthetic 32-bit guest
 * addresses (C3_ARENA_BASE + byte offset). Original code that stores record
 * pointers or does pointer arithmetic (e.g. field 0xD9 = record + 0x49C) then
 * behaves exactly as it did on the GameCube, and every access is bounds-
 * checked. The range never overlaps real GameCube addresses (0x80000000-
 * 0x817FFFFF), so script windows and arena records cannot be confused.
 * Multi-byte fields are big-endian, as in the original records. */
#define C3_ARENA_BASE 0x90000000u

typedef struct {
    uint8_t *bytes;
    uint32_t size;
} C3BattleArena;

/* Guest address <-> bytes. Returns NULL / 0 outside the arena. */
uint8_t *c3_battle_arena_bytes(const C3BattleArena *arena, uint32_t address, uint32_t width);
uint32_t c3_battle_arena_address(const C3BattleArena *arena, const uint8_t *bytes);

/* pokemonGetStatus / pokemonSetStatus for the FightPokemon / FightOutPokemon
 * selectors 0xCB..0x123, table-driven from pinned source (see
 * tools/prepare_battle_status_fields.py). A NULL object reads 0 and ignores
 * writes, as in the original. Returns 1 when the selector is handled here, 0
 * when it is computed by other original functions (not lowered yet), absent,
 * or the object/field lies outside the arena; *value is then untouched. */
int c3_battle_fight_status_get(const C3BattleArena *arena, uint32_t object,
                               uint32_t selector, uint32_t index, uint32_t *value);
int c3_battle_fight_status_set(C3BattleArena *arena, uint32_t object,
                               uint32_t selector, uint32_t index, uint32_t value);

#define C3_FIGHT_OUT_POKEMON_BYTES 0x6E0u
#define C3_RUNTIME_FIGHT_OUT_OFFSET 0x1314u /* three FightOutPokemon per trainer */
#endif
