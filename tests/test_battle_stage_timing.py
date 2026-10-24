"""Elapsed-time partition and final-pose sampling checks for the production step."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BattleStageTimingTests(unittest.TestCase):
    def test_elapsed_partitions_pause_and_overload(self):
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not shutil.which(cc[0]):
            self.skipTest("Host C compiler required")
        source = (ROOT / "platform/3ds/battle_stage.c").read_text()
        step = source[source.index("int c3_battle_stage_step("):source.index("int c3_battle_stage_draw(")]
        harness = r'''
#include "battle_stage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned samples,starts,finishes;
static int confusion_step(C3BattleStage *s,uint32_t ms,char *e,size_t n){(void)s;(void)ms;(void)e;(void)n;return 1;}
static float confusion_owner_seconds(const C3BattleStage *s){(void)s;return 0;}
static int confusion_bind_particles(C3BattleStage *s,char *e,size_t n){(void)s;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
static int stage_sound(C3BattleStage *s,uint32_t id,char *e,size_t n){return s->sound_cue(++s->cue_serial,id,e,n);}
static int stat_down_step(C3BattleStage *s,uint32_t ms,char *e,size_t n){(void)s;(void)ms;(void)e;(void)n;return 1;}
static int growl_step(C3BattleStage *s,uint32_t ms,char *e,size_t n){(void)s;(void)ms;(void)e;(void)n;return 1;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){(void)s;(void)a;return 1;}
static int tail_step(C3BattleStage *s,uint32_t ms,char *e,size_t n){(void)s;(void)ms;(void)e;(void)n;return 1;}
static int reflect_step(C3BattleStage *s,uint32_t ms,char *e,size_t n){(void)s;(void)ms;(void)e;(void)n;return 1;}
static int reflect_bind_particles(C3BattleStage *s,char *e,size_t n){(void)s;(void)e;(void)n;return 1;}
static int damage_step(C3BattleStage *s,uint32_t ms,char *e,size_t c){(void)ms;(void)e;(void)c;assert(!s->damage_available);return 1;}
static int damage_begin(C3BattleStage *s,char *e,size_t c){(void)s;(void)e;(void)c;assert(0);return 0;}
static int damage_bind_particles(C3BattleStage *s,char *e,size_t c){(void)e;(void)c;assert(!s->damage_available);return 1;}
static void camera_actors(C3BattleStage *s,C3BattleCameraActor a[4]){(void)s;memset(a,0,4*sizeof *a);}

static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
int c3_stage_renderer_set_modulation(size_t n,const uint8_t rgb[3]){(void)n;(void)rgb;return 1;}
int c3_title_animate(C3Title *m,uint32_t clip,float seconds){(void)m;(void)clip;(void)seconds;++samples;return 1;}
static void event(const C3BattlePreviewEvent *e,int finished,void *user){
    C3BattleStage *s=user;
    if(finished){++finishes;return;}
    ++starts;
    if(e->kind==C3_PREVIEW_EVENT_CRITICAL){s->presentation.duration_ms=0;return;}
    if(e->kind==C3_PREVIEW_EVENT_MOVE){
        if(s->bite_available)assert(c3_wzx_bite_begin(&s->bite,&s->bite_program));
        s->clips[0]=s->bite_available?s->bite_program.owner_clip:1;s->clocks[0]=0;s->fresh[0]=1;c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;}
}
'''
        harness += step
        harness += r'''
static C3BattleStage *stage(void){
    C3BattleStage *s=calloc(1,sizeof *s);assert(s);s->initialized=1;s->count=1;s->slots[0]=0;
    s->models[0].fps=30;s->models[0].clip_count=3;s->models[0].clips[0].loop=1;s->models[0].clips[0].frame_count=30;
    s->models[0].clips[1].frame_count=30;s->roles[0][4]=2;s->transforms[0][4]=1;
    uint16_t hp[4]={30,30,30,30};c3_battle_presentation_init(&s->presentation,hp);
    C3BattlePreviewEvent e[2]={{0},{0}};e[0].kind=C3_PREVIEW_EVENT_MOVE;e[0].sequence=1;e[0].actor=0;e[0].target=2;
    e[1].kind=C3_PREVIEW_EVENT_HP;e[1].sequence=2;e[1].actor=0;e[1].target=2;e[1].hp[2]=10;
    assert(c3_battle_presentation_submit(&s->presentation,e,2));return s;
}
static void same(const C3BattleStage *a,const C3BattleStage *b){assert(a->clips[0]==b->clips[0]);assert(a->clocks[0]==b->clocks[0]);assert(!memcmp(&a->presentation,&b->presentation,sizeof a->presentation));}
static void bite_partition(const char *path){
 char error[192];C3WzxBiteProgram program;assert(c3_wzx_bite_load(&program,path,error,sizeof error));
 C3BattleStage *a=stage(),*b=stage();
 for(unsigned k=0;k<2;++k){C3BattleStage *s=k?b:a;s->count=2;s->bite_available=1;s->bite_program=program;s->bite_owner=0;s->bite_model=1;s->slots[1]=UINT32_MAX-1u;
  s->models[0].clip_count=8;s->models[0].fps=120;s->models[0].clips[program.owner_clip].frame_count=400;
  s->models[1].clip_count=1;s->models[1].fps=120;s->models[1].clips[0].frame_count=69;s->transforms[1][4]=1;
 }
 assert(c3_battle_stage_step(a,1600,error,sizeof error));assert(c3_battle_stage_step(b,0,error,sizeof error));assert(c3_battle_stage_step(b,1600,error,sizeof error));same(a,b);assert(!memcmp(&a->bite,&b->bite,sizeof a->bite));assert(a->bite.units==96000);
 assert(c3_battle_stage_step(a,800,error,sizeof error));for(unsigned i=0;i<8;++i){assert(c3_battle_stage_step(b,0,error,sizeof error));assert(c3_battle_stage_step(b,100,error,sizeof error));}same(a,b);assert(!memcmp(&a->bite,&b->bite,sizeof a->bite));assert(a->bite.model_starts==1&&a->bite.sound_requests==1);
 assert(c3_battle_stage_step(a,350,error,sizeof error));for(unsigned i=0;i<35;++i){assert(c3_battle_stage_step(b,0,error,sizeof error));assert(c3_battle_stage_step(b,10,error,sizeof error));}same(a,b);assert(!memcmp(&a->bite,&b->bite,sizeof a->bite));assert(!a->bite.active&&a->bite.model_starts==1&&a->bite.sound_requests==1);
 free(a);free(b);
}
int main(int argc,char **argv){char error[192];C3BattleStage *a=stage(),*b=stage();
    assert(c3_battle_stage_step(a,1600,error,sizeof error));assert(samples==1);
    for(unsigned i=0;i<1600;++i){assert(c3_battle_stage_step(b,1,error,sizeof error));}same(a,b);
    assert(a->presentation.cursor==2&&a->presentation.hp[2]==10);
    C3BattleStage *primed=stage();assert(c3_battle_stage_step(primed,0,error,sizeof error));
    assert(primed->clips[0]==1&&primed->clocks[0]==0&&!primed->fresh[0]);
    assert(c3_battle_stage_step(primed,1600,error,sizeof error));same(a,primed);free(primed);
    /* Inserting zero-time calls at crossed event boundaries neither shifts
     * clocks nor starts or applies an event twice. */
    primed=stage();for(unsigned i=0;i<16;++i){assert(c3_battle_stage_step(primed,0,error,sizeof error));assert(c3_battle_stage_step(primed,100,error,sizeof error));}same(a,primed);free(primed);
    C3BattleStage saved=*a;assert(!c3_battle_stage_step(a,4001,error,sizeof error));assert(strstr(error,"overload"));assert(!memcmp(a,&saved,sizeof saved));
    c3_battle_presentation_pause(&a->presentation,1);saved=*a;assert(c3_battle_stage_step(a,4000,error,sizeof error));assert(a->clocks[0]==saved.clocks[0]);assert(!memcmp(&a->presentation,&saved.presentation,sizeof a->presentation));
    free(a);free(b);a=stage();b=stage();assert(c3_battle_stage_step(a,4000,error,sizeof error));for(unsigned i=0;i<40;++i){assert(c3_battle_stage_step(b,100,error,sizeof error));}same(a,b);free(a);free(b);
    a=stage();uint16_t hp[4]={30,30,30,30};c3_battle_presentation_init(&a->presentation,hp);
    C3BattlePreviewEvent chain[3]={{0},{0},{0}};for(unsigned i=0;i<3;++i){chain[i].sequence=i+1;chain[i].kind=i<2?C3_PREVIEW_EVENT_CRITICAL:C3_PREVIEW_EVENT_MOVE;}
    assert(c3_battle_presentation_submit(&a->presentation,chain,3));unsigned before_starts=starts,before_finishes=finishes;
    assert(c3_battle_stage_step(a,0,error,sizeof error));assert(a->presentation.cursor==2&&starts-before_starts==3&&finishes-before_finishes==2);
    assert(c3_battle_stage_step(a,0,error,sizeof error));assert(starts-before_starts==3&&finishes-before_finishes==2);
    assert(c3_battle_stage_step(a,1600,error,sizeof error));assert(a->presentation.cursor==3&&starts-before_starts==3&&finishes-before_finishes==3);free(a);
    if(argc==2)bite_partition(argv[1]);
    return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "h.c").write_text(harness)
            exe = d / "timing"
            flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            result = subprocess.run(cc + flags + ["-I"+str(ROOT/"include"), "-I"+str(ROOT/"platform/3ds"), str(d/"h.c"), str(ROOT/"src/battle_presentation.c"), str(ROOT/"src/wzx_bite.c"), str(ROOT/"src/battle_camera.c"), str(ROOT/"build/save_codec.c"), "-I"+str(ROOT/"src"), "-lm", "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            descriptor = ROOT / "private/bite-attack-v1/bite.c3w"
            if descriptor.is_file():
                result = subprocess.run([str(exe), str(descriptor)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

if __name__ == "__main__":
    unittest.main()
