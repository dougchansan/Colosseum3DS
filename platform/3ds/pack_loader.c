#include "pack_loader.h"
#include "pack_cache.h"
#include "boot_index.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

struct C3PackStream {
    C3PackJob *jobs;
    size_t count;
    volatile size_t done;
    Thread thread;
};

static uint64_t now_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000u); }

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

/* Read-ahead stops while this many read bytes wait for a decoder. */
#define READ_AHEAD_BYTES (8u * 1024u * 1024u)
/* Heap kept free before a decode beyond twice the raw pack (its bytes plus
 * the vertex arrays parsed from them); the pack cache yields it. */
#define DECODE_SLACK (2u * 1024u * 1024u)
/* Raw/compressed ratio assumed for a pack not read yet (Phenac: 3.75). */
#define RATIO_GUESS 4u

enum { UNREAD, READING, READY, TAKEN, FINISHED };

typedef struct {
    C3PackJob *job;
    uint64_t estimate;   /* raw bytes once read, else compressed x RATIO_GUESS */
    void *data;          /* pack bytes; NULL: the decoder loads from the file */
    size_t bytes;
    int state, cached;
} Slot;

typedef struct {
    Slot *slots;
    size_t count, done, pending;
    LightLock lock;
    CondVar changed;
    int helper_waiting;
    /* Decode time and raw bytes per decoder (0 main, 1 helper). */
    uint64_t busy_us[2], raw[2];
} Loader;

static uint64_t raw_size(const void *data, size_t bytes)
{
    size_t raw = 0;
    return c3_title_is_compressed(data, bytes, &raw) ? raw : bytes;
}

/* The whole file in one read, its size from the boot index. NULL when it
 * cannot be read as indexed (the decoder then loads it from the file). */
static void *read_pack(const char *path, size_t *bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    setvbuf(file, NULL, _IONBF, 0);
    uint64_t size = c3_boot_index_size(path);
    if (!size) {
        long end = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
        size = end > 0 && fseek(file, 0, SEEK_SET) == 0 ? (uint64_t)end : 0;
    }
    void *data = size && size <= C3_TITLE_MAX_FILE_BYTES ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, file) != (size_t)size) { free(data); data = NULL; }
    fclose(file);
    *bytes = data ? (size_t)size : 0;
    return data;
}

static Slot *next_unread(Loader *l)
{
    Slot *big = NULL, *small = NULL;
    for (size_t i = 0; i < l->count; ++i) {
        Slot *s = l->slots + i;
        if (s->state != UNREAD) continue;
        if (!big || s->estimate > big->estimate) big = s;
        if (!small || s->estimate < small->estimate) small = s;
    }
    /* Largest first for the main core; a small one when the second core
     * waits with nothing it can take. */
    return l->helper_waiting && small ? small : big;
}

static void reader(void *argument)
{
    Loader *l = (Loader *)argument;
    for (;;) {
        LightLock_Lock(&l->lock);
        Slot *s;
        while ((s = next_unread(l)) != NULL && l->pending >= READ_AHEAD_BYTES) CondVar_Wait(&l->changed, &l->lock);
        if (s) s->state = READING;
        LightLock_Unlock(&l->lock);
        if (!s) return;
        uint64_t started = now_us();
        size_t bytes = 0;
        void *data = read_pack(s->job->path, &bytes);
        s->job->read_us = now_us() - started;
        LightLock_Lock(&l->lock);
        s->data = data; s->bytes = bytes;
        if (data) { s->estimate = raw_size(data, bytes); l->pending += bytes; }
        s->state = READY;
        CondVar_Broadcast(&l->changed);
        LightLock_Unlock(&l->lock);
    }
}

/* A ready pack for this decoder, or NULL (under the lock). */
static Slot *take(Loader *l, int helper)
{
    Slot *best = NULL;
    uint64_t left = 0;
    for (size_t i = 0; i < l->count; ++i) {
        Slot *s = l->slots + i;
        if (s->state >= TAKEN) continue;
        left += s->estimate;
        if (s->state != READY) continue;
        if (!best || (helper ? s->estimate < best->estimate : s->estimate > best->estimate)) best = s;
    }
    /* The second core (measured several times slower on the console) takes
     * a pack only when it would finish before the main core, at its
     * measured rate, gets through everything else. */
    if (best && helper && l->raw[0] && l->raw[1] &&
        best->estimate * l->busy_us[1] / l->raw[1] > (left - best->estimate) * l->busy_us[0] / l->raw[0])
        best = NULL;
    if (best) best->state = TAKEN;
    return best;
}

static int untaken(const Loader *l)
{
    for (size_t i = 0; i < l->count; ++i) if (l->slots[i].state < TAKEN) return 1;
    return 0;
}

static void decode(Loader *l, Slot *s, int helper)
{
    C3PackJob *job = s->job;
    uint64_t started = now_us();
    job->error[0] = 0;
    job->core = svcGetProcessorID();
    job->cached = s->cached;
    c3_pack_cache_trim((size_t)s->estimate * 2u + DECODE_SLACK);
    int from_memory = s->data && c3_title_load_memory(job->title, s->data, s->bytes, job->error, sizeof job->error);
    /* Not read, or the bytes read no longer decode: from the file. */
    job->ok = from_memory || c3_title_load(job->title, job->path, job->error, sizeof job->error);
    if (!job->ok) {
        /* Perhaps no block large enough between cached packs: give the
         * cache's heap back and try once more. */
        if (!s->cached) { free(s->data); s->data = NULL; }
        c3_pack_cache_clear();
        job->ok = c3_title_load(job->title, job->path, job->error, sizeof job->error);
    }
    c3_title_last_load(&job->unpack_us, &job->decode_us);
    uint64_t elapsed = now_us() - started;
    job->microseconds = job->read_us + elapsed;
    if (s->cached) c3_pack_cache_release(s->data);
    else if (from_memory && c3_title_is_compressed(s->data, s->bytes, NULL)) c3_pack_cache_offer(job->path, s->data, s->bytes);
    else free(s->data);
    LightLock_Lock(&l->lock);
    if (!s->cached) l->pending -= s->bytes;
    l->busy_us[helper] += elapsed; l->raw[helper] += job->title->file_bytes ? job->title->file_bytes : s->estimate;
    s->data = NULL; s->state = FINISHED; ++l->done;
    CondVar_Broadcast(&l->changed);
    LightLock_Unlock(&l->lock);
}

static void helper_worker(void *argument)
{
    Loader *l = (Loader *)argument;
    LightLock_Lock(&l->lock);
    for (;;) {
        Slot *s = take(l, 1);
        if (s) {
            l->helper_waiting = 0;
            LightLock_Unlock(&l->lock);
            decode(l, s, 1);
            LightLock_Lock(&l->lock);
            continue;
        }
        if (!untaken(l)) break;
        if (!l->helper_waiting) { l->helper_waiting = 1; CondVar_Broadcast(&l->changed); }
        CondVar_Wait(&l->changed, &l->lock);
    }
    l->helper_waiting = 0;
    CondVar_Broadcast(&l->changed);
    LightLock_Unlock(&l->lock);
}

size_t c3_pack_load_all(C3PackJob *jobs, size_t count, void (*progress)(size_t done, size_t total))
{
    Loader l;
    memset(&l, 0, sizeof l);
    l.slots = calloc(count ? count : 1u, sizeof *l.slots);
    if (!l.slots) {
        /* No memory for the queue: one thread, list order, from the files. */
        for (size_t i = 0; i < count; ++i) { load_direct(jobs + i); jobs[i].core = svcGetProcessorID(); }
        if (progress) progress(count, count);
        for (size_t i = 0; i < count; ++i) if (!jobs[i].ok) return i + 1;
        return 0;
    }
    l.count = count;
    c3_boot_index_busy(1);
    LightLock_Init(&l.lock);
    CondVar_Init(&l.changed);
    size_t unread = 0;
    for (size_t i = 0; i < count; ++i) {
        Slot *s = l.slots + i;
        s->job = jobs + i;
        jobs[i].read_us = 0; jobs[i].cached = 0;
        size_t bytes = 0;
        const void *cached = c3_pack_cache_acquire(jobs[i].path, &bytes);
        if (cached) {
            s->data = (void *)cached; s->bytes = bytes; s->cached = 1;
            s->estimate = raw_size(cached, bytes); s->state = READY;
        } else {
            s->estimate = c3_boot_index_size(jobs[i].path) * RATIO_GUESS;
            ++unread;
        }
    }
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    Thread read_thread = NULL, helper = NULL;
    if (unread) {
        /* Above the decoders on the main core: it mostly waits on the card
         * and must issue the next read as soon as one completes. */
        read_thread = threadCreate(reader, &l, 32 * 1024, priority > 0x18 ? priority - 1 : priority, 0, false);
        if (!read_thread)
            for (size_t i = 0; i < count; ++i) if (l.slots[i].state == UNREAD) l.slots[i].state = READY;
    }
    /* The New 3DS's second application core (core 2). Where it is not
     * available (Old 3DS), the main thread decodes every pack. */
    if (count > 1) helper = threadCreate(helper_worker, &l, 128 * 1024, priority, 2, false);
    size_t reported = (size_t)-1;
    LightLock_Lock(&l.lock);
    for (;;) {
        Slot *s = take(&l, 0);
        if (s) {
            LightLock_Unlock(&l.lock);
            decode(&l, s, 0);
            LightLock_Lock(&l.lock);
        }
        size_t done = l.done;
        if (progress && done != reported) {
            LightLock_Unlock(&l.lock);
            progress(done, count); reported = done;
            LightLock_Lock(&l.lock);
            continue;
        }
        if (done >= count) break;
        /* Waiting for the reader or for the second core's last pack. */
        if (!s) CondVar_WaitTimeout(&l.changed, &l.lock, 4000000);
    }
    LightLock_Unlock(&l.lock);
    if (read_thread) { threadJoin(read_thread, U64_MAX); threadFree(read_thread); }
    if (helper) { threadJoin(helper, U64_MAX); threadFree(helper); }
    c3_boot_index_busy(0);
    free(l.slots);
    for (size_t i = 0; i < count; ++i) if (!jobs[i].ok) return i + 1;
    return 0;
}
