#ifndef C3_PACK_CACHE_H
#define C3_PACK_CACHE_H
#include "colosseum3ds/title.h"
#include <stddef.h>
#include <stdio.h>

/* Compressed pack bytes kept in spare linear memory between area loads, so
 * re-entering a recent area (a city after a house) skips the SD card and
 * only decompresses. Least recently used entries go first, and the cache
 * never keeps linear memory the renderer needs: field_scene empties it when
 * the renderer cannot start and trims it to a free margin afterwards. Raw
 * (uncompressed) packs are loaded directly and not cached. */
int c3_pack_cache_load(C3Title *title, const char *path, char *error, size_t capacity);
/* Evict least recently used entries until linear free space >= keep_free. */
void c3_pack_cache_trim(size_t keep_free);
void c3_pack_cache_clear(void);
void c3_pack_cache_trace(FILE *trace, const char *stage);
#endif
