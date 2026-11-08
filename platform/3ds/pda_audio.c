#include "pda_audio.h"
#include "battle_audio_player.h"
#include <stdint.h>

static char directory[512];
static uint32_t sequence;
static uint32_t active_sound;
static int opened;
static const uint32_t sounds[10]={13,14,15,17,16,18,19,20,21,22};
int c3_pda_audio_open(const char *root,char *error,size_t capacity){
    int n;
    if(opened)c3_pda_audio_close();
    if(!root){if(error&&capacity)snprintf(error,capacity,"Missing PDA audio root");return 0;}
    n=snprintf(directory,sizeof directory,"%s/ringtones",root);
    if(n<0||(size_t)n>=sizeof directory){if(error&&capacity)snprintf(error,capacity,"PDA audio path exceeds bound");return 0;}
    opened=c3_battle_audio_player_open(directory,error,capacity);
    sequence=0;active_sound=0;
    return opened;
}
int c3_pda_audio_preview(unsigned tone,char *error,size_t capacity){
    if(!opened||tone>=10){if(error&&capacity)snprintf(error,capacity,"Invalid PDA ringtone preview");return 0;}
    c3_pda_audio_stop();
    if(sequence==UINT32_MAX){if(error&&capacity)snprintf(error,capacity,"PDA cue identity exhausted");return 0;}
    ++sequence;
    if(!c3_battle_audio_player_cue(sequence,sounds[tone],error,capacity))return 0;
    active_sound=sounds[tone];return 1;
}
void c3_pda_audio_stop(void){
    char error[128];
    if(opened&&active_sound)c3_battle_audio_player_stop(sequence,active_sound,error,sizeof error);
    active_sound=0;
}
void c3_pda_audio_close(void){
    if(opened)c3_battle_audio_player_close();
    opened=0;sequence=0;active_sound=0;directory[0]=0;
}
void c3_pda_audio_trace(FILE *trace,const char *phase){
    if(opened)c3_battle_audio_player_trace(trace,phase);
}
