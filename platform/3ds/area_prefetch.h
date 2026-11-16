#ifndef C3_AREA_PREFETCH_H
#define C3_AREA_PREFETCH_H
#include "colosseum3ds/title.h"
#include <stdint.h>
#include <stdio.h>

/* A destination's map packs (<prefix>_map%u.c3t), streamed on the second
 * core while the world map is open. The world map's orbit preview draws
 * them; when the player travels there, the next field load takes them
 * instead of reading them again, so the journey's playing time is also
 * the area's loading time. One floor at a time; the module owns the
 * titles until a field takes or releases them. */
#define C3_AREA_PREFETCH_MAX 12u

/* Release any previous floor, then start streaming this one's map packs.
 * Returns the number of packs found (0: nothing to stream). The renderer
 * must no longer draw titles of a previous floor. */
unsigned c3_area_prefetch_start(uint32_t floor, FILE *trace);
uint32_t c3_area_prefetch_floor(void);
unsigned c3_area_prefetch_count(void);
/* Packs finished so far (never blocks). */
unsigned c3_area_prefetch_done(void);
/* Wait for the stream; then title(i) is valid when ok(i). */
void c3_area_prefetch_finish(void);
C3Title *c3_area_prefetch_title(unsigned index);
int c3_area_prefetch_ok(unsigned index);
/* Field load: move a finished pack with exactly this path into out (closing
 * out first) and return 1; 0 when it was not prefetched or failed. Waits
 * for the stream. Render-side state set by the preview is cleared. */
int c3_area_prefetch_take(const char *path, C3Title *out);
/* Close every title not taken and forget the floor. */
void c3_area_prefetch_release(void);
#endif
