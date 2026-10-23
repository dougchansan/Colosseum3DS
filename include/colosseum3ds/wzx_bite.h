#ifndef C3_WZX_BITE_H
#define C3_WZX_BITE_H
#include <stddef.h>
#include <stdint.h>

/* Executable, deliberately bounded WZX attack slice, not a general WZX VM.
 * Original sound is declared unavailable; damage GPT1/cameras are outside it.
 * Packed data is private. All fields below are native values, never PPC pointers. */
#define C3_WZX_BITE_BYTES 256u
#define C3_WZX_BITE_SOUND_UNSUPPORTED 1u

typedef struct {
    uint32_t owner_clip, model_clip, floor_start, floor_duration, model_start;
    uint32_t control_start, control_delay, model_frames, floor_from, floor_to;
    uint32_t part, unsupported, sound_id, sound_start, flags;
    float owner_rate, model_rate, anchor[3];
    uint8_t model_sha1[20];
} C3WzxBiteProgram;

typedef struct {
    C3WzxBiteProgram program;
    uint64_t units; /* milliseconds * 60; one original NTSC tick is 1000 units */
    uint32_t model_starts, sound_requests;
    uint8_t active, model_visible, floor_enabled, floor_rgb[3];
    float owner_seconds, model_seconds;
} C3WzxBite;

/* Failures preserve outputs. Error storage must not overlap inputs/outputs. */
int c3_wzx_bite_decode(C3WzxBiteProgram *, const void *, size_t, char *, size_t);
int c3_wzx_bite_load(C3WzxBiteProgram *, const char *, char *, size_t);
int c3_wzx_bite_begin(C3WzxBite *, const C3WzxBiteProgram *);
int c3_wzx_bite_step(C3WzxBite *, uint32_t milliseconds);
void c3_wzx_bite_cancel(C3WzxBite *);
#endif
