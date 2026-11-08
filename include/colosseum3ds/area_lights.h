#ifndef COLOSSEUM3DS_AREA_LIGHTS_H
#define COLOSSEUM3DS_AREA_LIGHTS_H

#include "colosseum3ds/title.h"

#define C3_AREA_LIGHTS_MAX_FRAMES 4096u
#define C3_AREA_LIGHTS_MAX_BYTES (64u+C3_TITLE_MAX_LIGHTS*C3_AREA_LIGHTS_MAX_FRAMES*4u)

typedef struct {
    unsigned char *colors;
    uint32_t light_count, frame_count, loop;
    float fps;
} C3AreaLights;

/* Zero-initialize first. Load/parse replace owned sample storage only after
 * validation. 1 success, 0 failure; original data stays live after failure. */
int c3_area_lights_parse(C3AreaLights *track, const void *data, size_t bytes,
                        char *error, size_t capacity);
int c3_area_lights_load(C3AreaLights *track, const char *path, char *error, size_t capacity);
void c3_area_lights_close(C3AreaLights *track);

/* Use the area's clock, independently of each actor's motion clock. Colors
 * hold their earlier quantized HSD sample. Fixed light positions, attenuation
 * and flags remain in each model's original light descriptors. */
int c3_area_lights_apply(const C3AreaLights *track, float seconds, C3Title *model);

#endif
