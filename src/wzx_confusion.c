#include "colosseum3ds/wzx_confusion.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PACK_BYTES (256u+C3_CONFUSION_MASK_BYTES)
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float f32(const uint8_t *p){uint32_t u=u32(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int valid(const C3ConfusionType12Profile *p)
{
    if(!p||p->phase>1)return 0;
    if(!p->phase)return p->start_tick==44&&p->duration==90&&p->owner_start==0&&p->owner_end==145&&p->part==62&&p->radius_per_tick==.15f&&p->pivot==0&&p->indirect_base==.25f;
    return p->start_tick==0&&p->duration==100&&p->owner_start==44&&p->owner_end==402&&p->part==53&&p->radius_per_tick==.3f&&p->pivot==1&&p->indirect_base==.25f;
}
int c3_wzx_confusion_decode(C3WzxConfusionProgram *out,const void *data,size_t bytes,char *e,size_t n)
{
    const uint8_t *b=data;uint8_t hash[20];
    if(!out||!b||bytes!=PACK_BYTES||memcmp(b,"C3CF",4)||u32(b+4)!=1||u32(b+8)!=bytes||u32(b+12)!=93||u32(b+16)!=12||u32(b+20)!=128||u32(b+24)!=128||u32(b+28)!=60)return fail(e,n,"Unsupported Confusion type12 profile.");
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    if(memcmp(b+32,pin,20))return fail(e,n,"Confusion source pin differs.");
    for(unsigned k=52;k<64;++k)if(b[k])return fail(e,n,"Unknown Confusion header extension.");
    for(unsigned k=192;k<224;++k)if(b[k])return fail(e,n,"Unknown Confusion effect extension.");
    for(unsigned k=244;k<256;++k)if(b[k])return fail(e,n,"Unknown Confusion checksum extension.");
    uint8_t *checked=malloc(bytes);C3WzxConfusionProgram *p=calloc(1,sizeof *p);
    if(!checked||!p){free(checked);free(p);return fail(e,n,"Confusion CPU allocation failed.");}
    memcpy(checked,b,bytes);memset(checked+224,0,20);c3_save_codec_sha1(hash,checked,bytes);free(checked);
    if(memcmp(hash,b+224,20)){free(p);return fail(e,n,"Confusion type12 checksum mismatch.");}
    for(unsigned phase=0;phase<2;++phase){const uint8_t *r=b+64+phase*64;C3ConfusionType12Profile *q=&p->phase[phase];
        q->phase=u32(r);q->start_tick=u32(r+4);q->duration=u32(r+8);q->owner_start=u32(r+12);q->owner_end=u32(r+16);q->part=u32(r+20);
        q->radius_per_tick=f32(r+24);q->pivot=f32(r+28);q->indirect_base=f32(r+32);
        for(unsigned k=36;k<64;++k)if(r[k]){free(p);return fail(e,n,"Unknown Confusion type12 ABI extension.");}
        if(q->phase!=phase||!valid(q)){free(p);return fail(e,n,"Unqualified Confusion duration/attachment/curve.");}
    }
    memcpy(p->intensity,b+256,C3_CONFUSION_MASK_BYTES);*out=*p;free(p);if(e&&n)e[0]=0;
    return 1;
}
int c3_wzx_confusion_load(C3WzxConfusionProgram *out,const char *path,char *e,size_t n)
{
    if(!out||!path)return fail(e,n,"Missing Confusion descriptor path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Cannot open Confusion type12 descriptor.");
    uint8_t *b=malloc(PACK_BYTES);if(!b){fclose(f);return fail(e,n,"Confusion file allocation failed.");}
    size_t bytes=fread(b,1,PACK_BYTES,f);int extra=fgetc(f),io=ferror(f);if(fclose(f))io=1;
    if(io||bytes!=PACK_BYTES||extra!=EOF){free(b);return fail(e,n,"Invalid Confusion type12 descriptor length.");}
    int ok=c3_wzx_confusion_decode(out,b,bytes,e,n);free(b);return ok;
}
int c3_confusion_type12_begin(C3ConfusionType12 *s,const C3ConfusionType12Profile *p,unsigned owner,unsigned target,uint32_t part,const float center[3],char *e,size_t n)
{
    if(!s||!valid(p)||!center||owner>=4||target>=4||owner==target||part!=p->part||(!p->phase&&owner!=1)||(p->phase&&owner<2))return fail(e,n,"Unqualified Confusion owner/body-map attachment.");
    for(unsigned k=0;k<3;++k)if(!isfinite(center[k])||fabsf(center[k])>1000)return fail(e,n,"Invalid Confusion one-shot position.");
    C3ConfusionType12 next={0};next.profile=*p;next.owner=owner;next.target=target;next.active=1;memcpy(next.center,center,sizeof next.center);*s=next;if(e&&n)e[0]=0;
    return 1;
}
int c3_confusion_type12_step(C3ConfusionType12 *s,uint32_t ms,char *e,size_t n)
{
    if(!s||!valid(&s->profile))return fail(e,n,"Invalid Confusion type12 state.");
    if(!s->active||s->paused)return 1;
    if(s->units>(uint64_t)s->profile.duration*1000||s->frame>s->profile.duration)return fail(e,n,"Invalid Confusion source clock.");
    uint64_t next=s->units+(uint64_t)ms*60u;if(next>(uint64_t)600*1000)return fail(e,n,"Confusion catch-up exceeds600source ticks.");
    if(next>(uint64_t)s->profile.duration*1000)next=(uint64_t)s->profile.duration*1000;
    C3ConfusionType12 result=*s;uint32_t until=(uint32_t)(next/1000u);
    while(result.frame<until&&result.active){
        /* fn8013F80C evaluates old frame, then increments, then returns frame<duration. */
        float progress=(float)result.frame/result.profile.duration,pivot=result.profile.pivot;
        float envelope=pivot!=0&&progress<=pivot?progress/pivot:(1-progress)/(1-pivot);
        result.radius=result.profile.radius_per_tick*.75f*(float)result.profile.duration*envelope*.1f;
        result.indirect=result.profile.indirect_base*(1-progress);
        ++result.frame;result.active=(uint8_t)(result.frame<result.profile.duration);
    }
    result.units=next;*s=result;if(e&&n)e[0]=0;
    return 1;
}
void c3_confusion_type12_pause(C3ConfusionType12 *s,int paused){if(s)s->paused=(uint8_t)(paused!=0);}
void c3_confusion_type12_cancel(C3ConfusionType12 *s){if(s){s->active=0;s->radius=0;s->indirect=0;}}
int c3_confusion_type12_frame(const C3ConfusionType12 *s,const float right[3],const float up[3],const float projected[2][2],unsigned width,unsigned height,C3ConfusionType12Frame *out,char *e,size_t n)
{
    if(!s||!valid(&s->profile)||!right||!up||!projected||!out||width<8||width>1024||height<8||height>1024)return fail(e,n,"Invalid Confusion captured-scene geometry input.");
    if(!isfinite(s->radius)||s->radius<0||!isfinite(s->indirect)||s->indirect<0||s->indirect>.25f)return fail(e,n,"Invalid Confusion source curve state.");
    if(!s->active||s->radius<=0){if(e&&n)e[0]=0;return 0;}
    float rr=0,uu=0,ru=0;for(unsigned k=0;k<3;++k){if(!isfinite(right[k])||!isfinite(up[k]))return fail(e,n,"Nonfinite Confusion inverse-view basis.");rr+=right[k]*right[k];uu+=up[k]*up[k];ru+=right[k]*up[k];}
    if(fabsf(rr-1)>.001f||fabsf(uu-1)>.001f||fabsf(ru)>.001f)return fail(e,n,"Confusion inverse-view basis is not orthonormal.");
    for(unsigned i=0;i<2;++i)for(unsigned k=0;k<2;++k)if(!isfinite(projected[i][k])||fabsf(projected[i][k])>10000)return fail(e,n,"Confusion projected corners are unavailable.");
    C3ConfusionType12Frame frame={0};static const int signs[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)frame.world[i][k]=s->center[k]+s->radius*(signs[i][0]*right[k]+signs[i][1]*up[k]);
    frame.captured_uv[0][0]=projected[1][0]-projected[0][0];frame.captured_uv[1][1]=projected[1][1]-projected[0][1];
    frame.captured_uv[0][2]=projected[0][0]+128.f/(float)width*s->indirect;frame.captured_uv[1][2]=projected[0][1]+128.f/(float)height*s->indirect;
    frame.indirect_matrix[0][0]=frame.indirect_matrix[1][1]=.5f;frame.indirect_matrix[0][2]=frame.indirect_matrix[1][2]=.5f*(s->indirect-1);
    frame.capture_model_flags=0x3010;frame.indirect_exponent=1;frame.capture_include_particles=0;
    frame.required_capabilities=C3_CONFUSION_CAPTURE|C3_CONFUSION_INDIRECT;*out=frame;if(e&&n)e[0]=0;
    return 1;
}
int c3_confusion_type12_require(uint32_t available,char *e,size_t n)
{
    if(!(available&C3_CONFUSION_CAPTURE))return fail(e,n,"Confusion type12 requires source visible-model scene capture.");
    if(!(available&C3_CONFUSION_INDIRECT))return fail(e,n,"Confusion type12 requires GX dependent-texture sampling; ordinary billboard is incorrect.");
    if(e&&n)e[0]=0;
    return 1;
}
