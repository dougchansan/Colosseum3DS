"""Execute the production battle HUD draw path against a bounded GPU recorder."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BattleHudTests(unittest.TestCase):
    def test_shader_color_contract_and_disabled_preconditions(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(cc[0]):
            self.skipTest('A host C compiler is required')
        source = (ROOT / 'platform/3ds/title_renderer.c').read_text()
        drawing = source[source.index('static void hud_quad('):
                         source.index('int c3_stage_renderer_draw(')]
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "stage_renderer.h"
typedef int C3D_Mtx;
typedef int C3D_TexEnv;
#define GPU_VERTEX_SHADER 0
#define GPU_TRIANGLES 0
#define GPU_CULL_NONE 0
#define GPU_ALWAYS 0
#define GPU_WRITE_COLOR 0
#define GPU_BLEND_ADD 0
#define GPU_SRC_ALPHA 0
#define GPU_ONE_MINUS_SRC_ALPHA 0
#define GPU_ZERO 0
#define GPU_ONE 0
#define C3D_Both 0
#define GPU_PRIMARY_COLOR 0
#define GPU_REPLACE 0
#define GPU_TEXTURE0 0
#define GPU_MODULATE 0
#define Mtx_OrthoTilt(...) ((void)0)
#define C3D_FVUnifMtx4x4(shader,location,matrix) ((void)(matrix))
#define C3D_CullFace(...) ((void)0)
#define C3D_DepthTest(...) ((void)0)
#define C3D_AlphaTest(...) ((void)0)
#define C3D_AlphaBlend(...) ((void)0)
#define C3D_TexEnvInit(...) ((void)0)
#define C3D_TexEnvSrc(env,...) ((void)(env))
#define C3D_TexEnvFunc(env,...) ((void)(env))
#define C3D_TexBind(unit,tex) ((void)(tex))
static C3D_TexEnv env;
static C3D_TexEnv *C3D_GetTexEnv(int stage){(void)stage;return &env;}
static C3StageBattleHud battle_hud;
static uint32_t hud_texture=UINT32_MAX;
static int color_pass_location=17,textures[1];
static unsigned quads,attributes,visible,green,white,vertices;
static float pass_x,pass_y,red,blue,grn,alpha;
static void C3D_FVUnifSet(int shader,int loc,float x,float y,float z,float w){
    (void)shader;(void)z;(void)w;assert(loc==17);pass_x=x;pass_y=y;
}
static void C3D_ImmDrawBegin(int primitive){(void)primitive;attributes=0;}
static void C3D_ImmSendAttrib(float x,float y,float z,float w){
    if(attributes%5==2){red=x;grn=y;blue=z;alpha=w;}
    if(attributes%5==0)++vertices;
    ++attributes;
}
static void C3D_ImmDrawEnd(void){
    assert(attributes==30);++quads;
    /* The actual title shader outputs vertexColor*pass.x + specular*pass.y.
     * HUD sends zero specular, so pass.x=0 makes every command black. */
    float r=red*pass_x,g=grn*pass_x,b=blue*pass_x;
    if(alpha>0&&(r>0||g>0||b>0))++visible;
    if(g>r&&g>b)++green;
    if(r==1&&g==1&&b==1)++white;
}
static float clamp01(float x){return fmaxf(0,fminf(1,x));}
'''
        harness += drawing + r'''
int main(void){
    C3TitleRenderStats stats={0};
    draw_battle_hud(&stats);assert(!quads&&!stats.draw_calls);
    battle_hud.enabled=1;
    draw_battle_hud(&stats);assert(!quads&&!stats.draw_calls);
    hud_texture=0;strcpy(battle_hud.lines[0],"A B");
    for(unsigned i=0;i<4;++i){battle_hud.hp[i]=25;battle_hud.maximum_hp[i]=100;}
    draw_battle_hud(&stats);
    assert(pass_x==1&&pass_y==0);
    assert(quads==11&&visible==11&&green==4&&white==2);
    assert(stats.draw_calls==11&&stats.vertices==66&&vertices==66);
    battle_hud.enabled=0;draw_battle_hud(&stats);assert(quads==11);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / 'hud.c').write_text(harness)
            exe = d / 'hud'
            flags = shlex.split(os.environ.get('CFLAGS', '-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
            result = subprocess.run(cc + flags + ['-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'platform/3ds'), str(d / 'hud.c'), '-lm', '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
