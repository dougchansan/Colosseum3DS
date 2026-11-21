#include "bottom_screen.h"
#include "movie_player.h"
#include "n3ds_platform.h"
#include "colosseum3ds/movie.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_BUFFERS 3u
#define AUDIO_FRAMES 1024u

static bool output_mono;

void c3_movie_player_set_mono(int mono)
{
    output_mono = !!mono;
}

typedef struct MovieAudio {
    ndspWaveBuf wave[AUDIO_BUFFERS];
    int16_t *samples;
    bool started;
    bool available;
    bool mono;
    Result result;
} MovieAudio;

static void audio_close(MovieAudio *audio)
{
    if (audio->started) {
        ndspChnWaveBufClear(0);
        ndspExit();
    }
    if (audio->samples) linearFree(audio->samples);
    memset(audio, 0, sizeof(*audio));
}

static void audio_open(MovieAudio *audio, const C3Movie *movie)
{
    float mix[12] = {1.0f, 1.0f};
    audio->mono = output_mono;
    if (!movie->audio_sample_frames) return;
    audio->result = ndspInit();
    if (R_FAILED(audio->result)) return;
    audio->started = true;
    audio->samples = linearAlloc(AUDIO_BUFFERS * AUDIO_FRAMES * 4u);
    if (!audio->samples) return;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetPaused(0, true);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)movie->sample_rate);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetMix(0, mix);
    for (unsigned i = 0; i < AUDIO_BUFFERS; ++i)
        audio->wave[i].data_vaddr = audio->samples + i * AUDIO_FRAMES * 2u;
    audio->available = true;
}

static int audio_queue(MovieAudio *audio, C3Movie *movie, char *error, size_t capacity)
{
    if (!audio->available) return 0;
    for (unsigned i = 0; i < AUDIO_BUFFERS; ++i) {
        size_t count;
        ndspWaveBuf *wave = &audio->wave[i];
        if (wave->status != NDSP_WBUF_FREE && wave->status != NDSP_WBUF_DONE) continue;
        if (c3_movie_read_audio(movie, wave->data_pcm16, AUDIO_FRAMES, &count, error, capacity)) return -1;
        if (!count) continue;
        if (audio->mono) c3_movie_downmix_mono(wave->data_pcm16, count);
        wave->nsamples = (u32)count;
        wave->looping = false;
        DSP_FlushDataCache(wave->data_vaddr, (u32)(count * 4u));
        ndspChnWaveBufAdd(0, wave);
    }
    return 0;
}

/* Frames are read and decoded ahead of the display as a pipeline: a reader
 * thread keeps a ring of stored (packed) frames filled from SD while a decode
 * thread, on the New 3DS's second application core where available, unpacks
 * them into a small ring of framebuffer-ordered slots. The main thread only
 * picks the due slot, copies it into the top framebuffer, and keeps the audio
 * queue and vblank pace, so neither SD latency nor decoding holds up a swap,
 * and a frame's read overlaps the previous frame's decode. */
#define FEED_SLOTS 3u
#define FEED_STORED 4u
#define FEED_STACK (32u * 1024u)

typedef struct FeedSlot {
    unsigned char *pixels;
    uint32_t frame;
    int ready; /* atomic: 1 owned by the display, 0 free for the decoder */
} FeedSlot;

typedef struct FeedStored {
    unsigned char *data;
    uint32_t frame, bytes;
    int loaded; /* atomic: 1 owned by the decoder, 0 free for the reader */
} FeedStored;

typedef struct Feed {
    C3Movie *movie;
    FeedSlot slot[FEED_SLOTS];
    FeedStored stored[FEED_STORED];
    uint32_t read_next, read_count; /* reader only */
    uint32_t decode_count;          /* decoder only */
    uint32_t target;  /* atomic: the frame due on screen now */
    int stop, failed; /* atomic */
    Thread reader, decoder;
    int core;         /* decoder: 2 second core, 0 main core, -1 inline */
    char read_error[160], decode_error[160];
    /* Each written by its own stage only; read after both are joined. */
    uint64_t read_us, read_max_us, decode_us, decode_max_us, stored_bytes;
    uint32_t produced, read_dropped, decode_dropped;
} Feed;

static uint64_t ticks_us(uint64_t ticks)
{
    return ticks / (SYSCLOCK_ARM11 / 1000000u);
}

/* Read the next frame into the next stored buffer, in order. Returns 1 when
 * one was read, 0 when the ring is full or the movie is read, -1 on failure. */
static int read_step(Feed *feed)
{
    C3Movie *movie = feed->movie;
    FeedStored *stored = &feed->stored[feed->read_count % FEED_STORED];
    uint32_t target = __atomic_load_n(&feed->target, __ATOMIC_ACQUIRE);
    uint64_t start;
    /* Frames already late are never shown: skip them without reading. */
    if (feed->read_next < target) { feed->read_dropped += target - feed->read_next; feed->read_next = target; }
    if (feed->read_next >= movie->frame_count || __atomic_load_n(&stored->loaded, __ATOMIC_ACQUIRE)) return 0;
    start = svcGetSystemTick();
    if (c3_movie_load_into(movie, feed->read_next, stored->data, movie->stored_capacity, &stored->bytes,
                           feed->read_error, sizeof(feed->read_error))) return -1;
    uint64_t read = ticks_us(svcGetSystemTick() - start);
    feed->read_us += read;
    if (read > feed->read_max_us) feed->read_max_us = read;
    feed->stored_bytes += stored->bytes;
    stored->frame = feed->read_next++;
    ++feed->read_count;
    __atomic_store_n(&stored->loaded, 1, __ATOMIC_RELEASE);
    return 1;
}

/* Decode the oldest stored frame into a free slot. Returns 1 when one was
 * decoded or discarded as late, 0 when there is nothing to do, -1 on failure. */
static int decode_step(Feed *feed)
{
    FeedStored *stored = &feed->stored[feed->decode_count % FEED_STORED];
    FeedSlot *slot = NULL;
    uint64_t start;
    if (!__atomic_load_n(&stored->loaded, __ATOMIC_ACQUIRE)) return 0;
    if (stored->frame < __atomic_load_n(&feed->target, __ATOMIC_ACQUIRE)) {
        ++feed->decode_dropped; /* read in time, but late by now */
    } else {
        for (unsigned i = 0; i < FEED_SLOTS && !slot; ++i)
            if (!__atomic_load_n(&feed->slot[i].ready, __ATOMIC_ACQUIRE)) slot = &feed->slot[i];
        if (!slot) return 0;
        start = svcGetSystemTick();
        if (c3_movie_decode_stored(feed->movie, stored->data, stored->bytes, slot->pixels, C3_MOVIE_FRAME_BYTES,
                                   feed->decode_error, sizeof(feed->decode_error))) return -1;
        uint64_t decode = ticks_us(svcGetSystemTick() - start);
        feed->decode_us += decode;
        if (decode > feed->decode_max_us) feed->decode_max_us = decode;
        ++feed->produced;
        slot->frame = stored->frame;
        __atomic_store_n(&slot->ready, 1, __ATOMIC_RELEASE);
    }
    ++feed->decode_count;
    __atomic_store_n(&stored->loaded, 0, __ATOMIC_RELEASE);
    return 1;
}

static void feed_fail(Feed *feed)
{
    __atomic_store_n(&feed->failed, 1, __ATOMIC_RELEASE);
}

static void reader_worker(void *argument)
{
    Feed *feed = argument;
    while (!__atomic_load_n(&feed->stop, __ATOMIC_ACQUIRE)) {
        int made = read_step(feed);
        if (made < 0) { feed_fail(feed); return; }
        if (!made) svcSleepThread(1000000); /* ring full, or the whole movie is read */
    }
}

static void decoder_worker(void *argument)
{
    Feed *feed = argument;
    while (!__atomic_load_n(&feed->stop, __ATOMIC_ACQUIRE)) {
        /* Without a reader thread, this thread reads too. */
        int read = feed->reader ? 0 : read_step(feed);
        int made = read < 0 ? -1 : decode_step(feed);
        if (made < 0) { feed_fail(feed); return; }
        if (!made && !read) svcSleepThread(1000000);
    }
}

/* Inline mode (no threads at all): one read and one decode per vblank. */
static void feed_inline(Feed *feed)
{
    if (feed->core < 0 && (read_step(feed) < 0 || decode_step(feed) < 0)) feed_fail(feed);
}

static const char *feed_error(const Feed *feed)
{
    return feed->read_error[0] ? feed->read_error : feed->decode_error;
}

static void feed_stop(Feed *feed)
{
    __atomic_store_n(&feed->stop, 1, __ATOMIC_RELEASE);
    if (feed->decoder) { threadJoin(feed->decoder, U64_MAX); threadFree(feed->decoder); feed->decoder = NULL; }
    if (feed->reader) { threadJoin(feed->reader, U64_MAX); threadFree(feed->reader); feed->reader = NULL; }
    for (unsigned i = 0; i < FEED_SLOTS; ++i) { free(feed->slot[i].pixels); feed->slot[i].pixels = NULL; }
    for (unsigned i = 0; i < FEED_STORED; ++i) { free(feed->stored[i].data); feed->stored[i].data = NULL; }
}

static int feed_start(Feed *feed, C3Movie *movie)
{
    s32 priority = 0x30, lower;
    memset(feed, 0, sizeof(*feed));
    feed->movie = movie;
    for (unsigned i = 0; i < FEED_SLOTS; ++i)
        if (!(feed->slot[i].pixels = malloc(C3_MOVIE_FRAME_BYTES))) return -1;
    for (unsigned i = 0; i < FEED_STORED; ++i)
        if (!(feed->stored[i].data = malloc(movie->stored_capacity))) return -1;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    lower = priority < 0x3f ? priority + 1 : priority;
    /* The reader mostly waits on SD, so it shares the main core below the
     * main thread; its CPU time is the request and copy overhead only. */
    feed->reader = threadCreate(reader_worker, feed, FEED_STACK, lower, -2, false);
    feed->core = 2;
    feed->decoder = threadCreate(decoder_worker, feed, FEED_STACK, priority, 2, false);
    if (!feed->decoder) {
        /* Old 3DS: decoding below the main thread on the main core still
         * fills the main thread's vblank waits. */
        feed->core = 0;
        feed->decoder = threadCreate(decoder_worker, feed, FEED_STACK, lower, -2, false);
    }
    if (!feed->decoder) {
        feed->core = -1;
        if (feed->reader) { /* inline mode reads on the main thread */
            __atomic_store_n(&feed->stop, 1, __ATOMIC_RELEASE);
            threadJoin(feed->reader, U64_MAX); threadFree(feed->reader); feed->reader = NULL;
            __atomic_store_n(&feed->stop, 0, __ATOMIC_RELEASE);
        }
    }
    return 0;
}

/* The ready slot to show for `frame`: the latest one not after it. Older
 * ready slots are stale and are handed back to the decoder. */
static FeedSlot *feed_take(Feed *feed, uint32_t frame)
{
    FeedSlot *best = NULL;
    for (unsigned i = 0; i < FEED_SLOTS; ++i) {
        FeedSlot *slot = &feed->slot[i];
        if (!__atomic_load_n(&slot->ready, __ATOMIC_ACQUIRE) || slot->frame > frame) continue;
        if (!best || slot->frame > best->frame) best = slot;
    }
    for (unsigned i = 0; best && i < FEED_SLOTS; ++i) {
        FeedSlot *slot = &feed->slot[i];
        if (slot != best && __atomic_load_n(&slot->ready, __ATOMIC_ACQUIRE) && slot->frame < best->frame)
            __atomic_store_n(&slot->ready, 0, __ATOMIC_RELEASE);
    }
    return best;
}

static void feed_release(FeedSlot *slot)
{
    __atomic_store_n(&slot->ready, 0, __ATOMIC_RELEASE);
}

static void display_frame(const unsigned char *pixels)
{
    /* libctru's framebuffer is column-major and vertically inverted, which is
     * the decoded frame order: the original 4:3 picture is one contiguous
     * copy at column 40, between black side bars, without stretching. */
    unsigned char *framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    const size_t bar = 40u * 240u * 2u;
    memset(framebuffer, 0, bar);
    memcpy(framebuffer + bar, pixels, C3_MOVIE_FRAME_BYTES);
    memset(framebuffer + bar + C3_MOVIE_FRAME_BYTES, 0, bar);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

int c3_movie_player_run(const char *path, const char *label, FILE *trace,
                        char *error, size_t capacity)
{
    C3Movie movie = {0};
    MovieAudio audio = {0};
    C3N3dsInput input = {0};
    Feed feed;
    uint64_t previous, elapsed = 0, next_status = 0, preroll_us = 0, copy_us = 0, copy_max_us = 0;
    uint32_t last_frame = UINT32_MAX, displayed = 0, skipped = 0, late = 0;
    int result = -1;
    memset(&feed, 0, sizeof(feed));
    if (c3_movie_open(&movie, path, error, capacity)) return -1;
    if (feed_start(&feed, &movie)) {
        snprintf(error, capacity, "Movie frame allocation failed.");
        goto done;
    }
    audio_open(&audio, &movie);
    if (audio_queue(&audio, &movie, error, capacity)) goto done;
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    if (trace) {
        fprintf(trace, "movie_begin name=%s frames=%lu fps=%lu/%lu audio=%s mix=%s result=%08lx pack=v%lu feed=%s\n",
                label, (unsigned long)movie.frame_count, (unsigned long)movie.fps_num,
                (unsigned long)movie.fps_den, audio.available ? "ndsp" :
                movie.audio_sample_frames ? "unavailable" : "no_embedded_track",
                audio.mono ? "mono" : "stereo", (unsigned long)audio.result, (unsigned long)movie.version,
                feed.core == 2 ? "core2" : feed.core == 0 ? "core0" : "inline");
        fprintf(trace, "movie_feed name=%s reader=%s stored_capacity=%lu\n", label,
                feed.reader ? "thread" : feed.core < 0 ? "inline" : "decoder", (unsigned long)movie.stored_capacity);
        fflush(trace);
    }
    /* Pre-roll: the clock and the audio start with the first frame on screen,
     * so opening the file and the first read are not counted as lateness. */
    {
        uint64_t start = svcGetSystemTick();
        FeedSlot *first = NULL;
        while (!first) {
            feed_inline(&feed);
            if (__atomic_load_n(&feed.failed, __ATOMIC_ACQUIRE)) {
                snprintf(error, capacity, "%s", feed_error(&feed));
                goto done;
            }
            first = feed_take(&feed, 0);
            if (!first) svcSleepThread(1000000);
        }
        display_frame(first->pixels);
        feed_release(first);
        last_frame = 0; displayed = 1;
        preroll_us = ticks_us(svcGetSystemTick() - start);
    }
    previous = c3_n3ds_active_time_us();
    if (audio.available) ndspChnSetPaused(0, false);
    result = 0;
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_active_time_us();
        uint64_t delta = now - previous;
        previous = now;
        /* The application clock excludes real APT suspend/sleep. Count SD
         * stalls normally so late video catches up instead of slowing time. */
        elapsed += delta;
        uint32_t frame = c3_movie_frame_at(&movie, elapsed);
        if (frame == movie.frame_count) goto done;
        __atomic_store_n(&feed.target, frame, __ATOMIC_RELEASE);
        if (audio_queue(&audio, &movie, error, capacity)) { result = -1; goto done; }
        if (now >= next_status) {
            c3_bottom_screen_title(0,1);
            next_status = now + UINT64_C(250000);
        }
        feed_inline(&feed);
        if (__atomic_load_n(&feed.failed, __ATOMIC_ACQUIRE)) {
            snprintf(error, capacity, "%s", feed_error(&feed));
            result = -1;
            goto done;
        }
        if (frame != last_frame) {
            FeedSlot *slot = feed_take(&feed, frame);
            if (slot && slot->frame != last_frame) {
                uint64_t start = svcGetSystemTick(), copied;
                if (slot->frame > last_frame + 1u) skipped += slot->frame - last_frame - 1u;
                display_frame(slot->pixels);
                copied = ticks_us(svcGetSystemTick() - start);
                copy_us += copied;
                if (copied > copy_max_us) copy_max_us = copied;
                last_frame = slot->frame;
                ++displayed;
                feed_release(slot);
            }
            /* The due frame was not decoded in time for this vblank. */
            if (!slot || slot->frame != frame) ++late;
        }
        gspWaitForVBlank();
    }
    result = 1;
done:
    feed_stop(&feed);
    if (trace) {
        uint32_t produced = feed.produced ? feed.produced : 1u;
        /* skipped: frames never shown. Of those, read_dropped were late
         * before they were read and decode_dropped were read but late before
         * decoding. read runs on the reader thread, decode on the decoder
         * (feed=core2 on New 3DS), copy (memcpy, flush, swap) on the main
         * thread; with the pipeline a frame costs max(read, decode). */
        fprintf(trace, "movie_end name=%s result=%d displayed=%lu skipped=%lu elapsed_us=%llu\n",
                label, result, (unsigned long)displayed, (unsigned long)skipped,
                (unsigned long long)elapsed);
        fprintf(trace, "movie_timing name=%s pack=v%lu feed=%s preroll_us=%llu produced=%lu read_dropped=%lu decode_dropped=%lu late_vblanks=%lu "
                "read_us_avg=%llu read_us_max=%llu decode_us_avg=%llu decode_us_max=%llu "
                "copy_us_avg=%llu copy_us_max=%llu stored_bytes=%llu\n",
                label, (unsigned long)movie.version, feed.core == 2 ? "core2" : feed.core == 0 ? "core0" : "inline",
                (unsigned long long)preroll_us, (unsigned long)feed.produced, (unsigned long)feed.read_dropped,
                (unsigned long)feed.decode_dropped,
                (unsigned long)late, (unsigned long long)(feed.read_us / produced),
                (unsigned long long)feed.read_max_us, (unsigned long long)(feed.decode_us / produced),
                (unsigned long long)feed.decode_max_us,
                (unsigned long long)(copy_us / (displayed ? displayed : 1u)),
                (unsigned long long)copy_max_us, (unsigned long long)feed.stored_bytes);
        fflush(trace);
    }
    audio_close(&audio);
    c3_movie_close(&movie);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    return result;
}
