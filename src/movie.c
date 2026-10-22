#include "colosseum3ds/movie.h"

#include <limits.h>
#include <string.h>

#define C3_MOVIE_HEADER_BYTES 64u
#define C3_MOVIE_MAX_BYTES (512u * 1024u * 1024u)

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
    if (!opened.video_file) return fail(error, capacity, "Prepared boot movie was not found.");
    if (fread(header, 1, sizeof(header), opened.video_file) != sizeof(header) ||
        memcmp(header, "C3V1", 4) || le32(header + 4) != 1 ||
        le32(header + 12) != C3_MOVIE_WIDTH || le32(header + 16) != C3_MOVIE_HEIGHT ||
        le32(header + 56) != 1 || le32(header + 60) != 0) {
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
    expected_audio = C3_MOVIE_HEADER_BYTES +
                     (uint64_t)opened.frame_count * C3_MOVIE_FRAME_BYTES;
    expected_total = expected_audio + (uint64_t)opened.audio_sample_frames * 4u;
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
    if (opened.audio_sample_frames) {
        opened.audio_file = fopen(path, "rb");
        if (!opened.audio_file) {
            fail(error, capacity, "Could not open the movie audio stream.");
            goto rejected;
        }
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

int c3_movie_read_frame(C3Movie *movie, uint32_t frame, void *pixels,
                        size_t capacity, char *error, size_t error_capacity)
{
    uint64_t offset;
    if (!movie || !movie->video_file || !pixels || frame >= movie->frame_count ||
        capacity < movie->frame_bytes)
        return fail(error, error_capacity, "Invalid movie frame request.");
    offset = C3_MOVIE_HEADER_BYTES + (uint64_t)frame * movie->frame_bytes;
    if (offset > LONG_MAX || fseek(movie->video_file, (long)offset, SEEK_SET) != 0 ||
        fread(pixels, 1, movie->frame_bytes, movie->video_file) != movie->frame_bytes)
        return fail(error, error_capacity, "Movie frame read failed.");
    return 0;
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
