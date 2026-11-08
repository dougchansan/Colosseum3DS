#include "colosseum3ds/wzx_damage.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t word(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const uint8_t *p){uint32_t n=word(p);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t cap,const char *s)
{if(e&&cap)snprintf(e,cap,"%s",s);return 0;}
static int nonzero(const uint8_t *p,size_t n)
{uint8_t any=0;for(size_t i=0;i<n;++i)any|=p[i];return any!=0;}
static int valid(const C3WzxDamageProgram *p)
{
    if(!p||p->owner_clip!=2||p->model_clip||p->model_start||p->particle_start||
       p->sound_start!=22||p->floor_start||p->floor_duration!=160||p->hp_start!=22||
       p->owner_end!=380||p->owner_start!=22||p->normalization_shift!=22||p->model_frames!=25||p->owner_rate!=.5f||p->model_rate!=.5f||
       p->model_part||p->particle_part!=12||p->particle_bone!=11||
       p->floor_from!=0x803A3A3Au||p->floor_hold!=0x803A3A3Au||p->floor_to!=0x80808080u||
       p->floor_hold_ticks!=60||p->floor_ramp_ticks!=100||p->sound_id!=0x86||
       p->particle_flags!=0x10A1||p->root_generator!=3||
       !nonzero(p->model_sha1,20)||!nonzero(p->particles_sha1,20))return 0;
    for(unsigned i=0;i<3;++i)if(!isfinite(p->anchor[i])||p->anchor[i]!=0)return 0;
    return 1;
}
int c3_wzx_damage_decode(C3WzxDamageProgram *out,const void *data,size_t bytes,char *e,size_t cap)
{
    static const uint8_t source[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t wzx[32]={0x4e,0xd0,0xfd,0x17,0x39,0xbb,0x6f,0xcb,0x2c,0xaa,0xc0,0xc4,0x9e,0x4a,0x53,0xdb,0x1e,0x0d,0x39,0x68,0x0c,0xfd,0x14,0x95,0x65,0x3e,0x14,0x6a,0x8a,0xac,0xd9,0xed};
    const uint8_t *b=data;uint8_t digest[20];C3WzxDamageProgram p={0};
    if(!out||!b||bytes!=C3_WZX_DAMAGE_BYTES||memcmp(b,"C3WD",4)||word(b+4)!=2||
       word(b+8)!=bytes||word(b+12)!=44||word(b+16)!=288||word(b+28)!=60||
       memcmp(b+128,source,20)||memcmp(b+148,wzx,32))
        return fail(e,cap,"Unsupported Bite defender WZX source/profile.");
    for(unsigned i=228;i<236;++i)if(b[i])return fail(e,cap,"Unknown Bite defender extension.");
    c3_save_codec_sha1(digest,b,236);
    if(memcmp(digest,b+236,20))return fail(e,cap,"Bite defender descriptor checksum mismatch.");
    p.owner_clip=word(b+20);p.model_clip=word(b+24);p.model_start=word(b+32);
    p.particle_start=word(b+36);p.sound_start=word(b+40);p.floor_start=word(b+44);
    p.floor_duration=word(b+48);p.hp_start=word(b+52);p.owner_end=word(b+56);p.model_frames=word(b+60);
    p.owner_rate=real(b+64);p.model_rate=real(b+68);
    for(unsigned i=0;i<3;++i)p.anchor[i]=real(b+72+4*i);
    p.model_part=word(b+84);p.particle_part=word(b+88);p.particle_bone=word(b+92);
    p.floor_from=word(b+96);p.floor_hold=word(b+100);p.floor_to=word(b+104);
    p.floor_hold_ticks=word(b+108);p.floor_ramp_ticks=word(b+112);
    p.owner_start=word(b+220);p.normalization_shift=word(b+224);
    p.sound_id=word(b+116);p.particle_flags=word(b+120);p.root_generator=word(b+124);
    memcpy(p.model_sha1,b+180,20);memcpy(p.particles_sha1,b+200,20);
    if(!valid(&p))return fail(e,cap,"Bite defender timing/bone profile is unsupported.");
    *out=p;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_damage_load(C3WzxDamageProgram *out,const char *path,char *e,size_t cap)
{
    uint8_t bytes[C3_WZX_DAMAGE_BYTES];
    if(!out||!path)return fail(e,cap,"Missing Bite defender descriptor path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Bite defender descriptor.");
    size_t n=fread(bytes,1,sizeof bytes,f);int extra=fgetc(f),error=ferror(f);
    if(fclose(f))error=1;
    if(error||n!=sizeof bytes||extra!=EOF)return fail(e,cap,"Invalid Bite defender descriptor length.");
    return c3_wzx_damage_decode(out,bytes,sizeof bytes,e,cap);
}
void c3_wzx_damage_cancel(C3WzxDamage *s)
{if(s){s->active=0;s->model_visible=0;s->floor_enabled=0;}}
int c3_wzx_damage_begin(C3WzxDamage *s,const C3WzxDamageProgram *p)
{
    if(!s||!valid(p))return 0;
    C3WzxDamage next={0};next.program=*p;next.active=1;
    for(unsigned i=0;i<3;++i)next.floor_rgb[i]=128;
    if(!c3_wzx_damage_step(&next,0))return 0;
    *s=next;return 1;
}
int c3_wzx_damage_step(C3WzxDamage *s,uint32_t ms)
{
    if(!s||!valid(&s->program))return 0;
    if(!s->active)return 1;
    const uint64_t bound=(uint64_t)C3_WZX_DAMAGE_MAX_TICKS*1000u;
    uint64_t advance=(uint64_t)ms*60u;
    if(s->units>bound||advance>bound-s->units)return 0;
    const C3WzxDamageProgram *p=&s->program;
    uint64_t next=s->units+advance;
    if(next>=(uint64_t)p->model_start*1000u)s->model_starts=1;
    if(next>=(uint64_t)p->particle_start*1000u)s->particle_starts=1;
    if(next>=(uint64_t)p->floor_start*1000u)s->floor_starts=1;
    if(next>=(uint64_t)p->sound_start*1000u)s->sound_requests=1;
    if(next>=(uint64_t)p->hp_start*1000u)s->hp_gates=1;
    s->units=next;
    uint64_t owner_units=next;
    if(owner_units>(uint64_t)p->owner_end*1000u)owner_units=(uint64_t)p->owner_end*1000u;
    owner_units=owner_units>=(uint64_t)p->owner_start*1000u?owner_units-(uint64_t)p->owner_start*1000u:0;
    s->owner_seconds=(float)((double)owner_units/60000.0)*p->owner_rate;
    s->owner_minimum_complete=(uint8_t)(next>=(uint64_t)p->owner_end*1000u);
    uint64_t model_units=next>=(uint64_t)p->model_start*1000u?next-(uint64_t)p->model_start*1000u:0;
    const uint64_t model_duration=(uint64_t)p->model_frames*2000u;
    s->model_finished=(uint8_t)(s->model_starts&&model_units>=model_duration);
    if(model_units>model_duration)model_units=model_duration;
    s->model_seconds=(float)((double)model_units/60000.0)*p->model_rate;
    s->model_visible=(uint8_t)(s->model_starts&&!s->model_finished);
    uint64_t age=next>=(uint64_t)p->floor_start*1000u?next-(uint64_t)p->floor_start*1000u:0;
    s->floor_finished=(uint8_t)(s->floor_starts&&age>=(uint64_t)p->floor_duration*1000u);
    s->floor_enabled=(uint8_t)(s->floor_starts&&!s->floor_finished);
    if(s->floor_starts){
        uint64_t ticks=age/1000u;
        float fraction=ticks<=p->floor_hold_ticks?0:(float)(ticks-p->floor_hold_ticks)/(float)p->floor_ramp_ticks;
        if(fraction>1)fraction=1;
        for(unsigned i=0;i<3;++i){int a=(uint8_t)(p->floor_hold>>(8*i)),b=(uint8_t)(p->floor_to>>(8*i));
            /* Source GSeffect interpolates floats and truncates the RGB byte. */
            s->floor_rgb[i]=(uint8_t)(a+fraction*(b-a));}
    }
    return 1;
}
int c3_wzx_damage_finish(C3WzxDamage *s,int model_done,int particles_done,int floor_done)
{
    if(!s||!valid(&s->program))return 0;
    if(s->completed)return 1;
    if(!s->active||!s->owner_minimum_complete||!s->model_finished||!s->floor_finished||
       !model_done||!particles_done||!floor_done)return 0;
    c3_wzx_damage_cancel(s);s->completed=1;return 1;
}
