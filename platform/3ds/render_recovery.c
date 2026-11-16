#include "render_recovery.h"
#include "n3ds_platform.h"
#include "stage_renderer.h"

#ifdef C3_RECOVERY_HOST_TEST
void consoleClear(void);
void gfxFlushBuffers(void);
void svcSleepThread(int64_t duration);
uint32_t C3D_FrameCounter(int screen);
#else
#include <3ds.h>
#include <citro3d.h>
#endif

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "host"
#endif

#define WATCHDOG_US UINT64_C(3000000)
#define HEARTBEAT_US UINT64_C(1000000)
/* An ordinary busy queue is retried every two milliseconds, so a frame that
 * is a little late costs that much and not a whole display refresh. */
#define POLL_NS INT64_C(2000000)
#define VBLANK_POLLS 8u

/* The paused recovery view has nothing to do between display refreshes. Wait
 * for Citro3D to count the next top-screen VBlank, but never longer than
 * 16 ms and never on a GSP event: the count stops while the application is
 * suspended, and input must keep being polled. */
static void wait_vblank(void)
{
    uint32_t before = C3D_FrameCounter(0);
    for (unsigned i = 0; i < VBLANK_POLLS && C3D_FrameCounter(0) == before; ++i) svcSleepThread(POLL_NS);
}

static void (*pause_callback)(int);
static bool callback_paused;

static void set_paused(bool paused)
{
    if (callback_paused == paused) return;
    callback_paused = paused;
    if (pause_callback) pause_callback(paused ? 1 : 0);
}

void c3_render_recovery_set_pause_callback(void (*callback)(int))
{
    if (pause_callback == callback) {
        if (!callback) callback_paused = false;
        return;
    }
    if (pause_callback && callback_paused) pause_callback(0);
    pause_callback = callback;
    if (!callback) callback_paused = false;
    else if (callback_paused) callback(1);
}

static void record(FILE *trace, const char *scene, const char *event,
                   unsigned frames, uint64_t elapsed, bool ready, bool exiting,
                   uint16_t pressed)
{
    if (!trace) return;
    C3TitleRenderSnapshot gpu;
    c3_stage_renderer_snapshot(&gpu);
    fprintf(trace, "render_recovery scene=%s event=%s frames=%u elapsed_us=%llu "
            "ready=%u exit=%u input=%04x counters=%lu,%lu build=%.12s\n",
            scene, event, frames, (unsigned long long)elapsed, (unsigned)ready,
            (unsigned)exiting, (unsigned)pressed,
            (unsigned long)C3D_FrameCounter(0), (unsigned long)C3D_FrameCounter(1),
            C3_BUILD_ID);
    fprintf(trace, "render_gpu submitted=%lu fill=%lu command=%lu transfer=%lu "
            "words=%lu draws=%lu vertices=%lu models=%lu textures=%lu bytes=%lu\n",
            (unsigned long)gpu.submissions, (unsigned long)gpu.memory_fills,
            (unsigned long)gpu.command_lists, (unsigned long)gpu.display_transfers,
            (unsigned long)gpu.last_command_words, (unsigned long)gpu.last_draw_calls,
            (unsigned long)gpu.last_vertices, (unsigned long)gpu.scene_models,
            (unsigned long)gpu.scene_textures, (unsigned long)gpu.gpu_buffer_bytes);
    fflush(trace);
}

static void display(const char *scene, uint64_t elapsed, bool ready,
                    bool draining, bool exiting, bool retry, uint16_t pressed)
{
    consoleClear();
    printf("Colosseum3DS | render recovery\nBuild %.12s | %s\n\n", C3_BUILD_ID, scene);
    printf("Rendering paused: GPU %s\nWait %.1f seconds | PAD %04X\n\n",
           ready ? "ready" : "busy", (double)elapsed / 1000000.0, (unsigned)pressed);
    C3TitleRenderSnapshot gpu;
    c3_stage_renderer_snapshot(&gpu);
    printf("Submit %lu | fill %lu draw %lu tx %lu\n",
           (unsigned long)gpu.submissions, (unsigned long)gpu.memory_fills,
           (unsigned long)gpu.command_lists, (unsigned long)gpu.display_transfers);
    if (exiting || draining) {
        printf("%s\nWaiting for GPU before releasing\nscene memory.\n\n",
               exiting ? "Exit requested." : "Changing scene.");
        printf("X: write diagnostics to trace\nSTART + SELECT: request exit\n\n");
    }
    else {
        printf("A: resume when GPU is ready\nX: write diagnostics to trace\nSTART + SELECT: request exit\n\n");
        if (retry) printf("Resume request queued.\n\n");
    }
    printf("A hard GPU/system fault may still\nrequire a physical reset. HOME may\nalso wait for GPU completion.\n");
    gfxFlushBuffers();
}

C3RenderRecoveryResult c3_render_recover(FILE *trace, const char *scene,
                                        unsigned submitted_frames, C3RenderRecoveryMode mode)
{
    const uint64_t started = c3_n3ds_time_us();
    uint64_t next_heartbeat = started + HEARTBEAT_US, next_display = 0;
    const bool draining = mode != C3_RECOVER_WAIT;
    bool paused = false, retry = false, pending_exit = mode == C3_RECOVER_EXIT;
    uint16_t deferred = 0, last_pressed = 0;
    if (!scene) scene = "scene";
    for (;;) {
        /* Both helpers make one NONBLOCK attempt. Shutdown may release GPU
         * resources only after an empty-frame barrier proves quiescence. */
        bool ready = draining || pending_exit ? c3_stage_renderer_try_shutdown() :
                                    c3_stage_renderer_is_idle();
        uint64_t now = c3_n3ds_time_us();
        uint64_t elapsed = now >= started ? now - started : 0;
        if (ready && (draining || pending_exit)) {
            record(trace, scene, "drained", submitted_frames, elapsed, true, pending_exit, 0);
            set_paused(false);
            return pending_exit ? C3_RECOVERY_EXIT : C3_RECOVERY_DRAINED;
        }
        if (!paused && elapsed >= WATCHDOG_US) {
            paused = true;
            set_paused(true);
            deferred = 0;
            record(trace, scene, "timeout", submitted_frames, elapsed, ready, pending_exit, 0);
            next_display = 0;
        }
        if (ready && !draining && (!paused || retry)) {
            if (paused) record(trace, scene, "resume", submitted_frames, elapsed, true, false, 0);
            else if (deferred) c3_n3ds_defer_buttons(deferred);
            set_paused(false);
            return paused ? C3_RECOVERY_RESUME_PAUSED : C3_RECOVERY_RESUME;
        }

        C3N3dsInput input = {0};
        bool running = c3_n3ds_poll(&input);
        last_pressed = input.pressed;
        if (!running || input.exit_requested) {
            if (!pending_exit) {
                record(trace, scene, "exit_requested", submitted_frames, elapsed, ready, true, input.pressed);
                next_display = 0;
            }
            pending_exit = true;
            deferred = 0;
        } else if (!paused && !draining) {
            /* Polling must not swallow an ordinary menu press while the GPU
             * takes a few milliseconds. Recovery controls are consumed only
             * after the watchdog has actually paused the scene. */
            deferred |= input.pressed;
        } else {
            if (!draining && !pending_exit && (input.pressed & C3_PAD_A)) {
                retry = true;
                record(trace, scene, "retry", submitted_frames, elapsed, ready, pending_exit, input.pressed);
                next_display = 0;
            }
            if (input.pressed & C3_PAD_X) {
                record(trace, scene, "snapshot", submitted_frames, elapsed, ready, pending_exit, input.pressed);
                next_display = 0;
            }
        }
        if ((paused || draining || pending_exit) && now >= next_display) {
            display(scene, elapsed, ready, draining, pending_exit, retry, last_pressed);
            next_display = now + UINT64_C(250000);
        }
        if ((paused || draining || pending_exit) && now >= next_heartbeat) {
            record(trace, scene, "heartbeat", submitted_frames, elapsed, ready, pending_exit, last_pressed);
            next_heartbeat = now + HEARTBEAT_US;
        }
        if (paused) wait_vblank();
        else svcSleepThread(POLL_NS);
    }
}
