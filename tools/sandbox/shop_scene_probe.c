/* Original nine-model shop renderer probe. Reads only private prepared packs.
 * This exercises geometry/lighting/TV; it does not run GSVM or claim gameplay. */
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stage_renderer.h"
#include "colosseum3ds/area.h"
#include "colosseum3ds/area_camera.h"
#include "colosseum3ds/area_lights.h"

#define ROOT "sdmc:/3ds/Colosseum3DS/boot/"

static void place(C3Title *model,const float position[3],float yaw)
{
    float sine=sinf(yaw),cosine=cosf(yaw);
    for(uint32_t i=0;i<model->vertex_count;++i) {
        C3TitleDrawVertex *v=model->draw_vertices+i;
        float x=v->pos[0],z=v->pos[2],nx=v->normal[0],nz=v->normal[2];
        v->pos[0]=cosine*x+sine*z+position[0];v->pos[1]+=position[1];
        v->pos[2]=-sine*x+cosine*z+position[2];
        v->normal[0]=cosine*nx+sine*nz;v->normal[2]=-sine*nx+cosine*nz;
    }
    for(uint32_t i=0;i<model->batch_count;++i) {
        float *p=model->pose.materials[i].specular_origin,x=p[0],z=p[2];
        p[0]=cosine*x+sine*z+position[0];p[1]+=position[1];p[2]=-sine*x+cosine*z+position[2];
    }
}

int main(void)
{
    gfxInitDefault();gfxSetDoubleBuffering(GFX_BOTTOM,false);consoleInit(GFX_BOTTOM,NULL);
    osSetSpeedupEnable(true);
    char error[256]={0},path[192];
    C3Title *models=calloc(9,sizeof(*models));const C3Title *pointers[9];
    C3Area area={0};C3FieldMap field={0};C3AreaCamera camera={0};C3AreaLights lights={0};
    C3FieldPlayer wes;
    FILE *trace=fopen("sdmc:/3ds/Colosseum3DS/shop-probe.log","w");
    float camera_seconds=4;
    FILE *config=fopen("sdmc:/3ds/Colosseum3DS/shop-probe.txt","r");
    if(config){if(fscanf(config,"%f",&camera_seconds)!=1)camera_seconds=4;fclose(config);}
    if(!isfinite(camera_seconds)||camera_seconds<0||camera_seconds>10)camera_seconds=4;
    int okay=models && c3_area_load(&area,ROOT "shop.c3a",error,sizeof error) && area.actor_count==5 &&
        c3_field_load(&field,ROOT "shop.c3f",error,sizeof error) && c3_field_spawn(&field,0,&wes) &&
        c3_area_camera_load(&camera,ROOT "shop_camera.c3c",error,sizeof error) &&
        c3_area_lights_load(&lights,ROOT "shop.c3l",error,sizeof error);
    size_t resident=0;
    for(unsigned i=0;i<9&&okay;++i) {
        if(i<2)snprintf(path,sizeof path,ROOT "shop_map%u.c3t",i);
        else if(i==2)snprintf(path,sizeof path,ROOT "shop_wes.c3t");
        else if(i==8)snprintf(path,sizeof path,ROOT "shop_tv.c3t");
        else snprintf(path,sizeof path,ROOT "%s",area.actors[i-3].file);
        okay=c3_title_load(models+i,path,error,sizeof error);pointers[i]=models+i;
        resident+=models[i].resident_bytes;
        if(trace){fprintf(trace,"load %u okay=%d resident=%lu error=%s\n",i,okay,(unsigned long)resident,error);fflush(trace);}
    }
    if(okay)okay=c3_stage_renderer_init(pointers,9,error,sizeof error)==0 &&
        c3_stage_renderer_set_tv(8,1,1,true,error,sizeof error);
    if(trace){fprintf(trace,"init okay=%d error=%s\n",okay,error);fflush(trace);}
    printf("Original Outskirt shop GPU probe\nNine models, original camera %.1fs\nNo scripts or dialogue in this probe\n%s\n",(double)camera_seconds,okay?"Renderer initialized":error);
    unsigned frame=0;
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        if(okay) {
            float seconds=frame/30.f;C3TitleCamera sampled;
            okay=c3_area_camera_sample(&camera,camera_seconds,&sampled);
            for(unsigned i=0;i<9&&okay;++i) {
                unsigned clip=i>2&&i<8?area.actors[i-3].motions[0]:0;
                if(clip>=models[i].clip_count){snprintf(error,sizeof error,"Invalid original idle clip");okay=0;break;}
                models[i].clips[clip].loop=1;
                okay=c3_title_animate(models+i,clip,seconds);
                if(i<8)okay=okay&&c3_area_lights_apply(&lights,seconds,models+i);
                if(i==2)place(models+i,wes.position,wes.yaw);
                if(i>2&&i<8)place(models+i,area.actors[i-3].position,area.actors[i-3].yaw);
            }
            C3TitleRenderStats stats;
            int result=okay?c3_stage_renderer_draw(pointers,9,&sampled,1,&stats,error,sizeof error):C3_RENDER_ERROR;
            if(result==C3_RENDER_BUSY){svcSleepThread(1000000);continue;}
            if(result==C3_RENDER_ERROR){okay=0;printf("DRAW FAIL %s\n",error);}
            else {
                C3TitleRenderSnapshot snapshot;c3_stage_renderer_snapshot(&snapshot);
                if(frame%15==0) {
                    printf("\x1b[7;1HFrame %u | time %.2f\nModels %lu textures %lu\nDraws %lu vertices %lu\nWords %lu | GPU %.2f MiB\nCPU owned %.2f MiB\n",frame,(double)seconds,
                        (unsigned long)snapshot.scene_models,(unsigned long)snapshot.scene_textures,
                        (unsigned long)stats.draw_calls,(unsigned long)stats.vertices,(unsigned long)snapshot.last_command_words,
                        snapshot.gpu_buffer_bytes/(1024.*1024.),resident/(1024.*1024.));
                    if(trace){fprintf(trace,"frame=%u seconds=%.3f models=%lu textures=%lu draws=%lu vertices=%lu words=%lu gpu_bytes=%lu submitted=%lu command=%lu transfer=%lu\n",frame,(double)seconds,
                        (unsigned long)snapshot.scene_models,(unsigned long)snapshot.scene_textures,(unsigned long)stats.draw_calls,
                        (unsigned long)stats.vertices,(unsigned long)snapshot.last_command_words,(unsigned long)snapshot.gpu_buffer_bytes,
                        (unsigned long)snapshot.submissions,(unsigned long)snapshot.command_lists,(unsigned long)snapshot.display_transfers);fflush(trace);}
                }
                ++frame;
            }
        }
        if(!okay){gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    while(!c3_stage_renderer_try_shutdown()){aptMainLoop();svcSleepThread(1000000);}
    c3_stage_renderer_fini();
    if(models){for(unsigned i=0;i<9;++i)c3_title_close(models+i);free(models);}
    c3_area_close(&area);c3_field_close(&field);c3_area_camera_close(&camera);c3_area_lights_close(&lights);
    if(trace)fclose(trace);
    gfxExit();return okay?0:1;
}
