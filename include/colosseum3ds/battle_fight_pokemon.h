#ifndef C3_BATTLE_FIGHT_POKEMON_H
#define C3_BATTLE_FIGHT_POKEMON_H
#include "colosseum3ds/battle_pokemon.h"

#define C3_BATTLE_FIGHT_POKEMON_BYTES 340u

/* Original 802078F0 field reset, preserving every untouched backing byte.
 * A NULL backing is the source no-op and succeeds. Failure (including a busy
 * Pokemon initializer) returns 0 and leaves backing unchanged. */
int c3_battle_fight_pokemon_init(
    uint8_t backing[C3_BATTLE_FIGHT_POKEMON_BYTES]);

/* Original deterministic 80206AEC constructor. source is borrowed immutable
 * storage for all 312 Pokemon bytes. source_reference is a nonzero opaque
 * 32-bit source handle stored BE at backing+0; it is never dereferenced or
 * derived from a host pointer. Only an original-game oracle supplies an actual
 * original guest address here. Native callers must maintain handle identity.
 *
 * NULL backing or NULL source is the original no-op and succeeds, ignoring
 * source_reference and entry_id. Otherwise a zero source_reference or any
 * overlap of source and backing is rejected. Rejecting overlap is an adapter
 * restriction; the original function does not provide snapshot-copy semantics.
 * Success copies all 312 source bytes, stores entry_id with original low16
 * semantics, resets status/flags, and preserves all unwritten padding.
 * Failure changes neither backing nor borrowed source. Serialize borrowed
 * storage for the call. No RNG is used. This does not sort a party, allocate a
 * trainer/sequence, or qualify complete battle entry. */
int c3_battle_fight_pokemon_create(
    uint8_t backing[C3_BATTLE_FIGHT_POKEMON_BYTES],
    const uint8_t source[C3_BATTLE_POKEMON_BYTES],
    uint32_t source_reference, int16_t entry_id);
#endif
