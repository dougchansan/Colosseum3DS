#ifndef COLOSSEUM3DS_BATTLE_AUDIO_H
#define COLOSSEUM3DS_BATTLE_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#define C3_BATTLE_AUDIO_MAX_CUES 8u
#define C3_BATTLE_AUDIO_MAX_FRAMES 320000u
#define C3_BATTLE_AUDIO_MAX_BYTES (8u*1024u*1024u)
typedef struct {
    uint32_t sound_id,rate,frames;
    const int16_t *pcm;
} C3BattleAudioCue;
typedef struct {
    unsigned char *storage;
    size_t bytes;
    uint32_t count;
    C3BattleAudioCue cues[C3_BATTLE_AUDIO_MAX_CUES];
} C3BattleAudio;
/* Zero initialize. Failure preserves an existing loaded pack. PCM is immutable,
 * mono little-endian signed16; separate copies are required for NDSP storage. */
int c3_battle_audio_parse(C3BattleAudio *,const void *,size_t,char *,size_t);
int c3_battle_audio_load(C3BattleAudio *,const char *,char *,size_t);
const C3BattleAudioCue *c3_battle_audio_find(const C3BattleAudio *,uint32_t);
void c3_battle_audio_close(C3BattleAudio *);
#endif
