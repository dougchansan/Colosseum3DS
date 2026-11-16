#include "n3ds_platform.h"
#include "stage_renderer.h"
#include "bottom_screen.h"

#include <3ds.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

static bool graphics_started;
static bool console_is_new;
static uint16_t previous_buttons;
static uint16_t deferred_buttons;
#ifdef C3_LINK_PROBE
/* Unattended link probes: one scripted button edge every `script_every`
 * frames, as if pressed on the pad; a trailing '*' repeats the script,
 * and tokens before a '|' run once before the rest repeats. */
static uint16_t script_keys[64];
static unsigned script_count, script_at, script_every = 30, script_frame, script_repeat, script_loop;
void c3_n3ds_set_script(const char *script, unsigned every)
{
    static const struct { const char *name; uint16_t bit; } names[] = {
        {"A", C3_PAD_A}, {"B", C3_PAD_B}, {"X", C3_PAD_X}, {"Y", C3_PAD_Y}, {"UP", C3_PAD_UP}, {"DOWN", C3_PAD_DOWN},
        {"LEFT", C3_PAD_LEFT}, {"RIGHT", C3_PAD_RIGHT}, {"START", C3_PAD_START}, {"WAIT", 0}};
    char token[16]; unsigned n = 0;
    script_count = script_at = script_frame = script_repeat = script_loop = 0;
    if (every) script_every = every;
    for (const char *p = script; p && *p && script_count < 64;) {
        if (*p == ',' || *p == '*' || *p == '|') {
            if (*p == '*') script_repeat = 1;
            if (*p == '|') { script_repeat = 1; script_loop = script_count; } /* prefix once, then repeat the rest */
            ++p; continue;
        }
        n = 0; while (*p && *p != ',' && *p != '*' && *p != '|' && n + 1 < sizeof token) token[n++] = *p++;
        token[n] = 0;
        for (unsigned k = 0; k < sizeof names / sizeof names[0]; ++k)
            if (!strcmp(token, names[k].name)) { script_keys[script_count++] = names[k].bit; break; }
    }
}
static uint16_t script_next(void)
{
    if (!script_count || ++script_frame < script_every) return 0;
    script_frame = 0;
    if (script_at >= script_count) { if (!script_repeat) return 0; script_at = script_loop; }
    return script_keys[script_at++];
}
#endif
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

#ifdef C3_CAMPAIGN
static int autopress_enabled;
static uint64_t autopress_interval_us, autopress_next_us;
void c3_n3ds_autopress(int enabled, float interval)
{
    autopress_enabled = enabled;
    autopress_interval_us = (uint64_t)(interval > 0.1f ? interval * 1000000.0f : 1600000.0f);
    autopress_next_us = c3_n3ds_time_us() + autopress_interval_us;
}
#endif
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
#ifdef C3_LINK_PROBE
    input->pressed |= script_next();
#endif
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
#ifdef C3_CAMPAIGN
    if (autopress_enabled && c3_n3ds_time_us() >= autopress_next_us) {
        input->pressed |= C3_PAD_A;
        autopress_next_us = c3_n3ds_time_us() + autopress_interval_us;
    }
#endif
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

int c3_n3ds_capture_top(const char *path)
{
    u16 width = 0, height = 0;
    const u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    /* Top screen memory is 240 (width) x 400 (height) columns, BGR8,
     * rotated: screen pixel (x, y) is at ((x * 240) + (239 - y)) * 3. */
    if (!fb || width != 240 || height < 400 || gfxGetScreenFormat(GFX_TOP) != GSP_BGR8_OES) return -1;
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    static u8 row[400 * 3];
    u8 header[54] = {'B', 'M'};
    u32 image = 400u * 240u * 3u, total = 54u + image;
    memcpy(header + 2, &total, 4);
    header[10] = 54; header[14] = 40;
    u32 w = 400, h = 240; memcpy(header + 18, &w, 4); memcpy(header + 22, &h, 4);
    header[26] = 1; header[28] = 24;
    memcpy(header + 34, &image, 4);
    int ok = fwrite(header, 1, sizeof header, file) == sizeof header;
    /* BMP rows run bottom to top; the BGR byte order matches. */
    for (int y = 239; ok && y >= 0; --y) {
        for (int x = 0; x < 400; ++x) memcpy(row + x * 3, fb + ((size_t)x * 240u + (239u - (unsigned)y)) * 3u, 3);
        ok = fwrite(row, 1, sizeof row, file) == sizeof row;
    }
    if (fclose(file) != 0) ok = 0;
    return ok ? 0 : -1;
}

int c3_n3ds_capture_bottom(const char *path)
{
    /* Bottom screen: 240 x 320 columns, rotated like the top screen, in
     * BGR8 or RGB565 (bottom_screen.c chooses). Written as a 24-bit BMP. */
    u16 width = 0, height = 0;
    const u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
    int format = gfxGetScreenFormat(GFX_BOTTOM);
    if (!fb || width != 240 || height < 320 || (format != GSP_BGR8_OES && format != GSP_RGB565_OES)) return -1;
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    static u8 row[320 * 3];
    u8 header[54] = {'B', 'M'};
    u32 image = 320u * 240u * 3u, total = 54u + image;
    memcpy(header + 2, &total, 4);
    header[10] = 54; header[14] = 40;
    u32 w = 320, h = 240; memcpy(header + 18, &w, 4); memcpy(header + 22, &h, 4);
    header[26] = 1; header[28] = 24;
    memcpy(header + 34, &image, 4);
    int ok = fwrite(header, 1, sizeof header, file) == sizeof header;
    for (int y = 239; ok && y >= 0; --y) {
        for (int x = 0; x < 320; ++x) {
            size_t at = (size_t)x * 240u + (239u - (unsigned)y);
            if (format == GSP_BGR8_OES) memcpy(row + x * 3, fb + at * 3u, 3);
            else {
                u16 p = (u16)(fb[at * 2u] | fb[at * 2u + 1u] << 8);
                row[x * 3] = (u8)((p & 31u) << 3);
                row[x * 3 + 1] = (u8)(((p >> 5) & 63u) << 2);
                row[x * 3 + 2] = (u8)((p >> 11) << 3);
            }
        }
        ok = fwrite(row, 1, sizeof row, file) == sizeof row;
    }
    if (fclose(file) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void top_dot(u8 *fb, int cx, int cy, int radius, u8 level)
{
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;
            u8 *p = fb + ((size_t)x * 240u + (239u - (unsigned)y)) * 3u;
            p[0] = level; p[1] = level; p[2] = level;
        }
}
void c3_n3ds_top_loading(float fraction)
{
    u16 width = 0, height = 0;
    if (gfxGetScreenFormat(GFX_TOP) != GSP_BGR8_OES) return;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    if (!fb || width != 240 || height < 400) return;
    if (!(fraction >= 0.0f)) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    memset(fb, 0, 400u * 240u * 3u);
    const uint8_t *ball = NULL;
    unsigned ball_w = 0, ball_h = 0;
    if (c3_ui_sprite_pixels(c3_bottom_screen_assets(), C3_UI_BALL, &ball, &ball_w, &ball_h)) {
        /* The original 16x16 UI Poke Ball at 3x, one clockwise turn per
         * 1.2 s, bilinear so the rotation stays smooth. */
        unsigned period = (unsigned)(SYSCLOCK_ARM11 * 6u / 5u);
        float turn = (float)(svcGetSystemTick() % period) / (float)period * 6.28318531f;
        c3_ui_spin_rgba(fb, 400, 240, ball, ball_w, ball_h, 200, 108, 48, cosf(turn), sinf(turn));
    } else {
        /* No sprite (older or missing ui.c3ui): ring of eight dots; the
         * bright one advances with time. */
        unsigned lead = (unsigned)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 10u)) % 8u;
        for (unsigned i = 0; i < 8; ++i) {
            float angle = (float)i * 0.785398163f;
            int x = 200 + (int)lroundf(sinf(angle) * 26.0f), y = 104 - (int)lroundf(cosf(angle) * 26.0f);
            unsigned age = (lead + 8u - i) % 8u;
            top_dot(fb, x, y, 6, (u8)(age == 0 ? 255 : age < 3 ? 160 - 40 * age : 50));
        }
    }
    /* Progress bar in the bottom UI's palette: muted frame, white fill. */
    for (int x = 100; x < 300; ++x)
        for (int y = 164; y < 172; ++y) {
            int edge = y == 164 || y == 171 || x == 100 || x == 299;
            int filled = x - 101 < (int)(fraction * 198.0f);
            u8 *p = fb + ((size_t)x * 240u + (239u - (unsigned)y)) * 3u;
            uint32_t colour = edge ? C3_UI_MUTED : filled ? C3_UI_WHITE : 0u;
            p[0] = (u8)colour; p[1] = (u8)(colour >> 8); p[2] = (u8)(colour >> 16);
        }
#ifdef C3_ROUNDTRIP_PROBE
    /* Review aid: keep a few loading frames (2nd, 12th, 22nd call). */
    {
        static unsigned calls, kept;
        if (++calls % 10u == 2u && kept < 3u) {
            char shot[64];
            mkdir("sdmc:/3ds/Colosseum3DS/probe", 0777);
            snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/probe/loading_%u_top.bmp", kept++);
            (void)c3_n3ds_capture_top(shot);
        }
    }
#endif
    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_TOP, false);
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
