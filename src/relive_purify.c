/* reliveCeremonyAll (pokemon_relive.c), split from relive.c so the field
 * story links the decision without the battle pack. */
#include "colosseum3ds/relive.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* PokemonBios offsets of the save record. */
enum { SPECIES = 0x00, MOVE0 = 0x78, FRIEND = 0xB0, NATIONAL_RIBBON = 0xC6, SHADOW_ID = 0xD8, DP = 0xDC,
       POOL_EXP = 0xE0, POOL_FRIEND = 0xE4, STATUS_ENTRY = 0xE8 };

int c3_relive_purify(C3GameState *state, unsigned slot, int forced, const C3BattlePack *pack, C3ReliveResult *out)
{
    uint8_t r[C3_GB_POKEMON_SIZE];
    C3ReliveResult o;
    memset(&o, 0, sizeof o);
    if (!state || !out || !pack || slot >= 6 || !c3_game_state_pokemon(state, slot, r) || !be16(r + SPECIES)) return 0;
    if (forced) put32(r + DP, 0);              /* reliveMain mode 1: pokemonSetStatus(0xC5, 0) */
    if (!c3_relive_is_shadow(r)) return 0;
    o.species = be16(r + SPECIES); o.shadow_id = be16(r + SHADOW_ID);
    uint32_t flag = c3_relive_purify_flag(o.shadow_id);
    /* fn_80121ADC / fn_80121B4C(0x3E): the status entry (attest2, +0xE8)
     * holding status 0x3E is cleared, padding kept. */
    if (be16(r + STATUS_ENTRY) == 0x3E) {
        uint8_t *e = r + STATUS_ENTRY;
        memset(e, 0, 7);           /* id, sub_id, byte4, byte5, count */
        memset(e + 8, 0, 6);       /* value, half_c */
        o.status_cleared = 1;
    }
    put32(r + DP, (uint32_t)-100);  /* pokemonSetDp(-1.0f): status 0xC5 = -100 */
    o.move0 = be16(r + MOVE0);      /* pokemonBiosGetPokemonWazaDataId(p, 0), no longer Shadow */
    o.pool_exp = be32(r + POOL_EXP);
    o.friendship_before = be16(r + FRIEND);
    unsigned friend = o.friendship_before + be16(r + POOL_FRIEND) + 0x46u;
    put16(r + FRIEND, friend > 0xFF ? 0xFF : friend);  /* pokemonBiosSetFriend clamps at 255 */
    put16(r + POOL_FRIEND, 0);
    o.friendship_after = be16(r + FRIEND);
    if (!c3_battle_pack_recover_exp(pack, r, o.pool_exp, &o.exp)) return 0;
    r[NATIONAL_RIBBON] = 1;          /* exribbonSetNarionalRibbon -> pokemonBiosSetNationalRibbon(1) */
    /* fn_801EECD8(darkId, 1): the Shadow table's purify flag, written with
     * the record so a failure leaves neither changed. */
    uint32_t old_flag = 0;
    if (flag && (!c3_game_state_get_flag(state, flag, &old_flag) || !c3_game_state_set_flag(state, flag, 1))) return 0;
    if (!c3_game_state_set_pokemon(state, slot, r)) { if (flag) (void)c3_game_state_set_flag(state, flag, old_flag); return 0; }
    *out = o;
    return 1;
}
