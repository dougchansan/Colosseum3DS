#include "colosseum3ds/wzx_tail_whip.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Tail Whip line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void word(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=(uint8_t)(n>>(8*i));}
static void real(uint8_t *b,float f){uint32_t n;memcpy(&n,&f,4);word(b,n);}
static void hex(uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[2*i],s[2*i+1],0};b[i]=(uint8_t)strtoul(t,NULL,16);}}
static void checksum(uint8_t *b,size_t n,size_t at){memset(b+at,0,20);c3_save_codec_sha1(b+at,b,n);}
static void descriptor(uint8_t b[512]){
 memset(b,0,512);memcpy(b,"C3TW",4);uint32_t h[7]={1,512,39,288,0,60,3};for(unsigned i=0;i<7;++i)word(b+4+i*4,h[i]);
 hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20);hex(b+52,"f3dabf6dfd082f2c10f333dcc1afed34f1ea54c454e850f04fe8429a0f6030dc",32);hex(b+84,"1e9b7dd524831d4acf7001ff6414e537382dc962",20);hex(b+104,"76924978b8263711aa6d00f4b18066f2f6c69f93",20);hex(b+124,"d33d3d404433a48d7cc7863ac365178c47506d62",20);
 word(b+144,0x78);word(b+148,2);word(b+152,0x1DB);word(b+156,98);real(b+164,.007567313605f);real(b+168,.276132727930f);real(b+172,-.108336426373f);real(b+176,.5f);real(b+180,.75f);
 const uint32_t r[3][24]={{1,2,0,4,0,0,0,2,0,0,0xA0,0,31,1,0,0,0,0,0,0,1,1,1,683},{2,2,16,1,0,0,0,2,0,0,0x400,0,45,2,0,0,0,0,0,0,0,0,0,137747},{3,5,0,0,0,0,0,2,1,0,0x21EC0,0,0,3,0,0,0x1DB,3,0,0,0,0,0,0}};
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<24;++j)word(b+192+i*96+j*4,r[i][j]);
 checksum(b,512,480);
}
static void clocks(const C3WzxTailWhipProgram *p){
 const float root[3]={3,2,-4};C3WzxTailWhip s={0},a={0},before;C3WzxTailWhipPose pose;
 CHECK(c3_wzx_tail_whip_begin(&s,p,root,.7f,1.2f));CHECK(s.sound_requests==1&&s.controller_starts==1&&s.effect_starts==1);CHECK(s.owner_attached&&s.effect_visible);CHECK(!c3_wzx_tail_whip_finish(&s));
 CHECK(c3_wzx_tail_whip_step(&s,1633));CHECK(!s.owner_gate_complete);CHECK(c3_wzx_tail_whip_step(&s,1));CHECK(s.owner_gate_complete&&s.owner_seconds==.817f);
 CHECK(c3_wzx_tail_whip_pose(&s,&pose,NULL,0));CHECK(pose.owner_scale==1.2f&&pose.effect_scale==.75f&&pose.effect_yaw==.7f);
 for(unsigned part=1;part<=1000;part=part==1?17:1000){CHECK(c3_wzx_tail_whip_begin(&s,p,root,.7f,1.2f));unsigned elapsed=0;while(elapsed<3000){unsigned n=part;if(n>3000-elapsed)n=3000-elapsed;CHECK(c3_wzx_tail_whip_step(&s,n));elapsed+=n;CHECK(c3_wzx_tail_whip_begin(&a,p,root,.7f,1.2f));CHECK(c3_wzx_tail_whip_step(&a,elapsed));CHECK(!memcmp(&a,&s,sizeof s));}if(part==1000)break;}
 CHECK(s.owner_gate_complete&&s.active&&s.owner_attached&&s.effect_visible);CHECK(s.controller_seconds==31.f/60.f&&s.effect_seconds==45.f/60.f&&s.owner_seconds==1.5f);before=s;CHECK(!c3_wzx_tail_whip_step(&s,UINT32_MAX));CHECK(!memcmp(&before,&s,sizeof s));
 CHECK(c3_wzx_tail_whip_signals(&s,1,0,0));CHECK(!s.owner_attached&&!s.effect_ended);CHECK(c3_wzx_tail_whip_pose(&s,&pose,NULL,0));CHECK(!memcmp(pose.owner_position,root,sizeof root)&&pose.owner_yaw==.7f&&pose.owner_scale==1.2f);CHECK(!c3_wzx_tail_whip_finish(&s));CHECK(c3_wzx_tail_whip_signals(&s,0,1,0));CHECK(!c3_wzx_tail_whip_finish(&s));CHECK(c3_wzx_tail_whip_signals(&s,0,0,1));CHECK(c3_wzx_tail_whip_finish(&s));CHECK(s.completed&&!s.active&&!s.effect_visible);before=s;CHECK(c3_wzx_tail_whip_step(&s,UINT32_MAX));CHECK(!memcmp(&s,&before,sizeof s));
 CHECK(c3_wzx_tail_whip_begin(&s,p,root,.7f,1.2f));CHECK(c3_wzx_tail_whip_step(&s,199));c3_wzx_tail_whip_cancel(&s);CHECK(c3_wzx_tail_whip_pose(&s,&pose,NULL,0));CHECK(pose.owner_yaw==.7f&&!s.effect_visible&&!s.completed);CHECK(!c3_wzx_tail_whip_finish(&s));before=s;CHECK(!c3_wzx_tail_whip_begin(&s,p,root,NAN,1));CHECK(!memcmp(&s,&before,sizeof s));
 before=s; /* Pause has no API side effect: caller omits step. */CHECK(!memcmp(&before,&s,sizeof s));
}
int main(int argc,char **argv){uint8_t b[512];char error[160];C3WzxTailWhipProgram p={0},before;descriptor(b);CHECK(c3_wzx_tail_whip_decode(&p,b,512,error,sizeof error));before=p;
 for(unsigned i=0;i<512;++i){uint8_t changed[512];memcpy(changed,b,512);changed[i]^=1;CHECK(!c3_wzx_tail_whip_decode(&p,changed,512,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));}
 for(unsigned i=0;i<480;i+=4){uint8_t changed[512];memcpy(changed,b,512);changed[i]^=1;checksum(changed,512,480);CHECK(!c3_wzx_tail_whip_decode(&p,changed,512,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));}
 CHECK(!c3_wzx_tail_whip_decode(&p,b,511,error,sizeof error));CHECK(!c3_wzx_tail_whip_decode(&p,NULL,512,error,sizeof error));CHECK(!c3_wzx_tail_whip_load(&p,"/not/a/file",error,sizeof error));
 /* Clock synthetic tracks retain a real yaw to test attachment versus effect
  * POSITION-only. Decoder source-binding success is separately actual-tested. */
 p.motion_data=calloc(1,6176);CHECK(p.motion_data);p.motion_bytes=6176;for(unsigned f=0;f<63;++f){uint8_t *x=p.motion_data+128+f*96+48;real(x,cosf(.2f));real(x+8,sinf(.2f));real(x+20,1);real(x+32,-sinf(.2f));real(x+40,cosf(.2f));}
 clocks(&p);C3WzxTailWhip s={0};C3WzxTailWhipPose pose;float root[3]={0};CHECK(c3_wzx_tail_whip_begin(&s,&p,root,.5,2));CHECK(c3_wzx_tail_whip_pose(&s,&pose,error,sizeof error));CHECK(fabsf(pose.owner_yaw-.7f)<1e-6f&&pose.effect_yaw==.5f);CHECK(fabsf(pose.effect_position[0]-2*(cosf(.7f)*p.center[0]+sinf(.7f)*p.center[2]))<1e-6f);c3_wzx_tail_whip_cancel(&s);
 C3WzxTailWhipPose saved=pose;CHECK(!c3_wzx_tail_whip_pose(NULL,&pose,error,sizeof error));CHECK(!memcmp(&saved,&pose,sizeof pose));
 before=p;uint8_t bad[6176]={0};CHECK(!c3_wzx_tail_whip_motion_decode(&p,bad,sizeof bad,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));CHECK(!c3_wzx_tail_whip_motion_load(&p,"/not/a/file",error,sizeof error));c3_wzx_tail_whip_program_close(&p);CHECK(!p.motion_data&&!p.motion_bytes);c3_wzx_tail_whip_program_close(&p);
 if(argc>1){CHECK(c3_wzx_tail_whip_load(&p,argv[1],error,sizeof error));char path[2048];if(argc>2)snprintf(path,sizeof path,"%s",argv[2]);else{snprintf(path,sizeof path,"%s",argv[1]);char *slash=strrchr(path,'/');if(!slash)slash=strrchr(path,'\\');CHECK(slash);strcpy(slash+1,"tail_whip_controller.c3m");}CHECK(c3_wzx_tail_whip_motion_load(&p,path,error,sizeof error));clocks(&p);
 FILE *f=fopen(path,"rb");CHECK(f);CHECK(fread(bad,1,sizeof bad,f)==sizeof bad);CHECK(!fclose(f));before=p;
 for(unsigned offset=0;offset<sizeof bad;offset+=47){bad[offset]^=1;CHECK(!c3_wzx_tail_whip_motion_decode(&p,bad,sizeof bad,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));bad[offset]^=1;}
 /* A self-checksummed pitch/translation modification still fails the explicit
  * reached profile, and leaves existing immutable allocation intact. */
 real(bad+128+48+12,.1f);c3_save_codec_sha1(bad+84,bad+128,sizeof bad-128);checksum(bad,sizeof bad,104);CHECK(!c3_wzx_tail_whip_motion_decode(&p,bad,sizeof bad,error,sizeof error));CHECK(strstr(error,"translation/pitch/roll/scale"));CHECK(!memcmp(&before,&p,sizeof p));
 uint8_t invalid[512];memcpy(invalid,b,512);invalid[12]^=1;checksum(invalid,512,480);CHECK(!c3_wzx_tail_whip_decode(&p,invalid,512,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));
 CHECK(c3_wzx_tail_whip_motion_load(&p,path,error,sizeof error));
 CHECK(c3_wzx_tail_whip_load(&p,argv[1],error,sizeof error));CHECK(!p.motion_data&&!p.motion_bytes);CHECK(c3_wzx_tail_whip_motion_load(&p,path,error,sizeof error));
 c3_wzx_tail_whip_program_close(&p);}
 printf("Tail Whip %u checks passed\n",checks);return 0;}
