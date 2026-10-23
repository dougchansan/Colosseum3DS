"""Run the actual sandbox harness with synthetic libretro callback sources.

These fixtures test byte capture/lifetime; they are never battle audio evidence.
"""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave
ROOT = Path(__file__).resolve().parents[1]
CORE = r'''
#include "libretro.h"
#include <pthread.h>
#include <signal.h>
#include <string.h>
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_audio_sample_t sample;
static retro_audio_sample_batch_t batch;
static int mode,frame;
void retro_set_environment(retro_environment_t cb){env=cb;}
void retro_set_video_refresh(retro_video_refresh_t cb){video=cb;}
void retro_set_audio_sample(retro_audio_sample_t cb){sample=cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){batch=cb;}
void retro_set_input_poll(retro_input_poll_t cb){(void)cb;}
void retro_set_input_state(retro_input_state_t cb){(void)cb;}
void retro_set_controller_port_device(unsigned a,unsigned b){(void)a;(void)b;}
void retro_init(void){}
void retro_deinit(void){}
void retro_unload_game(void){}
void retro_get_system_info(struct retro_system_info *s){memset(s,0,sizeof *s);s->library_name="synthetic-audio";s->library_version="1";s->need_fullpath=true;}
void retro_get_system_av_info(struct retro_system_av_info *s){memset(s,0,sizeof *s);s->geometry.base_width=s->geometry.max_width=2;s->geometry.base_height=s->geometry.max_height=2;s->timing.fps=60;s->timing.sample_rate=32768;}
bool retro_load_game(const struct retro_game_info *s){mode=strstr(s->path,"ratechange")?1:strstr(s->path,"overflow")?2:strstr(s->path,"interrupted")?3:0;return true;}
static void *send_batches(void *unused){(void)unused;const int16_t pcm[]={1,-1,32767,-32768,0,0};for(unsigned i=0;i<100;++i)batch(pcm,3);return NULL;}
void retro_run(void){
    if(mode==1&&frame==1){struct retro_system_av_info av;retro_get_system_av_info(&av);av.timing.sample_rate=44100;env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO,&av);}
    if(mode==2)batch(NULL,(size_t)-1);
    else {pthread_t thread;pthread_create(&thread,NULL,send_batches,NULL);for(unsigned i=0;i<100;++i)sample(1234,-2345);pthread_join(thread,NULL);}
    if(mode==3)raise(SIGTERM);
    const uint32_t pixels[4]={0xffffff,0xffffff,0xffffff,0xffffff};video(pixels,2,2,8);++frame;
}
'''


class SandboxAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):raise unittest.SkipTest('Host C compiler required')
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        cls.directory=Path(cls.temp.name);cls.harness=cls.directory/'harness';cls.core=cls.directory/'core.so'
        flags=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-format-truncation','-I'+str(ROOT/'tools/sandbox/include')]
        result=subprocess.run(cc+flags+[str(ROOT/'tools/sandbox/harness.c'),'-o',str(cls.harness),'-ldl','-l:libEGL.so.1','-l:libGL.so.1'],capture_output=True,text=True)
        if result.returncode and ('cannot find -l:' in result.stderr or 'EGL/' in result.stderr):
            raise unittest.SkipTest('Sandbox EGL/GL runtime libraries required')
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        (cls.directory/'core.c').write_text(CORE)
        result=subprocess.run(cc+flags+['-shared','-fPIC','-pthread',str(cls.directory/'core.c'),'-o',str(cls.core)],capture_output=True,text=True)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)

    def run_harness(self, directory, name='normal', audio=True):
        root=Path(directory);content=root/(name+'.3dsx');content.write_bytes(b'synthetic')
        args=[str(self.harness),str(content),'--core',str(self.core),'--frames','2','--seconds','10','--capture-every','1','--state',str(root/'state'),'--out',str(root/'out')]
        if audio:args+=['--audio-wav',str(root/'captured.wav')]
        return subprocess.run(args,capture_output=True,text=True,timeout=15)

    def test_stereo_bytes_av_rate_and_concurrent_callbacks(self):
        with tempfile.TemporaryDirectory(dir=self.directory) as directory:
            result=self.run_harness(directory)
            self.assertEqual(result.returncode,0,result.stderr)
            with wave.open(str(Path(directory)/'captured.wav')) as w:
                self.assertEqual((w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()),(2,2,32768,800))
                raw=w.readframes(800)
            frames=list(struct.iter_unpack('<hh',raw))
            self.assertEqual(frames.count((1234,-2345)),200)
            self.assertEqual(frames.count((1,-1)),200)
            self.assertEqual(frames.count((32767,-32768)),200)
            self.assertEqual(frames.count((0,0)),200)
            self.assertIn('AUDIO WAV END frames=800 bytes=3200 rate=32768',result.stderr)
            self.assertEqual((Path(directory)/'captured.wav').stat().st_mode&0o777,0o600)

    def test_no_option_preserves_count_only_behavior(self):
        with tempfile.TemporaryDirectory(dir=self.directory) as directory:
            result=self.run_harness(directory,audio=False)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertFalse((Path(directory)/'captured.wav').exists())
            self.assertIn('audio=800',result.stderr)
            self.assertNotIn('AUDIO WAV END',result.stderr)

    def test_existing_file_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=self.directory) as directory:
            path=Path(directory)/'captured.wav';path.write_bytes(b'keep')
            result=self.run_harness(directory)
            self.assertEqual(result.returncode,2)
            self.assertEqual(path.read_bytes(),b'keep')
            self.assertIn('exclusively create audio WAV',result.stderr)

    def test_signal_exit_finalizes_complete_callback_bytes(self):
        with tempfile.TemporaryDirectory(dir=self.directory) as directory:
            result=self.run_harness(directory,name='interrupted')
            self.assertEqual(result.returncode,143,result.stderr)
            with wave.open(str(Path(directory)/'captured.wav')) as w:
                self.assertEqual((w.getframerate(),w.getnframes()),(32768,400))
            self.assertIn('AUDIO WAV END frames=400',result.stderr)

    def test_rate_change_and_oversized_callback_are_explicit_failures(self):
        for name,frames in [('ratechange',400),('overflow',0)]:
            with self.subTest(name=name),tempfile.TemporaryDirectory(dir=self.directory) as directory:
                result=self.run_harness(directory,name=name)
                self.assertEqual(result.returncode,2,result.stderr)
                self.assertIn('audio WAV write/rate/bound failure',result.stderr)
                with wave.open(str(Path(directory)/'captured.wav')) as w:
                    self.assertEqual(w.getframerate(),32768)
                    self.assertEqual(w.getnframes(),frames)


if __name__=='__main__':unittest.main()
