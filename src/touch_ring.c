#include "colosseum3ds/touch_ring.h"
#include <string.h>

void c3_touch_ring_fold(C3TouchRing *ring, const C3TouchSection *s,
                        int held_now, uint16_t x_now, uint16_t y_now,
                        C3TouchEdges *out)
{
    uint32_t first = 0, last = 0;
    int any = 0;
    if (!ring || !out) return;
    memset(out, 0, sizeof *out);
    /* Entry j of the current cycle was written at tick0 + j*period; entries
     * after the newest index still hold the previous cycle's samples. */
    if (s && s->index < 8 && s->tick1 > 0 && s->tick0 > s->tick1) {
        int64_t period = (s->tick0 - s->tick1) / 8;
        if (period > 0) {
            int64_t newest = s->tick0 + (int64_t)s->index * period;
            unsigned count = 0;
            if (ring->newest && newest > ring->newest) {
                int64_t steps = (newest - ring->newest + period / 2) / period;
                count = steps > 8 ? 8u : (unsigned)steps;
            }
            for (unsigned k = count; k-- > 0;) {
                const uint32_t *e = s->entry[(s->index + 8u - k) & 7u];
                if (!e[1]) continue;
                if (!any) first = e[0];
                last = e[0];
                any = 1;
            }
            ring->newest = newest;
        }
    }
    if (!ring->held) {
        if (held_now) { out->pressed = 1; ring->x = x_now; ring->y = y_now; }
        else if (any) {
            out->pressed = out->released = 1;
            ring->x = (uint16_t)(first & 0xffffu); ring->y = (uint16_t)(first >> 16);
        }
    } else if (held_now) { ring->x = x_now; ring->y = y_now; }
    else {
        out->released = 1;
        if (any) { ring->x = (uint16_t)(last & 0xffffu); ring->y = (uint16_t)(last >> 16); }
    }
    ring->held = held_now ? 1u : 0u;
    out->held = ring->held;
    out->x = ring->x; out->y = ring->y;
}
