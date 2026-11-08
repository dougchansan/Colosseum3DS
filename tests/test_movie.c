#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/movie.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put32(unsigned char *p, uint32_t x)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (i * 8));
}

static void make_movie(const char *path, unsigned char header[64])
{
    unsigned char *frame = malloc(C3_MOVIE_FRAME_BYTES);
    const unsigned char pcm[] = {0x00, 0x80, 0xff, 0x7f, 0xff, 0xff, 0x34, 0x12};
    FILE *file = fopen(path, "wb");
    assert(file && frame);
    assert(fwrite(header, 1, 64, file) == 64);
    for (unsigned i = 0; i < 2; ++i) {
        memset(frame, (int)(0x21u + i), C3_MOVIE_FRAME_BYTES);
        assert(fwrite(frame, 1, C3_MOVIE_FRAME_BYTES, file) == C3_MOVIE_FRAME_BYTES);
    }
    assert(fwrite(pcm, 1, sizeof(pcm), file) == sizeof(pcm));
    assert(fclose(file) == 0);
    free(frame);
}

int main(void)
{
    char path[] = "/tmp/c3-movie-XXXXXX", error[160];
    int descriptor = mkstemp(path);
    unsigned char header[64] = {0}, *pixels = malloc(C3_MOVIE_FRAME_BYTES);
    C3Movie movie = {0};
    int16_t samples[4] = {0};
    size_t read_frames;
    assert(descriptor >= 0 && pixels);
    close(descriptor);
    memcpy(header, "C3V1", 4);
    const uint32_t fields[15] = {1, 64 + C3_MOVIE_FRAME_BYTES * 2 + 8,
        320, 240, 2, 30000, 1001, 64, C3_MOVIE_FRAME_BYTES,
        64 + C3_MOVIE_FRAME_BYTES * 2, 2, 32000, 2, 1, 0};
    for (unsigned i = 0; i < 15; ++i) put32(header + 4 + i * 4, fields[i]);
    make_movie(path, header);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) == 0);
    assert(c3_movie_frame_at(&movie, 0) == 0);
    assert(c3_movie_frame_at(&movie, 33366) == 0);
    assert(c3_movie_frame_at(&movie, 33367) == 1);
    assert(c3_movie_frame_at(&movie, 66734) == 2);
    assert(c3_movie_frame_at(&movie, UINT64_MAX) == 2);
    assert(c3_movie_read_frame(&movie, 1, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0);
    assert(pixels[0] == 0x22 && pixels[C3_MOVIE_FRAME_BYTES - 1] == 0x22);
    assert(c3_movie_read_audio(&movie, samples, 1, &read_frames, error, sizeof(error)) == 0);
    assert(read_frames == 1 && samples[0] == -32768 && samples[1] == 32767);
    assert(c3_movie_read_frame(&movie, 0, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0);
    assert(pixels[0] == 0x21);
    assert(c3_movie_read_audio(&movie, samples, 2, &read_frames, error, sizeof(error)) == 0);
    assert(read_frames == 1 && samples[0] == -1 && samples[1] == 0x1234);
    c3_movie_downmix_mono(samples, read_frames);
    assert(samples[0] == 2329 && samples[1] == 2329);
    assert(c3_movie_read_audio(&movie, samples, 2, &read_frames, error, sizeof(error)) == 0 && read_frames == 0);
    assert(c3_movie_read_frame(&movie, 2, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) != 0);
    assert(c3_movie_read_frame(&movie, 0, pixels, 2, error, sizeof(error)) != 0);
    c3_movie_close(&movie);
    /* Same-sign extremes detect 16-bit accumulation overflow; opposite signs
     * and asymmetric channels check actual mono mixing, not channel copying. */
    int16_t stereo[] = {-32768,-32768,32767,32767,-32768,32767,-2000,1000,1234,1234,77,88};
    const int16_t mono[] = {-32768,-32768,32767,32767,0,0,-500,-500,1234,1234,77,88};
    c3_movie_downmix_mono(stereo, 5);
    assert(!memcmp(stereo, mono, sizeof(stereo)));
    c3_movie_downmix_mono(stereo, 0);
    assert(!memcmp(stereo, mono, sizeof(stereo)));
    put32(header + 40, 64); /* Overlapping sections. */
    make_movie(path, header);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0 && !movie.video_file);
    put32(header + 40, 64 + C3_MOVIE_FRAME_BYTES * 2);
    put32(header + 20, UINT32_MAX); /* Overflowing count. */
    make_movie(path, header);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0);
    put32(header + 20, 2);
    put32(header + 8, 64 + C3_MOVIE_FRAME_BYTES * 2 + 12); /* Truncated audio. */
    put32(header + 44, 3);
    make_movie(path, header);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0);
    put32(header + 8, 64 + C3_MOVIE_FRAME_BYTES * 2 + 8);
    put32(header + 44, 2);
    make_movie(path, header);
    FILE *trailing = fopen(path, "ab");
    assert(trailing && fputc(0, trailing) == 0 && fclose(trailing) == 0);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0);
    unlink(path);
    free(pixels);
    puts("movie: frame timing, independent streams, PCM endian/mono, bounds and truncation passed");
    return 0;
}
