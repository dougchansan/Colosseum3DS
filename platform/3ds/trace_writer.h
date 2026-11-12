#ifndef C3_TRACE_WRITER_H
#define C3_TRACE_WRITER_H
#include <stdio.h>

/* Open a trace log whose writes never wait for the SD card. The returned FILE
 * is an ordinary stdio stream (fprintf/fflush/fclose work unchanged); its
 * bytes go to a RAM ring that a lower-priority thread on the same core drains
 * to the file, so the SD writes happen while the main thread sleeps in its
 * frame pacing. Falls back to fopen if the ring or thread cannot be created.
 * Bytes still in the ring when the application crashes are lost. */
FILE *c3_trace_open(const char *path);

#endif
