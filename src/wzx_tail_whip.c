#include "colosseum3ds/wzx_tail_whip.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const uint8_t *p){uint32_t n=word(p);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int hex(const uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[2*i],s[2*i+1],0};if(b[i]!=(uint8_t)strtoul(t,NULL,16))return 0;}return 1;}
static int zero(const uint8_t *b,size_t n){for(size_t i=0;i<n;++i)if(b[i])return 0;return 1;}
static int valid(const C3WzxTailWhipProgram *p){return p&&p->owner_gate==98&&p->sound_id==0x1DB&&p->model_rate==.5f&&p->class_scale==.75f&&
 hex(p->controller_sha1,"1e9b7dd524831d4acf7001ff6414e537382dc962",20)&&hex(p->effect_sha1,"76924978b8263711aa6d00f4b18066f2f6c69f93",20)&&hex(p->owner_sha1,"d33d3d404433a48d7cc7863ac365178c47506d62",20)&&
 p->center[0]==.007567313605f&&p->center[1]==.276132727930f&&p->center[2]==-.108336426373f;}
static int checksum(const uint8_t *b,size_t n,size_t at){uint8_t copy[C3_WZX_TAIL_WHIP_MOTION_BYTES],digest[20];if(n>sizeof copy)return 0;memcpy(copy,b,n);memset(copy+at,0,20);c3_save_codec_sha1(digest,copy,n);return !memcmp(digest,b+at,20);}
int c3_wzx_tail_whip_decode(C3WzxTailWhipProgram *out,const void *data,size_t n,char *e,size_t cap){
 const uint8_t *b=data;C3WzxTailWhipProgram p={0};
 if(!out||!b||n!=512||memcmp(b,"C3TW",4)||word(b+4)!=1||word(b+8)!=512||word(b+12)!=39||word(b+16)!=288||word(b+20)||word(b+24)!=60||word(b+28)!=3||
 !hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20)||!hex(b+52,"f3dabf6dfd082f2c10f333dcc1afed34f1ea54c454e850f04fe8429a0f6030dc",32))return fail(e,cap,"Unsupported Tail Whip source/profile.");
 if(!checksum(b,n,480))return fail(e,cap,"Tail Whip descriptor checksum mismatch.");
 if(word(b+144)!=0x78||word(b+148)!=2||word(b+160)||word(b+184)||word(b+188)||!zero(b+500,12))return fail(e,cap,"Unknown Tail Whip camera/timing extension.");
 static const uint32_t records[3][24]={
 {1,2,0,4,0,0,0,2,0,0,0xA0,0,31,1,0,0,0,0,0,0,1,1,1,683},
 {2,2,16,1,0,0,0,2,0,0,0x400,0,45,2,0,0,0,0,0,0,0,0,0,137747},
 {3,5,0,0,0,0,0,2,1,0,0x21EC0,0,0,3,0,0,0x1DB,3,0,0,0,0,0,0}};
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<24;++j)if(word(b+192+i*96+j*4)!=records[i][j])return fail(e,cap,"Tail Whip typed node/resource binding mismatch.");
 memcpy(p.controller_sha1,b+84,20);memcpy(p.effect_sha1,b+104,20);memcpy(p.owner_sha1,b+124,20);
 p.sound_id=word(b+152);p.owner_gate=word(b+156);for(unsigned i=0;i<3;++i)p.center[i]=real(b+164+4*i);p.model_rate=real(b+176);p.class_scale=real(b+180);
 if(!valid(&p))return fail(e,cap,"Tail Whip resource/center/rate profile mismatch.");
 c3_wzx_tail_whip_program_close(out);*out=p;if(e&&cap)e[0]=0;return 1;
}
static int load_bytes(const char *path,uint8_t *b,size_t n,char *e,size_t cap){if(!path)return fail(e,cap,"Missing Tail Whip path.");FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Tail Whip resource.");size_t got=fread(b,1,n,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;if(bad||got!=n||extra!=EOF)return fail(e,cap,"Invalid Tail Whip resource length.");return 1;}
int c3_wzx_tail_whip_load(C3WzxTailWhipProgram *p,const char *path,char *e,size_t cap){uint8_t b[512];if(!load_bytes(path,b,sizeof b,e,cap))return 0;return c3_wzx_tail_whip_decode(p,b,sizeof b,e,cap);}
void c3_wzx_tail_whip_program_close(C3WzxTailWhipProgram *p){if(p){free(p->motion_data);memset(p,0,sizeof *p);}}
int c3_wzx_tail_whip_motion_decode(C3WzxTailWhipProgram *p,const void *data,size_t n,char *e,size_t cap){
 const uint8_t *b=data;uint8_t digest[20];
 if(!valid(p)||!b||n!=6176||memcmp(b,"C3TM",4)||word(b+4)!=1||word(b+8)!=n||word(b+12)!=39||word(b+16)!=2||word(b+20)!=63||word(b+24)!=60||word(b+28)!=1||
 !hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20)||!hex(b+52,"0582c1f2a2cb0441da2b9ba08634a7dd788aae0892fec21040cc02ed9ce068b1",32)||!zero(b+124,4))return fail(e,cap,"Tail Whip controller bounds/source mismatch.");
 if(!checksum(b,n,104))return fail(e,cap,"Tail Whip controller checksum mismatch.");
 c3_save_codec_sha1(digest,b+128,n-128);if(memcmp(digest,b+84,20))return fail(e,cap,"Tail Whip controller track checksum mismatch.");
 /* Source GSpartGetTransform adds ancestor Euler angles. Both admitted joint0
  * and joint1 are pure yaw, unit scale, zero translation: atan2 retains exactly
  * that reached rotation. Any other transform requires a new qualification. */
 for(unsigned frame=0;frame<63;++frame)for(unsigned joint=0;joint<2;++joint){
  float a[12];for(unsigned k=0;k<12;++k){a[k]=real(b+128+frame*96+joint*48+k*4);if(!isfinite(a[k]))return fail(e,cap,"Nonfinite Tail Whip controller transform.");}
  const float eps=2e-6f;
  if(fabsf(a[1])>eps||fabsf(a[3])>eps||fabsf(a[4])>eps||fabsf(a[5]-1)>eps||fabsf(a[6])>eps||fabsf(a[7])>eps||fabsf(a[9])>eps||fabsf(a[11])>eps||fabsf(a[0]-a[10])>eps||fabsf(a[2]+a[8])>eps||fabsf(a[0]*a[0]+a[2]*a[2]-1)>eps||fabsf(atan2f(a[2],a[0]))>.27f||(!joint&&(fabsf(a[0]-1)>eps||fabsf(a[2])>eps)))return fail(e,cap,"Unsupported Tail Whip translation/pitch/roll/scale controller profile.");
 }
 c3_save_codec_sha1(digest,b,n);if(memcmp(digest,p->controller_sha1,20))return fail(e,cap,"Tail Whip descriptor/controller binding checksum mismatch.");
 uint8_t *copy=malloc(n);if(!copy)return fail(e,cap,"Tail Whip controller allocation failed.");memcpy(copy,b,n);free(p->motion_data);p->motion_data=copy;p->motion_bytes=n;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_tail_whip_motion_load(C3WzxTailWhipProgram *p,const char *path,char *e,size_t cap){uint8_t b[6176];if(!load_bytes(path,b,sizeof b,e,cap))return 0;return c3_wzx_tail_whip_motion_decode(p,b,sizeof b,e,cap);}
int c3_wzx_tail_whip_begin(C3WzxTailWhip *out,const C3WzxTailWhipProgram *p,const float root[3],float yaw,float scale){
 if(!out||!valid(p)||!p->motion_data||p->motion_bytes!=6176||!root||!isfinite(yaw)||!isfinite(scale)||scale<=0||scale>1000)return 0;
 for(unsigned i=0;i<3;++i)if(!isfinite(root[i])||fabsf(root[i])>100000)return 0;
 C3WzxTailWhip s={0};s.program=*p;memcpy(s.root,root,sizeof s.root);s.root_yaw=yaw;s.actor_scale=scale;s.active=s.owner_attached=s.effect_visible=1;s.controller_starts=s.effect_starts=s.sound_requests=1;*out=s;return 1;
}
int c3_wzx_tail_whip_step(C3WzxTailWhip *s,uint32_t ms){if(!s||!valid(&s->program))return 0;if(!s->active)return 1;uint64_t bound=(uint64_t)C3_WZX_TAIL_WHIP_MAX_TICKS*1000,advance=(uint64_t)ms*60;if(s->units>bound||advance>bound-s->units)return 0;s->units+=advance;
 /* Loader reserves track0 kind1/value0; fn801DD100 writes normalization0
  * to entry.field90. MotionStart801D9C1C duration0 calls fn801DF070,
  * which explicitly sets frame0 and rate lbl8047E3C8=.5. Gate98 is separate. */
 double seconds=(double)s->units/60000.;s->controller_seconds=(float)fmin(seconds*.5,31./60.);s->effect_seconds=(float)fmin(seconds*.5,45./60.);s->owner_seconds=(float)(seconds*.5);s->owner_gate_complete=s->units>=(uint64_t)s->program.owner_gate*1000;return 1;}
int c3_wzx_tail_whip_signals(C3WzxTailWhip *s,int controller,int effect,int audio){if(!s||!valid(&s->program))return 0;if(!s->active)return 1;if(controller){s->controller_ended=1;s->owner_attached=0;}if(effect){s->effect_ended=1;s->effect_visible=0;}if(audio)s->audio_done=1;return 1;}
int c3_wzx_tail_whip_finish(C3WzxTailWhip *s){if(!s||!valid(&s->program))return 0;if(s->completed)return 1;if(!s->active||!s->owner_gate_complete||!s->controller_ended||!s->effect_ended||!s->audio_done)return 0;c3_wzx_tail_whip_cancel(s);s->completed=1;return 1;}
void c3_wzx_tail_whip_cancel(C3WzxTailWhip *s){if(s){s->active=0;s->owner_attached=0;s->effect_visible=0;}}
int c3_wzx_tail_whip_pose(const C3WzxTailWhip *s,C3WzxTailWhipPose *out,char *e,size_t cap){if(!s||!out||!valid(&s->program)||!s->program.motion_data||s->program.motion_bytes!=6176)return fail(e,cap,"Missing Tail Whip controller state/track.");
 C3WzxTailWhipPose p={0};memcpy(p.owner_position,s->root,sizeof p.owner_position);p.owner_yaw=s->root_yaw;p.owner_scale=s->actor_scale;
 if(s->owner_attached){double frame=(double)s->units/1000.;if(frame>62)frame=62;unsigned a=(unsigned)frame,b=a<62?a+1:a;float fraction=(float)(frame-a);const uint8_t *x=s->program.motion_data+128+a*96+48,*y=s->program.motion_data+128+b*96+48;float angle_a=atan2f(real(x+8),real(x)),angle_b=atan2f(real(y+8),real(y));p.owner_yaw+=angle_a+(angle_b-angle_a)*fraction;}
 float c=cosf(p.owner_yaw),sn=sinf(p.owner_yaw);const float *v=s->program.center;
 p.effect_position[0]=p.owner_position[0]+s->actor_scale*(c*v[0]+sn*v[2]);p.effect_position[1]=p.owner_position[1]+s->actor_scale*v[1];p.effect_position[2]=p.owner_position[2]+s->actor_scale*(-sn*v[0]+c*v[2]);p.effect_yaw=s->root_yaw;p.effect_scale=s->program.class_scale;
 *out=p;if(e&&cap)e[0]=0;return 1;}
