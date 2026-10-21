#include "n3ds_platform.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

static bool graphics_started;
static uint16_t previous_buttons;

static uint16_t map_buttons(uint32_t keys)
{
    uint16_t result = 0;
    if (keys & KEY_DLEFT) result |= C3_PAD_LEFT;
    if (keys & KEY_DRIGHT) result |= C3_PAD_RIGHT;
    if (keys & KEY_DDOWN) result |= C3_PAD_DOWN;
    if (keys & KEY_DUP) result |= C3_PAD_UP;
    if (keys & (KEY_ZL | KEY_ZR)) result |= C3_PAD_Z;
    if (keys & KEY_R) result |= C3_PAD_R;
    if (keys & KEY_L) result |= C3_PAD_L;
    if (keys & KEY_A) result |= C3_PAD_A;
    if (keys & KEY_B) result |= C3_PAD_B;
    if (keys & KEY_X) result |= C3_PAD_X;
    if (keys & KEY_Y) result |= C3_PAD_Y;
    if (keys & KEY_START) result |= C3_PAD_START;
    return result;
}

static int8_t normalize_axis(int raw)
{
    /* Circle Pad calibrated range is about +/-156. Apply a small dead zone. */
    const int dead_zone = 12;
    const int maximum = 156;
    int sign = raw < 0 ? -1 : 1;
    int magnitude = raw < 0 ? -raw : raw;
    if (magnitude <= dead_zone) return 0;
    if (magnitude > maximum) magnitude = maximum;
    return (int8_t)(sign * (magnitude - dead_zone) * 127 / (maximum - dead_zone));
}

int c3_n3ds_init(char *error, size_t capacity)
{
    bool is_new = false;
    Result result;
    gfxInitDefault();
    graphics_started = true;
    gfxSet3D(false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_BOTTOM, NULL);
    previous_buttons = 0;
    result = APT_CheckNew3DS(&is_new);
    if (R_FAILED(result)) {
        snprintf(error, capacity, "New 3DS detection failed: 0x%08lX", (unsigned long)result);
        return -1;
    }
    if (!is_new) {
        snprintf(error, capacity, "A New 3DS/XL or New 2DS XL is required.");
        return -1;
    }
    /* This is a request to the system; it is not a measured clock-rate claim. */
    osSetSpeedupEnable(true);
    if (capacity) error[0] = '\0';
    return 0;
}

bool c3_n3ds_poll(C3N3dsInput *input)
{
    uint32_t keys;
    circlePosition circle = {0}, cstick = {0};
    if (!input || !aptMainLoop()) return false;
    hidScanInput(); /* libctru also scans IRRST for New 3DS extra inputs. */
    keys = hidKeysHeld();
    hidCircleRead(&circle);
    irrstCstickRead(&cstick);
    memset(input, 0, sizeof(*input));
    input->held = map_buttons(keys);
    input->pressed = (uint16_t)(input->held & ~previous_buttons);
    previous_buttons = input->held;
    input->stick_x = normalize_axis(circle.dx);
    input->stick_y = normalize_axis(circle.dy);
    input->cstick_x = normalize_axis(cstick.dx);
    input->cstick_y = normalize_axis(cstick.dy);
    input->trigger_l = (keys & KEY_L) ? 255 : 0;
    input->trigger_r = (keys & KEY_R) ? 255 : 0;
    input->exit_requested = (keys & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT);
    return !input->exit_requested;
}

uint64_t c3_n3ds_time_us(void)
{
    uint64_t ticks = svcGetSystemTick();
    /* svcGetSystemTick uses the base clock even with New 3DS speedup enabled. */
    return (ticks / SYSCLOCK_ARM11) * UINT64_C(1000000)
         + (ticks % SYSCLOCK_ARM11) * UINT64_C(1000000) / SYSCLOCK_ARM11;
}

const char *c3_n3ds_data_root(void)
{
    return "sdmc:/3ds/Colosseum3DS/data";
}

void c3_n3ds_wait_for_exit(void)
{
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

void c3_n3ds_shutdown(void)
{
    if (graphics_started) {
        osSetSpeedupEnable(false);
        gfxExit();
        graphics_started = false;
    }
}
