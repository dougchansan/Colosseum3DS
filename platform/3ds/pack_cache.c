#include "pack_cache.h"
#include <3ds.h>
#include <stdint.h>
#include <string.h>

#define CACHE_ENTRIES 160u
/* Linear memory left free when a new entry is added (the renderer and
 * audio allocate from the same space after loads). */
#define CACHE_ADD_MARGIN (8u * 1024u * 1024u)

typedef struct {
    char path[160];
    void *data;
    size_t bytes;
    uint32_t used;
} Entry;

static Entry entries[CACHE_ENTRIES];
static uint32_t clock_tick;
static unsigned hits, misses;

static void drop(Entry *e)
{
    if (e->data) linearFree(e->data);
    memset(e, 0, sizeof *e);
}

static Entry *oldest(void)
{
    Entry *best = NULL;
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
        if (entries[i].data && (!best || entries[i].used < best->used)) best = entries + i;
    return best;
}

void c3_pack_cache_trim(size_t keep_free)
{
    while (linearSpaceFree() < keep_free) {
        Entry *e = oldest();
        if (!e) break;
        drop(e);
    }
}

void c3_pack_cache_clear(void)
{
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) drop(entries + i);
}

void c3_pack_cache_trace(FILE *trace, const char *stage)
{
    if (!trace) return;
    size_t bytes = 0; unsigned count = 0;
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i) if (entries[i].data) { bytes += entries[i].bytes; ++count; }
    fprintf(trace, "pack_cache stage=%s entries=%u bytes=%lu hits=%u misses=%u linear_free=%lu\n", stage, count,
            (unsigned long)bytes, hits, misses, (unsigned long)linearSpaceFree());
    fflush(trace);
}

int c3_pack_cache_load(C3Title *title, const char *path, char *error, size_t capacity)
{
    for (unsigned i = 0; i < CACHE_ENTRIES; ++i)
        if (entries[i].data && !strcmp(entries[i].path, path)) {
            entries[i].used = ++clock_tick; ++hits;
            return c3_title_load_memory(title, entries[i].data, entries[i].bytes, error, capacity);
        }
    ++misses;
    FILE *file = fopen(path, "rb");
    if (!file) return c3_title_load(title, path, error, capacity);
    unsigned char head[8];
    long size = -1;
    int compressed = fread(head, 1, sizeof head, file) == sizeof head && c3_title_is_compressed(head, sizeof head, NULL);
    if (compressed && fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (!compressed || size <= 8 || strlen(path) >= sizeof entries[0].path) {
        fclose(file); return c3_title_load(title, path, error, capacity);
    }
    /* Room for this entry plus the margin; otherwise stream from the file. */
    c3_pack_cache_trim((size_t)size + CACHE_ADD_MARGIN);
    Entry *slot = NULL;
    for (unsigned i = 0; i < CACHE_ENTRIES && !slot; ++i) if (!entries[i].data) slot = entries + i;
    if (!slot) { slot = oldest(); drop(slot); }
    void *data = linearSpaceFree() >= (size_t)size + CACHE_ADD_MARGIN ? linearAlloc((size_t)size) : NULL;
    if (!data) { fclose(file); return c3_title_load(title, path, error, capacity); }
    int read = fseek(file, 0, SEEK_SET) == 0 && fread(data, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!read) { linearFree(data); return c3_title_load(title, path, error, capacity); }
    int loaded = c3_title_load_memory(title, data, (size_t)size, error, capacity);
    if (!loaded) { linearFree(data); return 0; }
    snprintf(slot->path, sizeof slot->path, "%s", path);
    slot->data = data; slot->bytes = (size_t)size; slot->used = ++clock_tick;
    return 1;
}
