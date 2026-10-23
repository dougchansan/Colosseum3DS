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
typedef struct { int textured; } C3D_TexEnv;
typedef int C3D_BufInfo;
typedef struct { float pos[3], uv[2], rgba[4], uv2[2], specular[3]; } TitleGPUVertex;
#define C3_HUD_MAX_VERTICES 2358u
static TitleGPUVertex storage[C3_HUD_MAX_VERTICES], *hud_vertices=storage;
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
#define GPU_PRIMARY_COLOR 1
#define GPU_REPLACE 0
#define GPU_TEXTURE0 2
#define GPU_MODULATE 0
#define Mtx_OrthoTilt(...) ((void)0)
#define C3D_FVUnifMtx4x4(shader,location,matrix) ((void)(matrix))
#define C3D_CullFace(...) ((void)0)
#define C3D_DepthTest(...) ((void)0)
#define C3D_AlphaTest(...) ((void)0)
#define C3D_AlphaBlend(...) ((void)0)
#define C3D_TexEnvInit(...) ((void)0)
#define C3D_TexEnvSrc(env,mode,src,b,c) ((env)->textured=((src)==GPU_TEXTURE0))
#define C3D_TexEnvFunc(env,...) ((void)(env))
#define C3D_TexBind(unit,tex) ((void)(tex))
#define C3D_SetViewport(...) ((void)0)
static void GSPGPU_FlushDataCache(const void *p,size_t bytes){assert(p==storage&&bytes<=sizeof storage);}
static C3D_BufInfo buffers;
static C3D_BufInfo *C3D_GetBufInfo(void){return &buffers;}
static void BufInfo_Init(C3D_BufInfo *b){assert(b==&buffers);}
static void BufInfo_Add(C3D_BufInfo *b,const void *v,size_t size,int count,int layout){assert(b==&buffers&&v==storage&&size==sizeof(TitleGPUVertex)&&count==5&&layout==0x43210);}
static C3D_TexEnv env;
static int env_dirty,submitted_textured;
static C3D_TexEnv *C3D_GetTexEnv(int stage){(void)stage;env_dirty=1;return &env;}
static C3StageBattleHud battle_hud;
static uint32_t hud_texture=UINT32_MAX;
static int color_pass_location=17,textures[1];
static unsigned draws,visible,green,white,vertices;
static float pass_x,pass_y;
static void C3D_FVUnifSet(int shader,int loc,float x,float y,float z,float w){
    (void)shader;(void)z;(void)w;assert(loc==17);pass_x=x;pass_y=y;
}
static void C3D_DrawArrays(int primitive,unsigned first,unsigned count){
    assert(primitive==GPU_TRIANGLES&&first+count<=C3_HUD_MAX_VERTICES);
    assert(count%6==0);
    if(env_dirty){submitted_textured=env.textured;env_dirty=0;}
    assert(submitted_textured==(first!=0)); /* Each glyph draw must submit textured TEV state. */
    ++draws;vertices+=count;
    for(unsigned i=first;i<first+count;i+=6){
        const TitleGPUVertex *v=&storage[i];float r=v->rgba[0]*pass_x,g=v->rgba[1]*pass_x,b=v->rgba[2]*pass_x;
        if(v->rgba[3]>0&&(r>0||g>0||b>0))++visible;
        if(g>r&&g>b)++green;
        if(r==1&&g==1&&b==1){++white;assert(v->uv[0]>=0&&v->uv[0]<1&&v->uv[1]>=0&&v->uv[1]<1);}
    }
}
static float clamp01(float x){return fmaxf(0,fminf(1,x));}
'''
        harness += drawing + r'''
int main(void){
    C3TitleRenderStats stats={0};
    draw_battle_hud(&stats);assert(!draws&&!stats.draw_calls);
    battle_hud.enabled=1;
    draw_battle_hud(&stats);assert(!draws&&!stats.draw_calls);
    hud_texture=0;strcpy(battle_hud.lines[0],"A B");
    for(unsigned i=0;i<4;++i){battle_hud.hp[i]=25;battle_hud.maximum_hp[i]=100;}
    draw_battle_hud(&stats);
    assert(pass_x==1&&pass_y==0);
    /* Atlas upload already preserves top-origin rows. An extra 1-V would
     * choose the previous glyph row upside down (D -> vertically flipped4). */
    assert(storage[54].uv[0]==1/16.f&&storage[54].uv[1]==4/8.f); /* A */
    assert(storage[56].uv[1]==5/8.f); /* Bottom edge advances down the atlas. */
    assert(draws==2&&visible==11&&green==4&&white==2);
    assert(stats.draw_calls==2&&stats.vertices==66&&vertices==66);
    /* All 384 cells plus nine background/bar quads exactly fit the arena. */
    for(unsigned row=0;row<8;++row){memset(battle_hud.lines[row],'W',48);battle_hud.lines[row][48]=0;}
    draw_battle_hud(&stats);assert(draws==4&&vertices==66+C3_HUD_MAX_VERTICES);
    battle_hud.enabled=0;draw_battle_hud(&stats);assert(draws==4);
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
