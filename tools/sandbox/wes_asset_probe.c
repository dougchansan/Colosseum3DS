/* Verify the genuine source-motion-zero extension and the unchanged three
 * locomotion clips. Build with src/title.c and -lm; inputs remain private. */
#include "colosseum3ds/title.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv)
{
    if(argc!=3){fprintf(stderr,"usage: wes_asset_probe OLD_WES NEW_WES\n");return 2;}
    C3Title *old=calloc(1,sizeof *old),*next=calloc(1,sizeof *next);
    char error[256];unsigned checked=0;
    if(!old||!next)return 1;
    if(!c3_title_load(old,argv[1],error,sizeof error)||
       !c3_title_load(next,argv[2],error,sizeof error)){
        fprintf(stderr,"%s\n",error);return 1;
    }
    if(old->clip_count!=3||next->clip_count!=4||next->vertex_count!=2445||
       old->vertex_count!=next->vertex_count||old->batch_count!=next->batch_count||
       old->texture_count!=next->texture_count||next->clips[3].frame_count!=21)return 1;
    size_t draw_bytes=next->vertex_count*sizeof *next->draw_vertices;
    for(unsigned clip=0;clip<next->clip_count;++clip){
        if(clip<3&&memcmp(old->clips+clip,next->clips+clip,sizeof old->clips[0]))return 1;
        for(unsigned half=0;half<next->clips[clip].frame_count*2u;++half){
            float seconds=half/(next->fps*2);
            if(!c3_title_animate(next,clip,seconds))return 1;
            for(unsigned v=0;v<next->vertex_count;++v)for(unsigned axis=0;axis<3;++axis)
                if(!isfinite(next->draw_vertices[v].pos[axis])||
                   !isfinite(next->draw_vertices[v].normal[axis]))return 1;
            if(clip<3){
                if(!c3_title_animate(old,clip,seconds)||
                   memcmp(old->draw_vertices,next->draw_vertices,draw_bytes)||
                   memcmp(old->pose.materials,next->pose.materials,
                          old->batch_count*sizeof old->pose.materials[0])){
                    fprintf(stderr,"locomotion differs: clip%u half%u\n",clip,half);return 1;
                }
            }
            ++checked;
        }
    }
    if(!c3_title_animate(old,0,0)||!c3_title_animate(next,3,0)||
       !memcmp(old->draw_vertices,next->draw_vertices,draw_bytes))return 1;
    printf("{\"file\":\"%s\",\"bytes\":%zu,\"resident_bytes\":%zu,\"clips\":%u,"
           "\"vertices\":%u,\"samples_checked\":%u,\"locomotion_bitwise_equal\":true,"
           "\"source_zero_differs_from_idle\":true}\n",argv[2],next->file_bytes,
           next->resident_bytes,next->clip_count,next->vertex_count,checked);
    c3_title_close(old);c3_title_close(next);free(old);free(next);return 0;
}
