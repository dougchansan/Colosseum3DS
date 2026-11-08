#ifndef C3_BATTLE_ENTRY_PACK_H
#define C3_BATTLE_ENTRY_PACK_H

#include "colosseum3ds/battle.h"
#include "colosseum3ds/battle_entry_session.h"

/* Private C3E1 pack produced by tools/prepare_battle_entry.py: original table
 * rows only. The decoded view borrows the caller's bytes; they must stay alive
 * and unchanged while any config built from it is in use. */
enum C3BattleEntryPackSection {
    C3_PACK_ENC, C3_PACK_FLR, C3_PACK_TRN, C3_PACK_SID, C3_PACK_TYP, C3_PACK_KND,
    C3_PACK_SPC, C3_PACK_RST, C3_PACK_AI, C3_PACK_MOV, C3_PACK_NAT, C3_PACK_RAT,
    C3_PACK_MSG, C3_PACK_ITX, C3_PACK_ITM, C3_PACK_GRW, C3_PACK_T44, C3_PACK_SEQ,
    C3_PACK_AKD, C3_PACK_ACT, C3_PACK_VAL, C3_PACK_SECTION_COUNT
};
typedef struct { const uint8_t *bytes; uint32_t size, count; } C3BattleEntryPackView;

#define C3_ENTRY_PACK_MAX_ACTIONS 16u
#define C3_ENTRY_PACK_MAX_SEQUENCES 128u

typedef struct C3BattleEntryPack {
    C3BattleEntryPackView sections[C3_PACK_SECTION_COUNT];
    uint32_t item_count, special_trainer_id;
    /* Decoded original action data/kind rows (refs are original row addresses). */
    C3BattleActionData actions[C3_ENTRY_PACK_MAX_ACTIONS];
    size_t action_count;
    C3BattleActionKind kinds[32];
    size_t kind_count;
    C3BattleSequenceResource sequences[C3_ENTRY_PACK_MAX_SEQUENCES];
    size_t sequence_count;
} C3BattleEntryPack;

/* Validate header, directory, counts and every section extent. Returns 1 on
 * success; error receives a short message (C3_BATTLE_ERROR_SIZE bytes). */
int c3_battle_entry_pack_decode(C3BattleEntryPack *pack, const uint8_t *data,
                                size_t size, char *error);

/* Original message group 0 lookup: GSchar bytes up to and including the
 * terminator. Returns 0 when the key is absent or malformed. */
int c3_battle_entry_pack_message(const C3BattleEntryPack *pack, uint32_t key,
                                 const uint8_t **bytes, size_t *size);

/* Build a session config for a one-trainer-per-side encounter from the pack
 * and the live save body (read only; never written). The body must be the
 * full C3_SAVE_BODY_SIZE image. The attest comes from game-data section 1
 * (body+0), the saved hero from body+0x70 and the preset mode from
 * body+0xE404. seed is the caller's labelled native session seed. The caller
 * still fills config->sequence/grid/visibility/abort scene services. */
int c3_battle_entry_pack_config(const C3BattleEntryPack *pack, uint16_t encounter_id,
                                const uint8_t *save_body, size_t body_size, uint32_t seed,
                                C3BattleEntrySessionConfig *config, char *error);
#endif
