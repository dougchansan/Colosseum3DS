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
    def test_tail_whip_is_an_explicit_eighth_cue(self):
        self.assertEqual(audio.selected_sounds(), (0x85, 0x86, 0x220))
        seven = audio.selected_sounds(confusion=True)
        self.assertEqual(seven, (0x85, 0x86, 0x220, 0x443, 0xE9, 0x472, 0x473))
        self.assertEqual(audio.selected_sounds(True, True), seven + (0x1DB,))
        with self.assertRaisesRegex(ValueError, 'requires --confusion'):
            audio.selected_sounds(tail_whip=True)
        with self.assertRaisesRegex(ValueError, 'Cue count'):
            audio.encode_pack([(i, b'\0\0') for i in range(14)])

    def test_stat_down_explicit_tenth_cue_and_binding(self):
        self.assertEqual(audio.selected_sounds(True, True, True, True),
                         audio.selected_sounds(True, True, True) + (0x19B,))
        for flags in ((False,False,False),(True,True,False),(True,False,True)):
            with self.assertRaisesRegex(ValueError, 'Stat down requires'):
                audio.selected_sounds(*flags, stat_down=True)
        row=bytearray(12);row[:2]=bytes((0x80,5));struct.pack_into('>H',row,4,0x20E)
        effect={'macro':0x22D,'volume':127,'pan':64,'key':60}
        with self.assertRaisesRegex(ValueError,'macro body'):
            audio.verify_stat_down_binding(row,effect,{})
        with self.assertRaisesRegex(ValueError,'binding'):
            audio.verify_stat_down_binding(row,{**effect,'macro':0x22B},{})
        with self.assertRaisesRegex(ValueError,'extent'):
            audio.verify_stat_down_binding(row[:-1],effect,{})
        code=struct.pack('>6I',0x190146,0,0x5610,0,0,0)
        layers=audio.resolve_layers({1:(0,code)},1)
        self.assertEqual(layers[0]['dry_aux_a_selection']['scale_percent'],25)
        with self.assertRaisesRegex(ValueError,'reverb'):
            audio.resolve_layers({1:(0,struct.pack('>2I',0x1A0146,0))},1)
        self.assertEqual(struct.unpack_from('<I',audio.encode_pack([(n,b'\0\0') for n in range(10)]),12)[0],10)

    def test_private_stat_down_preserves_nine_pcm_payloads(self):
        oldpath=os.environ.get('C3_AUDIO_GROWL_PACK');newpath=os.environ.get('C3_AUDIO_STAT_DOWN_PACK')
        if not oldpath or not newpath:self.skipTest('Private nine-cue and stat-down packs required')
        def payloads(path):
            data=Path(path).read_bytes();result={}
            for i in range(struct.unpack_from('<I',data,12)[0]):
                sound,rate,frames,offset,size,*_=struct.unpack_from('<8I',data,64+i*32)
                self.assertEqual((rate,size),(32000,frames*2));result[sound]=data[offset:offset+size]
            return result
        old,new=payloads(oldpath),payloads(newpath)
        self.assertEqual(len(old),9);self.assertEqual(tuple(new),audio.selected_sounds(True,True,True,True))
        for sound,pcm in old.items():self.assertEqual(new[sound],pcm)
        import hashlib
        self.assertEqual(len(new[0x19B])//2,51876)
        self.assertEqual(hashlib.sha256(new[0x19B]).hexdigest(),
                         '7de486d78f4fb5f6c3987657e2ffb731089b2d248cd7697ab8b653d4b89f3cf9')

    def test_attack_down_explicit_eleventh_cue_and_binding(self):
        self.assertEqual(audio.selected_sounds(True,True,True,True,True),
                         audio.selected_sounds(True,True,True,True)+(0x19A,))
        for flags in ((False,False,False,False),(True,True,True,False),(True,False,True,True)):
            with self.assertRaisesRegex(ValueError,'Attack down requires'):
                audio.selected_sounds(*flags,attack_down=True)
        row=bytearray(12);row[:2]=bytes((0x80,5));struct.pack_into('>H',row,4,0x20D)
        effect={'macro':0x22B,'volume':127,'pan':64,'key':60}
        with self.assertRaisesRegex(ValueError,'macro body'):
            audio.verify_attack_down_binding(row,effect,{})
        with self.assertRaisesRegex(ValueError,'binding'):
            audio.verify_attack_down_binding(row,{**effect,'macro':0x22D},{})
        with self.assertRaisesRegex(ValueError,'extent'):
            audio.verify_attack_down_binding(row[:-1],effect,{})

    def test_private_attack_down_preserves_ten_pcm_payloads(self):
        oldpath=os.environ.get('C3_AUDIO_STAT_DOWN_PACK');newpath=os.environ.get('C3_AUDIO_ATTACK_DOWN_PACK')
        if not oldpath or not newpath:self.skipTest('Private ten-cue and attack-down packs required')
        def payloads(path):
            data=Path(path).read_bytes();result={}
            for i in range(struct.unpack_from('<I',data,12)[0]):
                sound,rate,frames,offset,size,*_=struct.unpack_from('<8I',data,64+i*32)
                self.assertEqual((rate,size),(32000,frames*2));result[sound]=data[offset:offset+size]
            return result
        old,new=payloads(oldpath),payloads(newpath)
        self.assertEqual(len(old),10);self.assertEqual(tuple(new),audio.selected_sounds(True,True,True,True,True))
        for sound,pcm in old.items():self.assertEqual(new[sound],pcm)
        import hashlib
        self.assertEqual(len(new[0x19A])//2,50934)
        self.assertEqual(hashlib.sha256(new[0x19A]).hexdigest(),
                         'f8677eec71e71895b72a9983401193f76d90af50da304919ce359567ea16b809')

    def test_tackle_requires_all_prior_flags_and_exact_dispatch(self):
        prior = audio.selected_sounds(True, True, True, True, True)
        self.assertEqual(audio.selected_sounds(True, True, True, True, True, True),
                         prior + (0x4A2, 0xDE))
        for missing in range(5):
            flags = [True] * 5
            flags[missing] = False
            with self.assertRaisesRegex(ValueError, 'Tackle requires'):
                audio.selected_sounds(*flags, tackle=True)
        for sound, fx, macro in ((0x4A2, 0x33F, 0x260), (0xDE, 0x151, 0x62F)):
            row = bytes.fromhex('8005010a%04x000000000000' % fx)
            effect = {'macro': macro, 'volume': 127, 'pan': 64, 'key': 60}
            with self.assertRaisesRegex(ValueError, 'macro body'):
                audio.verify_tackle_binding(sound, row, effect, {})
            for field in effect:
                with self.assertRaisesRegex(ValueError, 'binding'):
                    audio.verify_tackle_binding(sound, row, {**effect, field: effect[field] + 1}, {})
            for offset in range(12):
                altered = bytearray(row); altered[offset] ^= 1
                with self.assertRaisesRegex(ValueError, 'binding'):
                    audio.verify_tackle_binding(sound, altered, effect, {})
            with self.assertRaisesRegex(ValueError, 'extent'):
                audio.verify_tackle_binding(sound, row[:-1], effect, {})
        with self.assertRaisesRegex(ValueError, 'Unknown Tackle'):
            audio.verify_tackle_binding(0x19A, bytes(12), {}, {})
        code = struct.pack('>6I', 0x230146, 0, 0x6610, 0, 0, 0)
        for tackle in (False, True):
            with self.assertRaisesRegex(ValueError, 'reverb'):
                audio.resolve_layers({0x62F: (0, code)}, 0x62F, tackle=tackle)
        self.assertEqual(struct.unpack_from('<I', audio.encode_pack([(i, b'\0\0') for i in range(13)]), 12)[0], 13)
        with self.assertRaisesRegex(ValueError, 'Cue count'):
            audio.encode_pack([(i, b'\0\0') for i in range(14)])

    def test_private_tackle_preserves_eleven_pcm_and_native_load(self):
        oldpath = os.environ.get('C3_AUDIO_ATTACK_DOWN_PACK')
        newpath = os.environ.get('C3_AUDIO_TACKLE_PACK')
        if not oldpath or not newpath:
            self.skipTest('Private eleven-cue and Tackle packs required')
        def payloads(path):
            data = Path(path).read_bytes(); result = {}
            for i in range(struct.unpack_from('<I', data, 12)[0]):
                sound, rate, frames, offset, size, *_ = struct.unpack_from('<8I', data, 64+i*32)
                self.assertEqual((rate, size), (32000, frames*2))
                result[sound] = data[offset:offset+size]
            return result
        old, new = payloads(oldpath), payloads(newpath)
        self.assertEqual(len(old), 11)
        self.assertEqual(tuple(new), audio.selected_sounds(True, True, True, True, True, True))
        for sound, pcm in old.items():
            self.assertEqual(new[sound], pcm)
        import hashlib
        for sound, frames, digest in (
                (0x4A2, 12609, '5d2ed07df9300c90489470695ab1cebcaf2043eb1fcf9261888524c11a7b89bf'),
                (0xDE, 68933, 'fded562149442b48dca093d95be0f664368e5635b879fdad80ac5c5f2ffc7578')):
            self.assertEqual(len(new[sound]) // 2, frames)
            self.assertEqual(hashlib.sha256(new[sound]).hexdigest(), digest)
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(cc[0]):
            self.skipTest('Host C compiler required for actual-pack parse')
        harness = r'''#include "colosseum3ds/battle_audio.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){C3BattleAudio a={0};char e[192];assert(argc==2);
assert(c3_battle_audio_load(&a,argv[1],e,sizeof e)&&a.count==13);
assert(c3_battle_audio_find(&a,0x4A2)&&c3_battle_audio_find(&a,0xDE));
unsigned char *p=malloc(a.bytes);assert(p);memcpy(p,a.storage,a.bytes);p[12]=14;
assert(!c3_battle_audio_parse(&a,p,a.bytes,e,sizeof e)&&a.count==13);
free(p);c3_battle_audio_close(&a);return 0;}'''
        flags = shlex.split(os.environ.get('CFLAGS', '-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp); (d/'test.c').write_text(harness)
            result = subprocess.run([*cc, *flags, '-I'+str(ROOT/'include'), str(d/'test.c'),
                                     str(ROOT/'src/battle_audio.c'), '-o', str(d/'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([str(d/'test'), newpath], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_growl_explicit_ninth_cue_and_source_binding(self):
        self.assertEqual(audio.selected_sounds(True, True, True), audio.selected_sounds(True, True) + (0x210,))
        for confusion, tail in ((False, False), (True, False), (False, True)):
            with self.assertRaisesRegex(ValueError, 'Growl requires'):
                audio.selected_sounds(confusion, tail, True)
        row=bytearray(12);row[:2]=bytes((0x80,5));struct.pack_into('>H',row,4,0x283)
        effect={'macro':0x39C,'volume':127,'pan':64,'key':60}
        with self.assertRaisesRegex(ValueError,'macro body'):
            audio.verify_growl_binding(row,effect,{})
        with self.assertRaisesRegex(ValueError,'binding'):
            audio.verify_growl_binding(row,{**effect,'macro':0x39B},{})
        with self.assertRaisesRegex(ValueError,'extent'):
            audio.verify_growl_binding(row[:-1],effect,{})

    def test_private_growl_and_all_eight_baseline_pcm_payloads(self):
        baseline=os.environ.get('C3_AUDIO_TAIL_WHIP_PACK')
        extended=os.environ.get('C3_AUDIO_GROWL_PACK')
        if not baseline or not extended:self.skipTest('Private eight-cue and Growl packs required')
        import hashlib
        def payloads(path):
            data=Path(path).read_bytes();result={}
            for i in range(struct.unpack_from('<I',data,12)[0]):
                sound,rate,frames,offset,size,*_=struct.unpack_from('<8I',data,64+i*32)
                self.assertEqual((rate,size),(32000,frames*2));result[sound]=data[offset:offset+size]
            return result
        old,new=payloads(baseline),payloads(extended)
        self.assertEqual(len(old),8);self.assertEqual(tuple(new),audio.selected_sounds(True,True,True))
        for sound,pcm in old.items():self.assertEqual(new[sound],pcm)
        self.assertEqual(len(new[0x210])//2,60571)
        self.assertEqual(hashlib.sha256(new[0x210]).hexdigest(),'51051ad4a42c91ef578e5812d3b51dc3266219193c1a3173187cb455a5bb3ea2')

    def test_tail_whip_binding_and_macro_drift_fail_closed(self):
        # Synthetic record assembled from the reviewed numeric dispatch fields.
        row = bytearray(12); row[0:2] = bytes((0x80, 5)); struct.pack_into('>H', row, 4, 0x24E)
        effect = {'macro': 0x402, 'volume': 127, 'pan': 64, 'key': 60}
        for field, wrong in (('macro', 0x401), ('volume', 126), ('pan', 63), ('key', 59)):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'binding'):
                audio.verify_tail_whip_binding(row, {**effect, field: wrong}, {})
        with self.assertRaisesRegex(ValueError, 'macro body'):
            audio.verify_tail_whip_binding(row, effect, {0x402: (8, bytes(8)), 0x401: (24, bytes(8))})
        with self.assertRaisesRegex(ValueError, 'extent'):
            audio.verify_tail_whip_binding(row[:-1], effect, {})
        row[1] = 4
        with self.assertRaisesRegex(ValueError, 'binding'):
            audio.verify_tail_whip_binding(row, effect, {})

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
        bad=dict(layers[0]['sweep']);bad['frequency']=32768
        with self.assertRaises(ValueError):next(audio.pitch_sweep(bad))

    def test_confusion_sweep_vibrato_and_dry_aux_source(self):
        def code(*steps): return b''.join(struct.pack('>II', *s) for s in steps)
        table = {1: (0, code((2 << 16 | 8, 0xff320000),
                            (0x000f021c, 0x00320100), (0x0f0146, 0),
                            (0x5910, 0), (0x1000007, 0xffff0000))),
                 2: (64, code((0x03200a1d, 0x100), (0x6e10, 0),
                              (0x1000007, 0xffff0000)))}
        child, parent = audio.resolve_layers(table, 1)
        self.assertNotIn('vibrato', child)
        self.assertNotIn('sweep', parent)
        self.assertEqual(parent['dry_aux_a_selection'],
                         {'controller': 1, 'scale_percent': 15, 'combine': 0, 'dry_only': True})
        sweep = audio.pitch_sweep(child['sweep'])
        values = [next(sweep) for _ in range(12)]
        self.assertEqual((values[0], values[9], values[10], values[11]),
                         (int(800*4096/32000)*61440, int(800*4096/32000)*614400,
                          0, int(800*4096/32000)*61440))
        # A ramp table makes source quadrant reflection and negative half explicit.
        sine = list(range(1024))
        vibrato = audio.pitch_vibrato(parent['vibrato'], sine)
        span = 2*256 + 15*256//100
        self.assertEqual([next(vibrato) for _ in range(4)],
                         [(span*819)>>4, (span*-409)>>4, (span*-409)>>4, (span*819)>>4])
        bad = dict(parent['vibrato']); bad['modulation_scaled'] = True
        with self.assertRaises(ValueError): next(audio.pitch_vibrato(bad, sine))
        with self.assertRaises(ValueError): next(audio.pitch_vibrato(parent['vibrato'], None))
        for a, b in [(0x000f021c, 0x00320000), (0x010f021c, 0x00320100),
                     (0x0320001d, 0x100)]:
            with self.assertRaises(ValueError):
                audio.resolve_layers({1: (0, code((a,b), (0,0)))}, 1)

    def test_private_prepared_baseline_pcm_identity(self):
        pack = Path(os.environ.get('C3_AUDIO_BASELINE_PACK', ROOT / 'private/battle-audio-confusion-v2/audio.c3u'))
        if not pack.exists(): self.skipTest('Private original-asset pack required')
        import hashlib
        expected = {0x85: 'a0d7e31534970d1c99113bffe1c5e37d06ef3939f4782116fa35ea1f429ae87c',
                    0x86: '3c6d8d8be98eae029dbb8f46ee3b8a4a4ed379124c316195d07a952765ac8ed0',
                    0x220: 'c0d140e017061b4bd57c289dfdc3b7bb19c004972746501d6ab6c7f8245acce1'}
        data = pack.read_bytes()
        self.assertEqual(struct.unpack_from('<I', data, 12)[0], 7)
        for i in range(7):
            sound, rate, frames, offset, size, *_ = struct.unpack_from('<8I', data, 64+i*32)
            self.assertEqual((rate, size), (32000, frames*2))
            if sound in expected:
                self.assertEqual(hashlib.sha256(data[offset:offset+size]).hexdigest(), expected.pop(sound))
        self.assertFalse(expected)

    def test_private_tail_whip_and_all_seven_baseline_pcm_payloads(self):
        baseline = os.environ.get('C3_AUDIO_BASELINE_PACK')
        extended = os.environ.get('C3_AUDIO_TAIL_WHIP_PACK')
        if not baseline or not extended:
            self.skipTest('Private baseline and Tail Whip packs required')
        import hashlib
        def payloads(path):
            data = Path(path).read_bytes()
            result = {}
            for i in range(struct.unpack_from('<I', data, 12)[0]):
                sound, rate, frames, offset, size, *_ = struct.unpack_from('<8I', data, 64+i*32)
                self.assertEqual((rate, size), (32000, frames*2))
                result[sound] = data[offset:offset+size]
            return result
        old, new = payloads(baseline), payloads(extended)
        self.assertEqual(tuple(new), audio.selected_sounds(True, True))
        self.assertEqual(len(old), 7)
        for sound, pcm in old.items():
            self.assertEqual(new[sound], pcm)
        self.assertEqual(len(new[0x1DB]) // 2, 92279)
        self.assertEqual(hashlib.sha256(new[0x1DB]).hexdigest(),
                         '40a877ee45ee811fc1f8f383bc32679a2f36c3f0078860e1db0c0361b4d2924d')

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
static ndspWaveBuf *channel_wave[4];
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
void ndspChnWaveBufAdd(unsigned n,ndspWaveBuf *w){assert(n>=4&&n<8&&w->data_vaddr&&w->nsamples==3);w->status=1;channel_wave[n-4]=w;++queued;}
int main(int argc,char **argv){assert(argc==2);char path[512],error[192];
 snprintf(path,sizeof path,"%s/audio.c3u",argv[1]);C3BattleAudio a={0};
 assert(c3_battle_audio_load(&a,path,error,sizeof error));assert(a.count==2);
 assert(c3_battle_audio_find(&a,85)->pcm[0]==-32768);assert(!c3_battle_audio_find(&a,999));
 unsigned char *copy=malloc(a.bytes);memcpy(copy,a.storage,a.bytes);copy[a.bytes-1]^=1;
 assert(!c3_battle_audio_parse(&a,copy,a.bytes,error,sizeof error));assert(a.count==2);free(copy);c3_battle_audio_close(&a);
 snprintf(path,sizeof path,"%s/max.c3u",argv[1]);
 assert(c3_battle_audio_load(&a,path,error,sizeof error));assert(a.count==13);
 assert(c3_battle_audio_find(&a,411));
 copy=malloc(a.bytes);memcpy(copy,a.storage,a.bytes);copy[12]=14;
 assert(!c3_battle_audio_parse(&a,copy,a.bytes,error,sizeof error));assert(a.count==13);
 free(copy);c3_battle_audio_close(&a);
 assert(c3_battle_audio_player_open(argv[1],error,sizeof error));assert(started==1&&allocated==2);
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==-1);
 assert(c3_battle_audio_player_done(0,85,error,sizeof error)==-1);
 assert(c3_battle_audio_player_done(2,999,error,sizeof error)==-1);
 assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(started==1);
 c3_battle_audio_player_pause(1);assert(paused==4);
 assert(c3_battle_audio_player_cue(2,85,error,sizeof error));assert(queued==1);
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==0);
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
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==0);
 c3_battle_audio_player_pause(1);assert(c3_battle_audio_player_done(2,85,error,sizeof error)==0);
 c3_battle_audio_player_pause(0);
 channel_wave[0]->status=NDSP_WBUF_DONE;
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==1);
 assert(c3_battle_audio_player_cue(4,85,error,sizeof error));
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==1); /* retired exact identity */
 assert(c3_battle_audio_player_done(4,85,error,sizeof error)==0);
 assert(c3_battle_audio_player_done(1,85,error,sizeof error)==-1); /* older but never queued */
 channel_wave[0]->status=NDSP_WBUF_FREE;
 assert(c3_battle_audio_player_done(4,85,error,sizeof error)==1);
 assert(c3_battle_audio_player_cue(5,85,error,sizeof error));
 assert(c3_battle_audio_player_done(4,85,error,sizeof error)==1);
 /* Reuse can retire a completed buffer even if it was never queried. */
 channel_wave[0]->status=NDSP_WBUF_DONE;
 assert(c3_battle_audio_player_cue(6,85,error,sizeof error));
 assert(c3_battle_audio_player_done(5,85,error,sizeof error)==1);
 FILE *trace=tmpfile();assert(trace);c3_battle_audio_player_trace(trace,"test");rewind(trace);
 char line[256];int found=0;while(fgets(line,sizeof line,trace))if(strstr(line,"sample_position=73"))++found;
 assert(found==4);fclose(trace);
 c3_battle_audio_player_close();assert(!started&&!allocated&&!hook_callback);
 assert(c3_battle_audio_player_done(2,85,error,sizeof error)==-1);c3_battle_audio_player_close();
 fail_alloc=1;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);fail_alloc=0;
 fail_alloc=2;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);fail_alloc=0;
 fail_ndsp=1;assert(!c3_battle_audio_player_open(argv[1],error,sizeof error));assert(!started&&!allocated);
 return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);(d/'3ds.h').write_text(sdk);(d/'test.c').write_text(harness)
            pcm=struct.pack('<3h',-32768,0,32767)
            (d/'audio.c3u').write_bytes(audio.encode_pack([(85,pcm),(86,pcm)]))
            (d/'max.c3u').write_bytes(audio.encode_pack([(n,pcm) for n in (*range(1,10),411,410,0x4A2,0xDE)]))
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror'))
            run=subprocess.run(cc+flags+['-I'+str(d),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'platform/3ds'),str(d/'test.c'),str(ROOT/'src/battle_audio.c'),str(ROOT/'platform/3ds/battle_audio_player.c'),'-o',str(d/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(d/'test'),str(d)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
