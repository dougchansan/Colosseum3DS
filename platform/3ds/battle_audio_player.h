#ifndef C3_BATTLE_AUDIO_PLAYER_H
#define C3_BATTLE_AUDIO_PLAYER_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* Sole NDSP owner during battle; movies must have closed their audio first.
 * Root is the directory containing audio.c3u. Returns1 success,0 diagnostic. */
int c3_battle_audio_player_open(const char *root,char *error,size_t capacity);
/* Cue sequence is the caller's unique, increasing crossed-source-cue identity,
 * including distinct IDs for sequential ally instances. Log the committed
 * battle event/group separately. Exact replay
 * is a successful no-op; older cues fail. Sound IDs are game requests, not
 * sample IDs. Four independent hardware voices allow source cue overlap. */
int c3_battle_audio_player_cue(uint32_t sequence,uint32_t sound_id,char *error,size_t capacity);
/* -1 invalid/missing identity,0 queued/playing/paused,1 SDK DONE/FREE or an
 * exactly known finished identity retained after channel reuse. No duration
 * estimate is used. The bounded cache retains the last64 terminal identities;
 * STOPPED returns -1, and older evicted pairs fail rather than being assumed done. */
int c3_battle_audio_player_done(uint32_t sequence,uint32_t sound_id,char *error,size_t capacity);
enum {
    C3_BATTLE_AUDIO_CUE_INVALID=-1,
    C3_BATTLE_AUDIO_CUE_ACTIVE=0,
    C3_BATTLE_AUDIO_CUE_FINISHED=1,
    C3_BATTLE_AUDIO_CUE_STOPPED=2
};
/* Exact pair lookup. STOPPED is explicit truncation, never SDK completion.
 * FINISHED preserves existing DONE/FREE and pause guards. */
int c3_battle_audio_player_status(uint32_t sequence,uint32_t sound_id,char *error,size_t capacity);
/* Stop only this exact known pair. Known terminal pairs are idempotent;
 * unknown/evicted pairs fail without touching any channel. Four voices remain.
 * done() returns -1 for STOPPED; acknowledge a source stop separately. Clearing
 * dry PCM adapts source key-off; original release-envelope timing is unqualified. */
int c3_battle_audio_player_stop(uint32_t sequence,uint32_t sound_id,char *error,size_t capacity);
void c3_battle_audio_player_pause(int paused);
/* Advisory NDSP hardware state: sample cursor/playing/paused; not proof that
 * a physical speaker or a recording contains audible output. */
void c3_battle_audio_player_trace(FILE *trace,const char *phase);
void c3_battle_audio_player_close(void);
#endif
