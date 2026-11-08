#include "colosseum3ds/wzx_tackle.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Tackle line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void word(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=(uint8_t)(n>>(i*8));}
static void real(uint8_t *b,float f){uint32_t n;memcpy(&n,&f,4);word(b,n);}
static void hex(uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char c[3]={s[2*i],s[2*i+1],0};b[i]=(uint8_t)strtoul(c,NULL,16);}}
static void checksum(uint8_t b[1024]){c3_save_codec_sha1(b+1004,b,1004);}
static void descriptor(uint8_t b[1024]){
 memset(b,0,1024);memcpy(b,"C3TK",4);uint32_t h[]={1,1024,33,130,133,0x4A2,0xDE};for(unsigned i=0;i<7;++i)word(b+4+4*i,h[i]);
 hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20);hex(b+52,"5754c2ac8e4e3caef0bad6486d7cb6a06f91869daae4e2e336a969fb65318439",32);hex(b+84,"c144e8784d610c38960b67b5dcb4c1776ff25ec0195df0e0b3c4b2ea06951f97",32);hex(b+116,"40269fb5a7111267999a9e9d9fd72f18c24a4b54a9b452267c782b7123f8bdef",32);hex(b+148,"34e7da926099fcdcfdee31a56d0be2e354f9ad27",20);
 uint32_t a[]={2,0,91680,0,60,2,4,120,240,53};for(unsigned i=0;i<10;++i)word(b+168+4*i,a[i]);real(b+208,.5f);real(b+212,.75f);
 hex(b+448,"69d22676ac5b639f22ae0032fb6a07ac3e1478bb",20);hex(b+468,"d33d3d404433a48d7cc7863ac365178c47506d62",20);word(b+488,1);
 const float anchors[2][3]={{1.492099244160272e-08f,.3874979615211487f,.2519835829734802f},{.08478743582963943f,.3640747368335724f,.13167962431907654f}};
 for(unsigned i=0;i<2;++i){uint8_t *r=b+256+i*96;uint32_t f[]={i?196:197,i,i?2:3,i?100:170,i?176:298,i?62:42,i?88:149,8};for(unsigned j=0;j<8;++j)word(r+j*4,f[j]);hex(r+32,i?"8755031c912d7d965ec352b20cd11d9385ef0250":"6ef86a8fadf7c8b97aeb86d602b88c6ad1adc838",20);hex(r+52,i?"d1fd5ff047537d95f3291119ccfdcdf786b22840":"a0121db064f81bac15efda57d805573e5a3e87f0",20);for(unsigned j=0;j<3;++j)real(r+72+j*4,anchors[i][j]);}
 for(unsigned i=0;i<4;++i){uint8_t *r=b+512+i*112;word(r,(i&1)+1);word(r+4,i==0?1:i==2?3:5);word(r+8,i==2?2:0);word(r+12,i==2?1:0);word(r+16,i&1);word(r+24,i==0?2:0);word(r+28,2);word(r+96,(i&1)?1:i==2?2:0);word(r+104,2);}checksum(b);
}
int main(int argc,char **argv){
 uint8_t b[1024];descriptor(b);C3WzxTackleProgram p={0},before;char error[160];
 CHECK(c3_wzx_tackle_decode(&p,b,sizeof b,error,sizeof error));before=p;
 /* Every typed field, identity, anchor and reserved byte fails atomically,
  * including corruption with a recomputed checksum. */
 const unsigned offsets[]={4,12,16,32,52,84,116,148,168,176,180,184,188,192,196,200,204,208,212,216,256,264,268,272,276,280,284,288,308,328,340,448,468,488,492,512,536,540,608,616,960};
 for(unsigned i=0;i<sizeof offsets/sizeof *offsets;++i){b[offsets[i]]^=1;checksum(b);CHECK(!c3_wzx_tackle_decode(&p,b,sizeof b,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));descriptor(b);}
 CHECK(!c3_wzx_tackle_decode(&p,b,1023,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));
 if(argc>1)CHECK(c3_wzx_tackle_load(&p,argv[1],error,sizeof error));
 C3WzxTackle s,copy;CHECK(c3_wzx_tackle_begin(&s,&p,0));CHECK(!c3_wzx_tackle_defend(&s,1));
 CHECK(c3_wzx_tackle_step(&s,2166));CHECK(!s.attack_sound_requests);CHECK(c3_wzx_tackle_step(&s,1));CHECK(s.attack_sound_requests&&!s.attack_complete);
 CHECK(c3_wzx_tackle_step(&s,49));CHECK(!s.attack_complete);CHECK(c3_wzx_tackle_step(&s,1));CHECK(s.attack_complete&&s.attack_stops&&s.attack_sound_stops&&s.units==133000);
 CHECK(c3_wzx_tackle_defend(&s,1));CHECK(s.units==20&&s.reset_except_requests&&s.particle_starts&&s.damage_sound_requests&&s.hp_gates);CHECK(!c3_wzx_tackle_defend(&s,1));
 CHECK(c3_wzx_tackle_step(&s,3000));CHECK(!c3_wzx_tackle_finish(&s,1,1,0,0));CHECK(!c3_wzx_tackle_finish(&s,1,1,1,1));
 CHECK(c3_wzx_tackle_step(&s,1966));CHECK(!c3_wzx_tackle_finish(&s,1,1,0,0));CHECK(c3_wzx_tackle_step(&s,1));CHECK(!c3_wzx_tackle_finish(&s,0,1,0,0));CHECK(!c3_wzx_tackle_finish(&s,1,0,0,0));CHECK(c3_wzx_tackle_finish(&s,1,1,0,0));CHECK(s.family_stop_requests&&s.damage_sound_stops&&!s.active);CHECK(c3_wzx_tackle_finish(&s,0,0,0,0));
 CHECK(c3_wzx_tackle_begin(&s,&p,1));CHECK(c3_wzx_tackle_step(&s,2217));CHECK(c3_wzx_tackle_defend(&s,1));CHECK(c3_wzx_tackle_step(&s,2932));CHECK(!c3_wzx_tackle_finish(&s,1,1,0,0));CHECK(c3_wzx_tackle_step(&s,1));CHECK(c3_wzx_tackle_finish(&s,1,1,0,0));
 CHECK(c3_wzx_tackle_begin(&s,&p,0));CHECK(c3_wzx_tackle_step(&s,2217));CHECK(c3_wzx_tackle_defend(&s,0));CHECK(s.miss&&!s.particle_starts&&!s.hp_gates&&!s.reset_except_requests&&!s.damage_sound_requests);
 CHECK(c3_wzx_tackle_begin(&s,&p,0));CHECK(c3_wzx_tackle_step(&s,2217));CHECK(c3_wzx_tackle_defend(&s,1));s.units=170000;CHECK(!c3_wzx_tackle_finish(&s,0,0,0,1));CHECK(c3_wzx_tackle_step(&s,1));CHECK(c3_wzx_tackle_finish(&s,0,0,0,1));CHECK(s.family_stop_requests);
 CHECK(c3_wzx_tackle_begin(&s,&p,0));copy=s;CHECK(!c3_wzx_tackle_step(&s,60001));CHECK(!memcmp(&s,&copy,sizeof s));c3_wzx_tackle_cancel(&s);CHECK(!s.active&&!s.family_stop_requests);c3_wzx_tackle_cancel(&s);
 CHECK(c3_wzx_tackle_begin(&s,&p,0));CHECK(c3_wzx_tackle_step(&s,10000));CHECK(s.units==133000&&s.handoff_units==467000&&!s.particle_starts&&!s.hp_gates);CHECK(c3_wzx_tackle_defend(&s,1));CHECK(s.units==467000&&s.handoff_units==467000&&s.particle_starts==1&&s.damage_sound_requests==1&&s.hp_gates==1);CHECK(!c3_wzx_tackle_finish(&s,0,0,0,0));CHECK(c3_wzx_tackle_finish(&s,1,1,0,0));
 printf("Tackle checks: %u\n",checks);return 0;
}
