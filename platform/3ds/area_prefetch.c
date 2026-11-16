#include "area_prefetch.h"
#include "pack_loader.h"
#include "colosseum3ds/floor_profile.h"
#include <stdlib.h>
#include <string.h>

static struct {
    uint32_t floor;
    unsigned count;
    C3Title titles[C3_AREA_PREFETCH_MAX];
    C3PackJob jobs[C3_AREA_PREFETCH_MAX];
    C3PackStream *stream;
} prefetch;

void c3_area_prefetch_finish(void)
{
    if (prefetch.stream) { c3_pack_stream_finish(prefetch.stream); prefetch.stream = NULL; }
}

void c3_area_prefetch_release(void)
{
    c3_area_prefetch_finish();
    for (unsigned i = 0; i < C3_AREA_PREFETCH_MAX; ++i) c3_title_close(prefetch.titles + i);
    memset(prefetch.jobs, 0, sizeof prefetch.jobs);
    prefetch.count = 0; prefetch.floor = 0;
}

unsigned c3_area_prefetch_start(uint32_t floor, FILE *trace)
{
    const C3FloorProfile *profile = c3_floor_profile(floor);
    c3_area_prefetch_release();
    prefetch.floor = floor;
    if (!profile) return 0;
    for (unsigned i = 0; i < C3_AREA_PREFETCH_MAX; ++i) {
        C3PackJob *job = prefetch.jobs + prefetch.count;
        FILE *f;
        snprintf(job->path, sizeof job->path, "sdmc:/3ds/Colosseum3DS/boot/%s_map%u.c3t", profile->prefix, i);
        if (!(f = fopen(job->path, "rb"))) { job->path[0] = 0; break; }
        fclose(f);
        job->title = prefetch.titles + prefetch.count++;
    }
    if (prefetch.count) prefetch.stream = c3_pack_stream_start(prefetch.jobs, prefetch.count);
    if (trace) {
        fprintf(trace, "area_prefetch floor=%03lx packs=%u stream=%d\n", (unsigned long)floor, prefetch.count,
                prefetch.stream != NULL);
        fflush(trace);
    }
    return prefetch.count;
}

uint32_t c3_area_prefetch_floor(void) { return prefetch.floor; }
unsigned c3_area_prefetch_count(void) { return prefetch.count; }

unsigned c3_area_prefetch_done(void)
{
    return prefetch.stream ? (unsigned)c3_pack_stream_done(prefetch.stream) : prefetch.count;
}

C3Title *c3_area_prefetch_title(unsigned index)
{
    return index < prefetch.count ? prefetch.titles + index : NULL;
}

int c3_area_prefetch_ok(unsigned index)
{
    return index < prefetch.count && !prefetch.stream && prefetch.jobs[index].ok && prefetch.titles[index].file_data;
}

int c3_area_prefetch_take(const char *path, C3Title *out)
{
    if (!path || !out || !prefetch.count) return 0;
    for (unsigned i = 0; i < prefetch.count; ++i) {
        if (strcmp(prefetch.jobs[i].path, path)) continue;
        c3_area_prefetch_finish();
        if (!c3_area_prefetch_ok(i)) return 0;
        c3_title_close(out);
        *out = prefetch.titles[i];
        memset(prefetch.titles + i, 0, sizeof prefetch.titles[i]);
        prefetch.jobs[i].ok = 0;
        /* The preview drew it as a retained model with its own placement. */
        out->retained = out->hidden = 0;
        memset(&out->placement, 0, sizeof out->placement);
        out->gpu_batches = 0;
        return 1;
    }
    return 0;
}
