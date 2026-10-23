#ifndef C3_BATTLE_PREVIEW_H
#define C3_BATTLE_PREVIEW_H

#include "colosseum3ds/battle_init.h"

/* Experimental, nonpersistent encounter-10 combat. This is deliberately not
 * the original FightPokemon ABI or an authoritative GSVM battle result. */
#define C3_PREVIEW_PACK_SIZE 1024u
#define C3_PREVIEW_MOVES 12u
#define C3_PREVIEW_MON_COUNT 4u
#define C3_PREVIEW_EVENT_LIMIT 64u
#define C3_PREVIEW_MAX_TURNS 4096u

typedef struct { uint8_t bytes[C3_PREVIEW_PACK_SIZE]; } C3BattlePreviewTables;
typedef struct {
    uint16_t id, power, effect;
    uint8_t pp, type, range, accuracy, secondary, snatchable;
    int8_t priority;
} C3BattlePreviewMove;
typedef struct {
    uint16_t species, hp, maximum_hp, stats[C3_STAT_COUNT], friendship;
    uint8_t level, ability, types[2];
    C3BattleInitialMove moves[4];
    /* Attack, Defense, Sp.Atk, Sp.Def, Speed, Accuracy, Evasion, -6..+6. */
    int8_t stages[7];
    uint8_t confusion, taunt, flinched, helped, snatch;
} C3BattlePreviewMon;
typedef struct { uint8_t slot, target; } C3BattlePreviewCommand;
typedef enum {
    C3_PREVIEW_RUNNING = 1, C3_PREVIEW_PLAYER_WON,
    C3_PREVIEW_PLAYER_LOST, C3_PREVIEW_DRAW
} C3BattlePreviewOutcome;
#define C3_PREVIEW_NO_SLOT 255u
typedef enum {
    C3_PREVIEW_EVENT_MESSAGE = 1, C3_PREVIEW_EVENT_INTRO,
    C3_PREVIEW_EVENT_SEND_OUT, C3_PREVIEW_EVENT_TURN,
    C3_PREVIEW_EVENT_MOVE, C3_PREVIEW_EVENT_HP,
    C3_PREVIEW_EVENT_FAINT, C3_PREVIEW_EVENT_STATUS,
    C3_PREVIEW_EVENT_MISS, C3_PREVIEW_EVENT_CRITICAL,
    C3_PREVIEW_EVENT_OUTCOME
} C3BattlePreviewEventKind;
typedef struct {
    C3BattlePreviewEventKind kind;
    uint32_t sequence, group;
    uint8_t actor, target, target_mask;
    uint16_t move, effect;
    uint16_t hp_before[4];
    uint8_t pp[4][4];
    int8_t stages[4][7];
    uint8_t confusion[4], taunt[4], reflect[2];
    C3BattlePreviewOutcome outcome;
    char text[128];
    uint16_t hp[4];
    uint64_t draws;
} C3BattlePreviewEvent;
typedef struct {
    uint32_t magic, seed, turn;
    C3BattlePreviewOutcome outcome;
    C3BattleRng rng;
    C3BattlePreviewMon mons[4];
    uint8_t reflect[2];
    C3BattleRequest original_request; /* Immutable: retained for comparison. */
    uint32_t opponent_trainer_id;
    /* Event context is committed with the private turn; never drives RNG. */
    uint32_t event_sequence, event_group;
    uint8_t event_actor, event_target;
    uint16_t event_move, event_effect;
    uint32_t event_count;
    C3BattlePreviewEvent events[C3_PREVIEW_EVENT_LIMIT];
} C3BattlePreview;

int c3_battle_preview_decode(C3BattlePreviewTables *out, const void *bytes,
                             size_t length, char *error);
int c3_battle_preview_load(C3BattlePreviewTables *out, const char *path, char *error);
int c3_battle_preview_move(const C3BattlePreviewTables *tables, uint16_t id,
                           C3BattlePreviewMove *out);
/* Uses an explicitly separate diagnostic seed. Generates the trainer ID with
 * two low-first draws, then invokes the existing roster initializer. It does
 * not claim to recover the original shared game's battle-entry RNG state. */
int c3_battle_preview_begin(C3BattlePreview *out, const C3BattleRequest *request,
                            const C3BattleInitTables *init,
                            const C3BattlePreviewTables *tables,
                            uint32_t diagnostic_seed, char *error);
/* Opponent-only targets for attacks; Helping Hand automatically uses the
 * ally. slot=4 is legal only when no usable move remains (Struggle).
 * No items, switching, capture, Call, rewards, or save writes are exposed.
 * Failed begin/turn operations preserve outputs and RNG. Serialize calls and
 * do not overlap outputs with inputs or error storage. */
uint8_t c3_battle_preview_legal_moves(const C3BattlePreview *battle,
                                     const C3BattlePreviewTables *tables,
                                     unsigned actor);
int c3_battle_preview_turn(C3BattlePreview *battle,
                           const C3BattlePreviewTables *tables,
                           const C3BattlePreviewCommand player[2], char *error);
const char *c3_battle_preview_name(unsigned actor);
const char *c3_battle_preview_move_name(uint16_t id);

/* Compiles the original fn_80232110 arithmetic through bounded scalar
 * accessors. Input is a supported preview state, not original battle memory.
 * critical is 1 or 2. Returns the pre-STAB/pre-random damage. */
int c3_battle_preview_base_damage(const C3BattlePreview *battle,
                                  const C3BattlePreviewTables *tables,
                                  unsigned attacker, unsigned defender,
                                  uint16_t move, uint16_t power,
                                  unsigned type, unsigned critical,
                                  uint32_t *damage);
#endif
