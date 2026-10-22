#include "movie_player.h"
#include "n3ds_platform.h"
#include "colosseum3ds/movie.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_BUFFERS 3u
#define AUDIO_FRAMES 1024u

typedef struct MovieAudio {
    ndspWaveBuf wave[AUDIO_BUFFERS];
    int16_t *samples;
    bool started;
    bool available;
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
        wave->nsamples = (u32)count;
        wave->looping = false;
        DSP_FlushDataCache(wave->data_vaddr, (u32)(count * 4u));
        ndspChnWaveBufAdd(0, wave);
    }
    return 0;
}

static void display_frame(const unsigned char *pixels)
{
    /* libctru's framebuffer is column-major and vertically inverted.
     * Center the original 4:3 image without stretching it to 5:3. */
    unsigned char *framebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    memset(framebuffer, 0, 400u * 240u * 2u);
    for (unsigned y = 0; y < C3_MOVIE_HEIGHT; ++y) {
        for (unsigned x = 0; x < C3_MOVIE_WIDTH; ++x) {
            size_t source = ((size_t)y * C3_MOVIE_WIDTH + x) * 2u;
            size_t destination = ((size_t)(x + 40u) * 240u + (239u - y)) * 2u;
            framebuffer[destination] = pixels[source];
            framebuffer[destination + 1] = pixels[source + 1];
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

int c3_movie_player_run(const char *path, const char *label, FILE *trace,
                        char *error, size_t capacity)
{
    C3Movie movie = {0};
    MovieAudio audio = {0};
    C3N3dsInput input = {0};
    unsigned char *pixels = NULL;
    uint64_t previous, elapsed = 0, next_status = 0;
    uint32_t last_frame = UINT32_MAX, displayed = 0, skipped = 0;
    int result = -1;
    if (c3_movie_open(&movie, path, error, capacity)) return -1;
    pixels = malloc(C3_MOVIE_FRAME_BYTES);
    if (!pixels) {
        snprintf(error, capacity, "Movie frame allocation failed.");
        goto done;
    }
    audio_open(&audio, &movie);
    if (audio_queue(&audio, &movie, error, capacity)) goto done;
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    if (trace) {
        fprintf(trace, "movie_begin name=%s frames=%lu fps=%lu/%lu audio=%s result=%08lx\n",
                label, (unsigned long)movie.frame_count, (unsigned long)movie.fps_num,
                (unsigned long)movie.fps_den, audio.available ? "ndsp" :
                movie.audio_sample_frames ? "unavailable" : "no_embedded_track", (unsigned long)audio.result);
        fflush(trace);
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
        if (audio_queue(&audio, &movie, error, capacity)) { result = -1; goto done; }
        if (now >= next_status) {
            consoleClear();
            printf("Colosseum3DS | native boot\n\n");
            printf("Original THP: %s\n", label);
            printf("Frame %lu / %lu | RGB565 320x240\n", (unsigned long)frame, (unsigned long)movie.frame_count);
            printf("Audio: %s\n\n", audio.available ? "PCM stereo / NDSP" :
                   movie.audio_sample_frames ? "NDSP unavailable" : "external BGM not connected");
            printf("PAD %04X | Stick %4d %4d\n", (unsigned)input.held, input.stick_x, input.stick_y);
            printf("Original boot logos play to completion.\n");
            printf("START + SELECT: exit\n");
            next_status = now + UINT64_C(250000);
        }
        if (frame != last_frame) {
            if (c3_movie_read_frame(&movie, frame, pixels, C3_MOVIE_FRAME_BYTES, error, capacity)) {
                result = -1;
                goto done;
            }
            if (last_frame != UINT32_MAX && frame > last_frame + 1u) skipped += frame - last_frame - 1u;
            display_frame(pixels);
            last_frame = frame;
            ++displayed;
        }
        gspWaitForVBlank();
    }
    result = 1;
done:
    if (trace) {
        fprintf(trace, "movie_end name=%s result=%d displayed=%lu skipped=%lu elapsed_us=%llu\n",
                label, result, (unsigned long)displayed, (unsigned long)skipped,
                (unsigned long long)elapsed);
        fflush(trace);
    }
    audio_close(&audio);
    free(pixels);
    c3_movie_close(&movie);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    return result;
}
