/* Standalone staged diagnosis of a noncompleting first GPU submission.
 * No game state/save implementation is linked. Original title data is
 * optional for the first three stages and never modified. */
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "stage_renderer.h"

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif

static C3Title synthetic;
static C3TitleDrawVertex points[6];
static C3TitleBatch batch;
static C3TitleTexture texture;
static unsigned char pixels[8 * 8 * 4];
static const char *const names[] = {"clear", "array", "array-fade", "title-array", "title-fade", "scene-return"};

static void make_synthetic(void)
{
    const float xy[6][2] = {{-.8f,.8f},{-.8f,-.8f},{.8f,-.8f},{-.8f,.8f},{.8f,-.8f},{.8f,.8f}};
    for (unsigned i=0;i<6;++i) {
        points[i].pos[0]=xy[i][0]; points[i].pos[1]=xy[i][1]; points[i].normal[2]=1;
        points[i].uv[0]=xy[i][0]>0?1:0; points[i].uv[1]=xy[i][1]>0?0:1;
        memset(points[i].rgba,255,4);
    }
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        unsigned char *p=pixels+(y*8+x)*4;
        p[0]=(y<4&&x<4)||(y>=4&&x>=4)?255:0;
        p[1]=x>=4?255:0; p[2]=y>=4&&x<4?255:0; p[3]=255;
    }
    batch=(C3TitleBatch){.count=6,.color_mode=4,.alpha_mode=3,.render_mode=0x4012,
        .alpha=1,.texture_blend=1,.alpha_compare0=7,.alpha_compare1=7,.depth_compare=3};
    texture=(C3TitleTexture){.width=8,.height=8,.pixels=pixels,.size=sizeof(pixels)};
    synthetic.draw_vertices=points; synthetic.batches=&batch; synthetic.textures=&texture;
    synthetic.vertex_count=6; synthetic.batch_count=1; synthetic.texture_count=1;
    synthetic.pose.materials[0].visible=synthetic.pose.materials[0].alpha=1;
    synthetic.pose.camera=(C3TitleCamera){.eye={0,0,2},.fov_y_degrees=60,.near_plane=.1f,.far_plane=10,.aspect=4.f/3.f};
}

static void record(FILE *trace, unsigned stage, const char *event)
{
    C3TitleRenderSnapshot s;
    c3_stage_renderer_snapshot(&s);
    if (trace) {
        fprintf(trace,"gpu_probe stage=%u name=%s event=%s submitted=%lu fill=%lu command=%lu transfer=%lu "
                "words=%lu draws=%lu vertices=%lu vblank=%lu,%lu\n",stage,names[stage],event,
                (unsigned long)s.submissions,(unsigned long)s.memory_fills,(unsigned long)s.command_lists,
                (unsigned long)s.display_transfers,(unsigned long)s.last_command_words,
                (unsigned long)s.last_draw_calls,(unsigned long)s.last_vertices,
                (unsigned long)C3D_FrameCounter(0),(unsigned long)C3D_FrameCounter(1));
        fflush(trace);
    }
    consoleClear();
    printf("Colosseum3DS | GPU probe\nBuild %.12s\n\nStage %u: %s\n%s\n\n",C3_BUILD_ID,stage,names[stage],event);
    printf("Submissions %lu | words %lu\nFill %lu | draw %lu | transfer %lu\n\n",
           (unsigned long)s.submissions,(unsigned long)s.last_command_words,
           (unsigned long)s.memory_fills,(unsigned long)s.command_lists,(unsigned long)s.display_transfers);
    printf("START+SELECT: guarded exit\nBusy recovery: X trace, A resume\n");
    gfxFlushBuffers();
}

int main(void)
{
    char error[256]={0};
    C3N3dsInput input={0};
    if(c3_n3ds_init(error,sizeof(error))) return 1;
    FILE *trace=fopen("sdmc:/3ds/Colosseum3DS/gpu-probe.log","w");
    unsigned start_stage=0,batch_limit=C3_TITLE_MAX_BATCHES;
    FILE *config=fopen("sdmc:/3ds/Colosseum3DS/gpu-probe.txt","r");
    if(config) {
        int fields=fscanf(config,"%u %u",&start_stage,&batch_limit);
        fclose(config);
        if(fields<1||start_stage>=6||!batch_limit||batch_limit>C3_TITLE_MAX_BATCHES) {
            snprintf(error,sizeof(error),"Invalid gpu-probe.txt: stage 0..5, batch count 1..64.");
            goto done;
        }
    }
    if(trace) {fprintf(trace,"gpu_probe_build %.12s start=%u batch_limit=%u\n",C3_BUILD_ID,start_stage,batch_limit);fflush(trace);}
    make_synthetic();
    C3Title *actual=calloc(1,sizeof(*actual));
    const C3Title *active=NULL;
    unsigned stage=start_stage;
    int result=0;
    if(!actual) {snprintf(error,sizeof(error),"Title allocation failed.");goto done;}
    for(;stage<6;++stage) {
        const C3Title *next=(stage==3||stage==4)?actual:&synthetic;
        if(next==actual&&!actual->file_data) {
            if(!c3_title_load(actual,"sdmc:/3ds/Colosseum3DS/boot/title.c3t",error,sizeof(error))||
               !c3_title_animate(actual,0,0)) {result=1;break;}
            for(unsigned i=batch_limit;i<actual->batch_count;++i) actual->pose.materials[i].visible=0;
        }
        if(active!=next) {
            if(c3_render_recover(trace,"gpu-probe-transition",stage,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT) {result=1;break;}
            if(c3_title_renderer_init(next,error,sizeof(error))) {result=1;break;}
            active=next;
        }
        record(trace,stage,"before-submit");
        int draw;
        do {
            draw=stage==0?c3_stage_renderer_probe_clear():
                c3_title_renderer_draw(active,(stage==2||stage==4)?.5f:1,NULL,error,sizeof(error));
            if(draw==C3_RENDER_BUSY&&c3_render_recover(trace,names[stage],stage,C3_RECOVER_WAIT)==C3_RECOVERY_EXIT) {result=1;break;}
        } while(draw==C3_RENDER_BUSY);
        if(result||draw==C3_RENDER_ERROR) {result=1;break;}
        record(trace,stage,"submitted");
        if(c3_render_recover(trace,names[stage],stage+1,C3_RECOVER_WAIT)==C3_RECOVERY_EXIT) {result=1;break;}
        record(trace,stage,"completed");
        uint64_t until=c3_n3ds_time_us()+UINT64_C(1500000);
        while(c3_n3ds_time_us()<until) {
            if(!c3_n3ds_poll(&input)) {result=1;break;}
            svcSleepThread(1000000);
        }
        if(result) break;
    }
    c3_render_recover(trace,"gpu-probe-exit",stage,C3_RECOVER_EXIT);
    c3_title_close(actual); free(actual);
    if(trace) {fprintf(trace,"gpu_probe_end complete=%u stages=%u error=%s\n",stage==6,stage,error);fflush(trace);}
    if(!result&&stage==6) {
        consoleClear(); printf("GPU submission probe complete\n\nSix stages completed.\n\nRead gpu-probe.log for IRQ counters.\nSTART: exit\n");
        gfxFlushBuffers();
        c3_n3ds_wait_for_exit();
    }
done:
    if(error[0]) {consoleClear();printf("GPU probe stopped\n%s\nSTART: exit\n",error);gfxFlushBuffers();c3_n3ds_wait_for_exit();}
    if(trace) fclose(trace);
    c3_n3ds_shutdown();
    return error[0]?1:0;
}
