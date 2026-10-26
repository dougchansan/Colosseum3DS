#include "pda_audio.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,opens,closes,stops,cues;
static int admitted=1;
static uint32_t last_sequence,last_sound;
static char last_root[512];
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
int c3_battle_audio_player_open(const char *root,char *error,size_t size)
{(void)error;(void)size;++opens;snprintf(last_root,sizeof last_root,"%s",root);return admitted;}
int c3_battle_audio_player_cue(uint32_t sequence,uint32_t sound,char *error,size_t size)
{(void)error;(void)size;++cues;last_sequence=sequence;last_sound=sound;return 1;}
int c3_battle_audio_player_stop(uint32_t sequence,uint32_t sound,char *error,size_t size)
{(void)error;(void)size;CHECK(sequence==last_sequence&&sound==last_sound);++stops;return 1;}
void c3_battle_audio_player_close(void){++closes;}
void c3_battle_audio_player_trace(FILE *trace,const char *phase){(void)trace;(void)phase;}
int main(void)
{
    char error[192];
    CHECK(!c3_pda_audio_preview(0,error,sizeof error)&&!cues);
    CHECK(!c3_pda_audio_open(NULL,error,sizeof error)&&!opens);
    CHECK(c3_pda_audio_open("/private",error,sizeof error)&&opens==1&&!strcmp(last_root,"/private/ringtones"));
    CHECK(c3_pda_audio_preview(3,error,sizeof error)&&last_sound==17&&last_sequence==1);
    CHECK(!c3_pda_audio_preview(10,error,sizeof error)&&!stops);
    CHECK(c3_pda_audio_preview(4,error,sizeof error)&&last_sound==16&&last_sequence==2&&stops==1);
    c3_pda_audio_stop();CHECK(stops==2);c3_pda_audio_stop();CHECK(stops==2);
    c3_pda_audio_close();CHECK(closes==1);c3_pda_audio_close();CHECK(closes==1);
    CHECK(!c3_pda_audio_preview(0,error,sizeof error));
    CHECK(c3_pda_audio_open("/private",error,sizeof error));
    admitted=0;CHECK(!c3_pda_audio_open("/missing",error,sizeof error)&&closes==2);
    CHECK(!c3_pda_audio_preview(0,error,sizeof error));c3_pda_audio_close();CHECK(closes==2);
    printf("PDA audio ownership: %u checks passed.\n",checks);return 0;
}
