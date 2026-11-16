#ifndef C3_DNA_KEY_SCENE_H
#define C3_DNA_KEY_SCENE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
int c3_dna_key_scene_available(void);
/* Requires the field renderer to be shut down. Returns retail result 0
 * (matched), 1 (cancel), 2 (invalid expected species), or -1 on failure. */
int c3_dna_key_scene_run(const uint16_t expected[3],FILE *trace,char *error,size_t capacity);
#endif
