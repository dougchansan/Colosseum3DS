#ifndef C3_LINK_BATTLE_H
#define C3_LINK_BATTLE_H

#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/link_protocol.h"
#include <stdint.h>

/* Lockstep link battles over a C3LinkSession (docs/link-play.md).
 * Nothing here touches libctru; the host tests run two and four consoles
 * over the loopback transport. */

typedef enum { C3_LINK_MODE_BATTLE_NOW = 3, C3_LINK_MODE_COLOSSEUM = 2 } C3LinkMode; /* m_eBattleMode values */
typedef enum { C3_LINK_SINGLE = 0, C3_LINK_DOUBLE = 1, C3_LINK_MULTI = 2 } C3LinkBattleType; /* m_eBattleType */

enum {
    C3_LINK_MSG_SETUP = C3_LINK_MSG_USER, C3_LINK_MSG_TEAM, C3_LINK_MSG_REFUSE,
    C3_LINK_MSG_COMMAND, C3_LINK_MSG_TURN, C3_LINK_MSG_HASH, C3_LINK_MSG_DESYNC,
    C3_LINK_MSG_TRADE_OFFER = C3_LINK_MSG_USER + 16, C3_LINK_MSG_TRADE_CONFIRM, C3_LINK_MSG_TRADE_CANCEL
};

/* Rule presets, menuRulePresets (menu_range_8007109C.c:1446). */
typedef struct C3LinkRule {
    uint16_t min_level, max_level, level_sum, min_count;
    uint8_t duplicate_species, duplicate_items;
} C3LinkRule;
extern const C3LinkRule c3_link_rule_presets[3];
/* fn_80077DB8: Pokemon each player brings. */
unsigned c3_link_team_size(C3LinkMode mode, C3LinkBattleType type, unsigned rule);
/* What the port uses: the original's count, except a multi battle with
 * rule 0 (six each, twelve per side) is capped at three each so a side fits
 * the engine's six-Pokemon party (docs/link-play.md, approximations). */
unsigned c3_link_team_count(C3LinkMode mode, C3LinkBattleType type, unsigned rule);
/* The fewest a player may bring: the whole-party case (six, rule 0) takes
 * the rule's minimum count (s16 @6, 1 for preset 0); otherwise exactly
 * c3_link_team_count. */
unsigned c3_link_team_minimum(C3LinkMode mode, C3LinkBattleType type, unsigned rule);

/* fn_80076398 / fn_80076F2C checks on one player's chosen team of `count`
 * 312-byte records, which must number minimum..maximum. Returns 1 when
 * legal; otherwise 0 and the reason. */
int c3_link_rules_check(const C3BattlePack *pack, const C3LinkRule *rule, const uint8_t *records,
                        unsigned count, unsigned minimum, unsigned maximum, char *reason, size_t capacity);
/* A party's first legal members for a rule (eggs, Shadow Pokemon, item 0xAF
 * and over-level members skipped; duplicates skipped when the rule forbids
 * them). Returns how many were copied into out (count x 312 bytes). */
unsigned c3_link_pick_team(const C3BattlePack *pack, const C3LinkRule *rule, const uint8_t party[6][C3_GB_POKEMON_SIZE],
                           unsigned wanted, uint8_t *out);

typedef enum {
    C3_LB_SETUP = 0, C3_LB_TEAMS, C3_LB_COMMANDS, C3_LB_WAITING, C3_LB_FINISHED, C3_LB_FAILED
} C3LinkBattlePhase;

typedef struct C3LinkBattle {
    C3LinkSession *session;
    const C3BattlePack *pack;
    C3LinkBattlePhase phase;
    uint8_t mode, type, rule, order, players;
    uint32_t seed, stage, pack_hash;
    uint32_t preset[C3_LINK_MAX_PLAYERS];      /* Battle Now: each player's trainer encounter */
    uint8_t side_of[C3_LINK_MAX_PLAYERS], slot_of[C3_LINK_MAX_PLAYERS];
    uint8_t team_count[C3_LINK_MAX_PLAYERS], team_in[C3_LINK_MAX_PLAYERS];
    uint8_t team[C3_LINK_MAX_PLAYERS][C3_GB_PARTY][C3_GB_POKEMON_SIZE];
    char names[C3_LINK_MAX_PLAYERS][C3_LINK_NAME_SIZE];
    C3GeneralBattle battle;                    /* canonical: identical on every console */
    /* host: commands gathered for the turn being built */
    C3GeneralCommand gathered[4];
    uint8_t have[4];
    uint32_t hash_turn, hashes[C3_LINK_MAX_PLAYERS];
    uint8_t hash_have[C3_LINK_MAX_PLAYERS];
    uint32_t turns_applied, last_hash;
    int new_turn;                              /* set when a turn was applied; cleared by the caller */
    char error[C3_GB_ERROR_SIZE];
} C3LinkBattle;

/* Host: choose the battle and tell everyone. Battle Now draws each
 * player's preset team (different trainers) from the pack with seed;
 * Colosseum waits for every player's team. players: 2, or 4 for multi. */
int c3_link_battle_host(C3LinkBattle *lb, C3LinkSession *s, const C3BattlePack *pack, C3LinkMode mode,
                        C3LinkBattleType type, unsigned rule, unsigned order, uint32_t stage, uint32_t seed);
/* Client: wait for the host's setup. */
int c3_link_battle_join(C3LinkBattle *lb, C3LinkSession *s, const C3BattlePack *pack);
/* Colosseum: this console's team (already rule-checked). */
int c3_link_battle_offer_team(C3LinkBattle *lb, const uint8_t *records, unsigned count);
/* Process link messages. Returns 0 when the battle failed (lb->error). */
int c3_link_battle_update(C3LinkBattle *lb);
/* Positions (bitmask over 0..3, canonical numbering) this console commands. */
unsigned c3_link_battle_local_positions(const C3LinkBattle *lb);
unsigned c3_link_battle_viewer(const C3LinkBattle *lb);
/* This console's commands for its positions (others ignored). */
int c3_link_battle_submit(C3LinkBattle *lb, const C3GeneralCommand commands[4]);
/* A first admitted encounter on a story Colosseum floor, for the stage. */
uint32_t c3_link_default_stage(const C3BattlePack *pack);
uint32_t c3_link_pack_hash(const C3BattlePack *pack);

#endif
