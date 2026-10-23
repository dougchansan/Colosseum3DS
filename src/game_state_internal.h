#ifndef C3_GAME_STATE_INTERNAL_H
#define C3_GAME_STATE_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
/* Private ownership shared only by the game-state service and typed adapters.
 * Never expose this layout or mutable save-body pointers to UI callers. */
typedef struct GameStateImpl {
    uint8_t *body, *card, *pack;
    size_t card_length;
    uint32_t random;
    double play_time;
    unsigned used_backup;
} GameStateImpl;
#endif
