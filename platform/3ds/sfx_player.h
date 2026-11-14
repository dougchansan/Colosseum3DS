#ifndef C3_SFX_PLAYER_H
#define C3_SFX_PLAYER_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Battle sound effects and Pokemon cries by GSsnd sound-request id
 * (tools/prepare_battle_sfx.py output, C3SX v1 "sfx.c3s").
 *
 * The original starts an FX row with fn_80166AB8(id) (sndFXStartEx, volume
 * 0x7F, centre pan) and stops it with fn_80166B18(id). Each id here is a
 * prerendered mono DSP-ADPCM sound; NDSP decodes it in hardware on one of
 * eight channels 12..19 (movie uses 0, battle cues 4..7, music 8..11). A
 * worker thread does every SD read, so callers never block on the card.
 * When all voices are busy the oldest one is replaced.
 *
 * Every call is safe without an open pack (or without NDSP, e.g. an
 * emulator lacking dspfirm.cdc): requests are ignored and status reports
 * "not playing", so the game runs silent rather than failing. */

int c3_sfx_open(const char *path, char *error, size_t capacity);
void c3_sfx_close(void);
/* 1 when the pack has a sound for `sound`. */
int c3_sfx_has(uint32_t sound);
/* Start `sound` at volume 0..127 (127 = as rendered). Returns a nonzero
 * handle, or 0 when the id is absent or the player is closed. */
uint32_t c3_sfx_play(uint32_t sound, uint32_t volume);
/* fn_80166B18: stop every voice playing `sound` (short fade). */
void c3_sfx_stop(uint32_t sound);
void c3_sfx_stop_handle(uint32_t handle);
void c3_sfx_stop_all(void);
/* 1 while `sound` is queued or audible (0 when closed). */
int c3_sfx_status(uint32_t sound);
int c3_sfx_handle_status(uint32_t handle);
/* SFX master volume 0..127 over fade_ms (sndMasterVolume fx group). */
void c3_sfx_master(uint32_t volume, uint32_t fade_ms);
/* Pause hook (game pause / APT suspend): holds every voice in place. */
void c3_sfx_pause(int paused);
void c3_sfx_trace(FILE *trace, const char *phase);
#endif
