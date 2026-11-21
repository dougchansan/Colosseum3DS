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

/* Frames are read and decoded ahead of the display by a feed thread, on the
 * New 3DS's second application core where available, into a small ring of
 * framebuffer-ordered slots. The main thread only picks the due slot, copies
 * it into the top framebuffer, and keeps the audio queue and vblank pace, so
 * neither SD latency nor decoding holds up a swap. */
#define FEED_SLOTS 3u
#define FEED_STACK (32u * 1024u)

typedef struct FeedSlot {
    unsigned char *pixels;
    uint32_t frame;
    int ready; /* atomic: 1 owned by the display, 0 free for the feed */
} FeedSlot;

typedef struct Feed {
    C3Movie *movie;
    FeedSlot slot[FEED_SLOTS];
    uint32_t next;  /* feed thread: next frame to produce */
    uint32_t target; /* atomic: the frame due on screen now */
    int stop, failed; /* atomic */
    Thread thread;
    int core;       /* 2: second core, 0: main core thread, -1: inline */
    char error[160];
    /* Written by the producer only; read after it is joined. */
    uint64_t read_us, read_max_us, decode_us, decode_max_us, stored_bytes;
    uint32_t produced, dropped;
} Feed;

static uint64_t ticks_us(uint64_t ticks)
{
    return ticks / (SYSCLOCK_ARM11 / 1000000u);
}

/* Produce the next frame into a free slot. Returns 1 when one was made, 0
 * when no slot is free or the movie is complete, -1 on failure. */
static int feed_step(Feed *feed)
{
    C3Movie *movie = feed->movie;
    uint32_t target = __atomic_load_n(&feed->target, __ATOMIC_ACQUIRE);
    FeedSlot *slot = NULL;
    uint64_t start, loaded, decoded;
    /* Frames already late are never shown: skip them without reading. */
    if (feed->next < target) { feed->dropped += target - feed->next; feed->next = target; }
    if (feed->next >= movie->frame_count) return 0;
    for (unsigned i = 0; i < FEED_SLOTS && !slot; ++i)
        if (!__atomic_load_n(&feed->slot[i].ready, __ATOMIC_ACQUIRE)) slot = &feed->slot[i];
    if (!slot) return 0;
    start = svcGetSystemTick();
    if (c3_movie_load_frame(movie, feed->next, feed->error, sizeof(feed->error))) return -1;
    loaded = svcGetSystemTick();
    if (c3_movie_decode_frame(movie, slot->pixels, C3_MOVIE_FRAME_BYTES,
                              feed->error, sizeof(feed->error))) return -1;
    decoded = svcGetSystemTick();
    uint64_t read = ticks_us(loaded - start), decode = ticks_us(decoded - loaded);
    feed->read_us += read; feed->decode_us += decode;
    if (read > feed->read_max_us) feed->read_max_us = read;
    if (decode > feed->decode_max_us) feed->decode_max_us = decode;
    feed->stored_bytes += movie->stored_bytes;
    ++feed->produced;
    slot->frame = feed->next++;
    __atomic_store_n(&slot->ready, 1, __ATOMIC_RELEASE);
    return 1;
}

static void feed_worker(void *argument)
{
    Feed *feed = argument;
    while (!__atomic_load_n(&feed->stop, __ATOMIC_ACQUIRE)) {
        int made = feed_step(feed);
        if (made < 0) { __atomic_store_n(&feed->failed, 1, __ATOMIC_RELEASE); return; }
        if (!made) {
            if (feed->next >= feed->movie->frame_count) return;
            svcSleepThread(1000000); /* every slot is waiting for the display */
        }
    }
}

static void feed_stop(Feed *feed)
{
    if (feed->thread) {
        __atomic_store_n(&feed->stop, 1, __ATOMIC_RELEASE);
        threadJoin(feed->thread, U64_MAX);
        threadFree(feed->thread);
        feed->thread = NULL;
    }
    for (unsigned i = 0; i < FEED_SLOTS; ++i) { free(feed->slot[i].pixels); feed->slot[i].pixels = NULL; }
}

static int feed_start(Feed *feed, C3Movie *movie)
{
    s32 priority = 0x30;
    memset(feed, 0, sizeof(*feed));
    feed->movie = movie;
    for (unsigned i = 0; i < FEED_SLOTS; ++i) {
        feed->slot[i].pixels = malloc(C3_MOVIE_FRAME_BYTES);
        if (!feed->slot[i].pixels) return -1;
    }
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    feed->core = 2;
    feed->thread = threadCreate(feed_worker, feed, FEED_STACK, priority, 2, false);
    if (!feed->thread) {
        /* Old 3DS: a lower-priority thread on the main core still overlaps
         * SD reads and decoding with the main thread's vblank waits. */
        feed->core = 0;
        feed->thread = threadCreate(feed_worker, feed, FEED_STACK, priority < 0x3f ? priority + 1 : priority, -2, false);
    }
    if (!feed->thread) feed->core = -1;
    return 0;
}

/* The ready slot to show for `frame`: the latest one not after it. Older
 * ready slots are stale and are handed back to the feed. */
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
        fflush(trace);
    }
    /* Pre-roll: the clock and the audio start with the first frame on screen,
     * so opening the file and the first read are not counted as lateness. */
    {
        uint64_t start = svcGetSystemTick();
        FeedSlot *first = NULL;
        while (!first) {
            if (feed.core < 0 && feed_step(&feed) < 0) __atomic_store_n(&feed.failed, 1, __ATOMIC_RELEASE);
            if (__atomic_load_n(&feed.failed, __ATOMIC_ACQUIRE)) {
                snprintf(error, capacity, "%s", feed.error);
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
        if (feed.core < 0 && feed_step(&feed) < 0) __atomic_store_n(&feed.failed, 1, __ATOMIC_RELEASE);
        if (__atomic_load_n(&feed.failed, __ATOMIC_ACQUIRE)) {
            snprintf(error, capacity, "%s", feed.error);
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
        /* skipped: frames never shown; dropped: of those, never even read
         * (already late when the feed reached them). read/decode run on the
         * feed thread, copy (memcpy + flush + swap) on the main thread. */
        fprintf(trace, "movie_end name=%s result=%d displayed=%lu skipped=%lu elapsed_us=%llu\n",
                label, result, (unsigned long)displayed, (unsigned long)skipped,
                (unsigned long long)elapsed);
        fprintf(trace, "movie_timing name=%s pack=v%lu feed=%s preroll_us=%llu produced=%lu dropped=%lu late_vblanks=%lu "
                "read_us_avg=%llu read_us_max=%llu decode_us_avg=%llu decode_us_max=%llu "
                "copy_us_avg=%llu copy_us_max=%llu stored_bytes=%llu\n",
                label, (unsigned long)movie.version, feed.core == 2 ? "core2" : feed.core == 0 ? "core0" : "inline",
                (unsigned long long)preroll_us, (unsigned long)feed.produced, (unsigned long)feed.dropped,
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
