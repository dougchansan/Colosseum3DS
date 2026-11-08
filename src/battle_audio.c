#include "colosseum3ds/battle_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int fail(char *e,size_t cap,const char *s)
{if(e&&cap)snprintf(e,cap,"%s",s);return 0;}
static uint32_t checksum(const unsigned char *p,size_t n)
{uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
void c3_battle_audio_close(C3BattleAudio *a)
{if(a){free(a->storage);memset(a,0,sizeof *a);}}
int c3_battle_audio_parse(C3BattleAudio *a,const void *data,size_t bytes,char *error,size_t cap)
{
    const unsigned char *p=data;C3BattleAudio next={0};
    if(!a||!p||bytes<64||bytes>C3_BATTLE_AUDIO_MAX_BYTES||memcmp(p,"C3AU",4)||
       word(p+4)!=1||word(p+8)!=bytes)return fail(error,cap,"Invalid battle audio header/size.");
    next.count=word(p+12);
    if(!next.count||next.count>C3_BATTLE_AUDIO_MAX_CUES||64u+32u*next.count>bytes||
       checksum(p+64,bytes-64)!=word(p+16))return fail(error,cap,"Battle audio checksum/count failed.");
    for(unsigned i=20;i<64;++i)if(p[i])return fail(error,cap,"Battle audio reserved header.");
    size_t offset=64u+32u*next.count;
    for(uint32_t i=0;i<next.count;++i){
        const unsigned char *r=p+64u+32u*i;C3BattleAudioCue *c=&next.cues[i];
        c->sound_id=word(r);c->rate=word(r+4);c->frames=word(r+8);
        if(!c->sound_id||c->rate<8000||c->rate>48000||!c->frames||
           c->frames>C3_BATTLE_AUDIO_MAX_FRAMES||word(r+12)!=offset||
           word(r+16)!=c->frames*2u||word(r+20)!=1||word(r+24)||word(r+28)||
           c->frames*2u>bytes-offset)return fail(error,cap,"Invalid battle audio PCM record.");
        for(uint32_t j=0;j<i;++j)if(next.cues[j].sound_id==c->sound_id)
            return fail(error,cap,"Duplicate battle audio sound ID.");
        offset+=c->frames*2u;
    }
    if(offset!=bytes)return fail(error,cap,"Trailing battle audio bytes.");
    next.storage=malloc(bytes);
    if(!next.storage)return fail(error,cap,"Battle audio allocation failed.");
    memcpy(next.storage,p,bytes);next.bytes=bytes;
    for(uint32_t i=0;i<next.count;++i)
        next.cues[i].pcm=(const int16_t *)(const void *)(next.storage+word(next.storage+64u+32u*i+12));
    c3_battle_audio_close(a);*a=next;
    if(error&&cap)error[0]=0;
    return 1;
}
int c3_battle_audio_load(C3BattleAudio *a,const char *path,char *error,size_t cap)
{
    if(!a||!path)return fail(error,cap,"Invalid battle audio load arguments.");
    FILE *f=fopen(path,"rb");
    if(!f)return fail(error,cap,"Missing battle audio.c3u; run prepare_battle_audio.py.");
    long length;
    if(fseek(f,0,SEEK_END)||(length=ftell(f))<64||length>(long)C3_BATTLE_AUDIO_MAX_BYTES||fseek(f,0,SEEK_SET)){
        fclose(f);return fail(error,cap,"Invalid battle audio file extent.");}
    unsigned char *data=malloc((size_t)length);
    if(!data){fclose(f);return fail(error,cap,"Battle audio input allocation failed.");}
    int read=fread(data,1,(size_t)length,f)==(size_t)length&&!ferror(f);
    fclose(f);
    int ok=read?c3_battle_audio_parse(a,data,(size_t)length,error,cap):fail(error,cap,"Battle audio read failed.");
    free(data);return ok;
}
const C3BattleAudioCue *c3_battle_audio_find(const C3BattleAudio *a,uint32_t sound)
{if(a)for(uint32_t i=0;i<a->count;++i)if(a->cues[i].sound_id==sound)return a->cues+i;return NULL;}
