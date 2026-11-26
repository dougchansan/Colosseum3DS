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
 * lowered separately (feat/original-battle-turns). The hero's bag (balls
 * at Shadow Pokemon, medicine, PP and battle items) is modelled; trainer
 * items and escape are not.
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
    /* version 3: nature rows (40 bytes) and heart gauge filter values (s8). */
    uint32_t nature_at, nature_count, dp_filter_at, dp_filter_count;
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
    /* doHizukiAiFlag (kind +15, floor status 0x2B): the partner notices an
     * opposing Shadow Pokemon when it first comes out (fn_8022E6F0). */
    uint8_t partner_aura;
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
    /* Shadow Pokemon of the hero (PokemonBios +0xDC, status 0xC5: heart
     * gauge x100) and its Shadow row's full gauge x100 (status 0xC4).
     * hyper: joutai 0x3E (Hyper Mode); gauge_dropped: flag 0xD1 (the send-out
     * gauge drop is once per battle). */
    int32_t dp, dp_max;
    uint8_t hyper, gauge_dropped; /* gauge_dropped is flag 0xD1 for either side: the
                                   * opponent's Shadow appearance check ran too */
    /* pokemonGetTokuseiDataId: species row +0x32[flag] (the flag is 0 when the
     * species has one ability); trainer records give the flag at +0, party
     * records at +0xCC. key_player / part: trainer record +5 / +6
     * (fightTrainerPokemonData KeyPlayerFlag, PartDataId), 0 for the hero's. */
    uint8_t ability, key_player, part;
    /* A Shadow Pokemon's slots (include/colosseum3ds/shadow_moves.h):
     * moves/pp/pp_max above are what the battle, its menu and the AI see
     * (Shadow Rush, "????" or the Pokemon's own move); own_* are the
     * record's own moves (pokemonBiosGetPokemonWazaPtr mode 0), which the
     * writeback and the snag record store. trainer: fightTrainerPokemonDataId
     * is set (a trainer's Pokemon: only slot 0 is Shadow Rush). hidden: bit
     * per slot showing a stand-in. The shown slots follow the heart gauge
     * during the battle (a drop that opens a move shows it at once). */
    uint16_t own_moves[4];
    uint8_t own_pp[4], own_pp_max[4], trainer, hidden;
} C3GeneralMon;

typedef struct C3GeneralCommand {
    uint8_t kind;   /* 0 move, 1 switch, 2 throw a ball (snag), 3 call (the hero's own Pokemon),
                     * 4 use a bag item (C3GeneralBagItem) */
    uint8_t slot;   /* move slot 0..3, 4 = Struggle; party index to switch in; ball item 1..12;
                     * or the bag entry */
    uint8_t target; /* battle position 0..3 (side*2 + active index); kind 4: the hero's party
                     * index, plus 16 x the move slot for a move-selective item (Ether) */
} C3GeneralCommand;

/* ---- the bag (fight_menu.c _fightMenuFightTrainerGcHeroOpenMenuSubItem) --
 * The hero's Items and Berries pockets as the battle began; balls stay in
 * balls[] (the Snag Machine). use is the item row's battleUseFunc
 * (ItemDataBios +0x20), param its itemEffectParam row (16 bytes,
 * itemParamGet*), filled by c3_battle_story_prepare_bag. count drops and
 * used rises only when the item's action runs in the turn. */
#define C3_GB_BAG 66u
enum {
    C3_GB_USE_NONE,     /* 0: no battle use (the Items pocket offers only CANCEL) */
    C3_GB_USE_MEDICINE, /* fn_80023B9C: one party Pokemon (HP, status, Revive) */
    C3_GB_USE_PP,       /* fn_80023968: one party Pokemon, a move when PP-selective */
    C3_GB_USE_BATTLE,   /* fn_8002217C: the Pokemon choosing (X items, Dire Hit, Guard Spec) */
    C3_GB_USE_REFUSED   /* fn_80023DA8 (message 0x44C6) and the Enigma Berry: not usable here */
};
typedef struct C3GeneralBagItem {
    uint16_t item, count, used;
    uint8_t pocket;     /* save pocket: 0 Items, 4 Berries */
    uint8_t use;        /* C3_GB_USE_* */
    uint8_t param[16];
    char name[C3_GB_NAME];
} C3GeneralBagItem;
/* Why an item can't be used (c3_general_battle_item_check), with the
 * original message each stands for. */
typedef enum {
    C3_GB_ITEM_OK,
    C3_GB_ITEM_NOT_NOW,    /* floor status 0x20 (FightKindData useItem) clear: fight message 0x75F5 */
    C3_GB_ITEM_CANT_USE,   /* no battle use, or one the engine does not model */
    C3_GB_ITEM_HYPER,      /* the Pokemon is in Hyper Mode: P*DA message 0x424D */
    C3_GB_ITEM_NO_EFFECT,  /* message 0x4261 */
    C3_GB_ITEM_NONE_LEFT,  /* every one left is already chosen this turn */
    C3_GB_ITEM_NEED_MOVE,  /* a PP-selective item without a valid move slot */
    C3_GB_ITEM_INVALID
} C3GeneralItemCheck;

typedef enum {
    C3_GB_EVENT_MESSAGE = 1, C3_GB_EVENT_MOVE, C3_GB_EVENT_HP, C3_GB_EVENT_FAINT,
    C3_GB_EVENT_SWITCH, C3_GB_EVENT_OUTCOME
} C3GeneralEventKind;

typedef struct C3GeneralEvent {
    uint8_t kind, actor, target;
    uint16_t move;
    uint16_t hp[4], max_hp[4]; /* per battle position after the event */
    uint8_t mon[4];     /* party index at each battle position after the event (255: none) */
    uint16_t species;  /* the Pokemon at `actor` when logged (0: none) */
    int8_t stat, stat_delta; /* a stage change: stat 0..6 and delta; stat -1 otherwise */
    uint8_t status;     /* a new status condition (C3_GB_SLEEP..), 0 otherwise */
    uint8_t ball;       /* the ball of the Pokemon at `actor` (item id, 0 none) */
    uint8_t hyper;      /* the Pokemon at `actor` went into Hyper Mode here */
    /* The opposing Shadow Pokemon's appearance (fn_8022E6F0 mode 0):
     * special: a battle special played on `actor` (fightOutPokemonDarkPokemonEffect:
     * 0x3A, 0xD9 in encounter 0x11); cutin: the partner's cut-in (model 0x32
     * playing special 0xA0 in the hero trainer's place) comes first;
     * waza_effect: a move whose attack sequence `actor` plays
     * (fightOutPokemonWazaEffect, encounter 0x11's tutorial); hold: the
     * partner's page stays up until the player presses A (its fight window
     * closes only after the aura), it never times out. */
    uint16_t special, waza_effect;
    uint8_t cutin, hold;
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
    /* The bag's Items and Berries pockets (C3GeneralBagItem). */
    uint8_t bag_count;
    C3GeneralBagItem bag[C3_GB_BAG];
    /* Floor status 0x5B: the turn's shared Quick Claw roll, drawn at the
     * start of the battle and again at the end of every turn. */
    uint16_t quick_claw_roll;
    /* The hero trainer's OkaneBai (fightTrainer status 0x48, the prize
     * multiplier): 1 at fightTrainerInit, 2 once a hero Pokemon holding the
     * Amulet Coin (item 189, soubi data 0x20) comes out (fn_8022D084). */
    uint8_t okane_bai;
    /* The partner (Rui) for fight text control 0x2C (c3_battle_story_prepare;
     * empty: "Rui") and flag 0x9A0 (the story has named Shadow Pokemon:
     * message 0x770A instead of 0x7717). */
    char partner_name[C3_GB_NAME];
    uint8_t shadow_named;
    uint32_t event_count;
    C3GeneralEvent events[C3_GB_EVENT_LIMIT];
} C3GeneralBattle;

typedef struct C3GeneralRewards {
    uint32_t result_code; /* fightFloorSetFightResultId: 2 hero won, 3 lost */
    uint32_t prize;       /* added on a win */
    /* okanePool kinds (the colosseum challenges): the prize opcode's x4
     * amount, for scriptAddPremium (flag 0A9E) instead of money. */
    uint32_t premium;
    uint32_t exp[C3_GB_PARTY];
    uint8_t levels_gained[C3_GB_PARTY];
    int wipeout;          /* a loss with the encounter's zenmetu flag set */
    int written;          /* the party was written back (backSaveDataFlag) */
    /* Level-up moves (species getWaza) learned (into a free slot, or over
     * the move the forget prompt chose: forgot[] holds it, 0 for a free
     * slot), and those not learned (no prompt bound, or the player gave up). */
    uint16_t learned[C3_GB_PARTY][4], skipped[C3_GB_PARTY][4], forgot[C3_GB_PARTY][4];
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
/* pokemonGetDarkPokemonLevel: 0 (gauge full) .. 6 (empty), 7 not Shadow. */
unsigned c3_general_dark_level(const C3GeneralMon *mon);
/* Re-derive a Pokemon's shown slots from own_moves and its heart gauge
 * (shadow_moves.h). The battle calls it when a Pokemon is built and after
 * every gauge change; a caller that edits dp/shadow/own_* calls it too. */
void c3_general_shadow_moves(C3GeneralMon *mon);
/* Whether the hero may Call the Pokemon at `position` (always for the
 * hero's own side outside link battles; it snaps a Shadow Pokemon out of
 * Hyper Mode or wakes a sleeper). */
int c3_general_battle_can_call(const C3GeneralBattle *battle, unsigned position);
/* fn_8022E6F0 mode 0 for the opponent's opening Pokemon (turn 0 only; once
 * per Pokemon): the partner notices a Shadow Pokemon (src/battle_general.c).
 * c3_battle_story_prepare calls it after naming the partner; later
 * send-outs run the check themselves. */
void c3_general_battle_appear_checks(C3GeneralBattle *battle);
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
    uint16_t skipped[C3_EXP_RECOVERY_MAX];        /* moves not learned (no prompt, or given up) */
    uint16_t forgot[C3_EXP_RECOVERY_MAX];         /* per learned move: the move it replaced, 0 none */
} C3ExpRecovery;
int c3_battle_pack_recover_exp(const C3BattlePack *pack, uint8_t record[C3_GB_POKEMON_SIZE],
                               uint32_t add, C3ExpRecovery *out);

/* The forget-a-move prompt (evolutionWazaLearn's callback, cbWazaForget, and
 * the in-battle fightMenuWazaWasure): asked when a level-up move finds four
 * moves known, with the record as it stands and the new move. It returns
 * the slot 0..3 to forget, or -1 when the player gives up learning. A slot
 * holding an HM (move row +0x12) is refused and counts as giving up. With
 * no prompt bound (host sweeps, tests) the move is not learned and is
 * reported in skipped[]. The writeback and c3_battle_pack_recover_exp ask it
 * level by level, in the original order. */
typedef int (*C3MoveForgetPrompt)(void *context, const C3BattlePack *pack,
                                  const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move);
void c3_general_set_forget_prompt(C3MoveForgetPrompt prompt, void *context);
/* Whether the move is an HM (wazaDataBiosGetHidenFlag, row +0x12): it can't be forgotten. */
int c3_battle_pack_move_is_hm(const C3BattlePack *pack, uint16_t move);
/* pokemonWazaCreate into slot: the move with its full PP and no PP Ups. */
int c3_battle_pack_set_move(const C3BattlePack *pack, uint8_t record[C3_GB_POKEMON_SIZE], unsigned slot, uint16_t move);

/* Whether the hero at `position` may use bag entry `bag` on party member
 * `party` (move: the move slot for a PP-selective item, else ignored) now.
 * `reserved` counts the same entry already chosen by an earlier position
 * this turn. A battle-only item (C3_GB_USE_BATTLE) acts on the Pokemon at
 * `position`, whatever `party` says. */
C3GeneralItemCheck c3_general_battle_item_check(const C3GeneralBattle *battle, unsigned position,
                                                unsigned bag, unsigned party, unsigned move,
                                                unsigned reserved);
/* Whether the bag entry's item is PP-selective (Ether: itemParamGetPPSelectFlag). */
int c3_general_bag_item_selects_move(const C3GeneralBagItem *item);
/* The battle-use class of an item row's battleUseFunc address. */
uint8_t c3_general_bag_use_class(uint32_t battle_use_func);

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
