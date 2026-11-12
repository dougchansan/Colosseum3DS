"""Production battle descriptor loader with synthetic CPU/GPU owners."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class BattleStageTests(unittest.TestCase):
    def test_bounds_and_independent_instances(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(cc[0]): self.skipTest('Host C compiler required')
        harness = r'''
#include "battle_stage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int owners, instances, gpu;
int c3_title_load(C3Title *t,const char *p,char *e,size_t c){(void)p;(void)e;(void)c;t->clip_count=11;t->fps=30;for(unsigned i=0;i<11;++i)t->clips[i].frame_count=30;t->clips[0].loop=1;t->file_data=malloc(1);++owners;return t->file_data!=NULL;}
int c3_title_instance(C3Title *t,const C3Title *o,char *e,size_t c){(void)e;(void)c;*t=*o;t->borrowed_storage=1;++instances;return 1;}
void c3_title_close(C3Title *t){if(t->file_data){if(t->borrowed_storage)--instances;else{free(t->file_data);--owners;}}memset(t,0,sizeof *t);}
int c3_title_animate(C3Title *t,uint32_t c,float seconds){(void)seconds;return c<t->clip_count;}
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)m;(void)n;(void)e;(void)c;assert(!gpu);gpu=1;return 0;}
uint64_t c3_stage_renderer_gpu_skin_mask(size_t m){(void)m;return 0;}
int c3_stage_renderer_set_static(size_t n,bool e){(void)n;(void)e;return 1;}
int c3_stage_renderer_set_modulation(size_t n,const uint8_t rgb[3]){(void)n;(void)rgb;return 1;}
int c3_stage_renderer_get_modulation(size_t i,uint8_t rgb[3],int *enabled){(void)i;memset(rgb,128,3);*enabled=0;return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t c){(void)p;(void)e;(void)c;return C3_RENDER_OK;}
int c3_stage_renderer_particles_source_order(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){(void)s;(void)a;return 1;}
bool c3_stage_renderer_try_shutdown(void){gpu=0;return true;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *cam,float f,C3TitleRenderStats *s,char *e,size_t c){(void)m;(void)n;(void)cam;(void)f;(void)s;(void)e;(void)c;return 0;}
int main(int argc,char **argv){assert(argc==3);C3BattleStage *s=calloc(1,sizeof *s);char e[192];uint16_t hp[4]={30,40,20,20};int valid=atoi(argv[2]);
assert(c3_battle_stage_load(s,argv[1],hp,NULL,e,sizeof e)==valid);
if(valid){assert(owners==6&&instances==1&&gpu);assert(s->models[4].borrowed_storage);assert(c3_battle_stage_step(s,16,e,sizeof e));C3BattlePreviewEvent send={0};send.kind=C3_PREVIEW_EVENT_SEND_OUT;send.sequence=1;send.actor=C3_PREVIEW_NO_SLOT;send.target=C3_PREVIEW_NO_SLOT;send.target_mask=15;assert(c3_battle_stage_submit(s,&send,1));assert(!c3_battle_stage_step(s,16,e,sizeof e));assert(strstr(e,"Unsupported source clip"));
memset(s->error,0,sizeof s->error);c3_battle_presentation_init(&s->presentation,hp);
for(unsigned i=0;i<s->count;++i){s->roles[i][5]=0;s->roles[i][1]=2;}
C3BattlePreviewEvent batch[2]={0};batch[0].kind=C3_PREVIEW_EVENT_INTRO;batch[0].sequence=1;batch[0].actor=batch[0].target=C3_PREVIEW_NO_SLOT;batch[1]=send;batch[1].sequence=2;
assert(c3_battle_stage_submit(s,batch,2));for(unsigned i=0;i<4;++i)assert(!s->presentation.visible[i]);
for(unsigned i=0;i<25;++i)assert(c3_battle_stage_step(s,100,e,sizeof e));
for(unsigned i=0;i<4;++i)assert(s->presentation.visible[i]);
C3BattlePreviewEvent move={0};move.kind=C3_PREVIEW_EVENT_MOVE;move.sequence=3;move.actor=0;move.target=2;move.move=44;
assert(c3_battle_stage_submit(s,&move,1));C3BattleStage *before=malloc(sizeof *before);assert(before);*before=*s;
assert(!c3_battle_stage_step(s,99999,e,sizeof e));assert(strstr(e,"overload"));assert(!memcmp(s,before,sizeof *s));free(before);
assert(c3_battle_stage_step(s,0,e,sizeof e));assert(s->clips[1]==2&&s->clocks[1]==0&&s->presentation.waiting);
for(unsigned i=0;i<25;++i){assert(c3_battle_stage_step(s,100,e,sizeof e));}
assert(s->presentation.cursor==1);
assert(c3_battle_stage_close(s));}
assert(!owners&&!instances&&!gpu);free(s);return 0;}
'''
        subprocess.run([__import__('sys').executable,str(ROOT/'tools/prepare_save_codec.py')],cwd=ROOT,check=True)
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);(d/'h.c').write_text(harness);exe=d/'test'
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
            result=subprocess.run(cc+flags+['-I'+str(ROOT/'src'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'platform/3ds'),str(d/'h.c'),str(ROOT/'platform/3ds/battle_stage.c'),str(ROOT/'src/battle_presentation.c'),str(ROOT/'src/wzx_bite.c'),str(ROOT/'src/wzx_damage.c'),str(ROOT/'src/wzx_confusion_sequence.c'),str(ROOT/'src/wzx_confusion.c'),str(ROOT/'src/battle_audio.c'),str(ROOT/'src/wzx_reflect.c'),str(ROOT/'src/wzx_tail_whip.c'),str(ROOT/'src/wzx_growl.c'),str(ROOT/'src/wzx_stat_down.c'),str(ROOT/'src/wzx_tackle.c'),str(ROOT/'src/particle.c'),str(ROOT/'src/particle_pack.c'),str(ROOT/'src/battle_camera.c'),str(ROOT/'build/save_codec.c'),'-lm','-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            data=bytearray(128+7*128);struct.pack_into('<4s4I',data,0,b'C3BV',1,len(data),7,6)
            struct.pack_into('<12f',data,32,0,10,20,0,0,0,0,45,1,1000,400/240,0)
            for i,slot in enumerate((0xffffffff,0,1,2,3,4,5)):
                off=128+i*128;data[off:off+6]=b'x.c3t\0'
                struct.pack_into('<3I',data,off+48,slot,3 if i==4 else 0xffffffff,0)
                struct.pack_into('<9I',data,off+60,0,*([0xffffffff]*8));struct.pack_into('<5f',data,off+96,0,0,0,0,1)
            variants=[(None,0,1),(0,ord('X'),0),(80,1,0),(79,127,0),(128+7,1,0),(128+48,9,0),(128+116,1,0)]
            for offset,value,valid in variants:
                candidate=bytearray(data)
                if offset is not None:candidate[offset]=value
                (d/'scene.c3v').write_bytes(candidate)
                run=subprocess.run([str(exe),str(d),str(valid)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
