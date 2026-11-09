#ifndef C3_BATTLE_STATE_H
#define C3_BATTLE_STATE_H

#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle_fight_out.h"
#include "colosseum3ds/battle_runtime_trainer.h"

/* Battle memory: the FightFloor record (lbl_8046DD90, 0xA4E8 bytes) as one
 * guest-addressed arena, with the original containment:
 *   FightFloor +0x00 condition slot, +0x10 turn count, +0x12 encounter ID,
 *              +0x14 FightSide[2] (0x5230 each), +0xA474.. targets
 *   FightSide  +0x00 side data ID, +0x04 condition slots x6,
 *              +0x64 FightTrainer[2] (0x28E4 each), +0x522C spikes flag
 * plus the battle result global (lbl_8047B5D8), the message-control
 * variables (msgctrlSetValue) and the original tables the queries read.
 *
 * c3_battle_state_from_entry runs the lowered fightFloorInit for the
 * encounter, then places each side's entry-built runtime trainer in that
 * side's first trainer slot, as the original entry writes it there. The
 * entry stores FightPokemon source links (+0, selector 0xCB) as native party
 * identities; each is relocated to that party slot in the trainer's embedded
 * Hero (trainer +0x34 + slot * 0x138), and any other value is refused. */
#define C3_BATTLE_STATE_BYTES C3_FIGHT_FLOOR_BYTES
#define C3_FIGHT_SIDE_OFFSET 0x14u
#define C3_FIGHT_SIDE_BYTES 0x5230u
#define C3_FIGHT_SIDE_TRAINER_OFFSET 0x64u
#define C3_BATTLE_HERO_PARTY_OFFSET 0x34u /* Hero copy at +4, its party at +0x30 */
#define C3_MSGCTRL_IDS 0x60u
/* Message-control values that name a message string are kept as tagged
 * references (resolved by the text renderer), never host pointers. */
#define C3_MSGREF_ITEM_NAME(id) (0xA1000000u | (uint32_t)(uint16_t)(id))
#define C3_MSGREF_ABILITY_NAME(id) (0xA2000000u | (uint32_t)(uint16_t)(id))

typedef struct {
    uint8_t memory[C3_BATTLE_STATE_BYTES];
    C3BattleArena arena;
    uint32_t floor;           /* FightFloor guest address */
    uint32_t trainer[2];      /* side s, trainer slot 0: [0] player, [1] opponent */
    uint16_t result;          /* lbl_8047B5D8: 0 while the battle runs */
    uint32_t msgctrl[C3_MSGCTRL_IDS];
    C3BattleSpeciesTable species;
    C3BattleDataRows moves, floors;
    C3BattleDataRows conditions; /* lbl_8035BBA8 rows (0x14), set by the caller
                                    from the battle DOL-table pack */
    C3BattleDataRows encounters, types, kinds; /* 56 / 8 / 32-byte rows; types
                                    and kinds admit index == count */
    C3BattleDataRows accuracy;   /* lbl_80375DD0 (multiplier, divisor) x13, set by
                                    the caller from the battle DOL-table pack */
    C3BattleRng rng;             /* the shared fn_800E0C54 stream; continues from
                                    the entry session at handoff */
} C3BattleState;

/* Computed fightFloorGetStatus fields read from the encounter tables:
 * 0x14 fight type (encounter row +1), 0x16 trainer count / 0x18 active count
 * (type row +0 / +2), 0x1A fight kind (encounter row +0), 0x31 Shadow Hyper
 * Mode flag (kind row +0x16), 0x34 host/enemy message flag (kind row +0x18). Other fields go to c3_battle_floor_status_get.
 * Returns 0 for a field neither handles. */
int c3_battle_floor_value(const C3BattleState *state, uint32_t field, uint32_t *value);

/* msgctrlSetValue: stores into the original variable for a known ID (IDs
 * 0x2D, 0x2E, 0x39, 0x4E, 0x58, 0x59, 0x5D keep 16 bits); unknown IDs are
 * ignored, as in the original. */
void c3_battle_msgctrl_set(C3BattleState *state, uint32_t id, uint32_t value);

/* fightFloorInit (fight_floor.c, 0x801F4860) on the state's FightFloor:
 * condition slots, both FightSides (fightSideInitAry with fightTrainerInit),
 * the thirteen target fields and their message variables, fields 0x50-0x58,
 * the active array, the first-attack random, the encounter ID and turn 1.
 * Returns 0 if the shared trainer initializer is busy or fails. */
int c3_battle_floor_init(C3BattleState *state, uint16_t encounter_id);

/* Copy and relocate a DONE entry session's trainer records. The pack's
 * species/move/floor rows are borrowed and must outlive the state. Returns 1,
 * or 0 with error set (C3_BATTLE_ERROR_SIZE) and the state unspecified. */
int c3_battle_state_from_entry(C3BattleState *state, const C3BattleEntrySession *session,
                               const C3BattleEntryPack *pack, char *error);

/* Guest addresses of a side's FightPokemon / active FightOutPokemon slot. */
uint32_t c3_battle_state_fight_pokemon(const C3BattleState *state, unsigned side, unsigned slot);
uint32_t c3_battle_state_fight_out(const C3BattleState *state, unsigned side, unsigned slot);

/* Original validity checks (each returns 1/0; an object outside the arena
 * reads as invalid and sets *fault when fault is non-NULL):
 *   pokemonCheckValid / pokemonCheckFightOut        (PokemonBios)
 *   fightPokemonCheckValid / fightPokemonCheckFightOut (FightPokemon, inline forms)
 *   fightOutPokemonCheckValid / fightOutPokemonCheckFightOut */
int c3_battle_pokemon_check_valid(const C3BattleState *state, uint32_t pokemon, int *fault);
int c3_battle_pokemon_check_fight_out(const C3BattleState *state, uint32_t pokemon, int *fault);
int c3_battle_fight_pokemon_check_valid(const C3BattleState *state, uint32_t fight_pokemon, int *fault);
int c3_battle_fight_pokemon_check_fight_out(const C3BattleState *state, uint32_t fight_pokemon, int *fault);
int c3_battle_fight_out_check_valid(const C3BattleState *state, uint32_t fight_out, int *fault);
int c3_battle_fight_out_check_fight_out(const C3BattleState *state, uint32_t fight_out, int *fault);

/* fn_8011B67C: is condition `id` stored in its slot of `object`? The
 * condition row gives the status kind (+0), slot index (+1) and record
 * selector (+8); the slot is statusGetStatus(kind, object, 0, selector, 0)
 * + index * 16 and holds the condition ID as a u16. Kinds 5 (Pokemon /
 * FightPokemon / FightOutPokemon) and 6 (FightWaza) are served; kind 0 and
 * kinds above 9 read as absent (statusGetStatus returns 0); kinds 1-4 and
 * 7-9 are original paths not modelled yet: absent, and *fault is set. */
int c3_battle_condition_present(const C3BattleState *state, uint32_t object,
                                uint16_t id, int *fault);
/* fn_802026E4 on a FightOutPokemon: categories 0x7C/0xC8 test its Pokemon
 * (PokemonBios +0x64 / +0xE8), 0xCD its FightPokemon, 0xD8 itself; other
 * categories return 0, as in the original. */
int c3_battle_condition_check(const C3BattleState *state, uint32_t fight_out,
                              uint16_t id, int *fault);
#endif
