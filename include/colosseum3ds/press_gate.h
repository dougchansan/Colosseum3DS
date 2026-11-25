#ifndef COLOSSEUM3DS_PRESS_GATE_H
#define COLOSSEUM3DS_PRESS_GATE_H
#include <stdint.h>

/* One button press acts in one UI layer. A layer that opens, or that has
 * just consumed a press, closes its gate: buttons down at that moment are
 * ignored until they are released, and only a fresh press (a new edge after
 * the release) reaches the layer. Without it a layer opened in the frame
 * of a press (or from a press replayed after a render wait) would act on a
 * press the player made for the layer before it, without ever having been
 * shown.
 *
 * Call c3_press_gate_take once every frame with that frame's held buttons
 * and edges; it returns the edges the layer may act on. */
typedef struct { uint16_t blocked; } C3PressGate;

/* The layer opened: everything down now (this frame's edges included)
 * belongs to an earlier layer. */
static inline void c3_press_gate_close(C3PressGate *gate) { gate->blocked = 0xFFFFu; }
/* The layer consumed these edges; they stay blocked until released. */
static inline void c3_press_gate_consume(C3PressGate *gate, uint16_t pressed) { gate->blocked |= pressed; }
static inline uint16_t c3_press_gate_take(C3PressGate *gate, uint16_t held, uint16_t pressed)
{
    uint16_t open = (uint16_t)(pressed & ~gate->blocked);
    gate->blocked &= held; /* a released button is free for its next press */
    return open;
}
#endif
