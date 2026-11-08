#include "colosseum3ds/wzx_growl.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Growl line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void word(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=(uint8_t)(n>>(8*i));}
static void real(uint8_t *b,float f){uint32_t n;memcpy(&n,&f,4);word(b,n);}
static void hex(uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[2*i],s[2*i+1],0};b[i]=(uint8_t)strtoul(t,NULL,16);}}
static void checksum(uint8_t *b){memset(b+300,0,20);c3_save_codec_sha1(b+300,b,1280);}
static void descriptor(uint8_t b[1280]){
 memset(b,0,1280);memcpy(b,"C3WG",4);const uint32_t header[]={1,1280,45,288,1,1,60};for(unsigned i=0;i<7;++i)word(b+4+i*4,header[i]);
 hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20);hex(b+52,"85862ce50519fc694b179761e5038bab74f4bb2ce9c15b7102a0b0937ef8f82e",32);hex(b+84,"c9df90e70c02580843f17988a99c48839de47776",20);hex(b+104,"66078c124021cd7ad7eb71a801ef713136b952ea",20);hex(b+124,"d33d3d404433a48d7cc7863ac365178c47506d62",20);hex(b+144,"64867627ce7029307e3064e1af883cafb52995b7e2834980d7722d328ef043da",32);
 real(b+176,.5);real(b+180,.75);word(b+184,58);word(b+188,9);word(b+192,22);const float anchors[]={-.01756220869719982f,.0642678514122963f,-.5523711442947388f,-.029487334191799164f,-.01608329452574253f,.006553320679813623f};for(unsigned i=0;i<6;++i)real(b+200+i*4,anchors[i]);
 const uint32_t timing[]={244,366,230,5,320,39,183,79,90,120,130,130,130,130,100,18};for(unsigned i=0;i<16;++i)word(b+224+i*4,timing[i]);
 const uint32_t records[5][12]={{1,2,130,1,1,0,0,1,2,2,0,0},{2,3,130,0,1,1,0,0,2,3,9,0},{3,5,130,0,0,1,0,0,2,1,0,0},{4,1,130,0,0,0,0,1,2,0,0,0},{5,4,90,0,0,0,1,1,3,0,0,0}};
 const uint32_t payload[5][19]={{0,0,0,0,0,0,1,64851},{18,0x10A1,365764,4},{0x210,3,0},{0,100,3},{0,120,2,1,3,3,0,0x80808080,0x802D2D2D,20,1,0x802D2D2D,0x802D2D2D,50,1,0x802D2D2D,0x80808080,50,1}};
 for(unsigned i=0;i<5;++i){uint8_t *r=b+320+i*192;for(unsigned j=0;j<12;++j)word(r+j*4,records[i][j]);for(unsigned j=0;j<19;++j)word(r+112+j*4,payload[i][j]);if(i==0)word(r+52,39);if(i==4){word(r+52,40);word(r+56,220);}}
 checksum(b);
}
static void at(C3WzxGrowl *s,const C3WzxGrowlProgram *p,uint32_t ms){CHECK(c3_wzx_growl_begin(s,p));CHECK(c3_wzx_growl_step(s,ms));}
static void clocks(const C3WzxGrowlProgram *p){C3WzxGrowl s,before,direct;
 at(&s,p,0);CHECK(s.active&&!s.floor_enabled&&!s.model_starts&&!s.sound_requests&&s.owner_seconds==0);CHECK(!c3_wzx_growl_finish(&s));before=s;CHECK(!c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_FINISHED,1,1));CHECK(!memcmp(&s,&before,sizeof s));
 at(&s,p,1499);CHECK(!s.floor_starts);CHECK(c3_wzx_growl_step(&s,1));CHECK(s.floor_starts&&s.floor_enabled&&s.floor_rgb[0]==128);
 at(&s,p,1667);CHECK(s.floor_rgb[0]==86);at(&s,p,1834);CHECK(s.floor_rgb[0]==45);at(&s,p,2667);CHECK(s.floor_rgb[0]==45);at(&s,p,3084);CHECK(s.floor_rgb[0]==86);at(&s,p,3500);CHECK(s.floor_rgb[0]==128&&!s.floor_enabled);
 at(&s,p,2166);CHECK(!s.model_starts&&!s.particle_starts&&!s.sound_requests);CHECK(c3_wzx_growl_step(&s,1));CHECK(s.model_starts==1&&s.particle_starts==1&&s.sound_requests==1&&s.control_starts==1&&s.model_visible);CHECK(fabsf(s.owner_seconds-1.0835f)<1e-6f);
 at(&s,p,3466);CHECK(!s.model_finished&&s.model_visible);CHECK(c3_wzx_growl_step(&s,1));CHECK(s.model_finished&&!s.model_visible&&s.model_seconds==39.f/60.f);
 at(&s,p,3833);CHECK(!s.stop_requested);CHECK(c3_wzx_growl_step(&s,1));CHECK(s.units==230000&&s.stop_requested&&s.active&&!s.owner_normal_complete&&!s.owner_minimum_complete&&!s.teardown_acknowledged);CHECK(s.stop_requests==1&&s.particle_kills==1&&s.sound_keyoffs==1&&s.restore_requests==1);CHECK(s.owner_seconds==115.f/60.f);
 before=s;CHECK(c3_wzx_growl_step(&s,UINT32_MAX));CHECK(!memcmp(&before,&s,sizeof s));CHECK(!c3_wzx_growl_finish(&s));
 CHECK(!c3_wzx_growl_acknowledge(&s,0,C3_GROWL_AUDIO_STOPPED,1,1));CHECK(!c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_PENDING,1,1));CHECK(!c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_STOPPED,0,1));CHECK(!c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_STOPPED,1,0));CHECK(!c3_wzx_growl_acknowledge(&s,1,(C3WzxGrowlAudio)9,1,1));CHECK(!memcmp(&before,&s,sizeof s));
 CHECK(c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_STOPPED,1,1));CHECK(s.audio_status==C3_GROWL_AUDIO_STOPPED);before=s;CHECK(c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_STOPPED,1,1));CHECK(!memcmp(&before,&s,sizeof s));CHECK(!c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_FINISHED,1,1));CHECK(!memcmp(&before,&s,sizeof s));CHECK(c3_wzx_growl_finish(&s)&&s.completed&&!s.active);CHECK(c3_wzx_growl_finish(&s));
 at(&s,p,4000);CHECK(c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_FINISHED,1,1));CHECK(c3_wzx_growl_finish(&s));CHECK(s.audio_status==C3_GROWL_AUDIO_FINISHED);
 for(unsigned part=1;part<=1000;part=part==1?17:1000){at(&s,p,0);unsigned elapsed=0;while(elapsed<5000){unsigned ms=part;if(ms>5000-elapsed)ms=5000-elapsed;CHECK(c3_wzx_growl_step(&s,ms));elapsed+=ms;at(&direct,p,elapsed);CHECK(!memcmp(&s,&direct,sizeof s));}if(part==1000)break;}
 at(&s,p,100);before=s;CHECK(!c3_wzx_growl_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));/* Pause omits step; retains fractional units. */CHECK(s.units==6000);CHECK(c3_wzx_growl_step(&s,17));CHECK(s.units==7020);
 c3_wzx_growl_cancel(&s);CHECK(s.cancelled&&!s.active&&!s.completed&&s.stop_requested&&!s.floor_enabled&&!s.model_visible);CHECK(c3_wzx_growl_acknowledge(&s,1,C3_GROWL_AUDIO_STOPPED,1,1));CHECK(!c3_wzx_growl_finish(&s));before=s;c3_wzx_growl_cancel(&s);CHECK(!memcmp(&before,&s,sizeof s));
 float root[3]={1,2,3},model[3],emitter[3];CHECK(c3_wzx_growl_anchors(p,root,0,2,model,emitter,NULL,0));for(unsigned i=0;i<3;++i){CHECK(fabsf(model[i]-(root[i]+2*p->owner_anchor[i]))<1e-6f);CHECK(fabsf(emitter[i]-(model[i]+.75f*p->emitter_anchor[i]))<1e-6f);}float saved[3];memcpy(saved,model,sizeof saved);CHECK(!c3_wzx_growl_anchors(p,root,NAN,2,model,emitter,NULL,0));CHECK(!memcmp(saved,model,sizeof saved));
}
int main(int argc,char **argv){uint8_t b[1280],bad[1280];char error[160];descriptor(b);C3WzxGrowlProgram p={0},before;CHECK(c3_wzx_growl_decode(&p,b,sizeof b,error,sizeof error));clocks(&p);before=p;
 for(unsigned i=0;i<1280;++i){memcpy(bad,b,sizeof b);bad[i]^=1;CHECK(!c3_wzx_growl_decode(&p,bad,sizeof bad,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));}
 for(unsigned i=0;i<1280;i+=4){if(i>=300&&i<320)continue;memcpy(bad,b,sizeof b);bad[i]^=1;checksum(bad);CHECK(!c3_wzx_growl_decode(&p,bad,sizeof bad,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));}
 CHECK(!c3_wzx_growl_decode(&p,b,1279,error,sizeof error));CHECK(!c3_wzx_growl_load(&p,"/not/a/file",error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));
 C3WzxGrowl s;CHECK(c3_wzx_growl_begin(&s,&p));c3_wzx_growl_program_close(&p);CHECK(c3_wzx_growl_step(&s,4000));CHECK(s.stop_requested);/* Value ownership survives programclose. */c3_wzx_growl_cancel(&s);
 if(argc>1){CHECK(c3_wzx_growl_load(&p,argv[1],error,sizeof error));clocks(&p);CHECK(c3_wzx_growl_load(&p,argv[1],error,sizeof error));c3_wzx_growl_program_close(&p);c3_wzx_growl_program_close(&p);CHECK(!p.model_rate);}
 printf("Growl %u checks passed\n",checks);return 0;}
