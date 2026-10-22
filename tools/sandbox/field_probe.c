/* Actual original-map/Wes visual integration probe, not gameplay or progress.
 * Loads private C3F/C3T assets, shares the real input/movement/camera/renderer,
 * and writes only isolated diagnostic CSVs. No game-state/save API is linked. */
#include <3ds.h>
#include "colosseum3ds/field.h"
#include "n3ds_platform.h"
#include "stage_renderer.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSET_ROOT "sdmc:/3ds/Colosseum3DS/field-probe/"
#define MAX_MODELS 7u

static void place_actor(C3Title *model, const C3FieldPlayer *player,
                        float *minimum_y, float *maximum_y)
{
    float sine=sinf(player->yaw), cosine=cosf(player->yaw);
    *minimum_y=INFINITY; *maximum_y=-INFINITY;
    for(uint32_t i=0;i<model->vertex_count;++i) {
        C3TitleDrawVertex *v=model->draw_vertices+i;
        float x=v->pos[0],z=v->pos[2],nx=v->normal[0],nz=v->normal[2];
        v->pos[0]=cosine*x+sine*z+player->position[0];
        v->pos[1]+=player->position[1];
        v->pos[2]=-sine*x+cosine*z+player->position[2];
        v->normal[0]=cosine*nx+sine*nz;
        v->normal[2]=-sine*nx+cosine*nz;
        *minimum_y=fminf(*minimum_y,v->pos[1]);
        *maximum_y=fmaxf(*maximum_y,v->pos[1]);
    }
}

static FILE *new_trace(unsigned *number)
{
    for(*number=0;*number<1000u;++*number) {
        char path[128];
        struct stat existing;
        snprintf(path,sizeof path,ASSET_ROOT "trace-%03u.csv",*number);
        /* libctru's FS error translation need not report EEXIST. Observe the
         * occupied name, then retain O_EXCL so a race cannot overwrite it. */
        if(stat(path,&existing)==0)continue;
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0) {
            FILE *file=fdopen(fd,"w");
            if(!file)close(fd);
            return file;
        }
        int saved_errno=errno;
        if(stat(path,&existing)==0)continue;
        errno=saved_errno;
        if(errno!=EEXIST)return NULL;
    }
    return NULL;
}

int main(void)
{
    C3FieldMap map={0}; C3FieldPlayer player={0}; C3N3dsInput input={0};
    C3Title *storage=NULL; const C3Title *models[MAX_MODELS]={0};
    C3TitleCamera camera={0}; C3TitleRenderStats stats={0};
    char error[256]={0},path[128];
    uint32_t opened=0,count=0,spawn_row=0,frame=0,ground_misses=0;
    unsigned trace_number=0; FILE *trace=NULL;
    int okay=0; float elapsed=0,max_ground_error=0;
    if(c3_n3ds_init(error,sizeof error))goto done;
    printf("ORIGINAL MAP / WES VISUAL PROBE\nNo gameplay or saved progress\n\nLoading private assets...\n");
    if(!c3_field_load(&map,ASSET_ROOT "outskirt.c3f",error,sizeof error))goto done;
    if(map.floor!=1u || map.map_pack_count+1u>MAX_MODELS || map.actor_bone!=UINT32_MAX ||
       map.actor_first_vertex!=0u || !c3_field_spawn(&map,0,&player)) {
        snprintf(error,sizeof error,"Unsupported real-map/actor/spawn layout");goto done;
    }
    count=map.map_pack_count+1u;
    storage=calloc(count,sizeof *storage);
    if(!storage){snprintf(error,sizeof error,"Probe model allocation failed");goto done;}
    for(uint32_t i=0;i<count;++i) {
        if(i<map.map_pack_count)snprintf(path,sizeof path,ASSET_ROOT "outskirt_map%lu.c3t",(unsigned long)i);
        else snprintf(path,sizeof path,ASSET_ROOT "outskirt_wes.c3t");
        if(!c3_title_load(storage+i,path,error,sizeof error))goto done;
        models[i]=storage+i; ++opened;
        if(!c3_title_animate(storage+i,0,0)){snprintf(error,sizeof error,"Initial sample failed");goto done;}
    }
    C3Title *wes=storage+map.map_pack_count;
    if(wes->vertex_count!=map.actor_vertex_count || wes->clip_count!=3u) {
        snprintf(error,sizeof error,"Wes vertex/clip layout disagrees with C3F");goto done;
    }
    if(c3_stage_renderer_init(models,count,error,sizeof error))goto done;
    trace=new_trace(&trace_number);
    if(!trace){snprintf(error,sizeof error,"Cannot create isolated probe trace (%d)",errno);goto done;}
    fprintf(trace,"# visual_probe_only=1 no_game_state_or_save_api=1\n");
    fprintf(trace,"# map_packs=%lu ignored_arrival_packs=%lu walk=%lu walls=%lu vertices=%lu\n",
            (unsigned long)map.map_pack_count,(unsigned long)map.arrival_pack_count,
            (unsigned long)map.walk_count,(unsigned long)map.wall_count,(unsigned long)map.actor_vertex_count);
    fprintf(trace,"frame,seconds,dt,spawn,stick_x,stick_y,held,x,y,z,yaw,motion,hits,ground_ok,ground,ground_error,mesh_min_y,mesh_max_y,eye_x,eye_y,eye_z,target_x,target_y,target_z,draw_calls,vertices,gpu_bytes\n");
    fflush(trace);
    uint64_t previous=c3_n3ds_active_time_us();
    okay=1;
    while(c3_n3ds_poll(&input)) {
        if(input.pressed&C3_PAD_START)break;
        uint64_t now=c3_n3ds_active_time_us();
        float delta=(float)(now-previous)/1000000.0f; previous=now;elapsed+=delta;
        if(input.pressed&C3_PAD_A)spawn_row=(spawn_row+1u)%map.spawn_count;
        if(input.pressed&C3_PAD_B)spawn_row=0;
        if(input.pressed&(C3_PAD_A|C3_PAD_B)) {
            if(!c3_field_spawn(&map,map.spawns[spawn_row].source_index,&player)) {
                snprintf(error,sizeof error,"Original spawn lookup failed");okay=0;break;
            }
        }
        if(!c3_field_move(&map,&player,input.stick_x,input.stick_y,input.held,map.camera_yaw,delta)) {
            snprintf(error,sizeof error,"Actual field movement failed");okay=0;break;
        }
        for(uint32_t i=0;i<map.map_pack_count;++i) {
            if(!c3_title_animate(storage+i,0,elapsed)) {
                snprintf(error,sizeof error,"Actual map sample failed");okay=0;break;
            }
        }
        if(!okay)break;
        uint32_t clip=player.motion==2?map.run_clip:player.motion==1?map.walk_clip:map.idle_clip;
        if(!c3_title_animate(wes,clip,player.animation_seconds)) {
            snprintf(error,sizeof error,"Actual Wes sample failed");okay=0;break;
        }
        float low,high;place_actor(wes,&player,&low,&high);
        c3_field_camera(&map,&player,&camera);
        float point[3]={player.position[0],player.position[1]+8.5f*map.scale,player.position[2]};
        float ground=NAN;
        int ground_ok=c3_field_ground(&map,point,&ground);
        float difference=ground_ok?player.position[1]-ground:NAN;
        if(!ground_ok)++ground_misses;
        else max_ground_error=fmaxf(max_ground_error,fabsf(difference));
        if(frame%15u==0u || input.pressed) {
            consoleClear();
            printf("ORIGINAL MAP / WES VISUAL PROBE\nNo gameplay or saved progress\n\n");
            printf("Circle Pad / D-pad: move\nA: next source spawn  B: spawn0\nSTART: exit\n\n");
            printf("Frame %lu Spawn %lu Trace %03u\n",(unsigned long)frame,
                   (unsigned long)map.spawns[spawn_row].source_index,trace_number);
            printf("XYZ %.3f %.3f %.3f\nYaw %.3f Motion %lu Hits %lu\n",
                   (double)player.position[0],(double)player.position[1],(double)player.position[2],
                   (double)player.yaw,(unsigned long)player.motion,(unsigned long)player.collision_count);
            printf("Ground %s error %.6f\nMax error %.6f Misses %lu\n",
                   ground_ok?"OK":"MISSING",(double)difference,(double)max_ground_error,(unsigned long)ground_misses);
            printf("Mesh Y %.3f..%.3f\nGPU calls %lu verts %lu\n",
                   (double)low,(double)high,(unsigned long)stats.draw_calls,(unsigned long)stats.vertices);
        }
        int result=c3_stage_renderer_draw(models,count,&camera,1,&stats,error,sizeof error);
        if(result==C3_RENDER_BUSY){svcSleepThread(1000000);continue;}
        if(result==C3_RENDER_ERROR){okay=0;break;}
        fprintf(trace,"%lu,%.6f,%.6f,%lu,%d,%d,%04x,%.6f,%.6f,%.6f,%.6f,%lu,%lu,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%lu,%lu,%lu\n",
                (unsigned long)frame,(double)elapsed,(double)delta,(unsigned long)map.spawns[spawn_row].source_index,
                input.stick_x,input.stick_y,input.held,(double)player.position[0],(double)player.position[1],
                (double)player.position[2],(double)player.yaw,(unsigned long)player.motion,
                (unsigned long)player.collision_count,ground_ok,(double)ground,(double)difference,
                (double)low,(double)high,(double)camera.eye[0],(double)camera.eye[1],(double)camera.eye[2],
                (double)camera.target[0],(double)camera.target[1],(double)camera.target[2],
                (unsigned long)stats.draw_calls,(unsigned long)stats.vertices,(unsigned long)stats.gpu_buffer_bytes);
        if(frame%15u==0u)fflush(trace);
        ++frame;
    }
done:
    if(trace){fprintf(trace,"# end okay=%d error=%s\n",okay,error);fclose(trace);}
    while(!c3_stage_renderer_try_shutdown()){aptMainLoop();svcSleepThread(1000000);}
    if(!okay) {
        consoleClear();printf("ORIGINAL MAP / WES VISUAL PROBE\nNo gameplay or saved progress\n\nFAIL\n%s\n",error);
        c3_n3ds_wait_for_exit();
    }
    for(uint32_t i=0;i<opened;++i)c3_title_close(storage+i);
    free(storage);c3_field_close(&map);c3_n3ds_shutdown();
    return okay?0:1;
}
