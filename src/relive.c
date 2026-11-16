#include "colosseum3ds/relive.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* PokemonBios offsets of the save record. */
enum { SPECIES = 0x00, SHADOW_ID = 0xD8, DP = 0xDC };

int c3_relive_is_shadow(const uint8_t r[C3_GB_POKEMON_SIZE])
{
    return r && be16(r + SHADOW_ID) && (int32_t)be32(r + DP) >= 0;
}

uint32_t c3_relive_purify_flag(uint16_t id)
{
    return id >= 1 && id <= 0x60 ? 0x4EEu + id - 1u : 0u;
}

int c3_relive_pickable(const uint8_t r[C3_GB_POKEMON_SIZE])
{
    return c3_relive_is_shadow(r) && be32(r + DP) == 0;
}

static int has_item(const C3GameState *state, unsigned id)
{
    for (unsigned pocket = 0; pocket < 6; ++pocket)
        for (unsigned slot = 0; slot < 64; ++slot) {
            C3GameItem item;
            if (!c3_game_state_item(state, pocket, slot, &item)) break;
            if (item.id == id && item.quantity) return 1;
        }
    return 0;
}

int c3_relive_counts(const C3GameState *state, C3ReliveCounts *out)
{
    C3GameSummary summary;
    C3ReliveCounts c = {0};
    if (!state || !out || !c3_game_state_summary(state, &summary)) return 0;
    for (unsigned i = 0; i < summary.party_count && i < 6; ++i) {
        uint8_t r[C3_GB_POKEMON_SIZE];
        uint32_t flag = 0;
        if (!c3_game_state_pokemon(state, i, r) || !be16(r + SPECIES) || !c3_relive_is_shadow(r)) continue;
        uint32_t id = c3_relive_purify_flag(be16(r + SHADOW_ID));
        /* fn_801EEC74: an id outside the table reads as not purified. */
        if (!id || !c3_game_state_get_flag(state, id, &flag) || !flag) ++c.remaining;
        if (be32(r + DP) == 0) ++c.ready;
    }
    c.has_item = has_item(state, C3_RELIVE_ITEM);
    *out = c;
    return 1;
}

int c3_relive_action(int mode, const C3ReliveCounts *c)
{
    if (!c) return C3_RELIVE_NONE;
    int base = c->has_item ? (c->remaining ? (c->ready ? 2 : 1) : 3) : (c->ready ? 4 : 5), action = base;
    if (mode == 0) {
        if (base == 1 || base == 3) action = 5;
        else if (base == 2) action = 4;
    } else if (mode == 1) action = 2;
    if (action == 4 && !c->ready) return C3_RELIVE_NONE; /* preReliveMain: no heart-opened Pokemon, reset */
    return action == 2 ? C3_RELIVE_PURIFY : action == 4 ? C3_RELIVE_PICK : C3_RELIVE_NONE;
}

