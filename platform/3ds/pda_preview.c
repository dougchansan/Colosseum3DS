#include "pda_preview.h"
#include "pda_spinda.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A PDA model is one actor split into a few parts. Its descriptor bound stays
 * 16 parts; the stage model bound is sized for whole field scenes. */
#define C3_PDA_MAX_PARTS 16u
_Static_assert(C3_PDA_MAX_PARTS<=C3_STAGE_MAX_MODELS,"PDA parts must fit the stage");
struct C3PdaPreview {
    C3Title models[C3_PDA_MAX_PARTS];
    const C3Title *draw[C3_PDA_MAX_PARTS];
    unsigned count,initialized;
    float center[3],radius,yaw,pitch;
};
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const unsigned char *p){uint32_t u=word(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
bool c3_pda_preview_close(C3PdaPreview **out)
{
    if(!out||!*out)return true;
    C3PdaPreview *s=*out;
    if(s->initialized&&!c3_stage_renderer_try_shutdown())return false;
    for(unsigned i=s->count;i>0;--i)c3_title_close(&s->models[i-1]);
    free(s);*out=NULL;return true;
}
int c3_pda_preview_open(C3PdaPreview **out,const char *root,uint16_t species,uint32_t pid,uint32_t ot,char *e,size_t n)
{
    unsigned char bytes[64+48*C3_PDA_MAX_PARTS];char path[512];
    if(!out||*out||!root)return fail(e,n,"Invalid PDA preview ownership.");
    if(!species||species>411)return fail(e,n,"Invalid saved species preview.");
    if(species==201)return fail(e,n,"Unown has no canonical default PKX binding; saved-form routing is unsupported.");
    if(species==308)return fail(e,n,"Spinda requires its original five-stage material expression.");
    if(species==400)return fail(e,n,"Metagross requires original GX emboss bump mapping.");
    bool shiny=((pid>>16)^(pid&65535u)^(ot>>16)^(ot&65535u))<8u;
    if(snprintf(path,sizeof path,"%s/%u/preview.c3p",root,species)>=(int)sizeof path)return fail(e,n,"PDA preview path too long.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Original PDA model assets are missing.");
    size_t size=fread(bytes,1,sizeof bytes,f);int extra=fgetc(f),io=ferror(f);fclose(f);
    if(io||extra!=EOF||size<64||memcmp(bytes,"C3PD",4)||word(bytes+4)!=2||word(bytes+8)!=size||word(bytes+12)!=species)return fail(e,n,"Invalid PDA model descriptor.");
    unsigned count=word(bytes+16);
    if(!count||count>C3_PDA_MAX_PARTS||size!=64+48*count)return fail(e,n,"PDA model part count exceeds limits.");
    for(unsigned i=50;i<64;++i)if(bytes[i])return fail(e,n,"Invalid PDA descriptor reserved bytes.");
    for(unsigned i=44;i<47;++i)if(bytes[i]>3)return fail(e,n,"Invalid source shiny routing.");
    float bounds[6];for(unsigned i=0;i<6;++i){bounds[i]=real(bytes+20+i*4);if(!isfinite(bounds[i]))return fail(e,n,"Invalid PDA model bounds.");}
    C3PdaPreview *s=calloc(1,sizeof *s);if(!s)return fail(e,n,"PDA preview allocation failed.");
    *out=s;
    float diagonal=0;
    for(unsigned i=0;i<3;++i){float d=bounds[i+3]-bounds[i];if(d<0)goto invalid;s->center[i]=(bounds[i]+bounds[i+3])*.5f;diagonal+=d*d;}
    s->radius=sqrtf(diagonal)*.5f;if(!isfinite(s->radius)||s->radius<=0)goto invalid;
    for(unsigned i=0;i<count;++i){const char *name=(const char *)bytes+64+48*i;
        const char *end=memchr(name,0,48);if(!end||end==name||strstr(name,"..")||strspn(name,"abcdefghijklmnopqrstuvwxyz0123456789_.")!=strlen(name))goto invalid;
        for(const char *p=end+1;p<name+48;++p)if(*p)goto invalid;
        if(snprintf(path,sizeof path,"%s/%u/%s",root,species,name)>=(int)sizeof path)goto invalid;
        if(!c3_title_load(&s->models[i],path,e,n))goto bad;
        s->count=i+1;s->draw[i]=&s->models[i];
        /* The preview only orbits its camera; sampled vertices are never rewritten. */
        s->models[i].retained=1;
        if(s->models[i].clip_count!=1||!s->models[i].clips[0].loop)goto invalid;
    }
    if(species==308){
        if(snprintf(path,sizeof path,"%s/%u/spinda.c3s",root,species)>=(int)sizeof path)goto invalid;
        if(!c3_pda_spinda_apply(s->models,s->count,pid,path,e,n))goto bad;
    }
    if(shiny)for(unsigned i=0;i<s->count;++i)for(unsigned j=0;j<s->models[i].batch_count;++j){
        C3TitleBatch *batch=&s->models[i].batches[j];
        if(batch->flags&C3_TITLE_SOURCE_ALPHA_MASK){
            /* This constant is an original solid texture folded at export,
             * so it follows the texture swap table rather than material RGB. */
            float source[4]={batch->diffuse[0],batch->diffuse[1],batch->diffuse[2],1.f};
            for(unsigned k=0;k<3;++k)batch->diffuse[k]=source[bytes[44+k]];
        }
    }
    if(c3_stage_renderer_init(s->draw,s->count,e,n)!=C3_RENDER_OK)goto bad;
    s->initialized=1;
    if(shiny){
        if(c3_stage_renderer_set_texture_routing(bytes+44,e,n)!=C3_RENDER_OK)goto bad;
        for(unsigned i=0;i<s->count;++i)if(!c3_stage_renderer_set_modulation(i,bytes+47))goto bad;
    }
    return 1;
invalid:fail(e,n,"Invalid PDA model part or bounds.");
bad:c3_pda_preview_close(out);return 0;
}
int c3_pda_preview_draw(C3PdaPreview *s,float seconds,C3TitleRenderStats *stats,char *e,size_t n)
{
    if(!s||!s->initialized||!isfinite(seconds)||seconds<0){fail(e,n,"Invalid PDA preview clock.");return C3_RENDER_ERROR;}
    for(unsigned i=0;i<s->count;++i)if(!c3_title_animate(&s->models[i],0,seconds)){fail(e,n,"PDA authored idle sampling failed.");return C3_RENDER_ERROR;}
    C3TitleCamera camera={0};float angle=seconds*.3f+s->yaw,distance=s->radius*3.4f;
    memcpy(camera.target,s->center,sizeof camera.target);
    float pitch=.18f+s->pitch;
    camera.eye[0]=s->center[0]+sinf(angle)*cosf(pitch)*distance;
    camera.eye[1]=s->center[1]+sinf(pitch)*distance;
    camera.eye[2]=s->center[2]+cosf(angle)*cosf(pitch)*distance;
    camera.fov_y_degrees=40;camera.near_plane=fmaxf(.001f,s->radius*.01f);
    camera.far_plane=distance+s->radius*4;camera.aspect=400.f/240.f;
    return c3_stage_renderer_draw(s->draw,s->count,&camera,1.f,stats,e,n);
}
void c3_pda_preview_control(C3PdaPreview *s,float x,float y,float dt)
{
    if(!s||!isfinite(x)||!isfinite(y)||!isfinite(dt)||dt<0)return;
    if(dt>.1f)dt=.1f;
    x=fmaxf(-1.f,fminf(1.f,x));y=fmaxf(-1.f,fminf(1.f,y));
    s->yaw=fmodf(s->yaw+x*dt*2.f,6.283185307f);
    s->pitch=fmaxf(-1.1f,fminf(1.1f,s->pitch+y*dt*1.5f));
}
