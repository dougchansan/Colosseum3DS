#include "dna_key_scene.h"
#include "colosseum3ds/dna_key.h"
#include "colosseum3ds/title.h"
#include "colosseum3ds/area_camera.h"
#include "stage_renderer.h"
#include "n3ds_platform.h"
#include "sfx_player.h"
#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#define ROOT "sdmc:/3ds/Colosseum3DS/boot/"
int c3_dna_key_scene_available(void){char path[160];for(unsigned i=0;i<12;i++){snprintf(path,sizeof path,ROOT "f387_obj%u.c3t",i);FILE *f=fopen(path,"rb");if(!f)return 0;fclose(f);}const char *paths[]={ROOT "f387_107c1800.c3c",ROOT "f387_107d1800.c3c"};for(unsigned i=0;i<2;i++){FILE *f=fopen(paths[i],"rb");if(!f)return 0;fclose(f);}return 1;}
int c3_dna_key_scene_run(const uint16_t expected[3],FILE *trace,char *error,size_t capacity){
    if(!expected){snprintf(error,capacity,"DNA expected species missing");return -1;}
    C3DnaKey state;if(!c3_dna_key_init(&state,expected))return state.result;
    C3Title *storage=calloc(12,sizeof *storage);if(!storage){snprintf(error,capacity,"DNA model allocation failed");return -1;}const C3Title *models[12];C3AreaCamera tracks[2]={{0}};int initialized=0,result=-1;char path[160];
    for(unsigned i=0;i<12;i++){snprintf(path,sizeof path,ROOT "f387_obj%u.c3t",i);if(!c3_title_load(storage+i,path,error,capacity))goto done;models[i]=storage+i;}
    if(!c3_area_camera_load(tracks,ROOT "f387_107c1800.c3c",error,capacity)||!c3_area_camera_load(tracks+1,ROOT "f387_107d1800.c3c",error,capacity))goto done;
    if(c3_stage_renderer_init(models,12,error,capacity))goto done;
    initialized=1;
    unsigned phase=0;float time=0;uint64_t previous=c3_n3ds_active_time_us();uint32_t wait_sound=0;float cursor_wait=0;
    c3_sfx_play(0x27,127);
#ifdef C3_DNA_KEY_PROBE
    unsigned probe_step=0,probe_capture=0;float probe_next=.15f;
    static const uint16_t probe_inputs[]={0x10,5,0x10,8,0x10,2,0x10,0x20,0x10,6,0x10,8,8,0x10};
#endif
    if(trace){fprintf(trace,"dna_scene floor=387 lifecycle=05960014 native=ce expected=%04x,%04x,%04x models=12\n",expected[0],expected[1],expected[2]);fflush(trace);}
    while(aptMainLoop()){
        C3N3dsInput input;if(!c3_n3ds_poll(&input)||input.exit_requested)goto done;
        uint64_t now=c3_n3ds_active_time_us();float dt=(now-previous)/1000000.f;previous=now;if(dt>.1f)dt=.1f;time+=dt;if(cursor_wait>0)cursor_wait-=dt;
        if(phase==0&&time>=(tracks[0].frame_count-1)/tracks[0].fps){phase=1;time=0;}
        if(phase==1){
            if(wait_sound&&!c3_sfx_status(wait_sound)){c3_dna_key_sound_done(&state);if(trace){fprintf(trace,"dna_sound_done sound=%03lx count=%u reset=%u\n",(unsigned long)wait_sound,state.count,state.reset_pending);fflush(trace);}wait_sound=0;}
            if(!wait_sound&&state.result!=-1){phase=2;time=0;c3_sfx_play(0x28,127);}
            else if(!wait_sound&&cursor_wait<=0){
                uint16_t keys=0;if(input.pressed&C3_PAD_UP)keys|=1;if(input.pressed&C3_PAD_DOWN)keys|=2;if(input.pressed&C3_PAD_LEFT)keys|=4;if(input.pressed&C3_PAD_RIGHT)keys|=8;if(input.pressed&C3_PAD_A)keys|=0x10;
                uint16_t buttons=(input.pressed&C3_PAD_B)?0x20:0;
#ifdef C3_DNA_KEY_PROBE
                if(probe_capture&&time>=probe_next&&probe_step<sizeof probe_inputs/sizeof probe_inputs[0]){
                    unsigned code=probe_inputs[probe_step++];keys=code==0x20?0:code;buttons=code==0x20?0x20:0;probe_next=time+.15f;
                }
#endif
                unsigned oldx=state.x,oldy=state.y;unsigned oldcount=state.count;
                c3_dna_key_input(&state,keys,buttons);
                if(trace&&(keys||buttons)){fprintf(trace,"dna_grid keys=%02x buttons=%02x x=%u y=%u count=%u previous_count=%u reset=%u result=%d sound=%03lx\n",keys,buttons,state.x,state.y,state.count,oldcount,state.reset_pending,state.result,(unsigned long)state.sound);fflush(trace);}
                if(oldx!=state.x||oldy!=state.y)cursor_wait=1.f/60;
                if(state.sound){c3_sfx_play(state.sound,127);if(state.sound==0x26||state.sound==0x4a1)wait_sound=state.sound;}
            }
        }
        C3TitleCamera camera;unsigned track=phase==2?1:0;float ct=phase==1?(tracks[0].frame_count-1)/tracks[0].fps:time;
        if(!c3_area_camera_sample(tracks+track,ct,&camera)){snprintf(error,capacity,"DNA source camera failed");goto done;}
        for(unsigned i=0;i<12;i++)if(!c3_title_animate(storage+i,state.clips[i],1.f/60)){snprintf(error,capacity,"DNA source model clip failed");goto done;}
        C3TitleRenderStats stats={0};int draw=c3_stage_renderer_draw(models,12,&camera,0,&stats,error,capacity);if(draw==C3_RENDER_BUSY){gspWaitForVBlank();continue;}if(draw)goto done;
#ifdef C3_DNA_KEY_PROBE
        if(phase==1&&time>.1f&&!probe_capture){c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/dna-key.bmp");probe_capture=1;}
        if(phase==1&&state.count==3&&state.result==0&&probe_capture==1){c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/dna-key-selected.bmp");probe_capture=2;}
#endif
        if(phase==2&&time>=(tracks[1].frame_count-1)/tracks[1].fps){result=state.result;break;}
    }
 done:
    if(initialized)c3_stage_renderer_shutdown();
    for(unsigned i=0;i<12;i++)c3_title_close(storage+i);
    for(unsigned i=0;i<2;i++)c3_area_camera_close(tracks+i);
    free(storage);
    if(trace){fprintf(trace,"dna_return result=%d flags_written=0\n",result);fflush(trace);}return result;
}
