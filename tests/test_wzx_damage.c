#include "colosseum3ds/wzx_damage.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"WZX damage failed line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(uint8_t *p,uint32_t n)
{p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static C3WzxDamageProgram synthetic(void)
{
    C3WzxDamageProgram p={0};p.owner_clip=2;p.model_start=p.particle_start=p.floor_start=0;
    p.sound_start=p.hp_start=22;p.floor_duration=160;p.owner_end=380;p.model_frames=25;
    p.owner_start=p.normalization_shift=22;
    p.owner_rate=p.model_rate=.5f;p.particle_part=12;p.particle_bone=11;
    p.floor_from=p.floor_hold=0x803A3A3Au;p.floor_to=0x80808080u;
    p.floor_hold_ticks=60;p.floor_ramp_ticks=100;p.sound_id=0x86;p.particle_flags=0x10a1;p.root_generator=3;
    memset(p.model_sha1,1,20);memset(p.particles_sha1,2,20);return p;
}
static void descriptor(uint8_t b[256])
{
    static const uint8_t source[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t wzx[32]={0x4e,0xd0,0xfd,0x17,0x39,0xbb,0x6f,0xcb,0x2c,0xaa,0xc0,0xc4,0x9e,0x4a,0x53,0xdb,0x1e,0x0d,0x39,0x68,0x0c,0xfd,0x14,0x95,0x65,0x3e,0x14,0x6a,0x8a,0xac,0xd9,0xed};
    const uint32_t fields[]={2,256,44,288,2,0,60,0,0,22,0,160,22,380,25,
        0x3f000000,0x3f000000,0,0,0,0,12,11,0x803a3a3a,0x803a3a3a,0x80808080,60,100,0x86,0x10a1,3};
    memset(b,0,256);memcpy(b,"C3WD",4);for(unsigned i=0;i<31;++i)put(b+4+4*i,fields[i]);
    memcpy(b+128,source,20);memcpy(b+148,wzx,32);memset(b+180,1,20);memset(b+200,2,20);
    put(b+220,22);put(b+224,22);c3_save_codec_sha1(b+236,b,236);
}
static void clocks(const C3WzxDamageProgram *p)
{
    C3WzxDamage s={0},before;
    CHECK(c3_wzx_damage_begin(&s,p));CHECK(s.active&&s.floor_rgb[0]==58&&s.model_visible);
    CHECK(s.units==0&&s.model_starts==1&&s.particle_starts==1&&s.floor_starts==1);
    CHECK(s.floor_enabled&&!s.sound_requests&&!s.hp_gates&&s.owner_seconds==0);
    CHECK(c3_wzx_damage_step(&s,366));CHECK(s.units==21960&&!s.sound_requests&&!s.hp_gates);
    CHECK(c3_wzx_damage_step(&s,1));CHECK(s.units==22020&&s.sound_requests==1&&s.hp_gates==1);
    CHECK(fabsf(s.owner_seconds-(.02f/120.f))<1e-6f);
    CHECK(c3_wzx_damage_step(&s,466));CHECK(!s.model_finished&&s.units==49980);
    CHECK(c3_wzx_damage_step(&s,1));CHECK(s.model_finished&&!s.model_visible);
    CHECK(c3_wzx_damage_step(&s,166));CHECK(s.units==60000&&s.floor_rgb[0]==58&&s.floor_enabled);
    CHECK(c3_wzx_damage_step(&s,833));CHECK(s.floor_rgb[0]==92);
    CHECK(c3_wzx_damage_step(&s,1));CHECK(s.units==110040&&s.floor_rgb[0]==93);
    CHECK(c3_wzx_damage_step(&s,833));CHECK(s.floor_finished&&!s.floor_enabled&&s.floor_rgb[0]==128);
    CHECK(!s.owner_minimum_complete);CHECK(!c3_wzx_damage_finish(&s,1,1,1));
    CHECK(c3_wzx_damage_step(&s,3666));CHECK(!s.owner_minimum_complete&&s.units==379980);
    CHECK(c3_wzx_damage_step(&s,1));CHECK(s.owner_minimum_complete&&s.active&&!s.completed);
    CHECK(fabsf(s.owner_seconds-(358.f/120.f))<1e-6f);
    CHECK(!c3_wzx_damage_finish(&s,1,0,1));CHECK(!c3_wzx_damage_finish(&s,0,1,1));CHECK(!c3_wzx_damage_finish(&s,1,1,0));
    CHECK(c3_wzx_damage_step(&s,1000));CHECK(s.active); /* surviving particles own completion */
    CHECK(c3_wzx_damage_finish(&s,1,1,1));CHECK(s.completed&&!s.active&&!s.model_visible&&!s.floor_enabled);
    before=s;CHECK(c3_wzx_damage_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));CHECK(c3_wzx_damage_finish(&s,1,1,1));
    for(unsigned partition=1;partition<=211;++partition){
        C3WzxDamage a={0},b={0};CHECK(c3_wzx_damage_begin(&a,p));unsigned elapsed=0;
        while(elapsed<6500){unsigned ms=partition;if(ms>6500-elapsed)ms=6500-elapsed;elapsed+=ms;
            CHECK(c3_wzx_damage_step(&a,ms));CHECK(c3_wzx_damage_begin(&b,p));CHECK(c3_wzx_damage_step(&b,elapsed));
            CHECK(!memcmp(&a,&b,sizeof a));}
        CHECK(a.model_starts==1&&a.particle_starts==1&&a.sound_requests==1&&a.floor_starts==1&&a.hp_gates==1);
    }
    CHECK(c3_wzx_damage_begin(&s,p));CHECK(c3_wzx_damage_step(&s,60000));CHECK(s.active&&s.units==3600000);
    before=s;CHECK(!c3_wzx_damage_step(&s,1));CHECK(!memcmp(&s,&before,sizeof s));
    CHECK(c3_wzx_damage_begin(&s,p));before=s;CHECK(!c3_wzx_damage_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));
    CHECK(c3_wzx_damage_step(&s,3500));c3_wzx_damage_cancel(&s);CHECK(!s.active&&!s.completed&&!c3_wzx_damage_finish(&s,1,1,1));
    before=s;C3WzxDamageProgram invalid=*p;invalid.particle_bone=12;
    CHECK(!c3_wzx_damage_begin(&s,&invalid));CHECK(!memcmp(&s,&before,sizeof s));invalid=*p;invalid.anchor[0]=NAN;
    CHECK(!c3_wzx_damage_begin(&s,&invalid));CHECK(!c3_wzx_damage_step(NULL,0));CHECK(!c3_wzx_damage_begin(NULL,p));
}
static void decoding(uint8_t b[256])
{
    C3WzxDamageProgram p={0},before;char error[192];CHECK(c3_wzx_damage_decode(&p,b,256,error,sizeof error));
    CHECK(p.particle_bone==11&&p.sound_id==0x86);clocks(&p);
    for(unsigned i=0;i<256;++i){before=p;b[i]^=128;CHECK(!c3_wzx_damage_decode(&p,b,256,error,sizeof error));
        CHECK(!memcmp(&p,&before,sizeof p));b[i]^=128;}
    for(unsigned i=0;i<256;++i)CHECK(!c3_wzx_damage_decode(&p,b,i,error,sizeof error));
    /* Recompute checksum after forbidden field edits: semantic checks still reject. */
    uint8_t copy[256];memcpy(copy,b,256);put(copy+92,12);c3_save_codec_sha1(copy+236,copy,236);
    CHECK(!c3_wzx_damage_decode(&p,copy,256,error,sizeof error));
    memcpy(copy,b,256);memset(copy+200,0,20);c3_save_codec_sha1(copy+236,copy,236);
    CHECK(!c3_wzx_damage_decode(&p,copy,256,error,sizeof error));
    memcpy(copy,b,256);copy[228]=1;c3_save_codec_sha1(copy+236,copy,236);
    CHECK(!c3_wzx_damage_decode(&p,copy,256,error,sizeof error));
    memcpy(copy,b,256);put(copy+4,1);c3_save_codec_sha1(copy+236,copy,236);
    CHECK(!c3_wzx_damage_decode(&p,copy,256,error,sizeof error));
}
int main(int argc,char **argv)
{
    C3WzxDamageProgram p=synthetic();clocks(&p);uint8_t b[256];descriptor(b);decoding(b);
    if(argc==2){char error[192];CHECK(c3_wzx_damage_load(&p,argv[1],error,sizeof error));clocks(&p);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);CHECK(fread(b,1,sizeof b,f)==sizeof b);CHECK(!fclose(f));decoding(b);}
    printf("WZX defender: %u checks passed (bounded source clock; external completion required).\n",checks);return 0;
}
