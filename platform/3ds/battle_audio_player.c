#include "battle_audio_player.h"
#include "colosseum3ds/battle_audio.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VOICES 4u
#define CHANNEL_BASE 4u
static struct {
    int started,paused,hooked;
    unsigned apt_pause_mask;
    aptHookCookie hook;
    uint32_t count;
    C3BattleAudioCue cues[C3_BATTLE_AUDIO_MAX_CUES];
    int16_t *pcm[C3_BATTLE_AUDIO_MAX_CUES];
    uint32_t sequences[C3_BATTLE_AUDIO_MAX_CUES];
    ndspWaveBuf voices[VOICES];
    uint32_t voice_sound[VOICES],voice_sequence[VOICES];
} audio;
static int fail(char *e,size_t n,const char *s)
{if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void apply_pause(void)
{
    if(audio.started)for(unsigned i=0;i<VOICES;++i)
        ndspChnSetPaused(CHANNEL_BASE+i,audio.paused||audio.apt_pause_mask);
}
static void application_hook(APT_HookType hook,void *user)
{
    (void)user;
    if(hook==APTHOOK_ONSUSPEND)audio.apt_pause_mask|=1u;
    else if(hook==APTHOOK_ONRESTORE)audio.apt_pause_mask&=~1u;
    else if(hook==APTHOOK_ONSLEEP)audio.apt_pause_mask|=2u;
    else if(hook==APTHOOK_ONWAKEUP)audio.apt_pause_mask&=~2u;
    /* Pause only: do not clear/requeue a wave or reset its DSP sample cursor.
     * Nested HOME/lid transitions keep both pause reasons until restored. */
    apply_pause();
}
void c3_battle_audio_player_close(void)
{
    if(audio.hooked){aptUnhook(&audio.hook);audio.hooked=0;}
    if(audio.started){for(unsigned i=0;i<VOICES;++i)ndspChnWaveBufClear(CHANNEL_BASE+i);ndspExit();}
    for(unsigned i=0;i<C3_BATTLE_AUDIO_MAX_CUES;++i)if(audio.pcm[i])linearFree(audio.pcm[i]);
    memset(&audio,0,sizeof audio);
}
int c3_battle_audio_player_open(const char *root,char *error,size_t cap)
{
    char path[512];C3BattleAudio pack={0};
    if(audio.started)return fail(error,cap,"Battle NDSP owner already active.");
    if(!root||snprintf(path,sizeof path,"%s/audio.c3u",root)>=(int)sizeof path)
        return fail(error,cap,"Invalid battle audio directory.");
    if(!c3_battle_audio_load(&pack,path,error,cap))return 0;
    audio.count=pack.count;
    for(uint32_t i=0;i<pack.count;++i){
        audio.cues[i]=pack.cues[i];audio.pcm[i]=linearAlloc(pack.cues[i].frames*2u);
        if(!audio.pcm[i]){c3_battle_audio_close(&pack);c3_battle_audio_player_close();return fail(error,cap,"Battle PCM linear allocation failed.");}
        memcpy(audio.pcm[i],pack.cues[i].pcm,pack.cues[i].frames*2u);
        DSP_FlushDataCache(audio.pcm[i],pack.cues[i].frames*2u);audio.cues[i].pcm=audio.pcm[i];
    }
    c3_battle_audio_close(&pack);
    Result result=ndspInit();
    if(R_FAILED(result)){c3_battle_audio_player_close();return fail(error,cap,"Battle NDSP unavailable; check installed DSP firmware.");}
    audio.started=1;ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for(unsigned i=0;i<VOICES;++i){float mix[12]={1,1};
        ndspChnReset(CHANNEL_BASE+i);ndspChnSetInterp(CHANNEL_BASE+i,NDSP_INTERP_LINEAR);
        ndspChnSetFormat(CHANNEL_BASE+i,NDSP_FORMAT_MONO_PCM16);ndspChnSetMix(CHANNEL_BASE+i,mix);
    }
    aptHook(&audio.hook,application_hook,NULL);audio.hooked=1;
    if(error&&cap)error[0]=0;
    return 1;
}
int c3_battle_audio_player_cue(uint32_t sequence,uint32_t sound,char *error,size_t cap)
{
    if(!audio.started||!sequence)return fail(error,cap,"Battle audio cue requires active owner and sequence.");
    uint32_t cue=0;
    while(cue<audio.count&&audio.cues[cue].sound_id!=sound)++cue;
    if(cue==audio.count)return fail(error,cap,"Unprepared game sound ID in battle audio cue.");
    if(sequence==audio.sequences[cue]){if(error&&cap)error[0]=0;return 1;}
    if(sequence<audio.sequences[cue])return fail(error,cap,"Out-of-order battle audio cue.");
    for(unsigned i=0;i<VOICES;++i){ndspWaveBuf *wave=&audio.voices[i];
        if(wave->status!=NDSP_WBUF_FREE&&wave->status!=NDSP_WBUF_DONE)continue;
        memset(wave,0,sizeof *wave);wave->data_vaddr=audio.pcm[cue];wave->nsamples=audio.cues[cue].frames;
        ndspChnSetRate(CHANNEL_BASE+i,(float)audio.cues[cue].rate);
        ndspChnSetPaused(CHANNEL_BASE+i,audio.paused||audio.apt_pause_mask);ndspChnWaveBufAdd(CHANNEL_BASE+i,wave);
        audio.voice_sound[i]=sound;audio.voice_sequence[i]=sequence;
        audio.sequences[cue]=sequence;if(error&&cap)error[0]=0;return 1;
    }
    return fail(error,cap,"Battle audio voice bound exceeded (four overlapping cues).");
}
void c3_battle_audio_player_pause(int paused)
{audio.paused=paused!=0;apply_pause();}
void c3_battle_audio_player_trace(FILE *trace,const char *phase)
{
    if(!trace)return;
    fprintf(trace,"battle_audio phase=%s started=%d manual_pause=%d apt_pause=%u cues=%lu\n",
        phase?phase:"snapshot",audio.started,audio.paused,audio.apt_pause_mask,(unsigned long)audio.count);
    if(audio.started)for(unsigned i=0;i<VOICES;++i)
        fprintf(trace,"battle_audio_voice channel=%u sound=%lu sequence=%lu playing=%u paused=%u sample_position=%lu wave_status=%u\n",
            CHANNEL_BASE+i,(unsigned long)audio.voice_sound[i],(unsigned long)audio.voice_sequence[i],
            (unsigned)ndspChnIsPlaying(CHANNEL_BASE+i),(unsigned)ndspChnIsPaused(CHANNEL_BASE+i),
            (unsigned long)ndspChnGetSamplePos(CHANNEL_BASE+i),(unsigned)audio.voices[i].status);
    fflush(trace);
}
