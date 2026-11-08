"""Original HSD point/spot half vectors and GX shininess, without GPU stubs."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RendererSpecularTests(unittest.TestCase):
    def test_point_half_vector_and_degenerate_original_fallback(self):
        compiler=shlex.split(os.environ.get("CC","cc"))
        if not shutil.which(compiler[0]):self.skipTest("Host C compiler required")
        source=(ROOT/"platform/3ds/title_renderer.c").read_text()
        normalize=source[source.index("static float normalize("):source.index("static unsigned morton8(")]
        specular=source[source.index("typedef struct { float position[3], half_direction[3], color[3]; } SpecularLight;"):
                        source.index("static void combiner(")]
        support=r'''
#include "colosseum3ds/title.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
typedef struct {float x,y,z;} C3D_FVec;
typedef struct {float translation[3];} C3D_Mtx;
static C3D_FVec FVec3_New(float x,float y,float z){return (C3D_FVec){x,y,z};}
static C3D_FVec Mtx_MultiplyFVec3(const C3D_Mtx *m,C3D_FVec v){(void)m;return v;}
static C3D_FVec Mtx_MultiplyFVecH(const C3D_Mtx *m,C3D_FVec v)
{return (C3D_FVec){v.x+m->translation[0],v.y+m->translation[1],v.z+m->translation[2]};}
static float clamp01(float v){return fmaxf(0,fminf(1,v));}
'''
        test=r'''
int main(void) {
    C3Title model={0};model.light_count=1;
    model.lights[0]=(C3TitleLight){.flags=14,.pos={10,0,-10},.rgba={1,.5,.25,1}};
    C3TitleMaterialState material={.specular_origin={0,0,-10}};
    C3D_Mtx view={{0,0,0}};SpecularLight lights[C3_TITLE_MAX_LIGHTS]={0};
    prepare_specular(&model,&material,&view,1,lights);
    assert(fabsf(lights[0].half_direction[0]-sqrtf(.5f))<1e-6f);
    assert(lights[0].half_direction[1]==0);
    assert(fabsf(lights[0].half_direction[2]-sqrtf(.5f))<1e-6f);
    assert(lights[0].position[0]==10 && lights[0].position[2]==-10);
    model.lights[0].pos[0]=0;model.lights[0].pos[2]=-5;
    prepare_specular(&model,&material,&view,1,lights);
    assert(lights[0].half_direction[2]==1);
    C3TitleBatch batch={.shininess=50,.specular={1,1,1}};
    C3TitleDrawVertex vertex={.pos={0,0,-10},.normal={0,0,1}};float rgb[3];
    raster_specular(&batch,&vertex,&view,1,lights,rgb);
    assert(rgb[0]==1 && fabsf(rgb[1]-128.f/255)<1e-6f && fabsf(rgb[2]-64.f/255)<1e-6f);
    vertex.normal[2]=-1;
    raster_specular(&batch,&vertex,&view,1,lights,rgb);
    assert(rgb[0]==0 && rgb[1]==0 && rgb[2]==0);
    model.lights[0].pos[2]=-10;
    prepare_specular(&model,&material,&view,1,lights);
    assert(lights[0].half_direction[2]==-1); /* Coincident light/joint fixed fallback. */
    model.lights[0].flags=15;model.lights[0].pos[2]=-5;
    model.lights[0].spot_cutoff=1;model.lights[0].spot_function=3;
    prepare_specular(&model,&material,&view,1,lights);
    assert(lights[0].half_direction[2]==1); /* Cone does not alter specular half vector. */
    model.lights[0].flags=13;model.lights[0].pos[2]=1;
    prepare_specular(&model,&material,&view,1,lights);
    assert(lights[0].position[2]==1048576 && lights[0].half_direction[2]==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"specular.c";path.write_text(support+normalize+specular+test)
            exe=Path(directory)/"specular"
            flags=shlex.split(os.environ.get("CFLAGS","-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            result=subprocess.run(compiler+flags+["-I",str(ROOT/"include"),str(path),"-lm","-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=="__main__":unittest.main()
