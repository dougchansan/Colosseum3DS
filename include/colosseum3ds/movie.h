#ifndef COLOSSEUM3DS_MOVIE_H
#define COLOSSEUM3DS_MOVIE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Streaming C3V1 adapter for original THP movies. The prepared
 * assets contain RGB565LE frames and optional interleaved PCM16LE stereo.
 * There is no full movie allocation or runtime THP decoder. */
#define C3_MOVIE_WIDTH 320u
#define C3_MOVIE_HEIGHT 240u
#define C3_MOVIE_FRAME_BYTES (C3_MOVIE_WIDTH * C3_MOVIE_HEIGHT * 2u)

typedef struct C3Movie {
    FILE *video_file, *audio_file;
    uint32_t frame_count, fps_num, fps_den;
    uint32_t frame_bytes, audio_offset;
    uint32_t audio_sample_frames, sample_rate, channels;
    uint32_t audio_cursor;
} C3Movie;

/* Pass a zero-initialized/closed movie. Validate every offset and the exact
 * file length before accepting it. Failure leaves the movie closed. */
int c3_movie_open(C3Movie *movie, const char *path, char *error, size_t capacity);
void c3_movie_close(C3Movie *movie);

/* elapsed_us is relative to the start of this movie. Return frame_count at
 * or after the end, allowing the caller to transition without reading EOF. */
uint32_t c3_movie_frame_at(const C3Movie *movie, uint64_t elapsed_us);
int c3_movie_read_frame(C3Movie *movie, uint32_t frame, void *pixels,
                        size_t capacity, char *error, size_t error_capacity);

/* Read up to capacity_frames stereo sample frames, converting little-endian
 * samples to host int16_t representation. Zero frames means clean EOF.
 * Video and audio use independent file positions. */
int c3_movie_read_audio(C3Movie *movie, int16_t *samples, size_t capacity_frames,
                       size_t *read_frames, char *error, size_t error_capacity);

/* In-place mono mix of interleaved signed PCM16 stereo. Average each pair
 * in 32-bit arithmetic and duplicate it to both channels; no sample clips. */
void c3_movie_downmix_mono(int16_t *samples, size_t frames);

#endif
