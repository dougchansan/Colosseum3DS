"""Execute the production RGB-only WZX combiner helper against GPU contracts."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class WzxModulationTests(unittest.TestCase):
    def test_source_multiply_two_preserves_alpha_and_disabled_state(self):
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not shutil.which(cc[0]):
            self.skipTest("Host C compiler required")
        text = (ROOT/"platform/3ds/title_renderer.c").read_text()
        body = text[text.index("static void apply_model_modulation("):text.index("static bool camera_matrices(")]
        harness = r'''#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned color,scale,source[3],function,calls; } C3D_TexEnv;
static C3D_TexEnv stage;
static uint8_t model_modulation[16][4];
enum { C3D_RGB=1,GPU_PREVIOUS=2,GPU_CONSTANT=3,GPU_PRIMARY_COLOR=4,GPU_MODULATE=5,GPU_TEVSCALE_2=6 };
static C3D_TexEnv *C3D_GetTexEnv(unsigned i){assert(i==5);++stage.calls;return &stage;}
static void C3D_TexEnvColor(C3D_TexEnv *e,unsigned c){e->color=c;}
static void C3D_TexEnvSrc(C3D_TexEnv *e,unsigned mode,unsigned a,unsigned b,unsigned c){assert(mode==C3D_RGB);e->source[0]=a;e->source[1]=b;e->source[2]=c;}
static void C3D_TexEnvFunc(C3D_TexEnv *e,unsigned mode,unsigned f){assert(mode==C3D_RGB);e->function=f;}
static void C3D_TexEnvScale(C3D_TexEnv *e,unsigned mode,unsigned scale){assert(mode==C3D_RGB);e->scale=scale;}
'''+body+r'''
int main(void){
apply_model_modulation(0);assert(!stage.calls);
model_modulation[0][0]=0x12;model_modulation[0][1]=0x34;model_modulation[0][2]=0x56;model_modulation[0][3]=1;
apply_model_modulation(0);
assert(stage.calls==1&&stage.color==0xff563412u&&stage.scale==GPU_TEVSCALE_2&&stage.function==GPU_MODULATE);
assert(stage.source[0]==GPU_PREVIOUS&&stage.source[1]==GPU_CONSTANT&&stage.source[2]==GPU_PRIMARY_COLOR);
/* The next draw's normal combiner owns restoration. Disabled modulation may
 * not change any state or alpha after that combiner has initialized it. */
model_modulation[0][3]=0;stage.color=0xabcdef01;stage.scale=0;
apply_model_modulation(0);assert(stage.calls==1&&stage.color==0xabcdef01&&stage.scale==0);
return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp);(d/"test.c").write_text(harness)
            flags=shlex.split(os.environ.get("CFLAGS","-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            run=subprocess.run(cc+flags+[str(d/"test.c"),"-o",str(d/"test")],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(d/"test")],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=="__main__": unittest.main()
