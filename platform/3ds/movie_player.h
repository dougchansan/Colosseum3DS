#ifndef COLOSSEUM3DS_MOVIE_PLAYER_H
#define COLOSSEUM3DS_MOVIE_PLAYER_H

#include <stddef.h>
#include <stdio.h>

/* Run one authentic boot logo to its end. Original tpc/gs_logo do not have
 * a button skip branch. START+SELECT remains the port's application exit.
 * Returns 0 on completion, 1 on application exit, -1 on playback failure. */
int c3_movie_player_run(const char *path, const char *label, FILE *trace,
                        char *error, size_t capacity);

#endif
