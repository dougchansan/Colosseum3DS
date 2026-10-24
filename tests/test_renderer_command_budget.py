"""Production particle command admission arithmetic at the exact boundary."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CommandBudgetTests(unittest.TestCase):
    def test_capacity_and_overflow_boundaries(self):
        cc=shlex.split(os.environ.get("CC","cc"))
        if not shutil.which(cc[0]):self.skipTest("Host C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/"budget.c";exe=Path(tmp)/"budget"
            source.write_text(r'''
#include "stage_renderer.h"
#include <assert.h>
#include <stdint.h>
int main(void){
    assert(C3_RENDER_COMMAND_BUFFER_BYTES==1048576u);
    uint32_t n=1133,limit=C3_RENDER_COMMAND_CAPACITY_WORDS-C3_STAGE_COMMAND_RESERVE_WORDS-n*C3_STAGE_PARTICLE_COMMAND_WORDS;
    assert(c3_stage_particle_commands_fit(limit,n));
    assert(!c3_stage_particle_commands_fit(limit+1,n));
    assert(c3_stage_particle_commands_fit(0,0));
    assert(!c3_stage_particle_commands_fit(0,1536));
    assert(!c3_stage_particle_commands_fit(UINT32_MAX,0));
    assert(!c3_stage_particle_commands_fit(0,UINT32_MAX));
    assert(!c3_stage_particle_commands_fit(UINT32_MAX,UINT32_MAX));
    assert(c3_stage_particle_commands_fit(C3_RENDER_COMMAND_CAPACITY_WORDS-C3_STAGE_COMMAND_RESERVE_WORDS,0));
    assert(!c3_stage_particle_commands_fit(C3_RENDER_COMMAND_CAPACITY_WORDS-C3_STAGE_COMMAND_RESERVE_WORDS+1,0));
    assert(c3_stage_particle_segment_fits(100,2,100+384+8192));
    assert(!c3_stage_particle_segment_fits(101,2,100+384+8192));
    assert(!c3_stage_particle_segment_fits(100,2,8192));
    assert(c3_stage_particle_commands_fit(100,2)); /* would admit a split overflow */
    return 0;
}
''')
            flags=shlex.split(os.environ.get("CFLAGS","-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            result=subprocess.run(cc+flags+["-I"+str(ROOT/"include"),"-I"+str(ROOT/"platform/3ds"),str(source),"-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode,0)
    def test_six_bank_registration_and_checked_app_srt_dispatch(self):
        cc=shlex.split(os.environ.get("CC","cc"))
        if not shutil.which(cc[0]):self.skipTest("Host C compiler required")
        implementation=(ROOT/"platform/3ds/particle_renderer.inc").read_text().split("static uint32_t particle_color")[0]
        harness=r'''
#include "stage_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {float x;} TitleGPUVertex;
static unsigned scene_count=1,scene_drawn,texture_count=5,uploads,allocations;
static size_t gpu_buffer_bytes;
static int textures[128];
static int fail(char *e,size_t n,const char*s){if(e&&n)snprintf(e,n,"%s",s);return -1;}
static void *linearAlloc(size_t n){++allocations;return malloc(n);}
static int upload_texture(int *t,const C3TitleTexture *s){(void)t;(void)s;++uploads;return 1;}
static const C3Particle *expected_particle;
static const C3ParticleCamera *expected_camera;
static C3ParticleQuad *expected_quad;
static char *expected_error;
static size_t expected_capacity;
static const float *expected_attachment;
static C3ParticleSystem *expected_system;
static uint64_t expected_frame;
static unsigned legacy_calls,srt_calls;
static int adapter_result=1;
int c3_particle_quad(const C3Particle *p,const float attachment[12],const C3ParticleCamera *camera,C3ParticleQuad *q,char *e,size_t n){
 assert(p==expected_particle&&attachment==expected_attachment&&camera==expected_camera&&q==expected_quad&&e==expected_error&&n==expected_capacity);
 ++legacy_calls;if(p->app_srt_handle){snprintf(e,n,"AppSRT requires system adapter");return -1;}
 if(adapter_result<0)snprintf(e,n,"legacy-adapter-failure");
 return adapter_result;
}
int c3_particle_quad_app_srt(C3ParticleSystem *s,const C3Particle *p,const C3ParticleCamera *camera,uint64_t frame,float native_scale,C3ParticleQuad *q,char *e,size_t n){
 assert(s==expected_system&&p==expected_particle&&camera==expected_camera&&q==expected_quad&&e==expected_error&&n==expected_capacity);
 assert(frame==expected_frame&&native_scale==.1f);++srt_calls;
 if(adapter_result<0)snprintf(e,n,"srt-adapter-failure");
 return adapter_result;
}
'''+implementation+r'''
int main(void){(void)particle_quads;(void)particle_texids;char e[192];uint8_t bytes[7]={0};C3ParticlePack packs[7]={0};
 assert(C3_STAGE_MAX_PARTICLE_BANKS==6);
 for(unsigned i=0;i<7;++i){packs[i].bank.bytes=bytes+i;packs[i].texture_count=1;packs[i].textures[0].width=packs[i].textures[0].height=8;}
 packs[4].bank.profile=C3_PARTICLE_PROFILE_GROWL_V1;
 for(unsigned i=0;i<6;++i){
  assert(c3_stage_renderer_particle_pack(&packs[i],e,sizeof e)==C3_RENDER_OK);
  assert(c3_stage_renderer_particle_pack(&packs[i],e,sizeof e)==C3_RENDER_OK);
 }
 assert(allocations==1&&uploads==6&&texture_count==11&&particle_binding_count==6);
 C3ParticlePack alias=packs[0];
 assert(c3_stage_renderer_particle_pack(&alias,e,sizeof e)==C3_RENDER_ERROR);
 assert(strstr(e,"already belong"));
 assert(c3_stage_renderer_particle_pack(&packs[6],e,sizeof e)==C3_RENDER_ERROR);
 assert(allocations==1&&uploads==6&&texture_count==11&&particle_binding_count==6);
 C3ParticleSystem s={0};float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
 for(unsigned i=0;i<6;++i){s.bank=packs[i].bank;
  assert(c3_stage_renderer_particles(&s,identity));assert(particle_pack==&packs[i]&&particle_textures[0]==5+i);
 }
 s.bank=packs[6].bank;assert(!c3_stage_renderer_particles(&s,identity));assert(particle_pack==&packs[5]);
 C3ParticleSystem growl={0},invalid={0};growl.bank=packs[4].bank;
 C3Particle *p=&growl.particles[0];p->active=1;p->size=1;C3ParticleCamera camera={0};C3ParticleQuad q;
 expected_particle=p;expected_camera=&camera;expected_quad=&q;expected_error=e;expected_capacity=sizeof e;expected_attachment=particle_attachment;
 /* A const binder deliberately cannot authorize mutable SRT lookup. */
 assert(c3_stage_renderer_particles(&growl,identity));assert(!particle_srt_system);
 p->app_srt_handle=16;assert(particle_build_quad(p,&camera,&q,e,sizeof e)==-1);assert(legacy_calls==1&&!srt_calls);
 assert(c3_stage_renderer_particles_app_srt(&growl));assert(particle_srt_system==&growl&&particle_system==&growl&&particle_pack==&packs[4]);
 const float world[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};assert(!memcmp(particle_attachment,world,sizeof world));
 /* Failed setters preserve all current mutable/const/pack/attachment state. */
 assert(!c3_stage_renderer_particles_app_srt(NULL));
 invalid.bank=packs[0].bank;assert(!c3_stage_renderer_particles_app_srt(&invalid));
 invalid.bank.profile=C3_PARTICLE_PROFILE_GROWL_V1;assert(!c3_stage_renderer_particles_app_srt(&invalid)); /* forged legacy-pack profile */
 invalid.bank=packs[6].bank;invalid.bank.profile=C3_PARTICLE_PROFILE_GROWL_V1;assert(!c3_stage_renderer_particles_app_srt(&invalid));
 invalid.bank=packs[4].bank;invalid.bank.profile=0;assert(!c3_stage_renderer_particles_app_srt(&invalid));
 assert(!c3_stage_renderer_particles(&growl,NULL));
 s.bank=packs[0].bank;float bad[12];memcpy(bad,identity,sizeof bad);bad[7]=NAN;assert(!c3_stage_renderer_particles(&s,bad));
 assert(particle_srt_system==&growl&&particle_system==&growl&&particle_pack==&packs[4]&&particle_textures[0]==9);
 assert(!memcmp(particle_attachment,world,sizeof world));
 expected_system=&growl;particle_render_frame=UINT64_C(0x100000001);expected_frame=particle_render_frame;
 for(int result=-1;result<=1;++result){adapter_result=result;unsigned before=srt_calls;
  assert(particle_build_quad(p,&camera,&q,e,sizeof e)==result);assert(srt_calls==before+1&&legacy_calls==1);
  if(result<0)assert(!strcmp(e,"srt-adapter-failure"));
 }
 ++particle_render_frame;expected_frame=particle_render_frame;adapter_result=1;assert(particle_build_quad(p,&camera,&q,e,sizeof e)==1);
 /* The same Growl bank's non-SRT sibling still uses the original quad path. */
 p->app_srt_handle=0;for(int result=-1;result<=1;++result){adapter_result=result;unsigned before=legacy_calls,other=srt_calls;
  assert(particle_build_quad(p,&camera,&q,e,sizeof e)==result);assert(legacy_calls==before+1&&srt_calls==other);
  if(result<0)assert(!strcmp(e,"legacy-adapter-failure"));
 }
 assert(c3_stage_renderer_particles(&growl,identity));assert(!particle_srt_system&&particle_system==&growl);
 p->app_srt_handle=16;assert(particle_build_quad(p,&camera,&q,e,sizeof e)==-1);
 s.bank=packs[0].bank;assert(c3_stage_renderer_particles(&s,identity));assert(!particle_srt_system&&particle_system==&s&&particle_pack==&packs[0]);
 assert(c3_stage_renderer_particles_app_srt(&growl));assert(c3_stage_renderer_particles(NULL,NULL));assert(!particle_pack&&!particle_system&&!particle_srt_system);
 scene_drawn=1;assert(c3_stage_renderer_particle_pack(&packs[0],e,sizeof e)==C3_RENDER_ERROR);
 free(particle_vertices);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/"banks.c";exe=Path(tmp)/"banks";source.write_text(harness)
            flags=shlex.split(os.environ.get("CFLAGS","-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            result=subprocess.run(cc+flags+["-I"+str(ROOT/"include"),"-I"+str(ROOT/"platform/3ds"),str(source),"-lm","-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode,0)
if __name__=="__main__":unittest.main()
