#ifndef C3_REALGAM_STAGE_PROBE_H
#define C3_REALGAM_STAGE_PROBE_H
#ifdef C3_REALGAM_STAGE_PROBE
#include <stddef.h>
#include <stdio.h>
/* Verification only: nine original arena descriptors, no actors or saves.
 * Returns 0 when all nine capture, -1 on failure or application exit. */
int c3_realgam_stage_probe(FILE *trace, char *error, size_t capacity);
#endif
#endif
