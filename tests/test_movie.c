#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/movie.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zstd.h>

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

static uint16_t pattern(unsigned frame, unsigned x, unsigned y)
{
    return (uint16_t)(x * 131u + y * 7919u + frame * 40503u);
}

/* Each decoded pixel (x, y) sits at column x, row 239 - y. */
static int framebuffer_order(const unsigned char *pixels, unsigned frame)
{
    const uint16_t *p = (const uint16_t *)(const void *)pixels;
    for (unsigned y = 0; y < C3_MOVIE_HEIGHT; ++y)
        for (unsigned x = 0; x < C3_MOVIE_WIDTH; ++x)
            if (p[x * C3_MOVIE_HEIGHT + (C3_MOVIE_HEIGHT - 1u - y)] != pattern(frame, x, y)) return 0;
    return 1;
}

/* Version 1 (raw, row-major) and version 2 (zstd frames in framebuffer
 * order behind an offset index) of the same two-frame movie with PCM. */
static void make_pattern_movie(const char *path, unsigned version, uint32_t *index_out)
{
    uint16_t *frame = malloc(C3_MOVIE_FRAME_BYTES);
    unsigned char *packed[2] = {0}, header[64] = {0}, word[4];
    size_t sizes[2] = {0};
    const unsigned char pcm[] = {0x00, 0x80, 0xff, 0x7f, 0xff, 0xff, 0x34, 0x12};
    uint32_t index[3], video_end;
    FILE *file = fopen(path, "wb");
    assert(file && frame);
    for (unsigned f = 0; f < 2; ++f)
        for (unsigned y = 0; y < C3_MOVIE_HEIGHT; ++y)
            for (unsigned x = 0; x < C3_MOVIE_WIDTH; ++x) {
                if (version == 1) frame[y * C3_MOVIE_WIDTH + x] = pattern(f, x, y);
                else frame[x * C3_MOVIE_HEIGHT + (C3_MOVIE_HEIGHT - 1u - y)] = pattern(f, x, y);
                if (version == 2 && y == C3_MOVIE_HEIGHT - 1u && x == C3_MOVIE_WIDTH - 1u) {
                    packed[f] = malloc(ZSTD_compressBound(C3_MOVIE_FRAME_BYTES));
                    sizes[f] = ZSTD_compress(packed[f], ZSTD_compressBound(C3_MOVIE_FRAME_BYTES),
                                             frame, C3_MOVIE_FRAME_BYTES, 3);
                    assert(!ZSTD_isError(sizes[f]));
                } else if (version == 1 && y == C3_MOVIE_HEIGHT - 1u && x == C3_MOVIE_WIDTH - 1u) {
                    packed[f] = malloc(C3_MOVIE_FRAME_BYTES);
                    memcpy(packed[f], frame, C3_MOVIE_FRAME_BYTES);
                    sizes[f] = C3_MOVIE_FRAME_BYTES;
                }
            }
    index[0] = version == 2 ? 64u + 3u * 4u : 64u;
    index[1] = index[0] + (uint32_t)sizes[0];
    index[2] = video_end = index[1] + (uint32_t)sizes[1];
    memcpy(header, "C3V1", 4);
    const uint32_t fields[15] = {version, video_end + 8u, 320, 240, 2, 30000, 1001, 64,
        C3_MOVIE_FRAME_BYTES, video_end, 2, 32000, 2, version, 0};
    for (unsigned i = 0; i < 15; ++i) put32(header + 4 + i * 4, fields[i]);
    assert(fwrite(header, 1, 64, file) == 64);
    for (unsigned i = 0; version == 2 && i < 3; ++i) {
        put32(word, index[i]);
        assert(fwrite(word, 1, 4, file) == 4);
    }
    for (unsigned f = 0; f < 2; ++f) assert(fwrite(packed[f], 1, sizes[f], file) == sizes[f]);
    assert(fwrite(pcm, 1, sizeof(pcm), file) == sizeof(pcm));
    assert(fclose(file) == 0);
    if (index_out) memcpy(index_out, index, sizeof(index));
    free(packed[0]); free(packed[1]); free(frame);
}

static void patch(const char *path, long offset, const void *bytes, size_t size)
{
    FILE *file = fopen(path, "r+b");
    assert(file && fseek(file, offset, SEEK_SET) == 0 && fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}

static void packed_movies(const char *path, unsigned char *pixels)
{
    C3Movie movie = {0};
    char error[160];
    int16_t samples[4];
    size_t read_frames;
    uint32_t index[3];
    unsigned char word[4];
    for (unsigned version = 1; version <= 2; ++version) {
        make_pattern_movie(path, version, index);
        assert(c3_movie_open(&movie, path, error, sizeof(error)) == 0 && movie.version == version);
        assert(c3_movie_read_frame(&movie, 1, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0);
        assert(framebuffer_order(pixels, 1));
        assert(c3_movie_load_frame(&movie, 0, error, sizeof(error)) == 0);
        assert(movie.stored_bytes == index[1] - index[0]);
        assert(c3_movie_decode_frame(&movie, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0);
        assert(framebuffer_order(pixels, 0));
        assert(c3_movie_decode_frame(&movie, pixels + 1, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) != 0);
        {   /* Caller-owned stored buffers (reader/decoder pipeline). */
            unsigned char *stored = malloc(movie.stored_capacity);
            uint32_t bytes = 0;
            assert(stored);
            assert(c3_movie_load_into(&movie, 1, stored, movie.stored_capacity - 1u, &bytes, error, sizeof(error)) != 0 ||
                   index[2] - index[1] < movie.stored_capacity);
            assert(c3_movie_load_into(&movie, 1, stored, movie.stored_capacity, &bytes, error, sizeof(error)) == 0);
            assert(bytes == index[2] - index[1]);
            assert(c3_movie_decode_stored(&movie, stored, bytes, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0);
            assert(framebuffer_order(pixels, 1));
            assert(c3_movie_load_into(&movie, 2, stored, movie.stored_capacity, &bytes, error, sizeof(error)) != 0);
            free(stored);
        }
        assert(c3_movie_read_audio(&movie, samples, 2, &read_frames, error, sizeof(error)) == 0);
        assert(read_frames == 2 && samples[0] == -32768 && samples[3] == 0x1234);
        assert(c3_movie_read_frame(&movie, 2, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) != 0);
        c3_movie_close(&movie);
    }
    /* Version 2 index and payload faults. */
    put32(word, 1);
    patch(path, 56, word, 4); /* Raw pixel format under version 2. */
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0 && !movie.video_file);
    put32(word, 2);
    patch(path, 56, word, 4);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) == 0);
    c3_movie_close(&movie);
    put32(word, index[0] + 4u); /* First frame not after the index. */
    patch(path, 64, word, 4);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0 && !movie.frame_offsets);
    put32(word, index[0]);
    patch(path, 64, word, 4);
    put32(word, index[0]); /* Empty (non-increasing) frame. */
    patch(path, 68, word, 4);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0);
    put32(word, index[1]);
    patch(path, 68, word, 4);
    put32(word, index[2] - 1u); /* Video end differs from the audio offset. */
    patch(path, 72, word, 4);
    assert(c3_movie_open(&movie, path, error, sizeof(error)) != 0);
    put32(word, index[2]);
    patch(path, 72, word, 4);
    {const unsigned char junk[4] = {0xff, 0xff, 0xff, 0xff};
     patch(path, (long)index[0] + 6, junk, sizeof(junk));} /* Corrupt zstd block. */
    assert(c3_movie_open(&movie, path, error, sizeof(error)) == 0);
    assert(c3_movie_read_frame(&movie, 0, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) != 0 ||
           !framebuffer_order(pixels, 0));
    assert(c3_movie_read_frame(&movie, 1, pixels, C3_MOVIE_FRAME_BYTES, error, sizeof(error)) == 0 &&
           framebuffer_order(pixels, 1));
    c3_movie_close(&movie);
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
    /* A sparse authentic-sized staffroll verifies streaming admission above
     * the former 512 MiB ceiling without allocating its 806 MB payload. */
    const uint32_t credits_bytes=64u+5250u*C3_MOVIE_FRAME_BYTES;
    put32(header+8,credits_bytes);put32(header+20,5250);
    put32(header+40,credits_bytes);put32(header+44,0);
    put32(header+48,0);put32(header+52,0);
    FILE *credits=fopen(path,"wb");
    assert(credits&&fwrite(header,1,sizeof header,credits)==sizeof header);
    assert(fseek(credits,(long)credits_bytes-1,SEEK_SET)==0&&fputc(0,credits)==0&&fclose(credits)==0);
    assert(c3_movie_open(&movie,path,error,sizeof error)==0);
    assert(movie.frame_count==5250&&!movie.audio_file);
    assert(c3_movie_read_frame(&movie,5249,pixels,C3_MOVIE_FRAME_BYTES,error,sizeof error)==0);
    c3_movie_close(&movie);
    packed_movies(path, pixels);
    unlink(path);
    free(pixels);
    puts("movie: frame timing, independent streams, PCM endian/mono, bounds and truncation, "
         "framebuffer order, packed v2 frames and index faults passed");
    return 0;
}
