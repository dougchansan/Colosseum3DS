#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "title_renderer.h"

static C3Title title;
static C3TitleDrawVertex points[6];
static C3TitleBatch batch;
static C3TitleTexture texture;
static unsigned char pixels[8*8*4];
int main(void) {
    gfxInitDefault(); gfxSetDoubleBuffering(GFX_BOTTOM, false); consoleInit(GFX_BOTTOM, NULL);
    const float xy[6][2] = {{-.8f,.8f},{-.8f,-.8f},{.8f,-.8f},{-.8f,.8f},{.8f,-.8f},{.8f,.8f}};
    for (unsigned i=0;i<6;i++) {
        points[i].pos[0]=xy[i][0];points[i].pos[1]=xy[i][1];points[i].normal[2]=1;
        points[i].uv[0]=xy[i][0]>.0f?1:0;points[i].uv[1]=xy[i][1]>.0f?0:1;
        memset(points[i].rgba,255,4);
    }
    for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++) {
        unsigned char *p=pixels+(y*8+x)*4;
        p[0]=(y<4&&x<4)||(y>=4&&x>=4)?255:0;
        p[1]=x>=4?255:0;p[2]=y>=4&&x<4?255:0;p[3]=255;
    }
    batch=(C3TitleBatch){.count=6,.color_mode=4,.alpha_mode=3,.render_mode=0x4012,.alpha=1,.texture_blend=1,.alpha_compare0=7,.alpha_compare1=7,.depth_compare=3};
    texture=(C3TitleTexture){.width=8,.height=8,.pixels=pixels,.size=sizeof(pixels)};
    title.draw_vertices=points;title.batches=&batch;title.textures=&texture;
    title.vertex_count=6;title.batch_count=1;title.texture_count=1;
    title.pose.materials[0].visible=title.pose.materials[0].alpha=1;
#ifdef C3_CORNER_TEV_TEST
    batch.flags=C3_TITLE_CULL_BACK|C3_TITLE_TEV_LERP;
    for(unsigned i=0;i<3;i++) {title.pose.materials[0].tev0[i]=0.2f;title.pose.materials[0].konst[i]=0.8f;}
#endif
    title.pose.camera=(C3TitleCamera){.eye={0,0,2},.fov_y_degrees=60,.near_plane=.1f,.far_plane=10,.aspect=4.f/3.f};
    char error[256]={0};int okay=c3_title_renderer_init(&title,error,sizeof(error))==0;
#ifdef C3_CORNER_TEV_TEST
    printf("Title TEV interpolation probe\nRGB endpoints 0.2 and 0.8\nBackface culling enabled\n\n%s\n",okay?"Renderer initialized":error);
#else
    printf("Title texture orientation probe\nTop left red, top right green\nBottom left blue, bottom right yellow\n\n%s\n",okay?"Renderer initialized":error);
#endif
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        if(okay&&c3_title_renderer_draw(&title,1,NULL,error,sizeof(error))) {okay=0;printf("DRAW FAIL %s\n",error);}
        if(!okay){gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    c3_title_renderer_shutdown();gfxExit();return okay?0:1;
}
