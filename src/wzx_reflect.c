#include "colosseum3ds/wzx_reflect.h"
#include "save_codec.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const uint8_t *p){uint32_t n=word(p);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t cap,const char *s)
{if(e&&cap)snprintf(e,cap,"%s",s);return 0;}
static int nonzero(const uint8_t *p,size_t n)
{uint8_t any=0;for(size_t i=0;i<n;++i)any|=p[i];return any!=0;}
static int valid(const C3WzxReflectProgram *p)
{
    return p&&p->owner_clip==1&&!p->model_clip&&p->owner_minimum==180&&p->ally_minimum==118&&
        p->texture_end==95&&p->floor_duration==190&&p->darken_ticks==20&&p->hold_ticks==110&&p->restore_ticks==60&&
        p->floor_from==0x80808080u&&p->floor_dark==0x802D2D2Du&&p->sound_id==0x220&&!p->root_generator&&
        p->particle_flags==0x18A9&&p->attachment==2&&p->position_type==1&&p->body_map_index==2&&
        p->espeon_part==62&&p->umbreon_part==42&&p->owner_rate==.5f&&p->model_rate==.5f&&
        nonzero(p->model_sha1,20)&&nonzero(p->particles_sha1,20)&&nonzero(p->parts_sha1,20);
}
int c3_wzx_reflect_decode(C3WzxReflectProgram *out,const void *data,size_t bytes,char *e,size_t cap)
{
    static const uint8_t source[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t wzx[32]={0x28,0xb2,0x05,0xcb,0xa8,0x6a,0x2c,0xe4,0xb5,0xe8,0x1f,0x1a,0x92,0x24,0xf0,0x59,0x3c,0xe0,0x33,0x22,0x07,0x73,0x28,0x4f,0x3e,0xfa,0xbc,0xf8,0x85,0xa5,0x70,0xac};
    const uint8_t *b=data;uint8_t digest[20];C3WzxReflectProgram p={0};
    if(!out||!b||bytes!=C3_WZX_REFLECT_BYTES||memcmp(b,"C3WR",4)||word(b+4)!=2||word(b+8)!=C3_WZX_REFLECT_BYTES||word(b+12)!=115||
       word(b+16)!=196||word(b+20)||word(b+32)!=3||word(b+108)!=UINT32_MAX||word(b+112)||
       word(b+116)!=1||word(b+120)||memcmp(b+128,source,20)||memcmp(b+148,wzx,32))
        return fail(e,cap,"Unsupported Reflect WZX source/profile.");
    for(unsigned i=240;i<268;++i)if(b[i])return fail(e,cap,"Unknown Reflect descriptor extension.");
    c3_save_codec_sha1(digest,b,268);
    if(memcmp(digest,b+268,20))return fail(e,cap,"Reflect descriptor checksum mismatch.");
    p.owner_clip=word(b+24);p.model_clip=word(b+28);p.owner_minimum=word(b+36);
    p.floor_duration=word(b+40);p.texture_end=word(b+44);p.owner_rate=real(b+48);p.model_rate=real(b+52);
    p.darken_ticks=word(b+56);p.hold_ticks=word(b+60);p.restore_ticks=word(b+64);
    p.floor_from=word(b+68);p.floor_dark=word(b+72);p.sound_id=word(b+76);p.root_generator=word(b+80);
    p.particle_flags=word(b+84);p.attachment=word(b+88);p.position_type=word(b+92);
    p.body_map_index=word(b+96);p.espeon_part=word(b+100);p.umbreon_part=word(b+104);p.ally_minimum=word(b+124);
    memcpy(p.model_sha1,b+180,20);memcpy(p.particles_sha1,b+200,20);memcpy(p.parts_sha1,b+220,20);
    if(!valid(&p))return fail(e,cap,"Reflect timing/attachment profile is unsupported.");
    c3_wzx_reflect_program_close(out);*out=p;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_reflect_load(C3WzxReflectProgram *out,const char *path,char *e,size_t cap)
{
    uint8_t bytes[C3_WZX_REFLECT_BYTES];if(!out||!path)return fail(e,cap,"Missing Reflect descriptor path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Reflect descriptor.");
    size_t n=fread(bytes,1,sizeof bytes,f);int extra=fgetc(f),error=ferror(f);if(fclose(f))error=1;
    if(error||n!=sizeof bytes||extra!=EOF)return fail(e,cap,"Invalid Reflect descriptor length.");
    return c3_wzx_reflect_decode(out,bytes,sizeof bytes,e,cap);
}
void c3_wzx_reflect_program_close(C3WzxReflectProgram *p)
{if(p){free(p->parts_data);memset(p,0,sizeof *p);}}
int c3_wzx_reflect_parts_decode(C3WzxReflectProgram *p,const void *data,size_t bytes,char *e,size_t cap)
{
    static const uint8_t source[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t espeon[20]={0x87,0x55,0x03,0x1c,0x91,0x2d,0x7d,0x96,0x5e,0xc3,0x52,0xb2,0x0c,0xd1,0x1d,0x93,0x85,0xef,0x02,0x50};
    static const uint8_t umbreon[20]={0x6e,0xf8,0x6a,0x8f,0xad,0xf7,0xc8,0xb9,0x7a,0xeb,0x86,0xd6,0x02,0xb8,0x8c,0x6a,0xd1,0xad,0xc8,0x38};
    const uint8_t *b=data;uint8_t digest[20];
    if(!valid(p)||!b||bytes!=C3_WZX_REFLECT_PARTS_BYTES||memcmp(b,"C3RP",4))
        return fail(e,cap,"Invalid Reflect live-part pack/profile.");
    static const uint32_t fields[15]={1,3728,2,120,181,119,12,62,42,128,2300,196,197,1,0};
    for(unsigned i=0;i<15;++i)if(word(b+4+4*i)!=fields[i])
        return fail(e,cap,"Reflect live-part track bounds/identity mismatch.");
    if(memcmp(b+64,source,20)||memcmp(b+84,espeon,20)||memcmp(b+104,umbreon,20)||word(b+124))
        return fail(e,cap,"Reflect live-part original source pins mismatch.");
    c3_save_codec_sha1(digest,b,bytes);
    if(memcmp(digest,p->parts_sha1,20))return fail(e,cap,"Reflect live-part binding checksum mismatch.");
    for(size_t i=128;i<bytes;i+=4){float v=real(b+i);if(!isfinite(v)||fabsf(v)>1000)
        return fail(e,cap,"Reflect live-part coordinate is outside bounds.");}
    uint8_t *copy=malloc(bytes);if(!copy)return fail(e,cap,"Reflect live-part allocation failed.");
    memcpy(copy,b,bytes);free(p->parts_data);p->parts_data=copy;p->parts_bytes=bytes;
    if(e&&cap)e[0]=0;
    return 1;
}
int c3_wzx_reflect_parts_load(C3WzxReflectProgram *p,const char *path,char *e,size_t cap)
{
    uint8_t bytes[C3_WZX_REFLECT_PARTS_BYTES];
    if(!valid(p)||!path)return fail(e,cap,"Missing Reflect live-part program/path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Reflect live-part pack.");
    size_t n=fread(bytes,1,sizeof bytes,f);int extra=fgetc(f),error=ferror(f);if(fclose(f))error=1;
    if(error||n!=sizeof bytes||extra!=EOF)return fail(e,cap,"Invalid Reflect live-part pack length.");
    return c3_wzx_reflect_parts_decode(p,bytes,sizeof bytes,e,cap);
}
int c3_wzx_reflect_position(const C3WzxReflectProgram *p,uint32_t species,float seconds,
    const float root[3],float yaw,float scale,float out[3],char *e,size_t cap)
{
    if(!valid(p)||!p->parts_data||p->parts_bytes!=C3_WZX_REFLECT_PARTS_BYTES||!root||!out||
       (species!=196&&species!=197)||!isfinite(seconds)||!isfinite(yaw)||!isfinite(scale)||scale<=0||scale>1000)
        return fail(e,cap,"Reflect live-part position requires loaded tracks and finite transform.");
    for(unsigned i=0;i<3;++i)if(!isfinite(root[i]))return fail(e,cap,"Nonfinite Reflect actor root.");
    uint32_t count=species==196?181u:119u,offset=species==196?128u:2300u;
    double frame=seconds<0?0:fmod((double)seconds*120.,(double)(count-1u));
    uint32_t first=(uint32_t)frame,next=first+1u;float fraction=(float)(frame-first),local[3],position[3];
    for(unsigned i=0;i<3;++i){float a=real(p->parts_data+offset+12u*first+4u*i),b=real(p->parts_data+offset+12u*next+4u*i);
        local[i]=(a+(b-a)*fraction)*scale;}
    float co=cosf(yaw),si=sinf(yaw);
    position[0]=co*local[0]+si*local[2]+root[0];position[1]=local[1]+root[1];
    position[2]=-si*local[0]+co*local[2]+root[2];
    for(unsigned i=0;i<3;++i)if(!isfinite(position[i]))return fail(e,cap,"Reflect live-part transform overflow.");
    memcpy(out,position,sizeof position);if(e&&cap)e[0]=0;return 1;
}
void c3_wzx_reflect_cancel(C3WzxReflect *s)
{if(s){s->active=0;s->model_visible=0;s->floor_enabled=0;}}
int c3_wzx_reflect_begin(C3WzxReflect *s,const C3WzxReflectProgram *p,uint32_t species)
{
    if(!s||!valid(p)||(species!=196&&species!=197))return 0;
    C3WzxReflect next={0};next.program=*p;next.active=1;next.species=species;
    next.owner_clip=species==196?p->owner_clip:0;
    next.owner_minimum=species==196?p->owner_minimum:p->ally_minimum;
    next.source_part=species==196?p->espeon_part:p->umbreon_part;
    if(!c3_wzx_reflect_step(&next,0))return 0;
    *s=next;return 1;
}
int c3_wzx_reflect_step(C3WzxReflect *s,uint32_t ms)
{
    if(!s||!valid(&s->program))return 0;
    if(!s->active)return 1;
    uint64_t bound=(uint64_t)C3_WZX_REFLECT_MAX_TICKS*1000u,advance=(uint64_t)ms*60u;
    if(s->units>bound||advance>bound-s->units)return 0;
    s->units+=advance;s->model_starts=s->particle_starts=s->floor_starts=s->sound_requests=1;
    const C3WzxReflectProgram *p=&s->program;
    s->owner_seconds=(float)((double)s->units/60000.0)*p->owner_rate;
    s->owner_minimum_complete=(uint8_t)(s->units>=(uint64_t)s->owner_minimum*1000u);
    uint64_t model_units=s->units,model_duration=(uint64_t)p->texture_end*2000u;
    s->model_finished=(uint8_t)(model_units>=model_duration);
    if(model_units>model_duration)model_units=model_duration;
    s->model_seconds=(float)((double)model_units/60000.0)*p->model_rate;
    s->model_visible=(uint8_t)!s->model_finished;
    s->floor_finished=(uint8_t)(s->units>=(uint64_t)p->floor_duration*1000u);
    s->floor_enabled=(uint8_t)!s->floor_finished;
    uint64_t tick=s->units/1000u;float amount;
    if(tick<p->darken_ticks)amount=(float)tick/p->darken_ticks;
    else if(tick<p->darken_ticks+p->hold_ticks)amount=1;
    else {amount=1-(float)(tick-p->darken_ticks-p->hold_ticks)/p->restore_ticks;if(amount<0)amount=0;}
    for(unsigned i=0;i<3;++i){int a=(uint8_t)(p->floor_from>>(8*i)),b=(uint8_t)(p->floor_dark>>(8*i));
        s->floor_rgb[i]=(uint8_t)(a+amount*(b-a));}
    return 1;
}
int c3_wzx_reflect_finish(C3WzxReflect *s,int model_done,int particles_done,int floor_done,int audio_done)
{
    if(!s||!valid(&s->program))return 0;
    if(s->completed)return 1;
    if(!s->active||!s->owner_minimum_complete||!s->model_finished||!s->floor_finished||
       !model_done||!particles_done||!floor_done||!audio_done)return 0;
    c3_wzx_reflect_cancel(s);s->completed=1;return 1;
}
unsigned c3_wzx_reflect_actor(unsigned caster,unsigned alive_mask,unsigned index)
{
    if(caster>=2||!(alive_mask&(1u<<caster))||index>=2)return C3_WZX_REFLECT_NO_ACTOR;
    if(!index)return caster;
    unsigned ally=caster^1u;return alive_mask&(1u<<ally)?ally:C3_WZX_REFLECT_NO_ACTOR;
}
