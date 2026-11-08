/* Source-only GPU check for the actual bike's added material features.
 * No disc data: uniform normals give independently calculable GX highlights,
 * and a four-color image checks generated/reflected UVs plus their cross terms. */
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "stage_renderer.h"

static C3Title scene;
static C3TitleDrawVertex points[24];
static C3TitleBatch batches[4];
static C3TitleTexture images[3];
static unsigned char white[8*8*4], cutout[8*8*4], corners[8*8*4];

static void quad(unsigned index, float left, float right, float top, float bottom,
                 float nx, float ny, float nz)
{
    const float xy[6][2]={{left,top},{left,bottom},{right,bottom},
                          {left,top},{right,bottom},{right,top}};
    for (unsigned i=0;i<6;++i) {
        C3TitleDrawVertex *p=&points[index*6+i];
        p->pos[0]=xy[i][0];p->pos[1]=xy[i][1];
        p->normal[0]=nx;p->normal[1]=ny;p->normal[2]=nz;
        p->uv[0]=xy[i][0]==right?1:0;p->uv[1]=xy[i][1]==top?0:1;
        memset(p->rgba,255,4);
    }
}

int main(void)
{
    gfxInitDefault();gfxSetDoubleBuffering(GFX_BOTTOM,false);consoleInit(GFX_BOTTOM,NULL);
    quad(0,-.9f,-.05f,.9f,.05f,.6f,0,.8f);
#ifdef C3_ARRIVAL_FADE_TEST
    quad(0,-.9f,-.05f,.9f,.05f,0,0,1); /* Saturates before the final fade. */
    const float fade=.5f;
    const unsigned expected_draws=7,expected_vertices=39;
#else
    const float fade=1;
    const unsigned expected_draws=6,expected_vertices=36;
#endif
    quad(1,.05f,.9f,.9f,.05f,0,0,1);
    quad(2,-.9f,-.05f,-.05f,-.9f,.6f,.6f,sqrtf(.28f));
    quad(3,.05f,.9f,-.05f,-.9f,.6f,.6f,sqrtf(.28f));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
        unsigned o=(y*8+x)*4;
        memset(white+o,255,4);memset(cutout+o,255,4);cutout[o+3]=x<4?0:255;
        corners[o]=(y<4&&x<4)||(y>=4&&x>=4)?255:0;
        corners[o+1]=x>=4?255:0;corners[o+2]=y>=4&&x<4?255:0;corners[o+3]=255;
    }
    images[0]=(C3TitleTexture){.width=8,.height=8,.pixels=white,.size=sizeof(white)};
    images[1]=(C3TitleTexture){.width=8,.height=8,.pixels=cutout,.size=sizeof(cutout)};
    images[2]=(C3TitleTexture){.width=8,.height=8,.pixels=corners,.size=sizeof(corners)};
    for(unsigned i=0;i<4;++i){
        batches[i]=(C3TitleBatch){.first=i*6,.count=6,.color_mode=4,.alpha_mode=0,
            .render_mode=0x201D,.flags=C3_TITLE_LIT|C3_TITLE_SPECULAR,.alpha=1,.texture_blend=1,
            .diffuse={1,1,1},.ambient={1,1,1},.specular={1,128.f/255.f,64.f/255.f},.shininess=50,
            .alpha_compare0=7,.alpha_compare1=7,.depth_compare=3};
        scene.pose.materials[i].visible=scene.pose.materials[i].alpha=1;
        scene.pose.materials[i].uv2_scale[0]=scene.pose.materials[i].uv2_scale[1]=1;
    }
    batches[1].flags|=C3_TITLE_TRANSLUCENT;batches[1].render_mode=0x4000201D;
    batches[1].texture=1;batches[1].alpha_mode=3;
    batches[1].alpha_compare0=batches[1].alpha_compare1=4;
    scene.pose.materials[1].specular_origin[0]=2; /* Joint origin differs from vertices. */
    for(unsigned i=2;i<4;++i){
        batches[i].flags=C3_TITLE_LIT|C3_TITLE_REFLECTION_BLEND;
        batches[i].render_mode=0x2015;batches[i].texture2=2;batches[i].texture2_blend=.5f;
    }
    scene.pose.materials[3].uv2_scale[0]=scene.pose.materials[3].uv2_scale[1]=0;
    scene.pose.materials[3].uv2_cross[0]=1;scene.pose.materials[3].uv2_cross[1]=-1;
    scene.pose.materials[3].uv2_offset[1]=1; /* (u,v) -> (v,1-u). */
    scene.draw_vertices=points;scene.batches=batches;scene.textures=images;
    scene.vertex_count=24;scene.batch_count=4;scene.texture_count=3;scene.light_count=2;
    scene.lights[0]=(C3TitleLight){.flags=4,.rgba={64.f/255.f,64.f/255.f,64.f/255.f,1}};
    scene.lights[1]=(C3TitleLight){.flags=9,.rgba={1,1,1,1},.pos={0,0,1}};
    C3TitleCamera camera={.eye={0,0,2},.fov_y_degrees=60,.near_plane=.1f,.far_plane=10,.aspect=4.f/3.f};
    const C3Title *models[]={&scene};char error[256]={0};
    int okay=c3_stage_renderer_init(models,1,error,sizeof(error))==0;
    printf("Arrival material probe\nTop: GX specular rational curve\nRight: joint origin + alpha cutout\nBottom: reflection and UV rotation\n\n%s\n",okay?"Renderer initialized":error);
    while(aptMainLoop()){
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        C3TitleRenderStats stats;
        if(okay){
            int result=c3_stage_renderer_draw(models,1,&camera,fade,&stats,error,sizeof(error));
            if(result==C3_RENDER_BUSY){svcSleepThread(1000000);continue;}
            if(result==C3_RENDER_ERROR){okay=0;printf("DRAW FAIL %s\n",error);}
        }
        if(okay&&(stats.draw_calls!=expected_draws||stats.vertices!=expected_vertices||stats.gpu_buffer_bytes!=2112)){
            okay=0;printf("Bad aggregate metrics %lu/%lu/%lu\n",(unsigned long)stats.draw_calls,
                (unsigned long)stats.vertices,(unsigned long)stats.gpu_buffer_bytes);
        }
        if(!okay){gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    while(!c3_stage_renderer_try_shutdown()){aptMainLoop();svcSleepThread(1000000);}
    c3_stage_renderer_fini();
    gfxExit();return okay?0:1;
}
