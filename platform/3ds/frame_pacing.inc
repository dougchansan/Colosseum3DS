/* Included by the sole Citro3D owner. Frame ownership, presentation cadence
 * and the interval performance counters of the shared stage renderer.
 *
 * Ownership. A draw acquires its frame when staging starts and submits it at
 * the end, so the scene's input, script and skinning for the next frame run
 * on the CPU while the GPU draws this one. (Acquiring right after submission,
 * as first written, serialised CPU and GPU: on hardware 20 ms of CPU plus an
 * 11-13 ms GPU frame missed the second VBlank and Outskirt fell to 26 fps.)
 * A busy GPU is polled for at most C3_PACE_ACQUIRE_LIMIT_US; the draw then
 * reports C3_RENDER_BUSY and the scene's existing c3_render_recover path
 * (watchdog, input, exit) applies unchanged. An owned frame outside a draw
 * exists only after c3_stage_renderer_probe_clear-style callers; is_idle and
 * shutdown close it empty.
 *
 * Cadence. Citro3D's VBlank counter is the clock (one count per top-screen
 * VBlank at its default 60 Hz rate; nothing here calls C3D_FrameRate). With
 * the previous frame acquired at count A, the next acquisition waits until
 * A+N (N = C3_RENDER_VBLANKS_PER_FRAME, default 2), so acquisitions are
 * VBlank-aligned and, with staging plus GPU work under N VBlanks, swaps are
 * N VBlanks apart: 30 presentations per second at a fixed interval. Slower
 * frames are not delayed further. Waits poll the counter with short sleeps and never block
 * on a GSP event, because Citro3D stops counting while the application is
 * suspended: each wait also ends at the time those VBlanks would have taken
 * since the frame was acquired, so a silent counter degrades to the same rate
 * on the system clock. N=0 disables all waits. */
#ifndef C3_RENDER_VBLANKS_PER_FRAME
#define C3_RENDER_VBLANKS_PER_FRAME 2u
#endif
#define C3_PACE_POLL_NS 500000
/* One top-screen refresh (59.83 Hz) and the slack allowed before giving up. */
#define C3_PACE_VBLANK_US UINT64_C(16715)
#define C3_PACE_SLACK_US UINT64_C(4000)
#define C3_PACE_ACQUIRE_LIMIT_US UINT64_C(100000)

static C3RenderPerf stage_perf;
static uint32_t pace_anchor;
static uint64_t pace_anchor_us;
static uint64_t frame_upload_us, frame_upload_bytes, frame_wait_us, busy_since_us;
static bool perf_clock_installed;

static uint64_t stage_now_us(void)
{
    uint64_t ticks = svcGetSystemTick();
    return (ticks / SYSCLOCK_ARM11) * UINT64_C(1000000) +
           (ticks % SYSCLOCK_ARM11) * UINT64_C(1000000) / SYSCLOCK_ARM11;
}

/* Every CPU-written linear range goes through here so uploads are counted. */
static void stage_flush(const void *address, size_t bytes)
{
    uint64_t started = stage_now_us();
    GSPGPU_FlushDataCache(address, bytes);
    frame_upload_us += stage_now_us() - started;
    frame_upload_bytes += bytes;
}

/* Wait until `vblanks` refreshes have been counted since the frame was acquired. */
static void pace_wait(uint32_t vblanks)
{
    const uint32_t target = pace_anchor + vblanks;
    const uint64_t deadline = pace_anchor_us + vblanks * C3_PACE_VBLANK_US + C3_PACE_SLACK_US;
    uint64_t started = stage_now_us(), now = started;
    while ((int32_t)(C3D_FrameCounter(0) - target) < 0 && now < deadline) {
        svcSleepThread(C3_PACE_POLL_NS);
        now = stage_now_us();
    }
    stage_perf.pace_us += now - started;
    frame_wait_us += now - started;
}

static void stage_frame_acquired(uint64_t now)
{
    frame_acquired = true;
    pace_anchor = C3D_FrameCounter(0);
    pace_anchor_us = now;
    if (busy_since_us) {
        stage_perf.gpu_wait_us += now - busy_since_us;
        ++stage_perf.gpu_busy;
        busy_since_us = 0;
    }
}

/* Hold for the cadence, then acquire, polling a busy GPU for at most
 * C3_PACE_ACQUIRE_LIMIT_US. Called when staging starts, so the scene's input,
 * script and skinning for this frame ran while the GPU drew the last one. */
static bool stage_frame_begin(void)
{
    if (!perf_clock_installed) {
        /* Count animation from the first frame, not the scene's loading. */
        c3_title_perf_clock(stage_now_us);
        c3_title_perf_take(NULL);
        perf_clock_installed = true;
    }
    frame_wait_us = 0;
    if (!frame_acquired) {
        uint64_t started, now;
        if (C3_RENDER_VBLANKS_PER_FRAME && pace_anchor_us) pace_wait(C3_RENDER_VBLANKS_PER_FRAME);
        started = now = stage_now_us();
        for (;;) {
            if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) { stage_frame_acquired(now); break; }
            if (!busy_since_us) busy_since_us = now;
            if (now - started >= C3_PACE_ACQUIRE_LIMIT_US) {
                frame_wait_us += now - started;
                return false;
            }
            svcSleepThread(C3_PACE_POLL_NS);
            now = stage_now_us();
        }
        frame_wait_us += now - started;
    }
    frame_upload_us = frame_upload_bytes = 0;
    return true;
}

/* Close an acquired frame after an error; its partial commands are submitted
 * exactly as before, and no following frame is acquired. */
static void stage_frame_abort(void)
{
    C3D_FrameEnd(0);
    frame_acquired = false;
}

/* Submit at once and fold this frame into the interval. Acquisition is
 * VBlank-aligned, so the swap interval stays even without holding here, and
 * the GPU gets the whole remaining interval. */
static void stage_frame_end(C3TitleRenderStats *stats)
{
    C3TitlePerf animation;
    uint64_t started, now;
    started = stage_now_us();
    C3D_FrameEnd(0);
    frame_acquired = false;
    now = stage_now_us();
    c3_title_perf_take(&animation);
    ++stage_perf.frames;
    stage_perf.end_us += now - started;
    stage_perf.animate_calls += animation.calls;
    stage_perf.vertices_animated += animation.vertices_animated;
    stage_perf.vertices_retained += animation.vertices_retained;
    stage_perf.animate_us += animation.animate_us;
    if (animation.animate_us > stage_perf.animate_max_us) stage_perf.animate_max_us = animation.animate_us;
    stage_perf.prepare_us += stats->prepare_us;
    if (stats->prepare_us > stage_perf.prepare_max_us) stage_perf.prepare_max_us = stats->prepare_us;
    stage_perf.submit_us += stats->submit_us;
    if (stats->submit_us > stage_perf.submit_max_us) stage_perf.submit_max_us = stats->submit_us;
    stage_perf.upload_us += stats->upload_us;
    stage_perf.upload_bytes += stats->upload_bytes;
    stage_perf.vertices_prepared += stats->vertices_prepared;
    stage_perf.vertices_static += stats->vertices_static;
    stage_perf.cached_models += stats->cached_models;
    stage_perf.cached_batches += stats->cached_batches;
    if (stats->gpu_previous_ms > stage_perf.gpu_max_ms) stage_perf.gpu_max_ms = stats->gpu_previous_ms;
}

/* The time this draw spent waiting, for the cadence and for the GPU, is
 * reported in stats->wait_us so callers can keep timing their CPU work alone. */
static void stage_frame_next(C3TitleRenderStats *stats)
{
    stats->wait_us = (uint32_t)frame_wait_us;
}

void c3_stage_renderer_perf_take(C3RenderPerf *perf)
{
    /* The field also traces on every new button press, which restarts its
     * one-second window: a walk of taps 0.96 s apart read as 28-29 frames
     * while running at a full 30 fps. Every interval carries its length. */
    static uint64_t interval_start_us;
    uint64_t now = stage_now_us();
    stage_perf.interval_us = interval_start_us ? now - interval_start_us : 0;
    interval_start_us = now;
    if (perf) *perf = stage_perf;
    memset(&stage_perf, 0, sizeof stage_perf);
}

void c3_stage_renderer_perf_place(uint64_t microseconds)
{
    stage_perf.place_us += microseconds;
}

void c3_stage_renderer_perf_format(char *text, size_t capacity)
{
    C3RenderPerf p;
    c3_stage_renderer_perf_take(&p);
    if (!text || !capacity) return;
    snprintf(text, capacity,
        " perf_frames=%lu perf_animate_us=%llu perf_animate_max_us=%llu perf_animate_calls=%lu"
        " perf_vertices_animated=%lu perf_vertices_retained=%lu perf_place_us=%llu"
        " perf_prepare_us=%llu perf_prepare_max_us=%llu perf_vertices_prepared=%lu"
        " perf_vertices_static=%lu perf_cached_models=%lu perf_cached_batches=%lu"
        " perf_upload_us=%llu perf_upload_bytes=%llu perf_submit_us=%llu perf_submit_max_us=%llu"
        " perf_end_us=%llu perf_pace_us=%llu perf_gpu_wait_us=%llu perf_gpu_busy=%lu perf_gpu_max_ms=%.3f"
        " perf_interval_us=%llu perf_fps=%.2f",
        (unsigned long)p.frames, (unsigned long long)p.animate_us, (unsigned long long)p.animate_max_us,
        (unsigned long)p.animate_calls, (unsigned long)p.vertices_animated, (unsigned long)p.vertices_retained,
        (unsigned long long)p.place_us, (unsigned long long)p.prepare_us, (unsigned long long)p.prepare_max_us,
        (unsigned long)p.vertices_prepared, (unsigned long)p.vertices_static,
        (unsigned long)p.cached_models, (unsigned long)p.cached_batches,
        (unsigned long long)p.upload_us, (unsigned long long)p.upload_bytes,
        (unsigned long long)p.submit_us, (unsigned long long)p.submit_max_us,
        (unsigned long long)p.end_us, (unsigned long long)p.pace_us, (unsigned long long)p.gpu_wait_us,
        (unsigned long)p.gpu_busy, (double)p.gpu_max_ms, (unsigned long long)p.interval_us,
        p.interval_us ? (double)p.frames * 1000000.0 / (double)p.interval_us : 0.0);
}
