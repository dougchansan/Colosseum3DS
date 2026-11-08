/* Source-only RTT orientation and lifetime probe. No game assets. */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "stage_renderer.h"

static C3Title screen, television;
static C3TitleDrawVertex screen_points[6], television_points[6];
static C3TitleBatch batch;
static C3TitleTexture screen_image, television_image;
static unsigned char white[8*8*4], corners[8*8*4];

static void quad(C3TitleDrawVertex *points, float extent)
{
    const float xy[6][2]={{-1,1},{-1,-1},{1,-1},{-1,1},{1,-1},{1,1}};
    for(unsigned i=0;i<6;++i) {
        points[i].pos[0]=xy[i][0]*extent;points[i].pos[1]=xy[i][1]*extent;
        points[i].normal[2]=1;
        points[i].uv[0]=xy[i][0]>0?1:0;points[i].uv[1]=xy[i][1]>0?0:1;
        memset(points[i].rgba,255,4);
    }
}

int main(void)
{
    gfxInitDefault();gfxSetDoubleBuffering(GFX_BOTTOM,false);consoleInit(GFX_BOTTOM,NULL);
    quad(screen_points,.9f);quad(television_points,2.f);
    memset(white,255,sizeof white);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        unsigned o=(y*8+x)*4;
        corners[o]=(y<4&&x<4)||(y>=4&&x>=4)?255:0;
        corners[o+1]=x>=4?255:0;corners[o+2]=y>=4&&x<4?255:0;corners[o+3]=255;
    }
    batch=(C3TitleBatch){.count=6,.color_mode=4,.alpha_mode=3,.render_mode=0x4012,
        .alpha=1,.texture_blend=1,.alpha_compare0=7,.alpha_compare1=7,.depth_compare=3};
    screen_image=(C3TitleTexture){.width=8,.height=8,.pixels=white,.size=sizeof white};
    television_image=(C3TitleTexture){.width=8,.height=8,.pixels=corners,.size=sizeof corners};
    screen.draw_vertices=screen_points;screen.batches=&batch;screen.textures=&screen_image;
    screen.vertex_count=6;screen.batch_count=screen.texture_count=1;
    screen.pose.materials[0].visible=screen.pose.materials[0].alpha=1;
    television=screen;television.draw_vertices=television_points;television.textures=&television_image;
    television.pose.camera=(C3TitleCamera){.eye={0,0,2},.fov_y_degrees=90,.near_plane=.1f,.far_plane=10,.aspect=1};
    C3TitleCamera camera={.eye={0,0,2},.fov_y_degrees=60,.near_plane=.1f,.far_plane=10,.aspect=4.f/3.f};
    const C3Title *models[]={&screen,&television};
    char error[256]={0};int okay=c3_stage_renderer_init(models,2,error,sizeof error)==0;
    if(okay)okay=c3_stage_renderer_set_tv(1,0,0,true,error,sizeof error);
    printf("256x256 texture-backed TV probe\nExpected: red | green\n          blue | yellow\nTwo GPU draws, one frame\n\n%s\n",okay?"Renderer initialized":error);
    unsigned frame=0;
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        if(okay) {
            C3TitleRenderStats stats;
            int result=c3_stage_renderer_draw(models,2,&camera,1,&stats,error,sizeof error);
            if(result==C3_RENDER_BUSY){svcSleepThread(1000000);continue;}
            if(result==C3_RENDER_ERROR){okay=0;printf("DRAW FAIL %s\n",error);}
            else if(stats.draw_calls!=2||stats.vertices!=12){okay=0;printf("Incorrect TV pass counts\n");}
            else if(frame++==30)printf("TV PASS: 2 draws, 12 vertices\nGPU bytes %lu\n",(unsigned long)stats.gpu_buffer_bytes);
        }
        if(!okay){gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    while(!c3_stage_renderer_try_shutdown()){aptMainLoop();svcSleepThread(1000000);}
    c3_stage_renderer_fini();gfxExit();return okay?0:1;
}
