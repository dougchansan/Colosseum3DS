#define _GNU_SOURCE
#include "trace_writer.h"

#include <3ds.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Once a second the scenes write their trace lines and fflush them. On a
 * New 3DS that SD write held the main thread past the next refresh, so one
 * frame a second took three VBlanks: the battle and field ran at 29.4 fps
 * instead of 29.9 (perf_late_wake counted exactly one late wake per second,
 * perf_late_acquire none). */
#define TRACE_RING (256u * 1024u)
#define TRACE_STACK (16u * 1024u)

typedef struct {
    FILE *file;
    unsigned char *ring;
    size_t head, tail; /* monotonic; bytes queued = head - tail */
    LightLock lock;
    LightEvent wake;
    Thread thread;
    volatile bool closing;
    unsigned long dropped;
} TraceSink;

static void drain(TraceSink *s)
{
    for (;;) {
        LightLock_Lock(&s->lock);
        size_t queued = s->head - s->tail, start = s->tail % TRACE_RING;
        size_t run = queued < TRACE_RING - start ? queued : TRACE_RING - start;
        LightLock_Unlock(&s->lock);
        if (!run) break;
        fwrite(s->ring + start, 1, run, s->file);
        LightLock_Lock(&s->lock);
        s->tail += run;
        LightLock_Unlock(&s->lock);
    }
    fflush(s->file);
}

static void writer(void *argument)
{
    TraceSink *s = argument;
    while (!s->closing) {
        LightEvent_Wait(&s->wake);
        drain(s);
    }
    drain(s);
}

static ssize_t sink_write(void *cookie, const char *data, size_t size)
{
    TraceSink *s = cookie;
    size_t done = 0;
    for (int attempts = 0; done < size; ) {
        LightLock_Lock(&s->lock);
        size_t space = TRACE_RING - (s->head - s->tail);
        size_t n = size - done < space ? size - done : space;
        for (size_t i = 0; i < n; ) {
            size_t at = (s->head + i) % TRACE_RING, run = TRACE_RING - at;
            if (run > n - i) run = n - i;
            memcpy(s->ring + at, data + done + i, run);
            i += run;
        }
        s->head += n;
        LightLock_Unlock(&s->lock);
        done += n;
        LightEvent_Signal(&s->wake);
        if (done < size) {
            /* The ring is full (minutes of output): give the writer up to
             * 100 ms, then drop rather than stall the game. */
            if (++attempts > 100) { s->dropped += size - done; break; }
            svcSleepThread(1000000);
        }
    }
    return (ssize_t)size;
}

static int sink_close(void *cookie)
{
    TraceSink *s = cookie;
    s->closing = true;
    LightEvent_Signal(&s->wake);
    threadJoin(s->thread, U64_MAX);
    threadFree(s->thread);
    if (s->dropped) fprintf(s->file, "trace_dropped bytes=%lu\n", s->dropped);
    int result = fclose(s->file);
    free(s->ring);
    free(s);
    return result;
}

FILE *c3_trace_open(const char *path)
{
    FILE *file = fopen(path, "w");
    if (!file) return NULL;
    TraceSink *s = calloc(1, sizeof *s);
    unsigned char *ring = s ? malloc(TRACE_RING) : NULL;
    if (!ring) { free(s); return file; }
    s->file = file; s->ring = ring;
    LightLock_Init(&s->lock);
    LightEvent_Init(&s->wake, RESET_ONESHOT);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (priority < 0x3F) ++priority; /* below the main thread: runs while it sleeps */
    s->thread = threadCreate(writer, s, TRACE_STACK, priority, -2, false);
    if (!s->thread) { free(ring); free(s); return file; }
    FILE *stream = fopencookie(s, "w", (cookie_io_functions_t){.write = sink_write, .close = sink_close});
    if (!stream) {
        s->closing = true; LightEvent_Signal(&s->wake);
        threadJoin(s->thread, U64_MAX); threadFree(s->thread);
        free(ring); free(s); return file;
    }
    return stream;
}
