"""Production NDSP exact-cue stop/status tests with a synthetic nine-cue pack."""
import ast
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('stop_audio_prepare',ROOT/'tools/prepare_battle_audio.py')
AUDIO=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(AUDIO)


class ExactCueStopTests(unittest.TestCase):
    def test_checked_pairs_channels_history_and_lifetime(self):
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):self.skipTest('Host C compiler required')
        # Reuse the established SDK contract from the existing audio fixture,
        # keeping its constants/prototypes in one place. It is a literal test
        # stub, not code from the production implementation being tested.
        tree=ast.parse((ROOT/'tests/test_prepare_battle_audio.py').read_text())
        sdk=next(ast.literal_eval(node.value) for node in ast.walk(tree)
                 if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sdk' for t in node.targets))
        harness=r'''
#include "3ds.h"
#include "battle_audio_player.h"
#include "colosseum3ds/battle_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ndspWaveBuf *waves[4];static unsigned clears[4],adds,allocations;static int started;
static bool paused[4];static void (*hook)(APT_HookType,void *);
void aptHook(aptHookCookie *c,void (*f)(APT_HookType,void *),void *u){(void)u;c->active=1;hook=f;}
void aptUnhook(aptHookCookie *c){assert(c->active);c->active=0;hook=NULL;}
bool ndspChnIsPlaying(int n){return waves[n-4]&&waves[n-4]->status==1;}
bool ndspChnIsPaused(int n){return paused[n-4];}
uint32_t ndspChnGetSamplePos(int n){(void)n;return 77;}
void *linearAlloc(size_t n){void *p=malloc(n);if(p)++allocations;return p;}
void linearFree(void *p){assert(!started);--allocations;free(p);}
void DSP_FlushDataCache(void *p,uint32_t n){assert(p&&n);}
Result ndspInit(void){started=1;return 0;}void ndspExit(void){assert(started);started=0;}
void ndspSetOutputMode(int n){(void)n;}void ndspChnReset(unsigned n){waves[n-4]=NULL;paused[n-4]=false;}
void ndspChnSetInterp(unsigned n,int v){(void)n;(void)v;}void ndspChnSetFormat(unsigned n,int v){(void)n;(void)v;}
void ndspChnSetMix(unsigned n,float *v){(void)n;(void)v;}void ndspChnSetRate(unsigned n,float v){(void)n;assert(v==32000);}
void ndspChnSetPaused(unsigned n,bool p){paused[n-4]=p;}
void ndspChnWaveBufAdd(unsigned n,ndspWaveBuf *w){waves[n-4]=w;w->status=1;++adds;}
void ndspChnWaveBufClear(unsigned n){++clears[n-4];if(waves[n-4])waves[n-4]->status=NDSP_WBUF_FREE;}
static unsigned clear_count(void){return clears[0]+clears[1]+clears[2]+clears[3];}
int main(int argc,char **argv){
 assert(argc==2||argc==3);char e[192];
 assert(!c3_battle_audio_player_stop(1,0x210,e,sizeof e));
 assert(c3_battle_audio_player_status(1,0x210,e,sizeof e)==-1);
 assert(c3_battle_audio_player_open(argv[1],e,sizeof e));assert(allocations==9);
 assert(c3_battle_audio_player_cue(1,0x210,e,sizeof e));assert(c3_battle_audio_player_cue(2,0x85,e,sizeof e));
 assert(c3_battle_audio_player_cue(3,0x86,e,sizeof e));assert(c3_battle_audio_player_cue(4,0x220,e,sizeof e));
 assert(!c3_battle_audio_player_cue(5,0x443,e,sizeof e)); /* still exactly four voices */
 unsigned before=clear_count();
 assert(!c3_battle_audio_player_stop(0,0x210,e,sizeof e));assert(!c3_battle_audio_player_stop(1,0x85,e,sizeof e));
 assert(!c3_battle_audio_player_stop(2,0x210,e,sizeof e));assert(!c3_battle_audio_player_stop(1,0xffff,e,sizeof e));
 assert(clear_count()==before&&adds==4);assert(c3_battle_audio_player_done(1,0x210,e,sizeof e)==0);
 c3_battle_audio_player_pause(1);assert(c3_battle_audio_player_stop(1,0x210,e,sizeof e));
 assert(clears[0]==1&&clears[1]==0&&clears[2]==0&&clears[3]==0);
 assert(c3_battle_audio_player_status(1,0x210,e,sizeof e)==C3_BATTLE_AUDIO_CUE_STOPPED);
 assert(c3_battle_audio_player_done(1,0x210,e,sizeof e)==-1&&strstr(e,"stopped"));
 assert(c3_battle_audio_player_stop(1,0x210,e,sizeof e));assert(clear_count()==before+1);
 assert(c3_battle_audio_player_cue(1,0x210,e,sizeof e)&&adds==4); /* no resurrection */
 assert(c3_battle_audio_player_cue(5,0x443,e,sizeof e)&&adds==5);
 assert(c3_battle_audio_player_status(1,0x210,e,sizeof e)==2);assert(c3_battle_audio_player_done(2,0x85,e,sizeof e)==0);
 waves[1]->status=NDSP_WBUF_DONE;assert(c3_battle_audio_player_done(2,0x85,e,sizeof e)==0); /* paused guard retained */
 c3_battle_audio_player_pause(0);assert(c3_battle_audio_player_done(2,0x85,e,sizeof e)==1);
 before=clear_count();assert(c3_battle_audio_player_stop(2,0x85,e,sizeof e)&&clear_count()==before);
 assert(c3_battle_audio_player_status(2,0x85,e,sizeof e)==1);
 assert(c3_battle_audio_player_cue(6,0x85,e,sizeof e));assert(c3_battle_audio_player_status(2,0x85,e,sizeof e)==1);
 assert(c3_battle_audio_player_stop(1,0x210,e,sizeof e)&&clear_count()==before); /* historical exact pair */
 /* App pause still holds genuine completion, and stop touches its own channel. */
 hook(APTHOOK_ONSUSPEND,NULL);waves[1]->status=NDSP_WBUF_DONE;assert(c3_battle_audio_player_done(6,0x85,e,sizeof e)==0);
 hook(APTHOOK_ONRESTORE,NULL);assert(c3_battle_audio_player_done(6,0x85,e,sizeof e)==1);
 assert(c3_battle_audio_player_stop(3,0x86,e,sizeof e));assert(clears[2]==1&&clears[3]==0);
 FILE *trace=tmpfile();assert(trace);c3_battle_audio_player_trace(trace,"stop_test");rewind(trace);char text[4096];size_t n=fread(text,1,sizeof text-1,trace);text[n]=0;fclose(trace);assert(strstr(text,"explicit_stop=1"));
 /* Bound history evicts old stopped identity; unknown must never become done. */
 for(uint32_t serial=7;serial<80;++serial){waves[0]->status=NDSP_WBUF_DONE;assert(c3_battle_audio_player_cue(serial,0x443,e,sizeof e));assert(c3_battle_audio_player_done(serial,0x443,e,sizeof e)==0);}
 before=clear_count();assert(c3_battle_audio_player_status(1,0x210,e,sizeof e)==-1);
 assert(!c3_battle_audio_player_stop(1,0x210,e,sizeof e)&&clear_count()==before);
 c3_battle_audio_player_close();assert(!allocations&&!started&&!hook);c3_battle_audio_player_close();
 assert(c3_battle_audio_player_done(79,0x443,e,sizeof e)==-1);
 assert(c3_battle_audio_player_open(argv[1],e,sizeof e));assert(c3_battle_audio_player_status(1,0x210,e,sizeof e)==-1);
 c3_battle_audio_player_close();assert(!allocations);
 if(argc==3){assert(c3_battle_audio_player_open(argv[2],e,sizeof e));assert(allocations==9);c3_battle_audio_player_close();assert(!allocations);}
 puts("Exact-cue stop/status and nine-cue lifetime passed");return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/'3ds.h').write_text(sdk);(d/'test.c').write_text(harness)
            pcm=struct.pack('<3h',-32768,0,32767)
            (d/'audio.c3u').write_bytes(AUDIO.encode_pack([(sound,pcm) for sound in AUDIO.selected_sounds(True,True,True)]))
            executable=d/'stop-test'
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
            if os.environ.get('C3_AUDIO_STOP_SANITIZE'):
                flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            command=cc+flags+['-I'+str(d),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'platform/3ds'),
                str(d/'test.c'),str(ROOT/'src/battle_audio.c'),str(ROOT/'platform/3ds/battle_audio_player.c'),'-o',str(executable)]
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            command=[str(executable),str(d)]
            if os.environ.get('C3_AUDIO_GROWL_PACK'):
                command.append(str(Path(os.environ['C3_AUDIO_GROWL_PACK']).parent))
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Exact-cue stop/status',result.stdout)


if __name__=='__main__':unittest.main()
