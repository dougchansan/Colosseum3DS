#include "pack_loader.h"
#include "pack_cache.h"
#include "boot_index.h"
#include <3ds.h>
#include <stdlib.h>

struct C3PackStream {
    C3PackJob *jobs;
    size_t count;
    volatile size_t done;
    Thread thread;
};

static void load_direct(C3PackJob *job)
{
    uint64_t started = svcGetSystemTick();
    job->error[0] = 0;
    job->ok = c3_title_load(job->title, job->path, job->error, sizeof job->error);
    job->microseconds = (svcGetSystemTick() - started) / (SYSCLOCK_ARM11 / 1000000u);
}

static void stream_worker(void *argument)
{
    C3PackStream *stream = (C3PackStream *)argument;
    for (size_t i = 0; i < stream->count; ++i) {
        load_direct(stream->jobs + i);
        __atomic_store_n(&stream->done, i + 1, __ATOMIC_SEQ_CST);
    }
}

C3PackStream *c3_pack_stream_start(C3PackJob *jobs, size_t count)
{
    C3PackStream *stream = calloc(1, sizeof *stream);
    if (!stream) return NULL;
    stream->jobs = jobs; stream->count = count;
    if (count) {
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        /* Slightly lower priority than the scene, so frames keep their pace. */
        stream->thread = threadCreate(stream_worker, stream, 128 * 1024, priority < 0x3f ? priority + 1 : priority, 2, false);
    }
    if (!stream->thread) stream_worker(stream);
    return stream;
}

size_t c3_pack_stream_done(const C3PackStream *stream)
{
    return stream ? __atomic_load_n(&stream->done, __ATOMIC_SEQ_CST) : 0;
}

void c3_pack_stream_finish(C3PackStream *stream)
{
    if (!stream) return;
    if (stream->thread) { threadJoin(stream->thread, U64_MAX); threadFree(stream->thread); }
    free(stream);
}

static void load_cached(C3PackJob *job)
{
    uint64_t started = svcGetSystemTick();
    job->error[0] = 0;
    job->core = svcGetProcessorID();
    job->ok = c3_pack_cache_load(job->title, job->path, job->error, sizeof job->error);
    job->microseconds = (svcGetSystemTick() - started) / (SYSCLOCK_ARM11 / 1000000u);
    c3_title_last_load(&job->unpack_us, &job->decode_us);
}

/* order[front..back) is still to load: the main thread takes the front
 * (largest), the helper the back (smallest). */
typedef struct {
    C3PackJob *jobs;
    size_t *order, count, front, back;
    volatile size_t done;
    LightLock lock;
} LoadQueue;

static C3PackJob *take(LoadQueue *queue, int helper)
{
    C3PackJob *job = NULL;
    LightLock_Lock(&queue->lock);
    if (queue->front < queue->back) job = queue->jobs + queue->order[helper ? --queue->back : queue->front++];
    LightLock_Unlock(&queue->lock);
    return job;
}

static void worker(void *argument)
{
    LoadQueue *queue = (LoadQueue *)argument;
    for (C3PackJob *job; (job = take(queue, 1)) != NULL;) {
        load_cached(job);
        __atomic_fetch_add(&queue->done, 1, __ATOMIC_SEQ_CST);
    }
}

size_t c3_pack_load_all(C3PackJob *jobs, size_t count, void (*progress)(size_t done, size_t total))
{
    size_t fixed_order[64], *order = fixed_order;
    uint64_t fixed_sizes[64], *sizes = fixed_sizes;
    if (count > 64) {
        order = malloc(count * sizeof *order); sizes = malloc(count * sizeof *sizes);
        if (!order || !sizes) {
            /* No memory for an order: one thread, list order. */
            free(order); free(sizes);
            for (size_t i = 0; i < count; ++i) load_cached(jobs + i);
            if (progress) progress(count, count);
            for (size_t i = 0; i < count; ++i) if (!jobs[i].ok) return i + 1;
            return 0;
        }
    }
    /* Stable sort by compressed size, largest first (insertion: a floor has
     * a few dozen packs). Unknown sizes (0) keep their list order. */
    for (size_t i = 0; i < count; ++i) {
        size_t j = i;
        sizes[i] = c3_boot_index_size(jobs[i].path);
        while (j && sizes[order[j - 1]] < sizes[i]) { order[j] = order[j - 1]; --j; }
        order[j] = i;
    }
    LoadQueue queue = {jobs, order, count, 0, count, 0, 0};
    LightLock_Init(&queue.lock);
    Thread helper = NULL;
    if (count > 1) {
        /* The New 3DS's second application core (core 2). Where it is not
         * available (Old 3DS), the main thread loads every job. */
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        helper = threadCreate(worker, &queue, 128 * 1024, priority, 2, false);
    }
    size_t reported = (size_t)-1;
    for (;;) {
        C3PackJob *job = take(&queue, 0);
        if (job) {
            load_cached(job);
            __atomic_fetch_add(&queue.done, 1, __ATOMIC_SEQ_CST);
        }
        size_t done = __atomic_load_n(&queue.done, __ATOMIC_SEQ_CST);
        if (progress && done != reported) { progress(done, count); reported = done; }
        if (done >= count) break;
        if (!job) svcSleepThread(2000000); /* wait for the helper's last job */
    }
    if (helper) { threadJoin(helper, U64_MAX); threadFree(helper); }
    if (order != fixed_order) { free(order); free(sizes); }
    for (size_t i = 0; i < count; ++i) if (!jobs[i].ok) return i + 1;
    return 0;
}
