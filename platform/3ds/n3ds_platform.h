#ifndef COLOSSEUM3DS_N3DS_PLATFORM_H
#define COLOSSEUM3DS_N3DS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* GameCube PAD bit positions; no libctru types cross this interface. */
enum {
    C3_PAD_LEFT = 0x0001, C3_PAD_RIGHT = 0x0002,
    C3_PAD_DOWN = 0x0004, C3_PAD_UP = 0x0008,
    C3_PAD_Z = 0x0010, C3_PAD_R = 0x0020, C3_PAD_L = 0x0040,
    C3_PAD_A = 0x0100, C3_PAD_B = 0x0200,
    C3_PAD_X = 0x0400, C3_PAD_Y = 0x0800, C3_PAD_START = 0x1000
};

typedef struct {
    uint16_t held;
    uint16_t pressed;
    int8_t stick_x, stick_y;
    int8_t cstick_x, cstick_y;
    uint8_t trigger_l, trigger_r;
    /* Bottom-screen stylus in 320x240 pixels; pressed/released are edges.
     * The position of a release is the last held position. */
    uint16_t touch_x, touch_y;
    bool touch_held, touch_pressed, touch_released;
    bool exit_requested;
} C3N3dsInput;

/* Graphics/console are initialized even on failure, so callers can show errors. */
int c3_n3ds_init(char *error, size_t capacity);
bool c3_n3ds_poll(C3N3dsInput *input);
/* Replay menu button edges consumed during a short cooperative render wait. */
void c3_n3ds_defer_buttons(uint16_t pressed);
uint64_t c3_n3ds_time_us(void);
/* Monotonic application time excluding actual APT suspension/sleep. SD/GPU
 * stalls still count; elapsed duration alone is never treated as suspension. */
uint64_t c3_n3ds_active_time_us(void);
/* Actual process heap grants and current linear free space. Call after trace
 * creation and scene allocation; New 3DS detection does not imply a grant. */
void c3_n3ds_log_memory(FILE *trace, const char *stage);
const char *c3_n3ds_data_root(void);
void c3_n3ds_wait_for_exit(void);
void c3_n3ds_shutdown(void);

int c3_n3ds_renderer_init(char *error, size_t capacity);
void c3_n3ds_renderer_draw(float angle_x, float angle_y);
void c3_n3ds_renderer_shutdown(void);

#endif
