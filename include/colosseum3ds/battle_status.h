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

/* Static original tables addressed by data ID (move rows: 56 bytes;
 * floor data rows: 24 bytes). Rows at or beyond count read as the original
 * NULL row (0). */
typedef struct {
    const uint8_t *rows;
    uint32_t count, stride;
} C3BattleDataRows;

/* wazaGetStatus / wazaSetStatus: statuses 1..0x24 read move data rows by
 * data_id; 0x25..0x34 read the FightWaza record at fight_waza (arena), with
 * 0x33/0x34 (type of the original/used move) lowered by hand. Move rows are
 * never written. Same return convention as above. */
int c3_battle_waza_status_get(const C3BattleArena *arena, const C3BattleDataRows *moves,
                              uint32_t fight_waza, uint16_t data_id, uint32_t status,
                              uint32_t index, uint32_t *value);
int c3_battle_waza_status_set(C3BattleArena *arena, uint32_t fight_waza, uint32_t status,
                              uint32_t index, uint32_t value);

/* fightFloorGetStatus / fightFloorSetStatus. floor is the FightFloor record
 * (arena address of lbl_8046DD90's 0xA4E8 bytes); object defaults to it for
 * fields 8..0x5E, fields 1..7 read floor data rows by slot. 0x37/0x38
 * (attacker's used/original move) are lowered by hand. Setters that also
 * drive presentation (e.g. attacker 0x36, defender 0x42) are not handled. */
#define C3_FIGHT_FLOOR_BYTES 0xA4E8u
int c3_battle_floor_status_get(const C3BattleArena *arena, const C3BattleDataRows *floors,
                               uint32_t floor, uint32_t object, uint32_t slot,
                               uint32_t field, uint32_t arg, uint32_t *value);
int c3_battle_floor_status_set(C3BattleArena *arena, uint32_t floor, uint32_t object,
                               uint32_t field, uint32_t arg, uint32_t value);

/* fightTargetGetPtrAsNowFightType for the fixed kinds: 0, 1 (FightFloor) and
 * 0x11..0x1D (floor attacker 0x36, defender 0x42, ...). Side/trainer-relative
 * kinds return 0 (not lowered yet). */
int c3_battle_target(const C3BattleArena *arena, uint32_t floor, uint32_t kind,
                     uint32_t index, uint32_t *target);

#define C3_FIGHT_OUT_POKEMON_BYTES 0x6E0u
#define C3_RUNTIME_FIGHT_OUT_OFFSET 0x1314u /* two active FightOutPokemon per trainer
                                              (trainer field 0x46); another record
                                              of this size sits at +0x20D4 (0x47) */
#endif
