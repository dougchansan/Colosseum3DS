#ifndef C3_BATTLE_STATE_H
#define C3_BATTLE_STATE_H

#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle_fight_out.h"
#include "colosseum3ds/battle_runtime_trainer.h"

/* Battle memory after trainer entry: the two runtime trainer records and the
 * FightFloor record (lbl_8046DD90) in one guest-addressed arena, plus the
 * battle result global (lbl_8047B5D8) and the original tables the queries
 * read. Records keep the original big-endian layout.
 *
 * Built from a finished entry session. The entry stores FightPokemon source
 * links (+0, selector 0xCB) as native party identities; the state relocates
 * each one to the arena address of that party slot in the trainer's embedded
 * Hero (trainer +0x34 + slot * 0x138), and refuses any other value.
 *
 * Not modelled yet (each a later lowering step): the FightFloor contents set
 * by battle start (zero, as in .bss, until those stages run), conditions. */
#define C3_BATTLE_STATE_TRAINER_STRIDE C3_BATTLE_RUNTIME_TRAINER_BYTES
#define C3_BATTLE_STATE_FLOOR_OFFSET (2u * C3_BATTLE_RUNTIME_TRAINER_BYTES)
#define C3_BATTLE_STATE_BYTES (C3_BATTLE_STATE_FLOOR_OFFSET + C3_FIGHT_FLOOR_BYTES)
#define C3_BATTLE_HERO_PARTY_OFFSET 0x34u /* Hero copy at +4, its party at +0x30 */

typedef struct {
    uint8_t memory[C3_BATTLE_STATE_BYTES];
    C3BattleArena arena;
    uint32_t trainer[2];      /* guest addresses: [0] player side, [1] opponent */
    uint32_t floor;           /* FightFloor guest address */
    uint16_t result;          /* lbl_8047B5D8: 0 while the battle runs */
    C3BattleSpeciesTable species;
    C3BattleDataRows moves, floors;
    C3BattleDataRows conditions; /* lbl_8035BBA8 rows (0x14), set by the caller
                                    from the battle DOL-table pack */
} C3BattleState;

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
