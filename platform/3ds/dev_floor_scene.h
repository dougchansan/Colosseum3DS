#ifndef COLOSSEUM3DS_DEV_FLOOR_SCENE_H
#define COLOSSEUM3DS_DEV_FLOOR_SCENE_H

#include <stddef.h>
#include <stdio.h>

/* DEVELOPER FLOOR LOADER (built only with C3_DEV_FLOOR=1). Shows a prepared
 * original floor with Wes, its source camera defaults and CCD collision.
 * It receives no game state: it cannot save, set story flags, run GSVM
 * natives or produce battle results. Requires that no other renderer is
 * initialized. Return 0 to the title, 1 on application exit, -1 on failure. */
int c3_dev_floor_scene_run(FILE *trace, char *error, size_t capacity);

#endif
