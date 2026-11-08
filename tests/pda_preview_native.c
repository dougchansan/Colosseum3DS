/* Host integration probe: real C3T loader/animation, GPU ownership stubbed. */
#include "pda_preview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int initialized,busy,frames,filters;
#ifdef C3_PDA_VALIDATE_BATCH
static unsigned invalid_batches;
#endif
#ifdef C3_PDA_VALIDATE_BATCH
#include "pda_valid_batch.inc"
#endif
int c3_stage_renderer_set_texture_routing(const uint8_t route[3],char *e,size_t n)
{(void)e;(void)n;assert(initialized&&route);++filters;return C3_RENDER_OK;}
int c3_stage_renderer_set_modulation(size_t index,const uint8_t brightness[3])
{(void)index;assert(initialized&&brightness);return 1;}
int c3_stage_renderer_init(const C3Title *const *models,size_t count,char *error,size_t capacity)
{(void)error;(void)capacity;assert(!initialized&&count&&models[0]);
size_t vertices=0,textures=0;assert(count<=C3_STAGE_MAX_MODELS);
for(size_t i=0;i<count;++i){vertices+=models[i]->vertex_count;textures+=models[i]->texture_count;}
assert(vertices<=C3_STAGE_MAX_VERTICES&&textures<=C3_STAGE_MAX_TEXTURES);
#ifdef C3_PDA_VALIDATE_BATCH
for(size_t i=0;i<count;++i)for(unsigned j=0;j<models[i]->batch_count;++j)if(!valid_batch(models[i],&models[i]->batches[j])){fprintf(stderr,"invalid part%lu batch%u flags%lx color%u alpha%u blend%f compare%u/%u refs%u/%u op%u\n",(unsigned long)i,j,(unsigned long)models[i]->batches[j].flags,models[i]->batches[j].color_mode,models[i]->batches[j].alpha_mode,models[i]->batches[j].texture2_blend,models[i]->batches[j].alpha_compare0,models[i]->batches[j].alpha_compare1,models[i]->batches[j].alpha_ref0,models[i]->batches[j].alpha_ref1,models[i]->batches[j].alpha_op);++invalid_batches;}
#endif
initialized=1;return C3_RENDER_OK;}
bool c3_stage_renderer_try_shutdown(void){if(busy)return false;initialized=0;return true;}
int c3_stage_renderer_draw(const C3Title *const *models,size_t count,const C3TitleCamera *camera,float fade,C3TitleRenderStats *stats,char *error,size_t capacity)
{
    (void)stats;(void)error;(void)capacity;assert(initialized&&fade==1.f);
    float distance=0;for(unsigned k=0;k<3;++k){float d=camera->eye[k]-camera->target[k];distance+=d*d;}
    distance=sqrtf(distance);
    for(size_t i=0;i<count;++i)for(unsigned j=0;j<models[i]->vertex_count;++j){
        const float *v=models[i]->draw_vertices[j].pos;float radius=0;
        for(unsigned k=0;k<3;++k){assert(isfinite(v[k]));float d=v[k]-camera->target[k];radius+=d*d;}
        /* Entire authored model must fit vertically in the orbit camera. */
        assert(sqrtf(radius)<distance*sinf(camera->fov_y_degrees*.5f*.01745329252f));
    }
    ++frames;return C3_RENDER_OK;
}
int main(int argc,char **argv)
{
    assert(argc>=2);char error[256];C3PdaPreview *preview=NULL;
    assert(!c3_pda_preview_open(&preview,argv[1],201,1,0,error,sizeof error)&&!preview);
    assert(c3_pda_preview_open(&preview,argv[1],196,0,0,error,sizeof error)&&filters==1);
    assert(c3_pda_preview_close(&preview));
    for(unsigned species=196;species<=197;++species){
        if(!c3_pda_preview_open(&preview,argv[1],species,0x12345678,0x87654321,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
        for(unsigned tick=0;tick<=1200;++tick)assert(c3_pda_preview_draw(preview,(float)tick/120.f,NULL,error,sizeof error)==C3_RENDER_OK);
        busy=1;assert(!c3_pda_preview_close(&preview)&&preview&&initialized);
        busy=0;assert(c3_pda_preview_close(&preview)&&!preview&&!initialized);
    }
    for(int arg=2;arg<argc;++arg){
        unsigned species=(unsigned)strtoul(argv[arg],NULL,10);
        if(!c3_pda_preview_open(&preview,argv[1],species,0x12345678,0x87654321,error,sizeof error)){fprintf(stderr,"species%u: %s\n",species,error);return 1;}
        for(unsigned tick=0;tick<=120;++tick){c3_pda_preview_control(preview,.5f,-.25f,1.f/120.f);assert(c3_pda_preview_draw(preview,(float)tick/120.f,NULL,error,sizeof error)==C3_RENDER_OK);}
        assert(c3_pda_preview_close(&preview));
        assert(c3_pda_preview_open(&preview,argv[1],species,0,0,error,sizeof error));
        assert(c3_pda_preview_draw(preview,0,NULL,error,sizeof error)==C3_RENDER_OK);
        assert(c3_pda_preview_close(&preview));
    }
    assert(frames==2402+(argc-2)*122);
#ifdef C3_PDA_VALIDATE_BATCH
    assert(!invalid_batches);
#endif
    printf("Original PDA models: %d sampled frames; source batch, budget, shiny, camera and ownership checks passed.\n",frames);
    return 0;
}
