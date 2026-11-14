#ifndef C3_MUSIC_PLAYER_H
#define C3_MUSIC_PLAYER_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Streamed MusyX music (tools/prepare_music.py output in <root>/music).
 *
 * The original GSsnd layer has two song slots, each playing one sound-table
 * row: fn_801659FC loads into resource 0x407 (current id lbl_8047B0A8) and
 * fn_80165A20 into resource 0x406 (lbl_8047B0AC). The title (0x46A), the GS
 * logo (0x4D1) and the floor scripts' area music (native 0x68) all use 0x406.
 * A row names a prerendered DSP-ADPCM stream; NDSP decodes it in hardware on
 * channels 8/9 (slot 0x407) and 10/11 (slot 0x406). A worker thread does
 * every SD read, so callers never block on the card.
 *
 * Every call is safe without an open pack: requests are then ignored and
 * status reports "not playing", so the game runs silent rather than failing. */
enum { C3_MUSIC_SLOT_407 = 0, C3_MUSIC_SLOT_406 = 1 };

int c3_music_open(const char *root, char *error, size_t capacity);
void c3_music_close(void);
/* Start `sound` in `slot`. volume is the game's 0..255 argument (127 and up
 * is full scale), fade_ms the fade-in. Re-requesting the sound already
 * playing in the slot only retargets its volume, as the original keeps a
 * loaded song running. Returns 1 when a stream starts or continues. */
int c3_music_play(int slot, uint32_t sound, uint32_t volume, uint32_t fade_ms);
/* soundStop: fade the slot playing `sound` out and release it. */
void c3_music_stop(uint32_t sound, uint32_t fade_ms);
void c3_music_stop_slot(int slot, uint32_t fade_ms);
/* fn_80165718 / sndVolume(group BGM): music master volume 0..127 over fade_ms. */
void c3_music_master(uint32_t volume, uint32_t fade_ms);
/* fn_801666BC: 2 while `sound` is queued or audible, 0 otherwise. */
int c3_music_status(uint32_t sound);
void c3_music_pause_slot(int slot, int paused);
void c3_music_set_mono(int mono);
void c3_music_trace(FILE *trace, const char *phase);
#endif
