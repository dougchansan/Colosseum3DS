#ifndef C3_BATTLE_GENERAL_H
#define C3_BATTLE_GENERAL_H

#include "colosseum3ds/battle_rng.h"
#include <stddef.h>
#include <stdint.h>

/* General story battle: any encounter in the private C3B2 pack
 * (tools/prepare_battle_encounters.py) against the hero's saved party.
 *
 * Scope, stated plainly: the encounter, trainer, party, move, species, type
 * chart and growth data are the original rows. The turn rules are a
 * documented Gen-3 style approximation (src/battle_general.c lists the move
 * effects it models), not the original battle scripts, which are being
 * lowered separately (feat/original-battle-turns). There is no bag, no
 * snagging and no escape; the opponent AI picks uniformly among legal moves.
 * The result is fed back to the story (2 hero win, 3 hero loss) and the
 * party's HP, PP and experience and the prize money are written back. */

#define C3_GB_ERROR_SIZE 192u
#define C3_GB_PARTY 6u
#define C3_GB_POKEMON_SIZE 0x138u
#define C3_GB_EVENT_LIMIT 96u
#define C3_GB_NAME 16u
#define C3_GB_MAX_TURNS 999u

typedef struct C3BattlePack {
    uint8_t *bytes;
    size_t size;
    uint32_t encounters, moves, species, types, growth;
    uint32_t species_at, types_at, growth_at, entries_at, names_at;
} C3BattlePack;

typedef struct C3BattleEncounterInfo {
    uint16_t id, reason, floor_data_id, battle_floor, trainer_id, trainer_kind;
    uint16_t prize_base;
    uint8_t kind, active_per_side, party_count, wipeout, shadow_count;
    uint16_t species[C3_GB_PARTY];
    uint8_t levels[C3_GB_PARTY];
    char trainer[C3_GB_NAME], title[C3_GB_NAME];
    /* fightEncountGetBgmSndDataId (0x8020DD80): the encounter's override,
     * else the battle floor's, else the opponent trainer kind's, else 1. */
    uint32_t bgm;
    uint32_t intro_wzx;      /* encounter row +20, else battle floor row +8 */
    uint32_t environment_sound; /* battle floor row +16 */
} C3BattleEncounterInfo;

typedef enum {
    C3_GB_RUNNING = 1, C3_GB_WON, C3_GB_LOST, C3_GB_DRAW
} C3GeneralOutcome;

enum { C3_GB_NONE = 0, C3_GB_SLEEP, C3_GB_POISON, C3_GB_BURN, C3_GB_FREEZE,
       C3_GB_PARALYSIS, C3_GB_TOXIC };

typedef struct C3GeneralMon {
    uint16_t species, hp, max_hp, stats[6]; /* HP Atk Def SpA SpD Spe */
    uint16_t moves[4], friendship, held_item, base_exp;
    uint8_t pp[4], pp_max[4];
    uint8_t level, types[2], growth, status, status_turns, toxic_count;
    uint8_t confusion, flinch, protect, recharge, shadow, participated;
    int8_t stages[7]; /* Atk Def SpA SpD Spe Acc Eva */
    uint8_t ivs[6];
    uint16_t evs[6];
    uint8_t nature, party_slot;
    uint8_t ball;      /* catch ball item (PokemonBios +0x0F; opponents: Poke Ball 4) */
    uint32_t exp;
    char name[C3_GB_NAME];
} C3GeneralMon;

typedef struct C3GeneralCommand {
    uint8_t kind;   /* 0 move, 1 switch */
    uint8_t slot;   /* move slot 0..3, 4 = Struggle; or party index to switch in */
    uint8_t target; /* battle position 0..3 (side*2 + active index) */
} C3GeneralCommand;

typedef enum {
    C3_GB_EVENT_MESSAGE = 1, C3_GB_EVENT_MOVE, C3_GB_EVENT_HP, C3_GB_EVENT_FAINT,
    C3_GB_EVENT_SWITCH, C3_GB_EVENT_OUTCOME
} C3GeneralEventKind;

typedef struct C3GeneralEvent {
    uint8_t kind, actor, target;
    uint16_t move;
    uint16_t hp[4], max_hp[4]; /* per battle position after the event */
    uint16_t species;   /* the Pokemon at `actor` when logged (0: none) */
    int8_t stat, stat_delta; /* a stage change: stat 0..6 and delta; stat -1 otherwise */
    uint8_t status;     /* a new status condition (C3_GB_SLEEP..), 0 otherwise */
    uint8_t ball;       /* the ball of the Pokemon at `actor` (item id, 0 none) */
    char text[96];
} C3GeneralEvent;

typedef struct C3GeneralBattle {
    uint32_t magic, encounter, turn;
    C3GeneralOutcome outcome;
    C3BattleRng rng;
    C3BattleEncounterInfo info;
    uint8_t active_count, party_count[2];
    uint8_t active[2][2];          /* party index per position, 255 = empty */
    uint8_t screens[2][2];         /* Reflect, Light Screen turns per side */
    C3GeneralMon party[2][C3_GB_PARTY];
    uint8_t player_order[C3_GB_PARTY];
    /* Link battles (src/link_battle.c, docs/link-play.md): both sides are
     * players. viewer is the side this console shows as its own; it only
     * changes message text ("Foe", "Go!"), never the state, so it is left
     * out of c3_general_battle_hash. owner_split (multi battles): each
     * active slot sends out only the Pokemon its own player brought. */
    uint8_t link, viewer, owner_split;
    uint8_t owner[2][C3_GB_PARTY];
    char side_name[2][C3_GB_NAME];
    uint32_t event_count;
    C3GeneralEvent events[C3_GB_EVENT_LIMIT];
} C3GeneralBattle;

typedef struct C3GeneralRewards {
    uint32_t result_code; /* fightFloorSetFightResultId: 2 hero won, 3 lost */
    uint32_t prize;       /* added on a win */
    uint32_t exp[C3_GB_PARTY];
    uint8_t levels_gained[C3_GB_PARTY];
    int wipeout;          /* a loss with the encounter's zenmetu flag set */
} C3GeneralRewards;

int c3_battle_pack_decode(C3BattlePack *pack, const void *bytes, size_t size, char *error);
int c3_battle_pack_load(C3BattlePack *pack, const char *path, char *error);
void c3_battle_pack_free(C3BattlePack *pack);
int c3_battle_pack_info(const C3BattlePack *pack, uint32_t encounter, C3BattleEncounterInfo *info);
const char *c3_battle_pack_reason(unsigned reason);
int c3_battle_pack_move_name(const C3BattlePack *pack, uint16_t move, char name[C3_GB_NAME]);
/* Original move row fields; range 4/6 hit several, 5/7 the user's side. */
typedef struct C3GeneralMoveInfo {
    int16_t power; int8_t priority;
    uint8_t pp, type, range, accuracy;
    uint16_t effect;
} C3GeneralMoveInfo;
int c3_battle_pack_move(const C3BattlePack *pack, uint16_t move, C3GeneralMoveInfo *info);
/* The species' cry: its voice sound request (species row +14), 0 none. */
uint16_t c3_battle_pack_species_voice(const C3BattlePack *pack, uint16_t species);

/* Purification's experience payout (_expRecover); see battle_general.c. */
#define C3_EXP_RECOVERY_MAX 16u
typedef struct C3ExpRecovery {
    int level_up;
    unsigned level_count, learned_count, skipped_count;
    uint8_t levels[C3_EXP_RECOVERY_MAX];          /* each level reached, in order */
    uint16_t learned[C3_EXP_RECOVERY_MAX];        /* moves put into a free slot */
    uint8_t learned_level[C3_EXP_RECOVERY_MAX];
    uint16_t skipped[C3_EXP_RECOVERY_MAX];        /* moves that needed a forget prompt */
} C3ExpRecovery;
int c3_battle_pack_recover_exp(const C3BattlePack *pack, uint8_t record[C3_GB_POKEMON_SIZE],
                               uint32_t add, C3ExpRecovery *out);

/* party: the save body's six consecutive 0x138-byte records, read-only. */
int c3_general_battle_begin(C3GeneralBattle *battle, const C3BattlePack *pack,
                            uint32_t encounter, const void *party,
                            uint32_t seed, char *error);
uint8_t c3_general_battle_legal_moves(const C3GeneralBattle *battle, const C3BattlePack *pack,
                                      unsigned position);
/* A legal command for any occupied position (the opponent policy; the
 * headless whole-game test also uses it for the hero). */
int c3_general_battle_choose(C3GeneralBattle *battle, const C3BattlePack *pack,
                             unsigned position, C3GeneralCommand *command);
/* commands[0..1] are the hero's positions 0 and 1 (ignored when empty). */
int c3_general_battle_turn(C3GeneralBattle *battle, const C3BattlePack *pack,
                           const C3GeneralCommand commands[2], char *error);
/* Experience, levels (stats recomputed from IVs/EVs/nature), HP and PP back
 * into the party records; prize money and the story result code. Shadow
 * Pokemon bank their experience in poolExp instead of levelling. */
int c3_general_battle_writeback(const C3GeneralBattle *battle, const C3BattlePack *pack,
                                uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE],
                                C3GeneralRewards *rewards);
/* ---- link battles (docs/link-play.md) ----------------------------------
 * A Pokemon from a 312-byte party record (0 for an egg or a damaged record)
 * or from a story trainer's preset party in the pack. */
int c3_general_mon_from_record(const C3BattlePack *pack, const uint8_t record[C3_GB_POKEMON_SIZE],
                               unsigned slot, C3GeneralMon *mon);
int c3_general_mon_from_trainer(const C3BattlePack *pack, uint32_t encounter, unsigned index, C3GeneralMon *mon);
/* Both sides from players. stage: an encounter whose floor, music and
 * intro the battle borrows (its trainer and party are not used). owner may
 * be NULL (each side is one player); names: each side's player name(s).
 * viewer: the side this console shows as its own. */
int c3_general_battle_begin_link(C3GeneralBattle *battle, const C3BattleEncounterInfo *stage,
                                 unsigned active_per_side, const C3GeneralMon mons[2][C3_GB_PARTY],
                                 const uint8_t counts[2], const uint8_t owner[2][C3_GB_PARTY],
                                 const char names[2][C3_GB_NAME], uint32_t seed, unsigned viewer,
                                 char *error);
/* commands[0..3]: every battle position (ignored when empty or fainted).
 * No command is drawn from the battle's RNG: all four come from players. */
int c3_general_battle_turn_link(C3GeneralBattle *battle, const C3BattlePack *pack,
                                const C3GeneralCommand commands[4], char *error);
int c3_general_battle_command_valid(const C3GeneralBattle *battle, const C3BattlePack *pack,
                                    unsigned position, C3GeneralCommand command);
/* The opponent policy with a caller's generator, so an automatic player
 * never draws from the shared battle RNG. */
int c3_general_battle_choose_with(const C3GeneralBattle *battle, const C3BattlePack *pack,
                                  unsigned position, C3BattleRng *rng, C3GeneralCommand *command);
/* FNV-1a over the battle state both consoles must agree on. */
uint32_t c3_general_battle_hash(const C3GeneralBattle *battle);
/* The battle seen from side 1: sides, positions and outcome swapped, so the
 * 2D/3D scenes (which show side 0 as the player's) can present it. */
void c3_general_battle_mirror(const C3GeneralBattle *in, C3GeneralBattle *out);

/* Whether the general engine models this move's effect exactly as listed
 * (1) or substitutes a plain hit / no effect (0). */
int c3_general_effect_modelled(uint16_t effect);
#endif
