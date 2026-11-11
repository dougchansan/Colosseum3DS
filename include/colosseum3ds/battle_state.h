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
#define C3_MSGREF_MESSAGE(id) (0xA3000000u | (uint32_t)(uint16_t)(id)) /* GSmsgGetGSchar(id) */

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
    C3BattleDataRows target_kinds; /* lbl_80375AC8 rows (8), set by the caller from
                                    the battle DOL-table pack */
    C3BattleDataRows trainers;   /* trainer data rows (52), from the entry pack */
    C3BattleDataRows growth;     /* growth rows (0x194: u32 EXP per level), from the entry pack */
} C3BattleState;

/* Computed fightFloorGetStatus fields read from the encounter tables:
 * 0x14 fight type (encounter row +1), 0x16 trainer count / 0x18 active count
 * (type row +0 / +2), 0x1A fight kind (encounter row +0), 0x31 Shadow Hyper
 * Mode flag (kind row +0x16), 0x34 host/enemy message flag (kind row +0x18);
 * generally 0x1C..0x34 read kind-row byte (field - 0x1C), and 0x19 is the
 * side's active maximum (trainer count x active count). Other fields go to c3_battle_floor_status_get.
 * Returns 0 for a field neither handles. */
int c3_battle_floor_value(const C3BattleState *state, uint32_t field, uint32_t *value);

/* Shared battle queries (guarded lowering; 0 means "not modelled"):
 *   weather: fightFloorGetNowTenkouDataId(floor, check_abilities); Cloud
 *     Nine / Air Lock on any battler slot is refused, then floor weather
 *     conditions 0x4E..0x55 give the weather.
 *   held item effect: fightOutPokemonGetSoubiItemSoubiDataId; 0 under
 *     condition 0x3D or with no held item, a held item is refused.
 *   ability on any battler: whether any battler slot (a superset of the valid
 *     battlers) has the ability.
 *   battler side: the FightSide holding a battler's trainer (target kind 2).
 *   fightWazaSetUseWazaStatus: used move, its power and type into FightWaza. */
int c3_battle_state_weather(const C3BattleState *state, int check_abilities, int *weather);
int c3_battle_state_held_item_effect(const C3BattleState *state, uint32_t fight_out, uint16_t *effect);
int c3_battle_state_ability_on_any_battler(const C3BattleState *state, uint16_t ability);
uint32_t c3_battle_state_battler_side(const C3BattleState *state, uint32_t fight_out);
int c3_battle_waza_set_use(C3BattleState *state, uint32_t fight_waza, uint16_t move);

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
/* fightSideCheckValid, heroCheckValid (0x8012A130: name not empty and hero
 * byte 0xA80 != 2) and fightTrainerCheckValid (result != 1, trainer field
 * 0x43 != 0, hero copy valid). */
int c3_battle_side_check_valid(const C3BattleState *state, uint32_t side, int *fault);
int c3_battle_hero_check_valid(const C3BattleState *state, uint32_t hero, int *fault);
int c3_battle_trainer_check_valid(const C3BattleState *state, uint32_t trainer, int *fault);
/* fightFloorGetValidFightOutPokemonCount(floor, mode, target, check_fight_out)
 * through fightFloorLoopValidFightOutPokemon (loop flag 0: active slot <
 * floor 0x18, trainer < floor 0x16, side < 2, each level validated). Mode 0
 * counts every valid battler except target, mode 1 those on target's side.
 * Mode 2 (the opposing side) is not modelled: returns 0. */
/* fightTargetGetPtr(kind, ptr, fight_type) (0x801F02AC): kinds 0/1, the
 * floor kinds 0x11..0x1D, sides (2 own / 3 other relative to ptr's side, 4/5
 * absolute), trainers (6/7 own, 8 own other than ptr, 9/10 other, 0xB side 0
 * trainer 0) and active Pokemon (0xC/0xD own, 0xE own other than ptr,
 * 0xF/0x10 other), counting trainers and active slots from the fight-type
 * row. Writes *target (0 when the original returns 0) and returns 1; returns
 * 0 when a branch is not modelled (kinds 8/0xE without ptr, more than two
 * trainers or active slots). _now uses floor field 0x14 as the fight type.
 * The relative ID (fightTargetGetTragetPtrToRelativeHostSideFightTargetId)
 * is the first relative kind (target row +1 != 0) whose pointer from side 0
 * is `target`, or 0. */
int c3_battle_state_target(const C3BattleState *state, uint16_t kind, uint32_t ptr, uint16_t fight_type,
                           uint32_t *target);
int c3_battle_state_target_now(const C3BattleState *state, uint16_t kind, uint32_t ptr, uint32_t *target);
int c3_battle_state_relative_target_id(const C3BattleState *state, uint32_t target, uint16_t fight_type,
                                       uint16_t *id);
/* fightFloorCreateFightPokemonEnemyAryEnemySideAll (0x801F2F3C), one step of
 * the battle-start sequence fightActionFlowKaisiPost: every battler in battle
 * on a valid side registers each opposing battler in battle in its enemy
 * array (selector 0x122, 4 entries of 12 bytes: target +0, mimic move +4,
 * initial HP +6, damage +8). Returns 0 when a branch is not modelled. */
int c3_battle_state_create_enemy_arrays(C3BattleState *state);
/* fightFloorSetMeetEnemyFightPokemonEnemySideAll (0x801F3178), the step of
 * fightActionFlowKaisiPost just before the enemy arrays: every battler in
 * battle on a valid side records each opposing battler in battle's entry ID
 * (FightPokemon 0xCE) in its met list (0xFD, 12 s16 slots, negative =
 * empty). Returns 0 when a branch is not modelled.
 * fightOutPokemonCheckMeetEnemyFightPokemon(fo, fp): 1 when fp (valid, with
 * valid Pokemon 0xCB and 0xCC, entry ID >= 0) is in fo's met list; 0 when
 * not or the battle result is 1; -1 when a record is unreadable. */
int c3_battle_state_set_meet_enemies(C3BattleState *state);
int c3_battle_fight_out_met(const C3BattleState *state, uint32_t fight_out, uint32_t fight_pokemon);
/* fightOutPokemonIsHinsi (0x802038A4): 1 when the battler is not valid
 * (fightOutPokemonCheckValid) or its Pokemon has 0 HP (selector 0x7B).
 * fightOutPokemonIsGcHeroFightOutPokemon: the battler's trainer is the
 * player (trainer data field 4 == 1, fightTrainerIsGcHero); 0 without one.
 * Both return -1 when a record is unreadable. */
int c3_battle_fight_out_is_fainted(const C3BattleState *state, uint32_t fight_out);
int c3_battle_fight_out_is_gc_hero(const C3BattleState *state, uint32_t fight_out);
int c3_battle_state_valid_fight_out_count(const C3BattleState *state, uint32_t mode, uint32_t target,
                                          int check_fight_out, uint16_t *count);
/* The battlers fightFloorLoopValidFightOutPokemon (loop flag 0) visits, in
 * its order (active slot, trainer, side). Returns 0 when a branch is not
 * modelled or a record is unreadable. */
int c3_battle_state_valid_fight_outs(const C3BattleState *state, uint32_t out[8], unsigned *count);
/* fightSideGetFightPokemonNokoriHp(side, trainers, pokemon): the summed HP
 * (selector 0x83) of each valid trainer's valid FightPokemon in battle.
 * Returns 0 when a branch is not modelled or a record is unreadable. */
/* pokemonInitJoutai: clears the Pokemon's status condition slot (selector
 * 0x7C, PokemonBios +0x64; fn_8011B950, padding kept). */
int c3_battle_pokemon_init_joutai(C3BattleState *state, uint32_t pokemon);
int c3_battle_side_remaining_hp(const C3BattleState *state, uint32_t side, uint16_t trainers,
                                uint16_t pokemon, uint32_t *hp);

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
/* Condition slot writes (field_range_80117E58.c), same slot resolution as
 * c3_battle_condition_present. Condition rows: kind +0, slot +1, write type
 * +2, count limit +3, kaisuu row +4, selector +8, link +0xA. Slots: ID +0,
 * argument +2, kaisuu +4 (s8), +5, count +6, +8 (u32), +0xC (u16).
 *   write (fn_8011B2C0): clear the slot, store ID, argument and the kaisuu
 *     count; write type 4 also counts up to the limit. Only kaisuu row 0
 *     (min = max = -1, no draw) is modelled; other rows set *fault.
 *   check write (fn_8011B444): 0 no slot, 1 refused, 2 writable.
 *   erase (fn_8011B788): zero the slot if it holds `id`. */
int c3_battle_condition_write(C3BattleState *state, uint32_t object, uint16_t id, uint16_t argument, int *fault);
int c3_battle_condition_check_write(const C3BattleState *state, uint32_t object, uint16_t id, int *fault);
void c3_battle_condition_erase(C3BattleState *state, uint32_t object, uint16_t id, int *fault);
#endif
