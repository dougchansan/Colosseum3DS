#ifndef COLOSSEUM3DS_GAME_STATE_H
#define COLOSSEUM3DS_GAME_STATE_H

#include <stddef.h>
#include <stdint.h>

#define C3_GAME_NAME_MAX 7u
#define C3_GAME_ERROR_SIZE 192u
#define C3_GAME_PARTY_COUNT 6u

/* Owned original GC6E01 save bytes and prepared retail data. Initialize to
 * {0}; free before discarding. Functions return 1 on success, 0 on failure.
 * The original big-endian payload remains authoritative: imported fields
 * outside a requested change are preserved, including unknown fields. */
typedef struct C3GameState { void *impl; } C3GameState;

typedef struct C3GamePokemon {
    uint16_t species;
    uint8_t level;
    uint16_t hp, max_hp;
    uint32_t personality;
    uint32_t experience;
    uint16_t shadow_id;
} C3GamePokemon;

typedef struct C3GameSummary {
    char name[32],partner[32];
    uint32_t trainer_id, money, play_seconds, saved_count;
    uint32_t floor, previous_floor;
    uint8_t spawn;
    uint8_t sound_mono;       /* Original audio option: 0 stereo, 1 mono. */
    uint8_t rumble_disabled;  /* Original no-vibration option: 0 on, 1 off. */
    uint8_t party_count;
    uint8_t used_backup;
    C3GamePokemon party[C3_GAME_PARTY_COUNT];
} C3GameSummary;

void c3_game_state_free(C3GameState *state);

/* Decode a genuine raw card file/GCI. prepared_path may be NULL when only
 * inspecting, loading, or saving an existing file. Flag access requires the
 * prepared flag definitions. On failure an existing state is unchanged. */
int c3_game_state_decode(C3GameState *state, const void *image, size_t length,
                         const char *prepared_path,
                         char error[C3_GAME_ERROR_SIZE]);
int c3_game_state_load(C3GameState *state, const char *path,
                       const char *prepared_path,
                       char error[C3_GAME_ERROR_SIZE]);

/* Reproduce the original new-game initialization using privately prepared
 * retail tables, strings, and card artwork. NULL/empty name means WES.
 * Accepted custom names contain 1..7 printable ASCII characters and may not
 * consist entirely of spaces. seed supplies the original 32-bit RNG state.
 * This replaces only the in-memory state, and never writes a save file. */
int c3_game_state_new(C3GameState *state, const char *name, uint32_t seed,
                      const char *prepared_path,
                      char error[C3_GAME_ERROR_SIZE]);

int c3_game_state_summary(const C3GameState *state, C3GameSummary *summary);
/* Reviewed native roster: ordinary Umbreon/Espeon use original growth bank 0.
 * Returns 0 for unsupported species/shadow records/levels, without guessing a
 * growth curve. At level 100 the valid range is zero (level cap). */
int c3_game_pokemon_experience(const C3GamePokemon *pokemon,uint32_t *progress,uint32_t *range);
typedef struct { uint16_t id,order; uint8_t read; } C3GameMail;
typedef struct { uint16_t species,raw; uint32_t trainer_id,personality; } C3GameMemo;
typedef struct { uint16_t id,quantity; } C3GameItem;
typedef struct { uint32_t id,value; } C3GameFlagChange;
typedef struct { uint16_t id,unlock_flag,level_flag; uint8_t level; } C3GameMailHint;
#define C3_GAME_MAIL_FLAG_MAX 40u
#define C3_GAME_MAIL_HINT_MAX 32u
typedef struct {
    uint16_t id,tone;
    unsigned flag_count,hint_count;
    C3GameFlagChange flags[C3_GAME_MAIL_FLAG_MAX];
    C3GameMailHint hints[C3_GAME_MAIL_HINT_MAX];
} C3GameMailRead;
int c3_game_state_mail_count(const C3GameState *state,unsigned *count);
int c3_game_state_mail(const C3GameState *state,unsigned index,C3GameMail *mail);
int c3_game_state_memo_count(const C3GameState *state,unsigned *count);
int c3_game_state_memo(const C3GameState *state,unsigned index,C3GameMemo *memo);
int c3_game_state_item(const C3GameState *state,unsigned pocket,unsigned slot,C3GameItem *item);
int c3_game_state_pokemon(const C3GameState *state,unsigned slot,uint8_t record[0x138]);
/* Native mail read transaction follows mailMain fn_801D228C; original flag
 * metadata comes from the admitted private PDA pack. No disk I/O. */
int c3_game_state_mail_read(C3GameState *state,const C3GameMailRead *action,int commit);
int c3_game_state_mail_preferences(C3GameState *state,int sort,int tone);
int c3_game_state_mail_settings(const C3GameState *state,unsigned *sort,unsigned *tone);
/* Inventory-only optimistic transaction. Region-wise conflicts reject stale
 * choices while preserving elapsed play time and all unrelated save bytes. */
int c3_game_state_inventory_commit(C3GameState *state,const uint8_t *before,const uint8_t *after,size_t bytes);
const uint8_t *c3_game_state_body(const C3GameState *state, size_t *length);

/* Retail resume semantics store a floor/previous-floor/spawn index, not an
 * arbitrary player position. Changing these fields does not run scripts. */
int c3_game_state_set_floor(C3GameState *state, uint32_t floor,
                            uint32_t previous_floor, uint8_t spawn);
/* The partner-present byte of the hero record (heroSetStatus selector 0x18,
 * hero +0xA98), written by script native 23 when the partner joins. */
int c3_game_state_set_partner(C3GameState *state, int present);
int c3_game_state_partner(const C3GameState *state);
/* The partner's name (hero +0xAC2, ten characters and a terminator).
 * Accepts 1..10 printable ASCII characters. */
int c3_game_state_set_partner_name(C3GameState *state, const char *name);
int c3_game_state_set_options(C3GameState *state, uint8_t sound_mono,
                              uint8_t rumble_disabled);
int c3_game_state_add_play_time(C3GameState *state, double seconds);

/* Source-defined packed flag access. Writes are clipped to the definition's
 * bit width, like the original value setter. Script/event dispatch remains
 * the caller's responsibility. Definition IDs and reserved types are checked. */
int c3_game_state_get_flag(const C3GameState *state, uint32_t id,
                           uint32_t *value);
int c3_game_state_set_flag(C3GameState *state, uint32_t id, uint32_t value);

/* Explicit user save: increment the original inner/outer save counters,
 * encode the full preserved payload, and retain the previous verified card
 * using the save service's recoverable .tmp/.bak protocol. Failed stores
 * leave the in-memory payload/counters unchanged. The parent directory must
 * already exist. Caller must serialize save operations. */
int c3_game_state_save(C3GameState *state, const char *path,
                       char error[C3_GAME_ERROR_SIZE]);

#endif
