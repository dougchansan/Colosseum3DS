#include "colosseum3ds/wzx_reflect.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Reflect failed line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static C3WzxReflectProgram synthetic(void)
{
    C3WzxReflectProgram p={0};p.owner_clip=1;p.owner_minimum=180;p.ally_minimum=118;p.texture_end=95;
    p.floor_duration=190;p.darken_ticks=20;p.hold_ticks=110;p.restore_ticks=60;
    p.floor_from=0x80808080;p.floor_dark=0x802D2D2D;p.sound_id=0x220;p.particle_flags=0x18A9;
    p.attachment=2;p.position_type=1;p.body_map_index=2;p.espeon_part=62;p.umbreon_part=42;
    p.owner_rate=p.model_rate=.5;memset(p.model_sha1,1,20);memset(p.particles_sha1,2,20);memset(p.parts_sha1,3,20);return p;
}
static void clocks(const C3WzxReflectProgram *p)
{
    C3WzxReflect s={0},before;
    CHECK(c3_wzx_reflect_begin(&s,p,196));CHECK(s.owner_clip==1&&s.source_part==62&&s.owner_minimum==180);
    CHECK(s.units==0&&s.active&&s.model_visible&&s.floor_enabled&&s.floor_rgb[0]==128);
    CHECK(s.model_starts==1&&s.particle_starts==1&&s.floor_starts==1&&s.sound_requests==1);
    CHECK(!c3_wzx_reflect_finish(&s,1,1,1,1));
    CHECK(c3_wzx_reflect_step(&s,166));CHECK(s.floor_rgb[0]==90);
    CHECK(c3_wzx_reflect_step(&s,168));CHECK(s.floor_rgb[0]==45);
    CHECK(c3_wzx_reflect_step(&s,1832));CHECK(s.units==129960&&s.floor_rgb[0]==45);
    CHECK(c3_wzx_reflect_step(&s,1));CHECK(s.units==130020&&s.floor_rgb[0]==45);
    CHECK(c3_wzx_reflect_step(&s,500));CHECK(s.floor_rgb[0]==86);
    CHECK(c3_wzx_reflect_step(&s,333));CHECK(s.owner_minimum_complete&&!s.floor_finished&&!s.model_finished);
    CHECK(c3_wzx_reflect_step(&s,166));CHECK(!s.floor_finished);
    CHECK(c3_wzx_reflect_step(&s,1));CHECK(s.floor_finished&&s.model_finished&&s.floor_rgb[0]==128);
    CHECK(!s.floor_enabled&&!s.model_visible&&s.active);
    CHECK(fabsf(s.model_seconds-95.f/60.f)<1e-6f);
    CHECK(!c3_wzx_reflect_finish(&s,1,0,1,1));CHECK(!c3_wzx_reflect_finish(&s,0,1,1,1));CHECK(!c3_wzx_reflect_finish(&s,1,1,0,1));
    CHECK(!c3_wzx_reflect_finish(&s,1,1,1,0));
    CHECK(c3_wzx_reflect_step(&s,1000));CHECK(s.active);CHECK(c3_wzx_reflect_finish(&s,1,1,1,1));
    before=s;CHECK(c3_wzx_reflect_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));
    CHECK(c3_wzx_reflect_begin(&s,p,197));CHECK(s.owner_clip==0&&s.source_part==42&&s.owner_minimum==118);
    for(unsigned part=1;part<=137;++part){
        C3WzxReflect a={0},b={0};CHECK(c3_wzx_reflect_begin(&a,p,197));unsigned elapsed=0;
        while(elapsed<4000){unsigned ms=part;if(ms>4000-elapsed)ms=4000-elapsed;elapsed+=ms;
            CHECK(c3_wzx_reflect_step(&a,ms));CHECK(c3_wzx_reflect_begin(&b,p,197));CHECK(c3_wzx_reflect_step(&b,elapsed));
            CHECK(!memcmp(&a,&b,sizeof a));}
        CHECK(a.model_starts==1&&a.particle_starts==1&&a.sound_requests==1);
    }
    before=s;CHECK(!c3_wzx_reflect_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));
    c3_wzx_reflect_cancel(&s);CHECK(!s.active&&!s.completed&&!c3_wzx_reflect_finish(&s,1,1,1,1));
    before=s;CHECK(!c3_wzx_reflect_begin(&s,p,288));CHECK(!memcmp(&s,&before,sizeof s));
}
static void little(uint8_t *p,uint32_t n)
{p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static void floating(uint8_t *p,float f){uint32_t n;memcpy(&n,&f,4);little(p,n);}
static void hex20(uint8_t *out,const char *text)
{for(unsigned i=0;i<20;++i){char n[3]={text[2*i],text[2*i+1],0};out[i]=(uint8_t)strtoul(n,NULL,16);}}
static void positions(void)
{
    uint8_t data[C3_WZX_REFLECT_PARTS_BYTES]={0};memcpy(data,"C3RP",4);
    const uint32_t fields[15]={1,3728,2,120,181,119,12,62,42,128,2300,196,197,1,0};
    for(unsigned i=0;i<15;++i)little(data+4+4*i,fields[i]);
    hex20(data+64,"51bfe4314a218dfaf9592fc80723c50498d0ee55");
    hex20(data+84,"8755031c912d7d965ec352b20cd11d9385ef0250");
    hex20(data+104,"6ef86a8fadf7c8b97aeb86d602b88c6ad1adc838");
    for(unsigned i=0;i<181;++i){floating(data+128+12*i,.01f*i);floating(data+132+12*i,1);floating(data+136+12*i,2);}
    for(unsigned i=0;i<119;++i){floating(data+2300+12*i,.02f*i);floating(data+2304+12*i,3);floating(data+2308+12*i,4);}
    C3WzxReflectProgram p=synthetic();c3_save_codec_sha1(p.parts_sha1,data,sizeof data);char error[192];
    float root[3]={10,20,30},out[3]={-1,-2,-3},saved[3];
    CHECK(!c3_wzx_reflect_position(&p,196,0,root,0,1,out,error,sizeof error));
    CHECK(c3_wzx_reflect_parts_decode(&p,data,sizeof data,error,sizeof error));
    CHECK(c3_wzx_reflect_position(&p,196,1.f/240.f,root,0,2,out,error,sizeof error));
    CHECK(fabsf(out[0]-10.01f)<1e-5f&&out[1]==22&&out[2]==34);
    CHECK(c3_wzx_reflect_position(&p,196,1.f/240.f,root,1.57079632679f,2,out,error,sizeof error));
    CHECK(fabsf(out[0]-14)<1e-5f&&out[1]==22&&fabsf(out[2]-29.99f)<1e-5f);
    CHECK(c3_wzx_reflect_position(&p,196,1.5f,root,0,1,out,error,sizeof error));
    CHECK(out[0]==10&&out[1]==21&&out[2]==32);
    CHECK(c3_wzx_reflect_position(&p,197,1.f/240.f,root,0,2,out,error,sizeof error));
    CHECK(fabsf(out[0]-10.02f)<1e-5f&&out[1]==26&&out[2]==38);
    memcpy(saved,out,sizeof out);CHECK(!c3_wzx_reflect_position(&p,288,0,root,0,1,out,error,sizeof error));CHECK(!memcmp(out,saved,sizeof out));
    CHECK(!c3_wzx_reflect_position(&p,196,0,root,NAN,1,out,error,sizeof error));
    CHECK(!c3_wzx_reflect_position(&p,196,0,root,0,0,out,error,sizeof error));CHECK(!memcmp(out,saved,sizeof out));
    uint8_t *prior=p.parts_data;
    for(unsigned i=0;i<sizeof data;++i){data[i]^=128;CHECK(!c3_wzx_reflect_parts_decode(&p,data,sizeof data,error,sizeof error));CHECK(p.parts_data==prior);data[i]^=128;}
    for(unsigned i=0;i<128;++i){data[i]^=128;c3_save_codec_sha1(p.parts_sha1,data,sizeof data);
        CHECK(!c3_wzx_reflect_parts_decode(&p,data,sizeof data,error,sizeof error));CHECK(p.parts_data==prior);data[i]^=128;}
    floating(data+128,NAN);c3_save_codec_sha1(p.parts_sha1,data,sizeof data);
    CHECK(!c3_wzx_reflect_parts_decode(&p,data,sizeof data,error,sizeof error));CHECK(p.parts_data==prior);
    floating(data+128,0);c3_save_codec_sha1(p.parts_sha1,data,sizeof data);
    CHECK(c3_wzx_reflect_parts_decode(&p,p.parts_data,p.parts_bytes,error,sizeof error));
    C3WzxReflect s={0};CHECK(c3_wzx_reflect_begin(&s,&p,196));CHECK(s.program.parts_data==p.parts_data);
    c3_wzx_reflect_cancel(&s);c3_wzx_reflect_program_close(&p);CHECK(!p.parts_data&&!p.parts_bytes);
    c3_wzx_reflect_program_close(&p);
}
int main(int argc,char **argv)
{
    positions();C3WzxReflectProgram p=synthetic();clocks(&p);
    CHECK(c3_wzx_reflect_actor(1,3,0)==1&&c3_wzx_reflect_actor(1,3,1)==0);
    CHECK(c3_wzx_reflect_actor(1,2,1)==C3_WZX_REFLECT_NO_ACTOR);
    CHECK(c3_wzx_reflect_actor(1,1,0)==C3_WZX_REFLECT_NO_ACTOR);
    CHECK(c3_wzx_reflect_actor(2,15,0)==C3_WZX_REFLECT_NO_ACTOR);
    CHECK(c3_wzx_reflect_actor(0,3,2)==C3_WZX_REFLECT_NO_ACTOR);
    if(argc==2){char error[192];CHECK(c3_wzx_reflect_load(&p,argv[1],error,sizeof error));clocks(&p);
        char parts_path[1024];
        if(argc>=3)snprintf(parts_path,sizeof parts_path,"%s",argv[2]);
        else {const char *slash=strrchr(argv[1],'/');CHECK(slash!=NULL);
            snprintf(parts_path,sizeof parts_path,"%.*s/reflect_parts.c3r",(int)(slash-argv[1]),argv[1]);}
        CHECK(c3_wzx_reflect_parts_load(&p,parts_path,error,sizeof error));
        C3WzxReflect borrowed={0};CHECK(c3_wzx_reflect_begin(&borrowed,&p,196));
        float root[3]={10,20,30},position[3],mid[3],loop[3];
        CHECK(c3_wzx_reflect_position(&p,196,0,root,0,1,position,error,sizeof error));
        CHECK(c3_wzx_reflect_position(&p,196,.75f,root,0,1,mid,error,sizeof error));
        CHECK(c3_wzx_reflect_position(&p,196,1.5f,root,0,1,loop,error,sizeof error));
        for(unsigned i=0;i<3;++i)CHECK(fabsf(position[i]-loop[i])<1e-6f);
        CHECK(memcmp(position,mid,sizeof position)!=0);
        float saved[3];memcpy(saved,position,sizeof saved);
        CHECK(!c3_wzx_reflect_position(&p,196,NAN,root,0,1,position,error,sizeof error));
        CHECK(!memcmp(saved,position,sizeof saved));
        CHECK(c3_wzx_reflect_position(&p,197,0,root,0,1,position,error,sizeof error));
        c3_wzx_reflect_cancel(&borrowed);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);uint8_t data[C3_WZX_REFLECT_BYTES];CHECK(fread(data,1,sizeof data,f)==sizeof data);CHECK(!fclose(f));
        for(unsigned i=0;i<C3_WZX_REFLECT_BYTES;++i){C3WzxReflectProgram before=p;data[i]^=128;
            CHECK(!c3_wzx_reflect_decode(&p,data,sizeof data,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));data[i]^=128;}
        for(unsigned i=0;i<C3_WZX_REFLECT_BYTES;++i)CHECK(!c3_wzx_reflect_decode(&p,data,i,error,sizeof error));
        uint8_t copy[C3_WZX_REFLECT_BYTES];memcpy(copy,data,sizeof copy);memset(copy+180,0,20);c3_save_codec_sha1(copy+268,copy,268);
        CHECK(!c3_wzx_reflect_decode(&p,copy,sizeof copy,error,sizeof error));
        memcpy(copy,data,sizeof copy);copy[108]=0;c3_save_codec_sha1(copy+268,copy,268);CHECK(!c3_wzx_reflect_decode(&p,copy,sizeof copy,error,sizeof error));
    }
    c3_wzx_reflect_program_close(&p);c3_wzx_reflect_program_close(&p);
    printf("Reflect: %u checks passed (source clock; live attachment/render integration required).\n",checks);return 0;
}
