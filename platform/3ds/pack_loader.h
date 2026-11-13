#ifndef C3_PACK_LOADER_H
#define C3_PACK_LOADER_H
#include "colosseum3ds/title.h"
#include <stddef.h>
#include <stdint.h>

/* One pack to load into a fixed C3Title slot. */
typedef struct {
    char path[160];
    C3Title *title;
    int ok;
    uint64_t microseconds;
    char error[192];
} C3PackJob;

/* Load every job, on the main core and (on New 3DS) the second application
 * core in parallel; jobs are taken in order from a shared counter. progress
 * (may be NULL) is called on the main thread with the number of finished
 * jobs. Returns the number of the first failed job + 1, or 0 when all
 * loaded. Uses c3_pack_cache_load, which is safe across the two threads. */
size_t c3_pack_load_all(C3PackJob *jobs, size_t count, void (*progress)(size_t done, size_t total));
#endif
