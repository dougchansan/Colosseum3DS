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
    def test_four_bank_registration_and_selection(self):
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
'''+implementation+r'''
int main(void){(void)particle_quads;(void)particle_texids;char e[192];uint8_t bytes[5]={0};C3ParticlePack packs[5]={0};
 assert(C3_STAGE_MAX_PARTICLE_BANKS==4);
 for(unsigned i=0;i<5;++i){packs[i].bank.bytes=bytes+i;packs[i].texture_count=1;packs[i].textures[0].width=packs[i].textures[0].height=8;}
 for(unsigned i=0;i<4;++i){
  assert(c3_stage_renderer_particle_pack(&packs[i],e,sizeof e)==C3_RENDER_OK);
  assert(c3_stage_renderer_particle_pack(&packs[i],e,sizeof e)==C3_RENDER_OK);
 }
 assert(allocations==1&&uploads==4&&texture_count==9&&particle_binding_count==4);
 C3ParticlePack alias=packs[0];
 assert(c3_stage_renderer_particle_pack(&alias,e,sizeof e)==C3_RENDER_ERROR);
 assert(strstr(e,"already belong"));
 assert(c3_stage_renderer_particle_pack(&packs[4],e,sizeof e)==C3_RENDER_ERROR);
 assert(allocations==1&&uploads==4&&texture_count==9&&particle_binding_count==4);
 C3ParticleSystem s={0};float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
 for(unsigned i=0;i<4;++i){s.bank=packs[i].bank;
  assert(c3_stage_renderer_particles(&s,identity));assert(particle_pack==&packs[i]&&particle_textures[0]==5+i);
 }
 s.bank=packs[4].bank;assert(!c3_stage_renderer_particles(&s,identity));assert(particle_pack==&packs[3]);
 assert(c3_stage_renderer_particles(NULL,NULL));assert(!particle_pack&&!particle_system);
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
