#ifndef COLOSSEUM3DS_RELIVE_H
#define COLOSSEUM3DS_RELIVE_H

#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/game_state.h"
#include <stdint.h>

/* Purification at the Relic Stone, lowered from src/game/pokemon_relive.c:
 * preReliveSetParameter / preReliveMain (the decision), reliveMain and
 * reliveCeremonyAll (the purification). Records are the save's 0x138-byte
 * party records, laid out as PokemonBios. */

/* heroItemCheckHaveItemDataId(0, 0x219): the item preReliveMain checks. */
#define C3_RELIVE_ITEM 0x219u

/* pokemonBiosGetDarkFlag: a Shadow record (Shadow data ID set, heart gauge
 * not negative). */
int c3_relive_is_shadow(const uint8_t record[C3_GB_POKEMON_SIZE]);
/* The story flag fn_801EECD8 sets for a Shadow data ID: the Shadow table
 * (common_rel data 0x1439D8, 0x38-byte rows, flag at +0x14) holds 0x4EE + id
 * - 1 for ids 1..0x60 (tools/prepare_game_state.py checks it); 0 otherwise. */
uint32_t c3_relive_purify_flag(uint16_t shadow_id);

typedef struct C3ReliveCounts {
    unsigned remaining; /* Shadow records whose purify flag is still clear */
    unsigned ready;     /* Shadow records with the heart gauge at 0 (native F2) */
    int has_item;       /* the hero holds item 0x219 */
} C3ReliveCounts;
int c3_relive_counts(const C3GameState *state, C3ReliveCounts *out);

/* preReliveMain's action for selection mode `mode` (preReliveSetParameter's
 * first argument): 4 = ask which heart-opened Pokemon to purify, 2 =
 * purify the given party member at once (mode 1), anything else = nothing. */
enum { C3_RELIVE_NONE = 0, C3_RELIVE_PURIFY = 2, C3_RELIVE_PICK = 4 };
int c3_relive_action(int mode, const C3ReliveCounts *counts);
/* menuPokemonOpen(7): whether a party member can be picked (a Shadow
 * record with the heart gauge at 0). */
int c3_relive_pickable(const uint8_t record[C3_GB_POKEMON_SIZE]);

/* What a purification did, for its messages. */
typedef struct C3ReliveResult {
    uint16_t species, shadow_id, move0;  /* move0: the first move after purification (message 0x3B10) */
    uint32_t pool_exp;                    /* stored-up experience paid out (message 0x3B0B) */
    uint16_t friendship_before, friendship_after;
    int status_cleared;                   /* status 0x3E entry removed */
    C3ExpRecovery exp;                    /* levels and moves of the payout */
} C3ReliveResult;
/* reliveCeremonyAll for party `slot` (reliveMain mode 1 first sets the
 * heart gauge to 0: `forced`). Writes the record and the purify flag. pack
 * supplies growth, base stats and level-up moves. Not ported: the
 * evolution check after a level-up, the forget-a-move prompt and the
 * hero's ex-ribbon save byte (exribbonSetNarionalRibbon's second write).
 * Returns 0 when the slot holds no Shadow Pokemon. */
int c3_relive_purify(C3GameState *state, unsigned slot, int forced, const C3BattlePack *pack,
                     C3ReliveResult *out);

#endif
