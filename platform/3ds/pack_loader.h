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
    /* Of microseconds: decompression and parsing (the rest is the SD card
     * and allocation), and the core that ran the job. */
    uint64_t unpack_us, decode_us;
    int core;
    char error[192];
} C3PackJob;

/* Load every job, on the main core and (on New 3DS) the second application
 * core in parallel. The main core takes the largest packs first and the
 * second core (several times slower per pack on the console) the smallest,
 * by compressed size from the boot index; with no sizes, list order. progress
 * (may be NULL) is called on the main thread with the number of finished
 * jobs. Returns the number of the first failed job + 1, or 0 when all
 * loaded. Uses c3_pack_cache_load, which is safe across the two threads. */
size_t c3_pack_load_all(C3PackJob *jobs, size_t count, void (*progress)(size_t done, size_t total));

/* Background loading while the scene runs (people streamed in after the
 * room): jobs load in order on the second application core straight from
 * their files (c3_title_load: heap only, so no linear-memory traffic races
 * the renderer). Without that core the jobs load inside start. Jobs must stay
 * alive until finish. */
typedef struct C3PackStream C3PackStream;
C3PackStream *c3_pack_stream_start(C3PackJob *jobs, size_t count);
/* Number of jobs finished so far (0..count). */
size_t c3_pack_stream_done(const C3PackStream *stream);
/* Wait for every job and free the stream. */
void c3_pack_stream_finish(C3PackStream *stream);
#endif
