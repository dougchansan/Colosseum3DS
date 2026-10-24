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
 * estimate is used. The bounded cache retains the last64 completed identities;
 * older evicted identities fail explicitly rather than being assumed done. */
int c3_battle_audio_player_done(uint32_t sequence,uint32_t sound_id,char *error,size_t capacity);
void c3_battle_audio_player_pause(int paused);
/* Advisory NDSP hardware state: sample cursor/playing/paused; not proof that
 * a physical speaker or a recording contains audible output. */
void c3_battle_audio_player_trace(FILE *trace,const char *phase);
void c3_battle_audio_player_close(void);
#endif
