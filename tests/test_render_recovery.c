#include "render_recovery.h"
#include "n3ds_platform.h"
#include "title_renderer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static uint64_t now_us, ready_us, exit_us, x_us, a_us;
static unsigned polls, sleeps, flushes, clears, idle_calls, shutdown_calls, frees;
static uint16_t deferred;
static unsigned pause_calls, resume_calls;
static uint64_t paused_at, resumed_at;
static bool audio_paused, clear_hook;
static void audio_pause(int paused)
{
    CHECK(audio_paused != (paused != 0));
    audio_paused = paused != 0;
    if (paused) { ++pause_calls; paused_at = now_us; }
    else { ++resume_calls; resumed_at = now_us; }
}

static bool active, false_poll, sent_x, sent_a, sent_exit;

uint64_t c3_n3ds_time_us(void) { return now_us; }
void svcSleepThread(int64_t ns)
{
    CHECK(ns > 0 && ns <= 100000000);
    now_us += (uint64_t)ns / 1000;
    ++sleeps;
    CHECK(sleeps < 2000);
}
void consoleClear(void) { ++clears; }
void gfxFlushBuffers(void) { ++flushes; }
uint32_t C3D_FrameCounter(int id) { (void)id; return 17; }
void c3_stage_renderer_snapshot(C3TitleRenderSnapshot *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); snapshot->submissions = 7; snapshot->memory_fills = 7; }
void c3_n3ds_defer_buttons(uint16_t pressed) { deferred |= pressed; }
bool c3_n3ds_poll(C3N3dsInput *input)
{
    ++polls;
    if (audio_paused && !clear_hook) c3_render_recovery_set_pause_callback(audio_pause);
    if (clear_hook && audio_paused) {
        c3_render_recovery_set_pause_callback(NULL);
        clear_hook = false;
    }
    memset(input, 0, sizeof(*input));
    if (!sent_x && now_us >= x_us) { input->pressed |= C3_PAD_X; sent_x = true; }
    if (!sent_a && now_us >= a_us) { input->pressed |= C3_PAD_A; sent_a = true; }
    if (!sent_exit && now_us >= exit_us) {
        sent_exit = true;
        input->exit_requested = !false_poll;
        return !false_poll;
    }
    return true;
}
bool c3_stage_renderer_is_idle(void) { ++idle_calls; return !active || now_us >= ready_us; }
bool c3_stage_renderer_try_shutdown(void)
{
    ++shutdown_calls;
    if (!active) return true;
    if (now_us < ready_us) return false;
    ++frees;
    active = false;
    return true;
}
static FILE *reset(uint64_t ready)
{
    c3_render_recovery_set_pause_callback(NULL);
    pause_calls = resume_calls = 0;
    paused_at = resumed_at = 0;
    audio_paused = clear_hook = false;
    c3_render_recovery_set_pause_callback(audio_pause);
    now_us = 0; ready_us = ready;
    exit_us = x_us = a_us = UINT64_MAX;
    polls = sleeps = flushes = clears = idle_calls = shutdown_calls = frees = 0;
    deferred = 0;
    active = true; false_poll = sent_x = sent_a = sent_exit = false;
    FILE *trace = tmpfile();
    CHECK(trace != NULL);
    return trace;
}
static void has_event(FILE *trace, const char *event)
{
    char output[16384];
    rewind(trace);
    size_t n = fread(output, 1, sizeof(output) - 1, trace);
    output[n] = 0;
    CHECK(strstr(output, event) != NULL);
}
int main(void)
{
    FILE *trace = reset(0);
    CHECK(c3_render_recover(trace, "test", 1, C3_RECOVER_WAIT) == C3_RECOVERY_RESUME);
    CHECK(now_us == 0 && sleeps == 0 && frees == 0);
    CHECK(clears == 0 && flushes == 0);
    CHECK(pause_calls == 0 && resume_calls == 0);
    fclose(trace);

    trace = reset(64000); a_us = 16000;
    CHECK(c3_render_recover(trace, "test", 2, C3_RECOVER_WAIT) == C3_RECOVERY_RESUME);
    CHECK(polls >= 3 && sleeps >= 3 && deferred == C3_PAD_A);
    CHECK(clears == 0 && flushes == 0 && frees == 0);
    CHECK(pause_calls == 0 && resume_calls == 0);
    fclose(trace);

    trace = reset(3600000); x_us = 3100000; a_us = 3200000;
    CHECK(c3_render_recover(trace, "test", 3, C3_RECOVER_WAIT) == C3_RECOVERY_RESUME_PAUSED);
    CHECK(polls > 200 && now_us >= ready_us && deferred == 0);
    CHECK(pause_calls == 1 && resume_calls == 1 && !audio_paused);
    CHECK(paused_at >= 3000000 && paused_at < 3016000);
    CHECK(resumed_at == now_us && now_us - ready_us < 16000);
    CHECK(clears > 0 && flushes > 0 && frees == 0);
    has_event(trace, "event=timeout");
    has_event(trace, "event=heartbeat");
    has_event(trace, "event=snapshot");
    has_event(trace, "event=retry");
    fclose(trace);

    for (unsigned apt = 0; apt < 2; ++apt) {
        trace = reset(400000); exit_us = 16000; false_poll = apt != 0;
        CHECK(c3_render_recover(trace, "test", 4, C3_RECOVER_WAIT) == C3_RECOVERY_EXIT);
        CHECK(sent_exit && now_us >= ready_us && frees == 1 && shutdown_calls > 1);
        CHECK(polls > 2 && deferred == 0);
        has_event(trace, "event=exit_requested");
        has_event(trace, "event=drained");
        unsigned prior_frees = frees, prior_sleeps = sleeps;
        CHECK(c3_render_recover(trace, "test", 4, C3_RECOVER_EXIT) == C3_RECOVERY_EXIT);
        CHECK(frees == prior_frees && sleeps == prior_sleeps);
        fclose(trace);
    }
    trace = reset(96000);
    CHECK(c3_render_recover(trace, "test", 5, C3_RECOVER_EXIT) == C3_RECOVERY_EXIT);
    CHECK(now_us >= ready_us && frees == 1 && shutdown_calls > 1);
    fclose(trace);
    for (unsigned apt = 0; apt < 2; ++apt) {
        trace = reset(400000); exit_us = 16000; false_poll = apt != 0;
        CHECK(c3_render_recover(trace, "transition", 6, C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT);
        CHECK(sent_exit && now_us >= ready_us && frees == 1 && shutdown_calls > 1);
        CHECK(polls > 2 && deferred == 0);
        has_event(trace, "event=exit_requested");
        has_event(trace, "event=drained");
        fclose(trace);
    }
    trace = reset(96000); a_us = 16000; x_us = 32000;
    CHECK(c3_render_recover(trace, "transition", 7, C3_RECOVER_DRAIN) == C3_RECOVERY_DRAINED);
    CHECK(now_us >= ready_us && frees == 1 && shutdown_calls > 1);
    CHECK(deferred == 0);
    has_event(trace, "event=snapshot");
    fclose(trace);
    for (unsigned mode = 0; mode < 2; ++mode) {
        trace = reset(3400000);
        C3RenderRecoveryMode recovery_mode = mode ? C3_RECOVER_EXIT : C3_RECOVER_DRAIN;
        CHECK(c3_render_recover(trace, "audio drain", 8, recovery_mode) ==
              (mode ? C3_RECOVERY_EXIT : C3_RECOVERY_DRAINED));
        CHECK(pause_calls == 1 && resume_calls == 1 && !audio_paused);
        CHECK(resumed_at == now_us);
        fclose(trace);
    }
    trace = reset(3100000); a_us = 3600000;
    CHECK(c3_render_recover(trace, "delayed retry", 9, C3_RECOVER_WAIT) == C3_RECOVERY_RESUME_PAUSED);
    CHECK(pause_calls == 1 && resume_calls == 1 && !audio_paused);
    CHECK(resumed_at >= a_us && resumed_at == now_us);
    CHECK(resumed_at - a_us < 32000);
    fclose(trace);
    trace = reset(3400000); a_us = 3200000; clear_hook = true;
    CHECK(c3_render_recover(trace, "clear audio hook", 9, C3_RECOVER_WAIT) == C3_RECOVERY_RESUME_PAUSED);
    CHECK(pause_calls == 1 && resume_calls == 1 && !audio_paused);
    CHECK(resumed_at == paused_at);
    fclose(trace);
    c3_render_recovery_set_pause_callback(NULL);
    printf("render recovery: %u checks passed\n", checks);
    return 0;
}
