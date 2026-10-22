"""Check GX COS2 angular attenuation, including the rear-hemisphere clamp."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RendererSpotlightTests(unittest.TestCase):
    def test_original_cos2_cone_and_distance(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not shutil.which(compiler[0]):
            self.skipTest("A host C compiler is required")
        source = (ROOT / "platform/3ds/title_renderer.c").read_text()
        normalize = source[source.index("static float normalize("):source.index("static unsigned morton8(")]
        light = source[source.index("static float attenuation("):source.index("/* HSD state.c")]
        test = r'''
int main(void) {
    C3TitleLight l={.pos={0,0,0},.interest={0,0,1},.spot_function=3,.spot_cutoff=60,
        .reference_distance=10,.reference_brightness=.25f,.distance_function=3};
    float forward[3]={0,0,-1}, backward[3]={0,0,1}, side[3]={1,0,0};
    float halfway[3]={sqrtf(1-.75f*.75f),0,-.75f};
    assert(fabsf(spotlight_attenuation(&l,forward)-1)<1e-6f);
    assert(spotlight_attenuation(&l,backward)==0);
    assert(spotlight_attenuation(&l,side)==0);
    assert(fabsf(spotlight_attenuation(&l,halfway)-.375f)<1e-6f);
    assert(fabsf(attenuation(&l,10)-.25f)<1e-6f);
    assert(fabsf(attenuation(&l,20)-(1.f/13))<1e-6f);
    l.interest[2]=0;
    assert(fabsf(spotlight_attenuation(&l,forward)-1)<1e-6f);
    l.spot_function=0;
    assert(spotlight_attenuation(&l,backward)==1);
    l.spot_function=3;l.spot_cutoff=0;
    assert(spotlight_attenuation(&l,backward)==1);
    l.spot_cutoff=91;
    assert(spotlight_attenuation(&l,backward)==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"spot.c"
            path.write_text('#include "colosseum3ds/title.h"\n#include <math.h>\n#include <assert.h>\n'+normalize+light+test)
            flags=shlex.split(os.environ.get("CFLAGS","-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror"))
            exe=Path(directory)/"spot"
            result=subprocess.run(compiler+flags+["-I",str(ROOT/"include"),str(path),"-lm","-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
