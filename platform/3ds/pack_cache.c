#include "pack_cache.h"
#include <3ds.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_ENTRIES 160u
/* Linear memory left free when a new entry is added (the renderer and
 * audio allocate from the same space after loads). */
#define CACHE_ADD_MARGIN (8u * 1024u * 1024u)

/* Two threads load packs at once (pack_loader): the table and linear
 * allocations are guarded by one lock, and an entry being decompressed is
 * marked busy so it is never evicted under the reader. Decompression itself
 * runs outside the lock. */
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

static void drop(Entry *e)
{
    if (e->data) linearFree(e->data);
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
    while (linearSpaceFree() < keep_free) {
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

void c3_pack_cache_trace(FILE *trace, const char *stage)
{
    if (!trace) return;
    size_t bytes = 0; unsigned count = 0;
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) if (entries[i].data) { bytes += entries[i].bytes; ++count; }
    unsigned h = hits, m = misses;
    leave();
    fprintf(trace, "pack_cache stage=%s entries=%u bytes=%lu hits=%u misses=%u linear_free=%lu\n", stage, count,
            (unsigned long)bytes, h, m, (unsigned long)linearSpaceFree());
    fflush(trace);
}

int c3_pack_cache_load(C3Title *title, const char *path, char *error, size_t capacity)
{
    enter();
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
        if (entries[i].data && !strcmp(entries[i].path, path)) {
            Entry *e = entries + i;
            e->used = ++clock_tick; ++hits; ++e->busy;
            leave();
            int loaded = c3_title_load_memory(title, e->data, e->bytes, error, capacity);
            enter(); --e->busy; leave();
            return loaded;
        }
    ++misses;
    leave();
    FILE *file = fopen(path, "rb");
    if (!file) return c3_title_load(title, path, error, capacity);
    unsigned char head[8];
    long size = -1;
    int compressed = fread(head, 1, sizeof head, file) == sizeof head && c3_title_is_compressed(head, sizeof head, NULL);
    if (compressed && fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (!compressed || size <= 8 || strlen(path) >= sizeof entries[0].path) {
        fclose(file); return c3_title_load(title, path, error, capacity);
    }
    /* Reserve a slot and the linear bytes under the lock; read outside it. */
    enter();
    trim_locked((size_t)size + CACHE_ADD_MARGIN);
    Entry *slot = NULL;
    for (unsigned i = 0; i < CACHE_ENTRIES && !slot; ++i) if (!entries[i].data && !entries[i].busy) slot = entries + i;
    if (!slot) { slot = oldest(); if (slot) drop(slot); }
    void *data = slot && linearSpaceFree() >= (size_t)size + CACHE_ADD_MARGIN ? linearAlloc((size_t)size) : NULL;
    if (data) { snprintf(slot->path, sizeof slot->path, "%s", path); slot->bytes = (size_t)size; slot->busy = 1; }
    leave();
    if (!data) {
        /* No linear room to keep it: read it into the heap for this load
         * only. Reopening by path cost a second SD directory lookup. */
        unsigned char *heap = malloc((size_t)size);
        if (!heap) { fclose(file); return c3_title_load(title, path, error, capacity); }
        int read = fseek(file, 0, SEEK_SET) == 0 && fread(heap, 1, (size_t)size, file) == (size_t)size;
        fclose(file);
        int loaded = read && c3_title_load_memory(title, heap, (size_t)size, error, capacity);
        free(heap);
        if (!read) return c3_title_load(title, path, error, capacity);
        return loaded;
    }
    int read = fseek(file, 0, SEEK_SET) == 0 && fread(data, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    int loaded = read && c3_title_load_memory(title, data, (size_t)size, error, capacity);
    enter();
    if (loaded) { slot->data = data; slot->used = ++clock_tick; slot->busy = 0; }
    else { linearFree(data); memset(slot, 0, sizeof *slot); }
    leave();
    if (!read) return c3_title_load(title, path, error, capacity);
    return loaded;
}
