#ifndef C3_LINK_TRADE_H
#define C3_LINK_TRADE_H

#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/link_protocol.h"
#include <stddef.h>
#include <stdint.h>

/* Trading between two consoles in a link session (docs/link-play.md). The
 * original traded with Ruby/Sapphire over the GBA cable; the port trades
 * between two Colosseum3DS saves with the same rules and the same 312-byte
 * party record. Nothing here writes a save: the caller's explicit Save
 * does. Message types C3_LINK_MSG_TRADE_* are in link_battle.h. */
#include "colosseum3ds/link_battle.h"

/* ---- trading (docs/link-play.md "Trading") ------------------------------
 * fn_8002EE74 / fn_8002EA5C checks on offering party slot `slot`. */
int c3_link_trade_check(const uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot, char *reason, size_t capacity);
/* After both confirmed: the received record goes into the slot given away,
 * with friendship 70 (0x46) as the original sets on completion. */
void c3_link_trade_apply(uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot, const uint8_t received[C3_GB_POKEMON_SIZE]);

typedef enum { C3_LT_CHOOSE = 0, C3_LT_OFFERED, C3_LT_CONFIRMED, C3_LT_DONE, C3_LT_CANCELLED, C3_LT_FAILED } C3LinkTradePhase;
typedef struct C3LinkTrade {
    C3LinkSession *session;
    C3LinkTradePhase phase;
    uint8_t partner, my_slot, their_slot, have_offer, partner_confirmed, i_confirmed;
    uint8_t mine[C3_GB_POKEMON_SIZE], theirs[C3_GB_POKEMON_SIZE];
    uint32_t my_hash, their_hash;
    char error[C3_GB_ERROR_SIZE];
} C3LinkTrade;
int c3_link_trade_begin(C3LinkTrade *t, C3LinkSession *s, uint8_t partner);
/* Offer a party slot (checked first); 0 with t->error when refused. */
int c3_link_trade_offer(C3LinkTrade *t, const uint8_t party[6][C3_GB_POKEMON_SIZE], unsigned slot);
/* Confirm once the partner's offer is shown (t->have_offer). */
int c3_link_trade_confirm(C3LinkTrade *t);
void c3_link_trade_cancel(C3LinkTrade *t);
/* Process messages; when both confirmed, applies the swap to party (in
 * memory only) and sets phase DONE. */
int c3_link_trade_update(C3LinkTrade *t, uint8_t party[6][C3_GB_POKEMON_SIZE]);
#endif
