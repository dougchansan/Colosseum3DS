#ifndef C3_BATTLE_H
#define C3_BATTLE_H

#include <stddef.h>
#include <stdint.h>

#define C3_BATTLE_ERROR_SIZE 192u
#define C3_BATTLE_PACK_SIZE 644u
#define C3_BATTLE_POKEMON_SIZE 0x138u
#define C3_BATTLE_PARTY_MAX 6u

/* Original big-endian table records for the admitted encounter 10. These
 * bytes describe a battle; they are not initialized FightPokemon objects.
 * The final roster record is the original species-zero terminator. */
typedef struct C3BattleData {
    uint8_t encounter[56], floor[24], kind[32], type[8];
    uint8_t trainer[2][52], opponent_kind[12], side[2][20];
    uint8_t roster[3][80];
} C3BattleData;

typedef struct C3BattleRosterSummary {
    uint16_t record_id, species;
    uint8_t level;
    int32_t moves[4];
} C3BattleRosterSummary;

typedef struct C3BattleSummary {
    uint16_t encounter_id, floor_data_id, battle_floor;
    uint16_t trainer_id[2], trainer_kind, prize_multiplier;
    uint32_t input_device[2], opponent_name_message;
    uint8_t trainers_per_side, party_limit, active_per_trainer;
    uint8_t lose_returns_to_healing, can_run, can_use_items, grants_experience;
    C3BattleRosterSummary opponent[2];
} C3BattleSummary;

typedef enum C3BattleRequestStatus {
    C3_BATTLE_NO_REQUEST = 0,
    C3_BATTLE_WAITING_BACKEND = 1
} C3BattleRequestStatus;

/* A private in-memory snapshot, never a save format or a claimed outcome.
 * No result/win API is exposed until the genuine combat backend exists. */
typedef struct C3BattleRequest {
    C3BattleRequestStatus status;
    uint32_t native98_args[3];
    C3BattleData data;
    uint8_t player_party[C3_BATTLE_PARTY_MAX][C3_BATTLE_POKEMON_SIZE];
    uint8_t player_party_count;
} C3BattleRequest;

/* Return 1 on successful validation. Failure leaves outputs unchanged. */
int c3_battle_decode(C3BattleData *data, const void *pack, size_t size,
                     char error[C3_BATTLE_ERROR_SIZE]);
int c3_battle_load(C3BattleData *data, const char *path,
                   char error[C3_BATTLE_ERROR_SIZE]);
int c3_battle_summary(const C3BattleData *data, C3BattleSummary *summary);

/* Snapshot the original GSVM call and the current original save-body party.
 * Success means WAITING_BACKEND, never "battle completed". Keep the caller's
 * GSVM native pending. This function performs no RNG, HP, reward, save, flag,
 * or scene writes, and accepts no synthetic victory result. */
int c3_battle_request(C3BattleRequest *request, const C3BattleData *data,
                      const uint32_t native98_args[3],
                      const void *save_body, size_t save_body_size,
                      char error[C3_BATTLE_ERROR_SIZE]);

#endif
