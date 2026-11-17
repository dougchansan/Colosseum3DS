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
    uint32_t shadow_at; /* version 2: the Shadow table (0x61 x 0x38-byte rows), else 0 */
    /* version 3: side-0 CPU trainers (encounters the hero watches, 525). */
    uint32_t side0_at, side0_count;
    /* version 3: trainer AI data rows (40 bytes) and AI value codes (s32). */
    uint32_t ai_at, ai_count, ai_value_at, ai_value_count;
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
    /* FightKindData (encounter fightKind row): backSaveDataFlag (the hero
     * copy is written back to the save, fn_8020B... heroBiosCopy), useItem,
     * nigeru, getExp, getOkane, okanePool, doCriticalAttack and
     * darkpokemonHypermode. */
    uint8_t back_save, use_item, can_run, get_exp, get_money, money_pool, critical, hyper_mode;
    /* Side 0 is a CPU trainer, not the hero (pack v3; encounter 525, Eagun
     * vs Skrub): both sides are chosen by the trainer AI, nothing is written
     * back, and the result is 2 when side 0 wins, 3 when side 1 does. */
    uint8_t cpu_side0, side0_party_count;
    char side0_trainer[C3_GB_NAME], side0_title[C3_GB_NAME];
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
    uint16_t ev_gain[6]; /* effort earned this battle (species giveEffort, +0x90) */
    char name[C3_GB_NAME];
} C3GeneralMon;

typedef struct C3GeneralCommand {
    uint8_t kind;   /* 0 move, 1 switch, 2 throw a ball (snag) */
    uint8_t slot;   /* move slot 0..3, 4 = Struggle; party index to switch in; or ball item 1..12 */
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
    /* Snagging (the Snag Machine): with snag set, the hero may throw a ball
     * from balls[] (bag counts by ball item 1..12) at a Shadow Pokemon of
     * the opposing trainer. caught[] holds, per opponent party index, the
     * ball that snagged it (0: not snagged). Never used in link battles. */
    uint8_t snag;
    uint16_t balls[13], balls_used[13];
    uint8_t caught[C3_GB_PARTY];
    /* Floor status 0x5B: the turn's shared Quick Claw roll, drawn at the
     * start of the battle and again at the end of every turn. */
    uint16_t quick_claw_roll;
    uint32_t event_count;
    C3GeneralEvent events[C3_GB_EVENT_LIMIT];
} C3GeneralBattle;

typedef struct C3GeneralRewards {
    uint32_t result_code; /* fightFloorSetFightResultId: 2 hero won, 3 lost */
    uint32_t prize;       /* added on a win */
    uint32_t exp[C3_GB_PARTY];
    uint8_t levels_gained[C3_GB_PARTY];
    int wipeout;          /* a loss with the encounter's zenmetu flag set */
    int written;          /* the party was written back (backSaveDataFlag) */
    /* Level-up moves (species getWaza) put into a free slot, and those that
     * needed the forget prompt (not ported: left unlearned and reported). */
    uint16_t learned[C3_GB_PARTY][4], skipped[C3_GB_PARTY][4];
    uint8_t learned_count[C3_GB_PARTY], skipped_count[C3_GB_PARTY];
    /* Snagged Shadow Pokemon (opponent party index) and the balls thrown. */
    uint8_t caught_count, caught[C3_GB_PARTY];
    uint16_t balls_used[13];
    unsigned stored[C3_GB_PARTY]; /* per caught: party slot 0..5, or 100 + box*30 + entry */
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

/* Shadow table row (common_rel 0x1439D8, pack version 2): catch rate (+0),
 * species (+2), level (+8 is fn_801EEF08's field08, the heart gauge the
 * save's runtime table starts from) and the snag flag (+0x12). Returns 0
 * for a version-1 pack or an id outside 1..0x60. */
typedef struct C3ShadowInfo {
    uint8_t catch_rate;
    uint16_t species, gauge, snag_flag;
} C3ShadowInfo;
int c3_battle_pack_shadow(const C3BattlePack *pack, unsigned id, C3ShadowInfo *info);
/* The ball item's name (1..12), "" otherwise. */
const char *c3_general_ball_name(unsigned ball);
/* Whether the hero at `position` may throw `ball` at `target` now. */
int c3_general_battle_can_snag(const C3GeneralBattle *battle, const C3BattlePack *pack,
                               unsigned position, unsigned ball, unsigned target);
/* The PokemonBios the hero receives for snagged opponent `index`: the
 * battle Pokemon's species, level, moves/PP, IVs, effort, held item, HP and
 * condition, catch metadata from owner, nickname = species name, and the
 * Shadow state pokemonSetDarkPokemonStatus writes (data ID +0xD8, heart
 * gauge +0xDC = gauge x 100, stored-up experience 0). The personality is
 * drawn from `seed` until it gives the trainer row's nature. */
typedef struct C3SnagOwner {
    uint8_t name[0x16];   /* hero name, GSchar (save body hero +0) */
    uint32_t trainer_id;  /* hero +0x2C */
    uint8_t attest[4];    /* gamedata attest, as on the hero's own records */
    uint16_t floor;
} C3SnagOwner;
int c3_general_snag_record(const C3BattlePack *pack, const C3GeneralBattle *battle, unsigned index,
                           const C3SnagOwner *owner, uint16_t gauge, uint32_t seed,
                           uint8_t record[C3_GB_POKEMON_SIZE]);

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
/* Experience, effort, levels (stats recomputed from IVs/EVs/nature) and
 * their level-up moves, HP, PP and the persistent status back into the party
 * records; prize money and the story result code. Shadow Pokemon bank their
 * experience in poolExp instead of levelling. Nothing is written when the
 * fight kind's backSaveDataFlag is clear (the original discards the hero
 * copy); no experience without getExp, no prize without getOkane. */
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
