"""Synthetic audio resource, ADPCM, macro and native lifetime regressions."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import prepare_battle_audio as audio


class PreparationTests(unittest.TestCase):
    def test_adpcm_signed_nibbles_history_and_clipping(self):
        self.assertEqual(audio.decode_adpcm(bytes([0, 0x17]) + bytes(6), 2, [0] * 16), [1, 7])
        self.assertEqual(audio.decode_adpcm(bytes([0, 0x8f]) + bytes(6), 2, [0] * 16), [-8, -1])
        coeff = [2048, 0] + [0] * 14
        self.assertEqual(audio.decode_adpcm(bytes([0, 0x11]) + bytes(6), 2, coeff), [1, 2])
        self.assertEqual(audio.decode_adpcm(bytes([15, 0x77]) + bytes(6), 2, [0] * 16), [32767, 32767])
        for data, frames, co in [(bytes([0x80]) + bytes(7), 1, [0]*16), (b'', 14, [0]*16), (bytes(8), 0, [0]*16), (bytes(8), 1, [0]*15)]:
            with self.assertRaises(ValueError): audio.decode_adpcm(data, frames, co)

    def test_macro_children_delays_keys_and_unknown_instructions(self):
        def code(*steps): return b''.join(struct.pack('>II', *s) for s in steps)
        table = {1: (8, code((2 << 16 | 8, 0xff320000), (7, 50 << 16), (0x3719, 0x100), (0x1c10, 0), (0x1000007, 0xffff0000))),
                 2: (64, code((7, 80 << 16), (0x3619, 0x100), (0x5510, 0), (0x1000007, 0xffff0000)))}
        layers = audio.resolve_layers(table, 1)
        self.assertEqual([(x['sample'], x['key'], x['start_ms']) for x in layers], [(0x55,54,80),(0x1c,55,50)])
        with self.assertRaisesRegex(ValueError, 'Unsupported macro opcode'):
            audio.resolve_layers({1:(0, code((0x7e,0)))}, 1)
        with self.assertRaisesRegex(ValueError, 'recursion'):
            audio.resolve_layers({1:(0, code((1 << 16 | 8,0xff320000)))}, 1)
        with self.assertRaises(ValueError): audio.macros(struct.pack('>5I',16,0,0,0,0))

    def test_reflect_source_sweep_and_surround_are_bounded(self):
        code = b''.join(struct.pack('>II', a, b) for a, b in [
            (0x4015,0),(0x01f4141d,0x100),(0x2819,0x100),
            (0x6010,0),(0x1000007,0xffff0000)])
        layers=audio.resolve_layers({0x7b0:(0,code)},0x7b0,start_ms=600)
        self.assertEqual((layers[0]['sample'],layers[0]['key'],layers[0]['start_ms']),(0x60,40,600))
        sweep=audio.pitch_sweep(layers[0]['sweep'])
        values=[next(sweep) for _ in range(23)]
        self.assertEqual((values[0],values[20],values[21],values[22]),(60<<16,1260<<16,0,60<<16))
        invalid=code[:8]+struct.pack('>II',0x01f4141d,0)+code[16:]
        with self.assertRaisesRegex(ValueError,'pitch-sweep'):
            audio.resolve_layers({1:(0,invalid)},1)
        bad=dict(layers[0]['sweep']);bad['frequency']=501
        with self.assertRaises(ValueError):next(audio.pitch_sweep(bad))

    def test_unknown_sample_and_pack_limits(self):
        with self.assertRaises(ValueError): audio.sample_record(bytes.fromhex('ffff')+bytes(30), 85)
        with self.assertRaises(ValueError): audio.encode_pack([])
        with self.assertRaises(ValueError): audio.encode_pack([(85,b'x')])
        data = audio.encode_pack([(85,struct.pack('<3h',-32768,0,32767))])
        self.assertEqual(data[:4], b'C3AU')
        self.assertEqual(struct.unpack_from('<I', data,16)[0],audio.checksum(data[64:]))


class NativeAudioTests(unittest.TestCase):
    def test_core_bounds_and_ndsp_owner_lifetime(self):
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):self.skipTest('Host C compiler required')
        sdk=r'''
#ifndef STUB_3DS_H
#define STUB_3DS_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int Result;
typedef enum {APTHOOK_ONSUSPEND,APTHOOK_ONRESTORE,APTHOOK_ONSLEEP,APTHOOK_ONWAKEUP} APT_HookType;
typedef struct {int active;} aptHookCookie;
void aptHook(aptHookCookie *,void (*)(APT_HookType,void *),void *);void aptUnhook(aptHookCookie *);
bool ndspChnIsPlaying(int);bool ndspChnIsPaused(int);uint32_t ndspChnGetSamplePos(int);
#define R_FAILED(x) ((x)<0)
#define NDSP_OUTPUT_STEREO 0
#define NDSP_INTERP_LINEAR 0
#define NDSP_FORMAT_MONO_PCM16 0
#define NDSP_WBUF_FREE 0
#define NDSP_WBUF_DONE 2
typedef struct {void *data_vaddr;uint32_t nsamples;int status;bool looping;} ndspWaveBuf;
void *linearAlloc(size_t);void linearFree(void *);void DSP_FlushDataCache(void *,uint32_t);
Result ndspInit(void);void ndspExit(void);void ndspSetOutputMode(int);
void ndspChnReset(unsigned);void ndspChnSetInterp(unsigned,int);void ndspChnSetFormat(unsigned,int);
void ndspChnSetMix(unsigned,float *);void ndspChnWaveBufClear(unsigned);
void ndspChnSetRate(unsigned,float);void ndspChnSetPaused(unsigned,bool);void ndspChnWaveBufAdd(unsigned,ndspWaveBuf *);
#endif
'''
        harness=r'''
#include "3ds.h"
#include "battle_audio_player.h"
#include "colosseum3ds/battle_audio.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned allocated,queued,cleared,started,paused;
static int fail_alloc,fail_ndsp;
static void (*hook_callback)(APT_HookType,void *);
static bool channel_paused[4];
void aptHook(aptHookCookie *c,void (*f)(APT_HookType,void *),void *u){(void)u;c->active=1;hook_callback=f;}
void aptUnhook(aptHookCookie *c){assert(c->active);c->active=0;hook_callback=NULL;}
bool ndspChnIsPlaying(int n){(void)n;return queued!=0;}
bool ndspChnIsPaused(int n){return channel_paused[n-4];}
uint32_t ndspChnGetSamplePos(int n){(void)n;return 73;}
void *linearAlloc(size_t n){if(fail_alloc&&allocated>=(unsigned)(fail_alloc-1))return NULL;void *p=malloc(n);if(p)++allocated;return p;}
void linearFree(void *p){assert(!started);--allocated;free(p);}
void DSP_FlushDataCache(void *p,uint32_t n){assert(p&&n);}
Result ndspInit(void){if(fail_ndsp)return -1;++started;return 0;}
void ndspExit(void){assert(cleared>=4);--started;}
void ndspSetOutputMode(int x){(void)x;}
void ndspChnReset(unsigned n){assert(n>=4&&n<8);}
void ndspChnSetInterp(unsigned n,int x){(void)n;(void)x;}
void ndspChnSetFormat(unsigned n,int x){(void)n;(void)x;}
void ndspChnSetMix(unsigned n,float *p){(void)n;assert(p[0]==1&&p[1]==1);}
void ndspChnWaveBufClear(unsigned n){assert(n>=4&&n<8);++cleared;}
void ndspChnSetRate(unsigned n,float r){(void)n;assert(r==32000);}
void ndspChnSetPaused(unsigned n,bool p){channel_paused[n-4]=p;if(p)++paused;}
void ndspChnWaveBufAdd(unsigned n,ndspWaveBuf *w){assert(n>=4&&n<8&&w->data_vaddr&&w->nsamples==3);w->status=1;++queued;}
int main(int argc,char **argv){assert(argc==2);char path[512],error[192];
 snprintf(path,sizeof path,"%s/audio.c3u",argv[1]);C3BattleAudio a={0};
 assert(c3_battle_audio_load(&a,path,error,sizeof error));assert(a.count==2);
 assert(c3_battle_audio_find(&a,85)->pcm[0]==-32768);assert(!c3_battle_audio_find(&a,999));
 unsigned char *copy=malloc(a.bytes);memcpy(copy,a.storage,a.bytes);copy[a.bytes-1]^=1;
 assert(!c3_battle_audio_parse(&a,copy,a.bytes,error,sizeof error));assert(a.count==2);free(copy);c3_battle_audio_close(&a);
 assert(c3_battle_audio_player_open(argv[1],error,sizeof error));assert(started==1&&allocated==2);
 assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(started==1);
 c3_battle_audio_player_pause(1);assert(paused==4);
 assert(c3_battle_audio_player_cue(2,85,error,sizeof error));assert(queued==1);
 assert(c3_battle_audio_player_cue(2,85,error,sizeof error));assert(queued==1);
 assert(!c3_battle_audio_player_cue(1,85,error,sizeof error));
 assert(!c3_battle_audio_player_cue(3,999,error,sizeof error));
 assert(c3_battle_audio_player_cue(2,86,error,sizeof error));
 assert(c3_battle_audio_player_cue(3,85,error,sizeof error));
 assert(c3_battle_audio_player_cue(3,86,error,sizeof error));
 assert(!c3_battle_audio_player_cue(4,85,error,sizeof error));
 unsigned before_queued=queued;
 c3_battle_audio_player_pause(0);assert(!ndspChnIsPaused(4));
 hook_callback(APTHOOK_ONSUSPEND,NULL);assert(ndspChnIsPaused(4));
 hook_callback(APTHOOK_ONSLEEP,NULL);hook_callback(APTHOOK_ONRESTORE,NULL);assert(ndspChnIsPaused(4));
 hook_callback(APTHOOK_ONWAKEUP,NULL);assert(!ndspChnIsPaused(4));
 assert(queued==before_queued&&ndspChnGetSamplePos(4)==73);
 FILE *trace=tmpfile();assert(trace);c3_battle_audio_player_trace(trace,"test");rewind(trace);
 char line[256];int found=0;while(fgets(line,sizeof line,trace))if(strstr(line,"sample_position=73"))++found;
 assert(found==4);fclose(trace);
 c3_battle_audio_player_close();assert(!started&&!allocated&&!hook_callback);c3_battle_audio_player_close();
 fail_alloc=1;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);fail_alloc=0;
 fail_alloc=2;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);fail_alloc=0;
 fail_ndsp=1;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);(d/'3ds.h').write_text(sdk);(d/'test.c').write_text(harness)
            pcm=struct.pack('<3h',-32768,0,32767)
            (d/'audio.c3u').write_bytes(audio.encode_pack([(85,pcm),(86,pcm)]))
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror'))
            run=subprocess.run(cc+flags+['-I'+str(d),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'platform/3ds'),str(d/'test.c'),str(ROOT/'src/battle_audio.c'),str(ROOT/'platform/3ds/battle_audio_player.c'),'-o',str(d/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(d/'test'),str(d)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
