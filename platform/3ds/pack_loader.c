#include "pack_loader.h"
#include "pack_cache.h"
#include <3ds.h>

typedef struct {
    C3PackJob *jobs;
    size_t count;
    volatile size_t next, done;
} LoadQueue;

static void run_jobs(LoadQueue *queue)
{
    for (;;) {
        size_t index = __atomic_fetch_add(&queue->next, 1, __ATOMIC_SEQ_CST);
        if (index >= queue->count) break;
        C3PackJob *job = queue->jobs + index;
        uint64_t started = svcGetSystemTick();
        job->error[0] = 0;
        job->ok = c3_pack_cache_load(job->title, job->path, job->error, sizeof job->error);
        job->microseconds = (svcGetSystemTick() - started) / (SYSCLOCK_ARM11 / 1000000u);
        __atomic_fetch_add(&queue->done, 1, __ATOMIC_SEQ_CST);
    }
}

static void worker(void *argument)
{
    run_jobs((LoadQueue *)argument);
}

size_t c3_pack_load_all(C3PackJob *jobs, size_t count, void (*progress)(size_t done, size_t total))
{
    LoadQueue queue = {jobs, count, 0, 0};
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
        size_t index = __atomic_fetch_add(&queue.next, 1, __ATOMIC_SEQ_CST);
        if (index < queue.count) {
            C3PackJob *job = jobs + index;
            uint64_t started = svcGetSystemTick();
            job->error[0] = 0;
            job->ok = c3_pack_cache_load(job->title, job->path, job->error, sizeof job->error);
            job->microseconds = (svcGetSystemTick() - started) / (SYSCLOCK_ARM11 / 1000000u);
            __atomic_fetch_add(&queue.done, 1, __ATOMIC_SEQ_CST);
        }
        size_t done = __atomic_load_n(&queue.done, __ATOMIC_SEQ_CST);
        if (progress && done != reported) { progress(done, count); reported = done; }
        if (done >= count) break;
        if (index >= queue.count) svcSleepThread(2000000); /* wait for the helper's last job */
    }
    if (helper) { threadJoin(helper, U64_MAX); threadFree(helper); }
    for (size_t i = 0; i < count; ++i) if (!jobs[i].ok) return i + 1;
    return 0;
}
