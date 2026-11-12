#include "n3ds_platform.h"
#include "stage_renderer.h"
#include "bottom_screen.h"

#include <3ds.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

static bool graphics_started;
static bool console_is_new;
static uint16_t previous_buttons;
static uint16_t deferred_buttons;
static aptHookCookie clock_hook;
static unsigned clock_pause_mask;
static uint64_t clock_pause_start, clock_paused_us;
static LightLock clock_lock;

static void application_clock_hook(APT_HookType hook, void *context)
{
    unsigned previous_mask;
    uint64_t now = c3_n3ds_time_us();
    (void)context;
    LightLock_Lock(&clock_lock);
    previous_mask = clock_pause_mask;
    if (hook == APTHOOK_ONSUSPEND) clock_pause_mask |= 1u;
    else if (hook == APTHOOK_ONRESTORE) clock_pause_mask &= ~1u;
    else if (hook == APTHOOK_ONSLEEP) clock_pause_mask |= 2u;
    else if (hook == APTHOOK_ONWAKEUP) clock_pause_mask &= ~2u;
    if (!previous_mask && clock_pause_mask) clock_pause_start = now;
    if (previous_mask && !clock_pause_mask) clock_paused_us += now - clock_pause_start;
    LightLock_Unlock(&clock_lock);
}

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
    deferred_buttons = 0;
    LightLock_Init(&clock_lock);
    clock_pause_mask = 0;
    clock_pause_start = clock_paused_us = 0;
    aptHook(&clock_hook, application_clock_hook, NULL);
    result = APT_CheckNew3DS(&is_new);
    if (R_FAILED(result)) {
        snprintf(error, capacity, "New 3DS detection failed: 0x%08lX", (unsigned long)result);
        return -1;
    }
    /* An original 3DS/2DS runs too: slower (268 MHz, no L2) and with less
     * application memory, so the 30 fps target applies to New models only.
     * The model is recorded in every memory trace line. */
    console_is_new = is_new;
    /* This is a request to the system; it is not a measured clock-rate claim. */
    osSetSpeedupEnable(true);
    c3_bottom_screen_init();
    if (capacity) error[0] = '\0';
    return 0;
}

static uint16_t previous_touch_x, previous_touch_y, stick_armed_direction;
static bool previous_touch;

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
    input->pressed = (uint16_t)((input->held & ~previous_buttons) | deferred_buttons);
    deferred_buttons = 0;
    previous_buttons = input->held;
    input->stick_x = normalize_axis(circle.dx);
    input->stick_y = normalize_axis(circle.dy);
    /* Menus read direction edges from `pressed`. A Circle Pad push past 80
     * of 127 adds one D-pad edge; it re-arms below 40. `held` is unchanged,
     * so field movement still reads the stick itself. */
    {
        uint16_t stick = 0;
        int x = input->stick_x, y = input->stick_y, ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        if (ax >= ay && ax > 80) stick = x > 0 ? C3_PAD_RIGHT : C3_PAD_LEFT;
        else if (ay > ax && ay > 80) stick = y > 0 ? C3_PAD_UP : C3_PAD_DOWN;
        if (stick && !stick_armed_direction) input->pressed |= stick;
        if (stick) stick_armed_direction = stick;
        else if (ax < 40 && ay < 40) stick_armed_direction = 0;
    }
    input->cstick_x = normalize_axis(cstick.dx);
    input->cstick_y = normalize_axis(cstick.dy);
    input->trigger_l = (keys & KEY_L) ? 255 : 0;
    input->trigger_r = (keys & KEY_R) ? 255 : 0;
    if (keys & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        previous_touch_x = touch.px; previous_touch_y = touch.py;
        input->touch_held = true;
    }
    input->touch_x = previous_touch_x; input->touch_y = previous_touch_y;
    input->touch_pressed = input->touch_held && !previous_touch;
    input->touch_released = !input->touch_held && previous_touch;
    previous_touch = input->touch_held;
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

void c3_n3ds_defer_buttons(uint16_t pressed)
{
    deferred_buttons |= pressed;
}

uint64_t c3_n3ds_active_time_us(void)
{
    uint64_t now, paused;
    LightLock_Lock(&clock_lock);
    now = c3_n3ds_time_us();
    paused = clock_paused_us;
    if (clock_pause_mask) paused += now - clock_pause_start;
    LightLock_Unlock(&clock_lock);
    return now - paused;
}

void c3_n3ds_log_memory(FILE *trace, const char *stage)
{
    if (!trace) return;
    struct mallinfo memory = mallinfo();
    fprintf(trace, "platform_memory stage=%s console=%s heap_bytes=%lu linear_heap_bytes=%lu linear_free_bytes=%lu\n",
            stage ? stage : "unspecified", console_is_new ? "new3ds" : "old3ds",
            (unsigned long)envGetHeapSize(), (unsigned long)envGetLinearHeapSize(),
            (unsigned long)linearSpaceFree());
    fprintf(trace, "platform_allocator stage=%s arena_bytes=%lu used_bytes=%lu free_in_arena_bytes=%lu\n",
            stage ? stage : "unspecified", (unsigned long)memory.arena,
            (unsigned long)memory.uordblks, (unsigned long)memory.fordblks);
    fflush(trace);
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
        c3_bottom_screen_close();
        c3_stage_renderer_fini();
        aptUnhook(&clock_hook);
        osSetSpeedupEnable(false);
        gfxExit();
        graphics_started = false;
    }
}
