#include "bottom_screen.h"
#include "movie_player.h"
#include "n3ds_platform.h"
#include "colosseum3ds/movie.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

/* Four buffers of 256 ms: one 32 KiB read per buffer keeps a second of PCM
 * queued while the reader's video batches occupy the SD. */
#define AUDIO_BUFFERS 4u
#define AUDIO_FRAMES 8192u

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

/* Fill one free wave buffer from the movie's PCM stream. */
static int audio_fill(MovieAudio *audio, C3Movie *movie, ndspWaveBuf *wave, char *error, size_t capacity)
{
    size_t count;
    if (c3_movie_read_audio(movie, wave->data_pcm16, AUDIO_FRAMES, &count, error, capacity)) return -1;
    if (!count) return 0;
    if (audio->mono) c3_movie_downmix_mono(wave->data_pcm16, count);
    wave->nsamples = (u32)count;
    wave->looping = false;
    DSP_FlushDataCache(wave->data_vaddr, (u32)(count * 4u));
    ndspChnWaveBufAdd(0, wave);
    return 0;
}

/* Frames are read and decoded ahead of the display as a pipeline.
 *  - A reader thread on the main core (below the main thread) reads runs of
 *    consecutive stored frames in one SD request of up to FEED_BATCH bytes,
 *    hands them out through a ring of stored-frame entries, and keeps the
 *    audio wave buffers filled. The 3DS pays several milliseconds per FS
 *    request, so per-frame and 4 KiB audio reads were mostly overhead.
 *  - Up to two decoder threads, on the New 3DS's second application core and
 *    on the main core below the main thread, each claim the oldest stored
 *    frame and unpack it into a free framebuffer-ordered slot.
 *  - The main thread only picks the due slot, copies it into the top
 *    framebuffer and keeps the vblank pace, so neither SD latency nor
 *    decoding holds up a swap. */
#define FEED_SLOTS 4u
#define FEED_STORED 16u
#define FEED_BATCH (768u * 1024u)
#define FEED_STACK (32u * 1024u)
#define FEED_DECODERS 2u
#define FEED_LOW_WATER 4u /* stored frames left before the reader tops up anyway */

enum { ENTRY_FREE, ENTRY_LOADED, ENTRY_CLAIMED };
enum { SLOT_FREE, SLOT_READY, SLOT_DECODING };

typedef struct FeedSlot {
    unsigned char *pixels;
    uint32_t frame;
    int state; /* atomic SLOT_* */
} FeedSlot;

typedef struct FeedStored {
    unsigned char *data;
    uint32_t frame, bytes, sequence;
    int state; /* atomic ENTRY_* */
} FeedStored;

typedef struct FeedDecoder {
    Thread thread;
    struct Feed *feed;
    int requested_core, actual_core;
    s32 priority;
    char error[160];
    /* Written by this decoder only; read after it is joined. */
    uint64_t decode_us, decode_max_us;
    uint32_t produced, dropped;
} FeedDecoder;

typedef struct Feed {
    C3Movie *movie;
    MovieAudio *audio;
    FeedSlot slot[FEED_SLOTS];
    FeedStored stored[FEED_STORED];
    FeedDecoder decoder[FEED_DECODERS];
    unsigned decoders;
    unsigned char *batch;
    uint32_t read_next, read_count; /* reader only */
    uint32_t decode_next;           /* atomic: sequence of the next entry to claim */
    uint32_t target;  /* atomic: the frame due on screen now */
    int stop, failed; /* atomic */
    Thread reader;
    int reader_core;
    char read_error[160];
    /* Reader statistics; read after it is joined. */
    uint64_t read_us, read_max_us, stored_bytes, audio_us, audio_max_us;
    uint32_t batches, frames_read, read_dropped, audio_reads;
} Feed;

static uint64_t ticks_us(uint64_t ticks)
{
    return ticks / (SYSCLOCK_ARM11 / 1000000u);
}

static void feed_fail(Feed *feed)
{
    __atomic_store_n(&feed->failed, 1, __ATOMIC_RELEASE);
}

/* Refill finished audio wave buffers (reader thread, or the main thread
 * before the reader starts). */
static int audio_refill(Feed *feed, char *error, size_t capacity)
{
    if (!feed->audio || !feed->audio->available) return 0;
    uint64_t start = svcGetSystemTick();
    int before = (int)feed->audio_reads;
    for (unsigned i = 0; i < AUDIO_BUFFERS; ++i) {
        ndspWaveBuf *wave = &feed->audio->wave[i];
        if (wave->status != NDSP_WBUF_FREE && wave->status != NDSP_WBUF_DONE) continue;
        if (audio_fill(feed->audio, feed->movie, wave, error, capacity)) return -1;
        ++feed->audio_reads;
    }
    if ((int)feed->audio_reads != before) {
        uint64_t spent = ticks_us(svcGetSystemTick() - start);
        feed->audio_us += spent;
        if (spent > feed->audio_max_us) feed->audio_max_us = spent;
    }
    return 0;
}

/* Read the next run of frames that fits the free entries and FEED_BATCH in
 * one request. Returns 1 when frames were read, 0 when there is nothing to
 * do, -1 on failure. */
static int read_step(Feed *feed)
{
    C3Movie *movie = feed->movie;
    uint32_t target = __atomic_load_n(&feed->target, __ATOMIC_ACQUIRE), count = 0, bytes = 0, got;
    uint64_t start, spent;
    if (audio_refill(feed, feed->read_error, sizeof(feed->read_error))) return -1;
    /* Frames already late are never shown: skip them without reading. */
    if (feed->read_next < target) { feed->read_dropped += target - feed->read_next; feed->read_next = target; }
    while (feed->read_next + count < movie->frame_count && count < FEED_STORED) {
        FeedStored *entry = &feed->stored[(feed->read_count + count) % FEED_STORED];
        uint32_t size = c3_movie_stored_size(movie, feed->read_next + count);
        if (__atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) != ENTRY_FREE) break;
        if (count && bytes + size > FEED_BATCH) break;
        bytes += size;
        ++count;
    }
    if (!count) return 0;
    /* Top up in batches: while enough frames wait for the decoders, read
     * only once half the ring (or the rest of the movie) is free, so each
     * request carries several frames instead of the one just released. */
    if (count < FEED_STORED / 2u && feed->read_next + count < movie->frame_count && bytes < FEED_BATCH / 2u &&
        feed->read_count - __atomic_load_n(&feed->decode_next, __ATOMIC_ACQUIRE) >= FEED_LOW_WATER)
        return 0;
    start = svcGetSystemTick();
    if (c3_movie_load_range(movie, feed->read_next, count, feed->batch, FEED_BATCH, &got,
                            feed->read_error, sizeof(feed->read_error))) return -1;
    spent = ticks_us(svcGetSystemTick() - start);
    feed->read_us += spent;
    if (spent > feed->read_max_us) feed->read_max_us = spent;
    feed->stored_bytes += got;
    ++feed->batches;
    feed->frames_read += count;
    for (uint32_t i = 0, at = 0; i < count; ++i) {
        FeedStored *entry = &feed->stored[feed->read_count % FEED_STORED];
        entry->bytes = c3_movie_stored_size(movie, feed->read_next);
        memcpy(entry->data, feed->batch + at, entry->bytes);
        at += entry->bytes;
        entry->frame = feed->read_next++;
        entry->sequence = feed->read_count++;
        __atomic_store_n(&entry->state, ENTRY_LOADED, __ATOMIC_RELEASE);
    }
    return 1;
}

/* Claim the oldest loaded entry and decode it into a free slot. Returns 1
 * when one was decoded or discarded as late, 0 when there is nothing to do,
 * -1 on failure. */
static int decode_step(Feed *feed, FeedDecoder *decoder)
{
    FeedSlot *slot = NULL;
    FeedStored *entry;
    uint32_t sequence;
    for (unsigned i = 0; i < FEED_SLOTS && !slot; ++i) {
        int expected = SLOT_FREE;
        if (__atomic_compare_exchange_n(&feed->slot[i].state, &expected, SLOT_DECODING, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) slot = &feed->slot[i];
    }
    if (!slot) return 0;
    for (;;) {
        sequence = __atomic_load_n(&feed->decode_next, __ATOMIC_ACQUIRE);
        entry = &feed->stored[sequence % FEED_STORED];
        if (__atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) != ENTRY_LOADED || entry->sequence != sequence) {
            __atomic_store_n(&slot->state, SLOT_FREE, __ATOMIC_RELEASE);
            return 0;
        }
        if (__atomic_compare_exchange_n(&feed->decode_next, &sequence, sequence + 1u, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
    }
    __atomic_store_n(&entry->state, ENTRY_CLAIMED, __ATOMIC_RELEASE);
    if (entry->frame < __atomic_load_n(&feed->target, __ATOMIC_ACQUIRE)) {
        ++decoder->dropped; /* read in time, but late by now */
        __atomic_store_n(&slot->state, SLOT_FREE, __ATOMIC_RELEASE);
    } else {
        uint64_t start = svcGetSystemTick();
        if (c3_movie_decode_stored(feed->movie, entry->data, entry->bytes, slot->pixels, C3_MOVIE_FRAME_BYTES,
                                   decoder->error, sizeof(decoder->error))) return -1;
        uint64_t spent = ticks_us(svcGetSystemTick() - start);
        decoder->decode_us += spent;
        if (spent > decoder->decode_max_us) decoder->decode_max_us = spent;
        ++decoder->produced;
        slot->frame = entry->frame;
        __atomic_store_n(&slot->state, SLOT_READY, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&entry->state, ENTRY_FREE, __ATOMIC_RELEASE);
    return 1;
}

static void reader_worker(void *argument)
{
    Feed *feed = argument;
    feed->reader_core = svcGetProcessorID();
    while (!__atomic_load_n(&feed->stop, __ATOMIC_ACQUIRE)) {
        int made = read_step(feed);
        if (made < 0) { feed_fail(feed); return; }
        if (!made) svcSleepThread(1000000); /* ring full, or the whole movie is read */
    }
}

static void decoder_worker(void *argument)
{
    FeedDecoder *decoder = argument;
    Feed *feed = decoder->feed;
    decoder->actual_core = svcGetProcessorID();
    while (!__atomic_load_n(&feed->stop, __ATOMIC_ACQUIRE)) {
        int made = decode_step(feed, decoder);
        if (made < 0) { feed_fail(feed); return; }
        if (!made) svcSleepThread(1000000);
    }
}

/* Without threads, the main thread reads and decodes one step per vblank. */
static void feed_inline(Feed *feed)
{
    if (!feed->reader && read_step(feed) < 0) feed_fail(feed);
    if (!feed->decoders && decode_step(feed, &feed->decoder[0]) < 0) feed_fail(feed);
}

static const char *feed_error(const Feed *feed)
{
    if (feed->read_error[0]) return feed->read_error;
    for (unsigned i = 0; i < FEED_DECODERS; ++i)
        if (feed->decoder[i].error[0]) return feed->decoder[i].error;
    return "Movie playback failed.";
}

static void feed_stop(Feed *feed)
{
    __atomic_store_n(&feed->stop, 1, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < FEED_DECODERS; ++i)
        if (feed->decoder[i].thread) {
            threadJoin(feed->decoder[i].thread, U64_MAX);
            threadFree(feed->decoder[i].thread);
            feed->decoder[i].thread = NULL;
        }
    if (feed->reader) { threadJoin(feed->reader, U64_MAX); threadFree(feed->reader); feed->reader = NULL; }
    for (unsigned i = 0; i < FEED_SLOTS; ++i) { free(feed->slot[i].pixels); feed->slot[i].pixels = NULL; }
    for (unsigned i = 0; i < FEED_STORED; ++i) { free(feed->stored[i].data); feed->stored[i].data = NULL; }
    free(feed->batch); feed->batch = NULL;
}

static int feed_start(Feed *feed, C3Movie *movie, MovieAudio *audio)
{
    s32 priority = 0x30, lower;
    static const int cores[FEED_DECODERS] = {2, 0};
    memset(feed, 0, sizeof(*feed));
    feed->movie = movie;
    feed->audio = audio;
    feed->reader_core = feed->decoder[0].actual_core = feed->decoder[1].actual_core = -1;
    if (!(feed->batch = malloc(FEED_BATCH))) return -1;
    for (unsigned i = 0; i < FEED_SLOTS; ++i)
        if (!(feed->slot[i].pixels = malloc(C3_MOVIE_FRAME_BYTES))) return -1;
    for (unsigned i = 0; i < FEED_STORED; ++i)
        if (!(feed->stored[i].data = malloc(movie->stored_capacity))) return -1;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    lower = priority < 0x3f ? priority + 1 : priority;
    /* The reader mostly waits on SD, so it shares the main core below the
     * main thread; its CPU time is the request and copy overhead only. */
    feed->reader = threadCreate(reader_worker, feed, FEED_STACK, lower, 0, false);
    /* Second core at the main thread's priority (nothing else of ours runs
     * there); main core below the main thread, filling its vblank waits. A
     * decoder that cannot be created (Old 3DS has no core 2) is skipped. */
    for (unsigned i = 0; i < FEED_DECODERS; ++i) {
        FeedDecoder *decoder = &feed->decoder[feed->decoders];
        decoder->feed = feed;
        decoder->requested_core = cores[i];
        decoder->priority = cores[i] == 2 ? priority : lower;
        decoder->thread = threadCreate(decoder_worker, decoder, FEED_STACK, decoder->priority, cores[i], false);
        if (decoder->thread) ++feed->decoders;
    }
    return 0;
}

/* The ready slot to show next: the latest one after `shown` and not after
 * `due`. Ready slots at or before it are stale and are handed back. */
static FeedSlot *feed_take(Feed *feed, uint32_t due, uint32_t shown)
{
    FeedSlot *best = NULL;
    for (unsigned i = 0; i < FEED_SLOTS; ++i) {
        FeedSlot *slot = &feed->slot[i];
        if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != SLOT_READY || slot->frame > due) continue;
        if (shown != UINT32_MAX && slot->frame <= shown) {
            __atomic_store_n(&slot->state, SLOT_FREE, __ATOMIC_RELEASE);
            continue;
        }
        if (!best || slot->frame > best->frame) best = slot;
    }
    for (unsigned i = 0; best && i < FEED_SLOTS; ++i) {
        FeedSlot *slot = &feed->slot[i];
        if (slot != best && __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == SLOT_READY &&
            slot->frame < best->frame)
            __atomic_store_n(&slot->state, SLOT_FREE, __ATOMIC_RELEASE);
    }
    return best;
}

static void feed_release(FeedSlot *slot)
{
    __atomic_store_n(&slot->state, SLOT_FREE, __ATOMIC_RELEASE);
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

static const char *codec_name(const C3Movie *movie)
{
    return movie->version == 1 ? "raw" : movie->pixel_format == 3u ? "lz4" : "zstd";
}

int c3_movie_player_run(const char *path, const char *label, FILE *trace,
                        char *error, size_t capacity)
{
    C3Movie movie = {0};
    MovieAudio audio = {0};
    C3N3dsInput input = {0};
    Feed feed;
    s32 main_priority = 0x30;
    uint64_t previous, elapsed = 0, next_status = 0, preroll_us = 0, copy_us = 0, copy_max_us = 0;
    uint32_t last_frame = UINT32_MAX, displayed = 0, skipped = 0, late = 0;
    int result = -1;
    memset(&feed, 0, sizeof(feed));
    svcGetThreadPriority(&main_priority, CUR_THREAD_HANDLE);
    if (c3_movie_open(&movie, path, error, capacity)) return -1;
    audio_open(&audio, &movie);
    if (feed_start(&feed, &movie, &audio)) {
        snprintf(error, capacity, "Movie frame allocation failed.");
        goto done;
    }
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    if (trace) {
        fprintf(trace, "movie_begin name=%s frames=%lu fps=%lu/%lu audio=%s mix=%s result=%08lx pack=v%lu codec=%s\n",
                label, (unsigned long)movie.frame_count, (unsigned long)movie.fps_num,
                (unsigned long)movie.fps_den, audio.available ? "ndsp" :
                movie.audio_sample_frames ? "unavailable" : "no_embedded_track",
                audio.mono ? "mono" : "stereo", (unsigned long)audio.result, (unsigned long)movie.version,
                codec_name(&movie));
        fprintf(trace, "movie_feed name=%s main_core=%d main_priority=0x%02lx reader=%s decoders=%u "
                "stored_capacity=%lu batch_bytes=%u\n", label, (int)svcGetProcessorID(), (unsigned long)main_priority,
                feed.reader ? "thread" : "inline", feed.decoders, (unsigned long)movie.stored_capacity, FEED_BATCH);
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
            first = feed_take(&feed, 0, UINT32_MAX);
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
            FeedSlot *slot = feed_take(&feed, frame, last_frame);
            uint32_t shown = slot ? slot->frame : UINT32_MAX;
            if (slot) {
                uint64_t start = svcGetSystemTick(), copied;
                if (shown > last_frame + 1u) skipped += shown - last_frame - 1u;
                display_frame(slot->pixels);
                copied = ticks_us(svcGetSystemTick() - start);
                copy_us += copied;
                if (copied > copy_max_us) copy_max_us = copied;
                last_frame = shown;
                ++displayed;
                feed_release(slot);
            }
            /* The due frame was not decoded in time for this vblank. */
            if (shown != frame) ++late;
        }
        gspWaitForVBlank();
    }
    result = 1;
done:
    feed_stop(&feed);
    if (trace) {
        uint32_t produced = 0, decode_dropped = 0;
        for (unsigned i = 0; i < FEED_DECODERS; ++i) {
            produced += feed.decoder[i].produced;
            decode_dropped += feed.decoder[i].dropped;
        }
        /* skipped: frames never shown. read_dropped were late before they were
         * read, decode_dropped were read but late before decoding. Reads are
         * batched (batches, us per batch, MB/s while reading); decode is per
         * decoder thread with the core it actually ran on; copy (memcpy,
         * flush, swap) is on the main thread. */
        fprintf(trace, "movie_end name=%s result=%d displayed=%lu skipped=%lu elapsed_us=%llu\n",
                label, result, (unsigned long)displayed, (unsigned long)skipped,
                (unsigned long long)elapsed);
        fprintf(trace, "movie_timing name=%s codec=%s preroll_us=%llu produced=%lu read_dropped=%lu decode_dropped=%lu "
                "late_vblanks=%lu copy_us_avg=%llu copy_us_max=%llu\n",
                label, codec_name(&movie), (unsigned long long)preroll_us, (unsigned long)produced,
                (unsigned long)feed.read_dropped, (unsigned long)decode_dropped, (unsigned long)late,
                (unsigned long long)(copy_us / (displayed ? displayed : 1u)), (unsigned long long)copy_max_us);
        fprintf(trace, "movie_read name=%s core=%d batches=%lu frames=%lu bytes=%llu batch_us_avg=%llu batch_us_max=%llu "
                "read_kib_s=%llu audio_reads=%lu audio_us_avg=%llu audio_us_max=%llu\n",
                label, feed.reader_core, (unsigned long)feed.batches, (unsigned long)feed.frames_read,
                (unsigned long long)feed.stored_bytes,
                (unsigned long long)(feed.read_us / (feed.batches ? feed.batches : 1u)),
                (unsigned long long)feed.read_max_us,
                (unsigned long long)(feed.read_us ? feed.stored_bytes * 1000000u / 1024u / feed.read_us : 0u),
                (unsigned long)feed.audio_reads,
                (unsigned long long)(feed.audio_us / (feed.audio_reads ? feed.audio_reads : 1u)),
                (unsigned long long)feed.audio_max_us);
        for (unsigned i = 0; i < FEED_DECODERS; ++i) {
            const FeedDecoder *decoder = &feed.decoder[i];
            if (!decoder->requested_core && !decoder->priority && !decoder->produced) continue;
            fprintf(trace, "movie_decode name=%s decoder=%u requested_core=%d core=%d priority=0x%02lx produced=%lu "
                    "dropped=%lu decode_us_avg=%llu decode_us_max=%llu\n",
                    label, i, decoder->requested_core, decoder->actual_core, (unsigned long)decoder->priority,
                    (unsigned long)decoder->produced, (unsigned long)decoder->dropped,
                    (unsigned long long)(decoder->decode_us / (decoder->produced ? decoder->produced : 1u)),
                    (unsigned long long)decoder->decode_max_us);
        }
        fflush(trace);
    }
    audio_close(&audio);
    c3_movie_close(&movie);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    return result;
}
