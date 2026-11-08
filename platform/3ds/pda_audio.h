#ifndef C3_PDA_AUDIO_H
#define C3_PDA_AUDIO_H
#include <stddef.h>
#include <stdio.h>
/* Shared battle player is the sole NDSP owner. Caller must close movie/battle
 * audio before opening PDA audio, and close this wrapper before resuming it. */
int c3_pda_audio_open(const char *root,char *error,size_t capacity);
int c3_pda_audio_preview(unsigned tone,char *error,size_t capacity);
void c3_pda_audio_stop(void);
void c3_pda_audio_close(void);
void c3_pda_audio_trace(FILE *trace,const char *phase);
#endif
