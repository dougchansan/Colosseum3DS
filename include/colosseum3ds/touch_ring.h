#ifndef COLOSSEUM3DS_TOUCH_RING_H
#define COLOSSEUM3DS_TOUCH_RING_H
#include <stdint.h>

/* HID shared memory keeps the last eight touchscreen samples in a ring
 * (section at word 42: s64 tick when entry 0 was written, s64 the tick of
 * the cycle before, u32 newest index, then eight {x | y<<16, valid} word
 * pairs at word 8). libctru's hidScanInput reads only the newest sample,
 * once per frame, so at ~17 field frames per second a tap lifted between
 * two frames is never seen. Folding the samples written since the previous
 * poll recovers it. */
typedef struct {
    int64_t tick0, tick1;
    uint32_t index;
    uint32_t entry[8][2];
} C3TouchSection;

typedef struct {
    int64_t newest;   /* time of the newest sample already folded; 0 none */
    uint16_t x, y;    /* last reported position */
    uint8_t held;
} C3TouchRing;

typedef struct {
    uint16_t x, y;
    uint8_t held, pressed, released;
} C3TouchEdges;

/* Edges for this poll. `held_now`/`x_now`/`y_now` are the newest state as
 * libctru reports it; `section` may be NULL (current state only). A touch
 * that began and ended since the previous poll reports pressed and released
 * together, at its first sample's position. A release reports the last
 * touched position. */
void c3_touch_ring_fold(C3TouchRing *ring, const C3TouchSection *section,
                        int held_now, uint16_t x_now, uint16_t y_now,
                        C3TouchEdges *out);
#endif
