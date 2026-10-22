/* Host structural/animation probe for privately prepared first-area assets.
 * Build with src/title.c, src/field.c, src/area_lights.c, src/area_camera.c
 * and -lm. It exercises
 * every authored sample and its half-step; it does not claim GPU execution. */
#include "colosseum3ds/title.h"
#include "colosseum3ds/field.h"
#include "colosseum3ds/area_lights.h"
#include "colosseum3ds/area_camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    if(argc<2){fprintf(stderr,"usage: area_asset_probe PRIVATE_AREA_DIRECTORY [MODEL_BASENAME ...]\n");return 2;}
    const char *names[]={"shop_map0.c3t","shop_map1.c3t","shop_wes.c3t",
        "shop_npc0.c3t","shop_npc1.c3t","shop_npc2.c3t","shop_npc3.c3t","shop_npc4.c3t",
        "outskirt_npc2.c3t","outskirt_npc3.c3t","shop_tv.c3t"};
    C3Title *model=calloc(1,sizeof(*model));
    C3AreaLights lights={0};C3FieldMap field={0};
    C3AreaCamera camera={0};
    char path[1024],error[256];
    if(!model)return 1;
    if(argc==2){
        snprintf(path,sizeof path,"%s/shop.c3l",argv[1]);
        if(!c3_area_lights_load(&lights,path,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    }
    unsigned count=argc>2?(unsigned)argc-2u:(unsigned)(sizeof names/sizeof names[0]);
    printf("{\"packs\":[");
    for(unsigned m=0;m<count;++m) {
        const char *name=argc>2?argv[2+m]:names[m];
        snprintf(path,sizeof path,"%s/%s",argv[1],name);
        if(!c3_title_load(model,path,error,sizeof error)){fprintf(stderr,"%s: %s\n",name,error);return 1;}
        unsigned samples=0;
        for(uint32_t clip=0;clip<model->clip_count;++clip) {
            for(uint32_t half=0;half<2u*model->clips[clip].frame_count;++half) {
                float time=(float)half/(2.0f*model->fps);
                if(!c3_title_animate(model,clip,time)) {
                    fprintf(stderr,"%s clip%u half%u: singular sample\n",name,clip,half);return 1;
                }
                if(argc==2&&m<8&&!c3_area_lights_apply(&lights,time,model))return 1;
                for(uint32_t v=0;v<model->vertex_count;++v)
                    for(unsigned axis=0;axis<3;++axis)
                        if(!isfinite(model->draw_vertices[v].pos[axis])||!isfinite(model->draw_vertices[v].normal[axis]))return 1;
                ++samples;
            }
        }
        printf("%s{\"file\":\"%s\",\"bytes\":%zu,\"resident_bytes\":%zu,\"clips\":%u,\"vertices\":%u,\"samples_checked\":%u}",
            m?",":"",name,model->file_bytes,model->resident_bytes,model->clip_count,model->vertex_count,samples);
        c3_title_close(model);
    }
    if(argc>2){printf("]}\n");free(model);return 0;}
    snprintf(path,sizeof path,"%s/shop.c3f",argv[1]);
    if(!c3_field_load(&field,path,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    printf("],\"entrances\":[");
    for(uint32_t i=0;i<field.spawn_count;++i) {
        C3FieldPlayer player;float ground;
        if(!c3_field_spawn(&field,field.spawns[i].source_index,&player)||
           !c3_field_ground(&field,player.position,&ground))return 1;
        printf("%s{\"index\":%u,\"ground\":%.9g,\"spawn_y\":%.9g}",i?",":"",
            field.spawns[i].source_index,(double)ground,(double)player.position[1]);
    }
    snprintf(path,sizeof path,"%s/shop_camera.c3c",argv[1]);
    if(!c3_area_camera_load(&camera,path,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
    for(uint32_t i=0;i<camera.frame_count*2u;++i) {
        C3TitleCamera sampled;
        if(!c3_area_camera_sample(&camera,i/(camera.fps*2),&sampled))return 1;
    }
    printf("],\"light_frames\":%u,\"light_count\":%u,\"camera_frames\":%u}\n",
           lights.frame_count,lights.light_count,camera.frame_count);
    c3_area_lights_close(&lights);c3_area_camera_close(&camera);c3_field_close(&field);free(model);
    return 0;
}
