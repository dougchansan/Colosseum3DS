#ifndef COLOSSEUM3DS_CORE_H
#define COLOSSEUM3DS_CORE_H
#include <stddef.h>
#include <stdint.h>

/* Startup regression checks run the actual selected decompilation source. */
int c3_port_selftest(void);
/* Probe common.fsys only: success does not mean the game is bootable. */
int c3_data_probe(const char *root, char *diagnostic, size_t capacity);
void c3_rng_seed(uint32_t seed);
uint32_t c3_rng_next(void);
float c3_side_plane(const float normal[3], const float p1[3], const float p2[3]);

#endif
