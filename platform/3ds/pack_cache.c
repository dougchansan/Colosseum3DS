#include "pack_cache.h"
#include <3ds.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_ENTRIES 160u

/* Two threads decode packs at once (pack_loader): the table is guarded by
 * one lock, and an entry being decoded is marked busy so it is never
 * evicted under the reader. Decompression itself runs outside the lock. */
typedef struct {
    char path[160];
    void *data;
    size_t bytes;
    uint32_t used;
    unsigned busy;
} Entry;

static Entry entries[CACHE_ENTRIES];
static uint32_t clock_tick;
static unsigned hits, misses;
static LightLock lock;
static int lock_ready;

static void enter(void)
{
    /* First use is on the main thread, before any loader thread exists. */
    if (!lock_ready) { LightLock_Init(&lock); lock_ready = 1; }
    LightLock_Lock(&lock);
}
static void leave(void) { LightLock_Unlock(&lock); }

size_t c3_pack_cache_heap_free(void)
{
    struct mallinfo memory = mallinfo();
    size_t heap = (size_t)envGetHeapSize(), used = (size_t)memory.uordblks;
    return heap > used ? heap - used : 0;
}

static void drop(Entry *e)
{
    free(e->data);
    memset(e, 0, sizeof *e);
}

static Entry *oldest(void)
{
    Entry *best = NULL;
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
        if (entries[i].data && !entries[i].busy && (!best || entries[i].used < best->used)) best = entries + i;
    return best;
}

static void trim_locked(size_t keep_free)
{
    while (c3_pack_cache_heap_free() < keep_free) {
        Entry *e = oldest();
        if (!e) break;
        drop(e);
    }
}

void c3_pack_cache_trim(size_t keep_free)
{
    enter(); trim_locked(keep_free); leave();
}

void c3_pack_cache_clear(void)
{
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) if (!entries[i].busy) drop(entries + i);
    leave();
}

size_t c3_pack_cache_bytes(void)
{
    size_t bytes = 0;
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) if (entries[i].data) bytes += entries[i].bytes;
    leave();
    return bytes;
}

void c3_pack_cache_trace(FILE *trace, const char *stage)
{
    if (!trace) return;
    size_t bytes = 0; unsigned count = 0;
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) if (entries[i].data) { bytes += entries[i].bytes; ++count; }
    unsigned h = hits, m = misses;
    leave();
    fprintf(trace, "pack_cache stage=%s entries=%u bytes=%lu hits=%u misses=%u heap_free=%lu linear_free=%lu\n", stage,
            count, (unsigned long)bytes, h, m, (unsigned long)c3_pack_cache_heap_free(), (unsigned long)linearSpaceFree());
    fflush(trace);
}

const void *c3_pack_cache_acquire(const char *path, size_t *bytes)
{
    const void *data = NULL;
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES && !data; ++i)
        if (entries[i].data && !strcmp(entries[i].path, path)) {
            Entry *e = entries + i;
            e->used = ++clock_tick; ++e->busy;
            data = e->data; *bytes = e->bytes;
        }
    if (data) ++hits; else ++misses;
    leave();
    return data;
}

void c3_pack_cache_release(const void *data)
{
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
        if (entries[i].data == data && entries[i].busy) { --entries[i].busy; break; }
    leave();
}

void c3_pack_cache_offer(const char *path, void *data, size_t bytes)
{
    if (!data) return;
    if (strlen(path) >= sizeof entries[0].path) { free(data); return; }
    enter();
    Entry *slot = NULL;
    int present = 0;
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) {
        if (entries[i].data && !strcmp(entries[i].path, path)) present = 1;
        else if (!entries[i].data && !slot) slot = entries + i;
    }
    if (!present && !slot && (slot = oldest()) != NULL) drop(slot);
    if (present || !slot) { leave(); free(data); return; }
    snprintf(slot->path, sizeof slot->path, "%s", path);
    slot->data = data; slot->bytes = bytes; slot->used = ++clock_tick; slot->busy = 0;
    /* Newest last: older entries go first, then this one if still short. */
    trim_locked(C3_PACK_CACHE_KEEP_FREE);
    leave();
}
