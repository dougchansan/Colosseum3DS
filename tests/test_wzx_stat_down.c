#include "colosseum3ds/wzx_stat_down.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Stat-down line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void word(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=(uint8_t)(n>>(8*i));}
static void real(uint8_t *b,float f){uint32_t n;memcpy(&n,&f,4);word(b,n);}
static void hex(uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[i*2],s[i*2+1],0};b[i]=(uint8_t)strtoul(t,NULL,16);}}
static void checksum(uint8_t b[1024]){memset(b+236,0,20);c3_save_codec_sha1(b+236,b,1024);}
static void descriptor(uint8_t b[1024]){
 memset(b,0,1024);memcpy(b,"C3SG",4);uint32_t h[]={1,1024,0x29,4,2,2,3};for(unsigned i=0;i<7;++i)word(b+4+i*4,h[i]);hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20);hex(b+52,"5e08695ee116a4a5aaded4deef4fd1af71e5f4178992e187610a7e92a63e7c1e",32);hex(b+84,"7f42a1c5a723ed7563e290ede2feb2b1828f90498553feaea96ec1e650bdefeb",32);hex(b+116,"daf3b931c7dda451a6c275588735609b92feb25b",20);
 uint32_t a[]={0x19B,0,0,60,50,2};for(unsigned i=0;i<6;++i)word(b+136+i*4,a[i]);real(b+160,.75);real(b+164,.5);uint32_t q[]={0,0x803763CE,448,256,1644,0,1,2,0xA1};for(unsigned i=0;i<9;++i)word(b+168+i*4,q[i]);
 for(unsigned i=0;i<2;++i){uint8_t *r=b+256+i*96;uint32_t fields[]={i?196:197,i,0,i,i?90:59,i?180:118,0,i?62:42};for(unsigned j=0;j<8;++j)word(r+j*4,fields[j]);hex(r+32,i?"d1fd5ff047537d95f3291119ccfdcdf786b22840":"a0121db064f81bac15efda57d805573e5a3e87f0",20);hex(r+52,i?"8755031c912d7d965ec352b20cd11d9385ef0250":"6ef86a8fadf7c8b97aeb86d602b88c6ad1adc838",20);word(r+84,UINT32_MAX);}
 const uint32_t records[3][12]={{1,3,0,0,1,0,0,0,2,2,0,0},{2,5,0,0,0,1,0,0,2,1,0,0},{3,4,0,0,0,0,0,0,2,1,0,0}};
 const uint32_t payload[3][19]={{0,0xA1,1644,4},{0x19B,3,0},{0,50,2,2,3,3,0,0x80808080,0x80437A3D,10,1,0x80437A3D,0x80437A3D,10,1,0x80437A3D,0x80808080,30,1}};
 for(unsigned i=0;i<3;++i){uint8_t *r=b+448+i*192;for(unsigned j=0;j<12;++j)word(r+j*4,records[i][j]);for(unsigned j=0;j<19;++j)word(r+112+j*4,payload[i][j]);}checksum(b);
}
static void red_descriptor(uint8_t b[1024]){
 descriptor(b);word(b+12,0x27);word(b+20,1);hex(b+52,"0e0103bdcf6f2dca317e8ae86cfbc5294de2bcd2d7f1af4bf42c04a01bdf08de",32);hex(b+84,"9b54c2d13b9c50949ca13d7fec95ba1d49effb8268490ac1a65d4b04dcd27d97",32);hex(b+116,"e81bc82423aa2b31ab7aa49ed08c881a8d630b6f",20);word(b+136,0x19A);word(b+144,0x78);word(b+196,0);word(b+200,1);memset(b+448,0,576);
 const uint32_t records[3][12]={{1,3,0,0,1,0,0,0,2,0,0,0},{2,4,0,0,0,0,0,0,2,1,0,0},{3,5,0,0,0,0,0,0,2,1,0,0}};
 const uint32_t payload[3][19]={{0,1,1644,4},{0,50,2,2,3,3,0,0x80808080,0x80454681,10,1,0x80454681,0x80454681,10,1,0x80454681,0x80808080,30,1},{0x19A,3,0}};
 for(unsigned i=0;i<3;++i){uint8_t *r=b+448+i*192;for(unsigned j=0;j<12;++j)word(r+j*4,records[i][j]);for(unsigned j=0;j<19;++j)word(r+112+j*4,payload[i][j]);}checksum(b);
}
static void at(C3WzxStatDown *s,const C3WzxStatDownProgram *p,unsigned slot,uint32_t ms,const uint8_t rgb[3]){CHECK(c3_wzx_stat_down_begin(s,p,slot,rgb));CHECK(c3_wzx_stat_down_step(s,ms));}
static void clocks(const C3WzxStatDownProgram *p){const uint8_t neutral[3]={128,128,128},prior[3]={80,90,100};C3WzxStatDown s,direct,before;const unsigned expected_color[3]={p->special==0x27?129u:61u,p->special==0x27?70u:122u,p->special==0x27?69u:67u};
 for(unsigned slot=0;slot<2;++slot){at(&s,p,slot,0,prior);CHECK(s.active&&s.owner_clip==slot&&s.owner_minimum==(slot?180u:118u)&&s.color_enabled);CHECK(s.particle_starts==1&&s.color_starts==1&&s.sound_requests==1&&!s.family_kills&&!memcmp(s.color_rgb,prior,3));CHECK(!c3_wzx_stat_down_finish(&s));
 at(&s,p,slot,167,neutral);CHECK(s.color_rgb[0]==expected_color[0]&&s.color_rgb[1]==expected_color[1]&&s.color_rgb[2]==expected_color[2]);at(&s,p,slot,334,neutral);CHECK(s.color_rgb[0]==expected_color[0]&&s.color_enabled);at(&s,p,slot,833,neutral);CHECK(!s.color_finished);CHECK(c3_wzx_stat_down_step(&s,1));CHECK(s.color_finished&&!s.color_enabled&&!s.teardown_requested&&s.active);
 at(&s,p,slot,167,prior);CHECK(s.color_rgb[0]==prior[0]+expected_color[0]-128&&s.color_rgb[1]==prior[1]+expected_color[1]-128&&s.color_rgb[2]==prior[2]+expected_color[2]-128);CHECK(c3_wzx_stat_down_step(&s,1000));CHECK(!memcmp(s.color_rgb,prior,3)&&!s.teardown_requested);
 /* Source color interpolation is integer60Hz age, low-byteRGBA, truncation,
  * followed by captured modulation+color-128 saturated addition. */
 for(unsigned age=0;age<50;++age){at(&s,p,slot,(age*1000+59)/60,prior);float f=age<10?(float)age/10:age<20?1:1-(float)(age-20)/30;for(unsigned k=0;k<3;++k){int c=(uint8_t)(128+f*((int)expected_color[k]-128));int expected=(int)prior[k]+c-128;if(expected<0)expected=0;if(expected>255)expected=255;CHECK(s.color_rgb[k]==expected);}}
 for(unsigned part=1;part<=1000;part=part==1?17:1000){at(&s,p,slot,0,prior);unsigned elapsed=0;while(elapsed<5000){unsigned ms=part;if(ms>5000-elapsed)ms=5000-elapsed;CHECK(c3_wzx_stat_down_step(&s,ms));elapsed+=ms;at(&direct,p,slot,elapsed,prior);CHECK(!memcmp(&s,&direct,sizeof s));}if(part==1000)break;}
 CHECK(s.units==300000&&s.owner_seconds==(float)((double)(300000u%((slot?180u:118u)*1000u))/60000.*.5)&&s.owner_minimum_complete&&s.active&&!s.teardown_requested);before=s;CHECK(!c3_wzx_stat_down_observe(&s,1,C3_STAT_DOWN_AUDIO_STOPPED));CHECK(!memcmp(&before,&s,sizeof s));CHECK(c3_wzx_stat_down_observe(&s,0,C3_STAT_DOWN_AUDIO_FINISHED));CHECK(!s.teardown_requested);CHECK(c3_wzx_stat_down_observe(&s,1,C3_STAT_DOWN_AUDIO_ACTIVE));CHECK(s.teardown_requested&&!s.completed&&s.family_kills==1&&s.sound_keyoffs==1&&s.restore_requests==1);CHECK(!c3_wzx_stat_down_finish(&s));
 before=s;CHECK(!c3_wzx_stat_down_acknowledge(&s,0,C3_STAT_DOWN_AUDIO_FINISHED,1,1));CHECK(!c3_wzx_stat_down_acknowledge(&s,1,C3_STAT_DOWN_AUDIO_ACTIVE,1,1));CHECK(!c3_wzx_stat_down_acknowledge(&s,1,C3_STAT_DOWN_AUDIO_FINISHED,0,1));CHECK(!c3_wzx_stat_down_acknowledge(&s,1,C3_STAT_DOWN_AUDIO_FINISHED,1,0));CHECK(!memcmp(&before,&s,sizeof s));CHECK(c3_wzx_stat_down_acknowledge(&s,1,C3_STAT_DOWN_AUDIO_FINISHED,1,1));CHECK(c3_wzx_stat_down_finish(&s)&&s.completed&&!s.active);before=s;CHECK(c3_wzx_stat_down_step(&s,UINT32_MAX));CHECK(!memcmp(&before,&s,sizeof s));
 /* Known-finished PS/audio before ownergate gives exactfirstgate across all
  * elapsed partitions;50colorend never becomes the terminal timestamp. */
 for(unsigned part=1;part<=1000;part=part==1?17:1000){at(&s,p,slot,0,prior);CHECK(c3_wzx_stat_down_observe(&s,1,C3_STAT_DOWN_AUDIO_FINISHED));for(unsigned elapsed=0;elapsed<5000;elapsed+=part)CHECK(c3_wzx_stat_down_step(&s,part));CHECK(s.units==(uint64_t)(slot?180:118)*1000&&s.teardown_requested&&s.owner_seconds==0);if(part==1000)break;}
 at(&s,p,slot,500,prior);before=s;CHECK(!c3_wzx_stat_down_step(&s,UINT32_MAX));CHECK(!memcmp(&before,&s,sizeof s));/* Pause omits step: fractionalunits30000 retained. */CHECK(s.units==30000);c3_wzx_stat_down_cancel(&s);CHECK(!s.active&&s.cancelled&&!s.completed&&s.teardown_requested&&!memcmp(s.color_rgb,prior,3));CHECK(c3_wzx_stat_down_acknowledge(&s,1,C3_STAT_DOWN_AUDIO_STOPPED,1,1));CHECK(!c3_wzx_stat_down_finish(&s));before=s;c3_wzx_stat_down_cancel(&s);CHECK(!memcmp(&before,&s,sizeof s));
 }
 at(&s,p,0,0,neutral);before=s;CHECK(!c3_wzx_stat_down_begin(&s,p,2,neutral));CHECK(!memcmp(&before,&s,sizeof s));const uint8_t black[3]={0,0,0};at(&s,p,0,167,black);CHECK(s.color_rgb[0]==(p->special==0x27?1:0)&&!s.color_rgb[1]&&!s.color_rgb[2]);const uint8_t white[3]={255,255,255};at(&s,p,0,167,white);CHECK(s.color_rgb[0]==(p->special==0x27?255:188)&&s.color_rgb[1]==(p->special==0x27?197:249)&&s.color_rgb[2]==(p->special==0x27?196:194));
}
static void negative(const uint8_t b[1024]){
 uint8_t bad[1025];char error[160];C3WzxStatDownProgram p={0},before;CHECK(c3_wzx_stat_down_decode(&p,b,1024,error,sizeof error));before=p;
 for(unsigned i=0;i<1024;++i){memcpy(bad,b,1024);bad[i]^=1;CHECK(!c3_wzx_stat_down_decode(&p,bad,1024,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));}
 for(unsigned i=0;i<1024;i+=4){if(i>=236&&i<256)continue;memcpy(bad,b,1024);bad[i]^=1;checksum(bad);CHECK(!c3_wzx_stat_down_decode(&p,bad,1024,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));}
 CHECK(!c3_wzx_stat_down_decode(&p,b,1023,error,sizeof error));CHECK(!c3_wzx_stat_down_decode(&p,bad,1025,error,sizeof error));CHECK(!c3_wzx_stat_down_load(&p,"/not/a/file",error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));C3WzxStatDown s;const uint8_t prior[3]={100,120,140};CHECK(c3_wzx_stat_down_begin(&s,&p,1,prior));c3_wzx_stat_down_program_close(&p);CHECK(c3_wzx_stat_down_step(&s,4000)&&s.owner_minimum_complete);c3_wzx_stat_down_cancel(&s);c3_wzx_stat_down_program_close(&p);
}
static void mixed(const uint8_t green[1024],const uint8_t red[1024]){
 const unsigned regions[][2]={{12,4},{20,4},{52,32},{84,32},{116,20},{136,4},{144,4},{196,4},{200,4},{448,192},{640,192},{832,192}};char error[160];
 for(unsigned direction=0;direction<2;++direction){const uint8_t *source=direction?red:green,*other=direction?green:red;C3WzxStatDownProgram p={0},before;CHECK(c3_wzx_stat_down_decode(&p,source,1024,error,sizeof error));before=p;
  for(unsigned i=0;i<sizeof regions/sizeof regions[0];++i){uint8_t b[1024];memcpy(b,source,1024);memcpy(b+regions[i][0],other+regions[i][0],regions[i][1]);checksum(b);CHECK(!c3_wzx_stat_down_decode(&p,b,1024,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));}
 }
}
int main(int argc,char **argv){uint8_t green[1024],red[1024];char error[160];descriptor(green);red_descriptor(red);C3WzxStatDownProgram p={0};
 for(unsigned variant=0;variant<2;++variant){const uint8_t *b=variant?red:green;CHECK(c3_wzx_stat_down_decode(&p,b,1024,error,sizeof error));CHECK(p.special==(variant?0x27u:0x29u)&&p.stat_id==(variant?1u:2u));CHECK(p.sequence_flags==(variant?0x78u:0)&&p.attachment_flags==(variant?0u:2u)&&p.particle_mode==(variant?1u:0xA1u));CHECK(p.sound_id==(variant?0x19Au:0x19Bu));clocks(&p);negative(b);}
 mixed(green,red);
 for(int arg=1;arg<argc;++arg){CHECK(c3_wzx_stat_down_load(&p,argv[arg],error,sizeof error));clocks(&p);FILE *f=fopen(argv[arg],"rb");CHECK(f);uint8_t bytes[1024];CHECK(fread(bytes,1,sizeof bytes,f)==sizeof bytes);CHECK(fclose(f)==0);CHECK(!memcmp(bytes,p.special==0x27?red:green,1024));/* GREENv1 unchanged; actualRED matchesitsseparatetemplate. */CHECK(c3_wzx_stat_down_load(&p,argv[arg],error,sizeof error));c3_wzx_stat_down_program_close(&p);CHECK(!p.owner_rate);}
 printf("Stat-down RED/GREEN %u checks passed\n",checks);return 0;}
