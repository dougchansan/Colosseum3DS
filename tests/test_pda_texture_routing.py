"""Execute production GX texture routing with colored source corners."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PdaTextureRoutingTests(unittest.TestCase):
    def test_routing_preserves_alpha_tiling_orientation_and_source_pixels(self):
        cc=shlex.split(os.environ.get("CC","cc"))
        if not shutil.which(cc[0]):
            self.skipTest("Host C compiler required")
        source=(ROOT/"platform/3ds/title_renderer.c").read_text()
        morton=source[source.index("static unsigned morton8("):source.index("static GPU_TEXTURE_WRAP_PARAM wrap_mode(")]
        writer=source[source.index("static void write_texture_pixels("):source.index("static bool upload_texture(")]
        harness=r'''
#include "colosseum3ds/title.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct { void *data; } C3D_Tex;
static void C3D_TexFlush(C3D_Tex *gpu){(void)gpu;}
'''+morton+writer+r'''
int main(void){
 uint8_t pixels[8*8*4],original[sizeof pixels],output[sizeof pixels];
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  unsigned at=(y*8+x)*4;pixels[at]=17+x;pixels[at+1]=41+y;
  pixels[at+2]=91+x+y;pixels[at+3]=151+x*3+y;
 }
 memcpy(original,pixels,sizeof pixels);
 C3TitleTexture texture={.width=8,.height=8,.pixels=pixels,.size=sizeof pixels};C3D_Tex gpu={output};
 /* All64 route triples include repeated channels and alpha-to-RGB. */
 for(unsigned r=0;r<4;r++)for(unsigned g=0;g<4;g++)for(unsigned b=0;b<4;b++){
  uint8_t route[3]={r,g,b};write_texture_pixels(&gpu,&texture,route);
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   const uint8_t *in=pixels+((7-y)*8+x)*4;const uint8_t *out=output+morton8(x,y)*4;
   assert(out[0]==in[3]&&out[1]==in[b]&&out[2]==in[g]&&out[3]==in[r]);
  }
 }
 assert(!memcmp(original,pixels,sizeof pixels));return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"probe.c";path.write_text(harness)
            exe=Path(tmp)/"probe"
            subprocess.run([*cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(ROOT/"include"),str(path),"-o",str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,capture_output=True,text=True)


if __name__=="__main__":
    unittest.main()
