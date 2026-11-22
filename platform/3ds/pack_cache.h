#ifndef C3_PACK_CACHE_H
#define C3_PACK_CACHE_H
#include <stddef.h>
#include <stdio.h>

/* Compressed pack bytes kept in spare heap between area loads, so
 * re-entering a recent area (a city after a house) skips the SD card (about
 * 40-60 ms per file open plus the read on the console) and only decodes.
 * Least recently used entries go first. The cache only ever holds memory
 * above a free-heap floor: an entry is kept only while the heap keeps
 * C3_PACK_CACHE_KEEP_FREE free, a loader makes room before each decode, and
 * scenes that need more (battles, the world map) empty it. Linear memory is
 * left to the renderer. Safe across the pack loader's threads. */
#define C3_PACK_CACHE_KEEP_FREE (16u * 1024u * 1024u)

/* The cached bytes of path (marked in use until release), or NULL. */
const void *c3_pack_cache_acquire(const char *path, size_t *bytes);
void c3_pack_cache_release(const void *data);
/* Hand over a malloc'd compressed pack just decoded from path: kept as the
 * most recent entry while the free-heap floor allows, otherwise freed. */
void c3_pack_cache_offer(const char *path, void *data, size_t bytes);
/* Evict least recently used entries (not in use) until the heap has
 * keep_free bytes free or nothing is left to evict. */
void c3_pack_cache_trim(size_t keep_free);
void c3_pack_cache_clear(void);
/* Compressed bytes the cache holds now. */
size_t c3_pack_cache_bytes(void);
/* Heap bytes not handed out by the allocator (fragmentation not counted). */
size_t c3_pack_cache_heap_free(void);
void c3_pack_cache_trace(FILE *trace, const char *stage);
#endif
