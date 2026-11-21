#include "colosseum3ds/movie.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#define C3_MOVIE_HEADER_BYTES 64u
#define C3_MOVIE_MAX_BYTES (1024u * 1024u * 1024u) /* 5250-frame staffroll: 806400064 bytes */
#define C3_MOVIE_RAW_FORMAT 1u    /* version 1: RGB565LE, row-major */
#define C3_MOVIE_PACKED_FORMAT 2u /* version 2: zstd frames, framebuffer order */
#define C3_MOVIE_STORED_MAX ZSTD_COMPRESSBOUND(C3_MOVIE_FRAME_BYTES)

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return -1;
}

void c3_movie_close(C3Movie *movie)
{
    if (!movie) return;
    if (movie->video_file) fclose(movie->video_file);
    if (movie->audio_file) fclose(movie->audio_file);
    free(movie->frame_offsets);
    free(movie->stored);
    ZSTD_freeDCtx(movie->zstd);
    memset(movie, 0, sizeof(*movie));
}

int c3_movie_open(C3Movie *movie, const char *path, char *error, size_t capacity)
{
    unsigned char header[C3_MOVIE_HEADER_BYTES];
    C3Movie opened = {0};
    uint32_t total, video_offset;
    uint64_t expected_audio, expected_total;
    long file_size;
    if (!movie || !path || movie->video_file || movie->audio_file)
        return fail(error, capacity, "Movie open requires a closed movie and a path.");
    opened.video_file = fopen(path, "rb");
    if (!opened.video_file) return fail(error, capacity, "Prepared movie was not found.");
    /* Each frame is one whole read straight into the frame buffer: stdio
     * buffering would split it into many small SD requests. */
    setvbuf(opened.video_file, NULL, _IONBF, 0);
    opened.version = fread(header, 1, sizeof(header), opened.video_file) == sizeof(header) ?
                     le32(header + 4) : 0;
    if (memcmp(header, "C3V1", 4) || (opened.version != 1 && opened.version != 2) ||
        le32(header + 12) != C3_MOVIE_WIDTH || le32(header + 16) != C3_MOVIE_HEIGHT ||
        le32(header + 56) != (opened.version == 1 ? C3_MOVIE_RAW_FORMAT : C3_MOVIE_PACKED_FORMAT) ||
        le32(header + 60) != 0) {
        fail(error, capacity, "Invalid C3V1 movie header or unsupported pixel format.");
        goto rejected;
    }
    total = le32(header + 8);
    opened.frame_count = le32(header + 20);
    opened.fps_num = le32(header + 24);
    opened.fps_den = le32(header + 28);
    video_offset = le32(header + 32);
    opened.frame_bytes = le32(header + 36);
    opened.audio_offset = le32(header + 40);
    opened.audio_sample_frames = le32(header + 44);
    opened.sample_rate = le32(header + 48);
    opened.channels = le32(header + 52);
    /* Version 2's video section is an offset index and the packed frames;
     * its end (the audio offset) is checked against the index below. */
    expected_audio = opened.version == 1 ?
                     C3_MOVIE_HEADER_BYTES + (uint64_t)opened.frame_count * C3_MOVIE_FRAME_BYTES :
                     opened.audio_offset;
    expected_total = (uint64_t)opened.audio_offset + (uint64_t)opened.audio_sample_frames * 4u;
    if (!opened.frame_count || opened.frame_count > 18000u ||
        !opened.fps_num || opened.fps_num > 60000u ||
        !opened.fps_den || opened.fps_den > 1001u ||
        opened.fps_num < opened.fps_den ||
        opened.fps_num > 60u * opened.fps_den ||
        video_offset != C3_MOVIE_HEADER_BYTES || opened.frame_bytes != C3_MOVIE_FRAME_BYTES ||
        expected_audio != opened.audio_offset || expected_total != total ||
        total > C3_MOVIE_MAX_BYTES ||
        (opened.audio_sample_frames ? (opened.sample_rate != 32000u || opened.channels != 2u) :
                                     (opened.sample_rate != 0u || opened.channels != 0u))) {
        fail(error, capacity, "Movie dimensions, timing, or section bounds are invalid.");
        goto rejected;
    }
    if (fseek(opened.video_file, 0, SEEK_END) != 0 ||
        (file_size = ftell(opened.video_file)) < 0 || (uint64_t)file_size != total) {
        fail(error, capacity, "Movie file length does not match its header.");
        goto rejected;
    }
    opened.stored_frame = UINT32_MAX;
    if (opened.version == 1) {
        opened.stored_capacity = C3_MOVIE_FRAME_BYTES;
    } else {
        size_t entries = (size_t)opened.frame_count + 1u;
        uint64_t first = C3_MOVIE_HEADER_BYTES + (uint64_t)entries * 4u;
        unsigned char *index = malloc(entries * 4u);
        int valid;
        opened.frame_offsets = malloc(entries * sizeof(*opened.frame_offsets));
        opened.zstd = ZSTD_createDCtx();
        if (!index || !opened.frame_offsets || !opened.zstd) {
            free(index);
            fail(error, capacity, "Movie index allocation failed.");
            goto rejected;
        }
        valid = fseek(opened.video_file, C3_MOVIE_HEADER_BYTES, SEEK_SET) == 0 &&
                fread(index, 4, entries, opened.video_file) == entries;
        for (size_t i = 0; valid && i < entries; ++i) {
            uint32_t at = opened.frame_offsets[i] = le32(index + i * 4u);
            if (!i) valid = at == first;
            else {
                uint32_t size = at - opened.frame_offsets[i - 1];
                valid = at > opened.frame_offsets[i - 1] && size <= C3_MOVIE_STORED_MAX;
                if (valid && size > opened.stored_capacity) opened.stored_capacity = size;
            }
        }
        free(index);
        if (!valid || opened.frame_offsets[opened.frame_count] != opened.audio_offset) {
            fail(error, capacity, "Movie frame index is invalid.");
            goto rejected;
        }
    }
    opened.stored = malloc(opened.stored_capacity);
    if (!opened.stored) {
        fail(error, capacity, "Movie frame buffer allocation failed.");
        goto rejected;
    }
    if (opened.audio_sample_frames) {
        opened.audio_file = fopen(path, "rb");
        if (!opened.audio_file) {
            fail(error, capacity, "Could not open the movie audio stream.");
            goto rejected;
        }
        setvbuf(opened.audio_file, NULL, _IONBF, 0);
    }
    *movie = opened;
    if (error && capacity) error[0] = '\0';
    return 0;
rejected:
    c3_movie_close(&opened);
    return -1;
}

uint32_t c3_movie_frame_at(const C3Movie *movie, uint64_t elapsed_us)
{
    uint64_t end_us, frame;
    if (!movie || !movie->fps_num || !movie->fps_den) return 0;
    /* Test the end first: this bounds the following multiplication even if
     * a caller passes UINT64_MAX after a suspend or timing error. */
    end_us = ((uint64_t)movie->frame_count * movie->fps_den * UINT64_C(1000000)
              + movie->fps_num - 1u) / movie->fps_num;
    if (elapsed_us >= end_us) return movie->frame_count;
    frame = elapsed_us * movie->fps_num / ((uint64_t)movie->fps_den * UINT64_C(1000000));
    return (uint32_t)frame;
}

int c3_movie_load_frame(C3Movie *movie, uint32_t frame, char *error, size_t error_capacity)
{
    uint64_t offset;
    uint32_t bytes;
    if (!movie || !movie->video_file || !movie->stored || frame >= movie->frame_count)
        return fail(error, error_capacity, "Invalid movie frame request.");
    movie->stored_frame = UINT32_MAX;
    if (movie->version == 1) {
        offset = C3_MOVIE_HEADER_BYTES + (uint64_t)frame * movie->frame_bytes;
        bytes = movie->frame_bytes;
    } else {
        offset = movie->frame_offsets[frame];
        bytes = movie->frame_offsets[frame + 1u] - movie->frame_offsets[frame];
    }
    if (offset > LONG_MAX || fseek(movie->video_file, (long)offset, SEEK_SET) != 0 ||
        fread(movie->stored, 1, bytes, movie->video_file) != bytes)
        return fail(error, error_capacity, "Movie frame read failed.");
    movie->stored_bytes = bytes;
    movie->stored_frame = frame;
    return 0;
}

/* Row-major to framebuffer order (see movie.h) in 16x16 tiles, so both the
 * source rows and the destination columns stay within a few cache lines. */
static void transpose(const uint16_t *source, uint16_t *destination)
{
    for (unsigned y0 = 0; y0 < C3_MOVIE_HEIGHT; y0 += 16u)
        for (unsigned x0 = 0; x0 < C3_MOVIE_WIDTH; x0 += 16u)
            for (unsigned x = x0; x < x0 + 16u; ++x) {
                uint16_t *column = destination + (size_t)x * C3_MOVIE_HEIGHT + (C3_MOVIE_HEIGHT - 1u);
                for (unsigned y = y0; y < y0 + 16u; ++y)
                    column[-(ptrdiff_t)y] = source[(size_t)y * C3_MOVIE_WIDTH + x];
            }
}

int c3_movie_decode_frame(C3Movie *movie, void *pixels, size_t capacity,
                          char *error, size_t error_capacity)
{
    size_t size;
    if (!movie || !pixels || ((uintptr_t)pixels & 1u) || movie->stored_frame == UINT32_MAX ||
        capacity < C3_MOVIE_FRAME_BYTES)
        return fail(error, error_capacity, "Invalid movie frame request.");
    if (movie->version == 1) {
        transpose((const uint16_t *)(const void *)movie->stored, pixels);
        return 0;
    }
    size = ZSTD_decompressDCtx(movie->zstd, pixels, C3_MOVIE_FRAME_BYTES,
                               movie->stored, movie->stored_bytes);
    if (ZSTD_isError(size) || size != C3_MOVIE_FRAME_BYTES)
        return fail(error, error_capacity, "Movie frame data is corrupt.");
    return 0;
}

int c3_movie_read_frame(C3Movie *movie, uint32_t frame, void *pixels,
                        size_t capacity, char *error, size_t error_capacity)
{
    if (!movie || !pixels || capacity < C3_MOVIE_FRAME_BYTES)
        return fail(error, error_capacity, "Invalid movie frame request.");
    if (c3_movie_load_frame(movie, frame, error, error_capacity)) return -1;
    return c3_movie_decode_frame(movie, pixels, capacity, error, error_capacity);
}

int c3_movie_read_audio(C3Movie *movie, int16_t *samples, size_t capacity_frames,
                       size_t *read_frames, char *error, size_t error_capacity)
{
    size_t count;
    uint64_t offset;
    unsigned char *bytes = (unsigned char *)samples;
    if (read_frames) *read_frames = 0;
    if (!movie || !movie->video_file || !samples || !read_frames || !capacity_frames ||
        movie->audio_cursor > movie->audio_sample_frames)
        return fail(error, error_capacity, "Invalid movie audio request.");
    count = movie->audio_sample_frames - movie->audio_cursor;
    if (count > capacity_frames) count = capacity_frames;
    if (!count) return 0;
    offset = movie->audio_offset + (uint64_t)movie->audio_cursor * 4u;
    if (!movie->audio_file || offset > LONG_MAX ||
        fseek(movie->audio_file, (long)offset, SEEK_SET) != 0 ||
        fread(bytes, 4, count, movie->audio_file) != count)
        return fail(error, error_capacity, "Movie audio read failed.");
    for (size_t i = 0; i < count * 2u; ++i) {
        uint16_t value = (uint16_t)bytes[i * 2] | (uint16_t)bytes[i * 2 + 1] << 8;
        /* Convert without an out-of-range unsigned-to-signed cast. */
        samples[i] = (int16_t)(value < 0x8000u ? (int32_t)value : (int32_t)value - 65536);
    }
    movie->audio_cursor += (uint32_t)count;
    *read_frames = count;
    return 0;
}

void c3_movie_downmix_mono(int16_t *samples, size_t frames)
{
    if (!samples) return;
    for (size_t i = 0; i < frames; ++i) {
        int32_t sum = (int32_t)samples[0] + (int32_t)samples[1];
        samples[0] = samples[1] = (int16_t)(sum / 2);
        samples += 2;
    }
}
