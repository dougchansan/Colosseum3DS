#include "colosseum3ds/wzx_bite.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float f32(const uint8_t *p){uint32_t u=u32(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int valid(const C3WzxBiteProgram *p)
{
    if(!p||p->owner_clip!=5||p->model_clip||p->floor_start>3600||!p->floor_duration||
       p->floor_duration>3600||p->model_start>=p->control_start||
       p->control_start>3600||!p->control_delay||p->control_delay>60||
       p->floor_start+p->floor_duration>p->control_start||!p->model_frames||p->model_frames>600||
       p->owner_rate!=.5f||p->model_rate!=.5f||p->part!=59||
       p->unsupported!=C3_WZX_BITE_SOUND_UNSUPPORTED||p->sound_id!=0x85||
       p->sound_start!=p->model_start||p->flags!=0x00201048u)return 0;
    for(unsigned i=0;i<3;++i)if(!isfinite(p->anchor[i])||fabsf(p->anchor[i])>1000)return 0;
    return 1;
}
int c3_wzx_bite_decode(C3WzxBiteProgram *out,const void *data,size_t bytes,char *e,size_t n)
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t wzx[32]={0x0c,0xb9,0x91,0x2d,0xcb,0xe7,0xc4,0x62,0x6d,0x63,0x78,0x9a,0x34,0xcf,0xe1,0xa3,0x6e,0x86,0x9a,0x08,0x78,0x45,0x4b,0x8c,0xde,0x87,0x84,0x34,0x81,0x42,0x16,0xf7};
    const uint8_t *b=data;uint8_t digest[20];C3WzxBiteProgram p={0};
    if(!out||!b||bytes!=C3_WZX_BITE_BYTES||memcmp(b,"C3WX",4)||u32(b+4)!=1||
       u32(b+8)!=bytes||u32(b+12)!=44||u32(b+16)!=197||u32(b+28)!=60||
       memcmp(b+104,pin,20)||memcmp(b+124,wzx,32))return fail(e,n,"Unsupported Bite WZX source/profile.");
    for(unsigned i=176;i<188;++i)if(b[i])return fail(e,n,"Unknown Bite model-hash extension.");
    for(unsigned i=208;i<bytes;++i)if(b[i])return fail(e,n,"Unknown Bite WZX extension.");
    c3_save_codec_sha1(digest,b,188);
    if(memcmp(digest,b+188,20))return fail(e,n,"Bite WZX descriptor checksum mismatch.");
    p.owner_clip=u32(b+20);p.model_clip=u32(b+24);p.floor_start=u32(b+32);
    p.floor_duration=u32(b+36);p.model_start=u32(b+40);p.control_start=u32(b+44);
    p.control_delay=u32(b+48);p.model_frames=u32(b+52);p.floor_from=u32(b+56);p.floor_to=u32(b+60);
    p.owner_rate=f32(b+64);p.model_rate=f32(b+68);
    for(unsigned i=0;i<3;++i)p.anchor[i]=f32(b+72+4*i);
    p.part=u32(b+84);p.unsupported=u32(b+88);p.sound_id=u32(b+92);
    p.sound_start=u32(b+96);p.flags=u32(b+100);memcpy(p.model_sha1,b+156,20);
    if(!valid(&p))return fail(e,n,"Bite WZX timing or attachment is outside the supported slice.");
    *out=p;if(e&&n)e[0]=0;return 1;
}
int c3_wzx_bite_load(C3WzxBiteProgram *out,const char *path,char *e,size_t n)
{
    uint8_t bytes[C3_WZX_BITE_BYTES];
    if(!out||!path)return fail(e,n,"Missing Bite descriptor path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Cannot open Bite WZX descriptor.");
    size_t count=fread(bytes,1,sizeof bytes,f);int extra=fgetc(f),error=ferror(f);
    if(fclose(f))error=1;
    if(error||count!=sizeof bytes||extra!=EOF)return fail(e,n,"Invalid Bite WZX descriptor length.");
    return c3_wzx_bite_decode(out,bytes,sizeof bytes,e,n);
}
void c3_wzx_bite_cancel(C3WzxBite *s)
{if(s){s->active=0;s->model_visible=0;s->floor_enabled=0;}}
int c3_wzx_bite_begin(C3WzxBite *s,const C3WzxBiteProgram *p)
{
    if(!s||!valid(p))return 0;
    C3WzxBite next={0};next.program=*p;next.active=1;next.floor_enabled=(uint8_t)(p->floor_start==0);
    for(unsigned i=0;i<3;++i)next.floor_rgb[i]=(uint8_t)(p->floor_from>>(8*i));
    *s=next;return 1;
}
int c3_wzx_bite_step(C3WzxBite *s,uint32_t ms)
{
    if(!s||!valid(&s->program))return 0;
    if(!s->active)return 1;
    const C3WzxBiteProgram *p=&s->program;
    uint64_t end=(uint64_t)(p->control_start+p->control_delay)*1000;
    if(s->units>end)return 0;
    uint64_t next=s->units+(uint64_t)ms*60u;
    if(next>end)next=end;
    /* All crossed entries are observed even after a long frame. No time or
     * combat RNG is discarded; scalar model sampling may omit render frames. */
    if(next>=(uint64_t)p->model_start*1000u&&!s->model_starts)s->model_starts=1;
    if(next>=(uint64_t)p->sound_start*1000u&&!s->sound_requests)s->sound_requests=1;
    s->units=next;
    s->owner_seconds=(float)((double)next/60000.0)*p->owner_rate;
    s->model_seconds=next>=(uint64_t)p->model_start*1000u?
        (float)((double)(next-(uint64_t)p->model_start*1000u)/60000.0)*p->model_rate:0;
    s->model_visible=(uint8_t)(s->model_starts&&s->model_seconds*60.0f<p->model_frames);
    uint64_t tick=next/1000u;
    s->floor_enabled=(uint8_t)(tick>=p->floor_start);
    uint64_t age=tick>p->floor_start?tick-p->floor_start:0;
    if(age>p->floor_duration)age=p->floor_duration;
    for(unsigned i=0;i<3;++i){
        int a=(uint8_t)(p->floor_from>>(8*i)),b=(uint8_t)(p->floor_to>>(8*i));
        /* float interpolation followed by truncation is GSeffect's byte rule. */
        s->floor_rgb[i]=(uint8_t)(a+((float)age/(float)p->floor_duration)*(b-a));
    }
    if(next==end)c3_wzx_bite_cancel(s);
    return 1;
}
