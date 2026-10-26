#include "colosseum3ds/wzx_growl.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static float real(const uint8_t *b){uint32_t n=word(b);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int hex(const uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[2*i],s[2*i+1],0};if(b[i]!=(uint8_t)strtoul(t,NULL,16))return 0;}return 1;}
static const uint32_t payloads[5][19]={
 {0,0,0,0,0,0,1,64851},{18,0x10A1,365764,4},{0x210,3,0},{0,100,3},
 {0,120,2,1,3,3,0,0x80808080,0x802D2D2D,20,1,0x802D2D2D,0x802D2D2D,50,1,0x802D2D2D,0x80808080,50,1}};
static const uint32_t records[5][12]={
 {1,2,130,1,1,0,0,1,2,2,0,0},{2,3,130,0,1,1,0,0,2,3,9,0},{3,5,130,0,0,1,0,0,2,1,0,0},{4,1,130,0,0,0,0,1,2,0,0,0},{5,4,90,0,0,0,1,1,3,0,0,0}};
static int valid(const C3WzxGrowlProgram *p){
 if(!p||p->model_rate!=.5f||p->class_scale!=.75f||p->owner_part!=58||p->model_part!=9||p->retained_bone!=22||p->owner_start||p->owner_normal!=244||p->owner_minimum!=366||p->stop_tick!=230||p->model_end!=39||p->owner_end!=183||p->floor_start!=90||p->floor_duration!=120||p->model_start!=130||p->particle_start!=130||p->sound_start!=130||p->control_start!=130||p->control_delay!=100||p->root_generator!=18||
 !hex(p->model_sha1,"c9df90e70c02580843f17988a99c48839de47776",20)||!hex(p->particles_sha1,"66078c124021cd7ad7eb71a801ef713136b952ea",20)||!hex(p->owner_sha1,"d33d3d404433a48d7cc7863ac365178c47506d62",20))return 0;
 const float owner[3]={-.01756220869719982f,.0642678514122963f,-.5523711442947388f},emitter[3]={-.029487334191799164f,-.01608329452574253f,.006553320679813623f};
 for(unsigned i=0;i<3;++i)if(p->owner_anchor[i]!=owner[i]||p->emitter_anchor[i]!=emitter[i])return 0;
 for(unsigned i=0;i<5;++i){const C3WzxGrowlNode *n=p->nodes+i;uint32_t fields[12]={n->key,n->kind,n->start,n->attachment,n->position_type,n->link,n->source,n->target,n->timing_index,n->flags,n->part,n->state};
  if(memcmp(fields,records[i],sizeof fields)||memcmp(n->resource,payloads[i],sizeof n->resource))return 0;
  for(unsigned j=0;j<16;++j){uint32_t expected=i==0&&j==1?39:i==4&&j==1?40:i==4&&j==2?220:0;if(n->timing[j]!=expected)return 0;}}
 return 1;
}
int c3_wzx_growl_decode(C3WzxGrowlProgram *out,const void *data,size_t bytes,char *e,size_t cap){
 const uint8_t *b=data;uint8_t copy[1280],digest[20];C3WzxGrowlProgram p={0};
 if(!out||!b||bytes!=1280||memcmp(b,"C3WG",4)||word(b+4)!=1||word(b+8)!=1280||word(b+12)!=45||word(b+16)!=288||word(b+20)!=1||word(b+24)!=1||word(b+28)!=60||
 !hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20)||!hex(b+52,"85862ce50519fc694b179761e5038bab74f4bb2ce9c15b7102a0b0937ef8f82e",32)||!hex(b+144,"64867627ce7029307e3064e1af883cafb52995b7e2834980d7722d328ef043da",32))return fail(e,cap,"Unsupported Growl source/profile.");
 memcpy(copy,b,bytes);memset(copy+300,0,20);c3_save_codec_sha1(digest,copy,bytes);if(memcmp(digest,b+300,20))return fail(e,cap,"Growl descriptor checksum mismatch.");
 if(word(b+236)!=5||word(b+240)!=320||word(b+252)!=79)return fail(e,cap,"Growl node/sample bounds mismatch.");
 for(unsigned i=288;i<300;++i)if(b[i])return fail(e,cap,"Unknown Growl descriptor extension.");
 memcpy(p.model_sha1,b+84,20);memcpy(p.particles_sha1,b+104,20);memcpy(p.owner_sha1,b+124,20);
 p.model_rate=real(b+176);p.class_scale=real(b+180);p.owner_part=word(b+184);p.model_part=word(b+188);p.retained_bone=word(b+192);p.owner_start=word(b+196);
 for(unsigned i=0;i<3;++i){p.owner_anchor[i]=real(b+200+4*i);p.emitter_anchor[i]=real(b+212+4*i);}
 p.owner_normal=word(b+224);p.owner_minimum=word(b+228);p.stop_tick=word(b+232);p.model_end=word(b+244);p.owner_end=word(b+248);
 p.floor_start=word(b+256);p.floor_duration=word(b+260);p.model_start=word(b+264);p.particle_start=word(b+268);p.sound_start=word(b+272);p.control_start=word(b+276);p.control_delay=word(b+280);p.root_generator=word(b+284);
 for(unsigned i=0;i<5;++i){const uint8_t *r=b+320+i*192;C3WzxGrowlNode *n=p.nodes+i;
  n->key=word(r);n->kind=word(r+4);n->start=word(r+8);n->attachment=word(r+12);n->position_type=word(r+16);n->link=word(r+20);n->source=word(r+24);n->target=word(r+28);n->timing_index=word(r+32);n->flags=word(r+36);n->part=word(r+40);n->state=word(r+44);
  for(unsigned j=0;j<16;++j)n->timing[j]=word(r+48+j*4);
  for(unsigned j=0;j<19;++j)n->resource[j]=word(r+112+j*4);
  if(word(r+188))return fail(e,cap,"Unknown Growl node extension.");}
 if(!valid(&p))return fail(e,cap,"Growl typed node/timing/resource/anchor profile mismatch.");
 *out=p;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_growl_load(C3WzxGrowlProgram *out,const char *path,char *e,size_t cap){uint8_t b[1280];if(!out||!path)return fail(e,cap,"Missing Growl path/output.");FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Growl descriptor.");size_t n=fread(b,1,sizeof b,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;if(bad||n!=sizeof b||extra!=EOF)return fail(e,cap,"Invalid Growl descriptor length.");return c3_wzx_growl_decode(out,b,n,e,cap);}
void c3_wzx_growl_program_close(C3WzxGrowlProgram *p){if(p)memset(p,0,sizeof *p);}
int c3_wzx_growl_begin(C3WzxGrowl *out,const C3WzxGrowlProgram *p){if(!out||!valid(p))return 0;C3WzxGrowl s={0};s.program=*p;s.active=1;for(unsigned i=0;i<3;++i)s.floor_rgb[i]=128;*out=s;return 1;}
static void stop(C3WzxGrowl *s){s->stop_requested=1;s->stop_requests=s->particle_kills=s->sound_keyoffs=s->restore_requests=1;s->model_visible=s->floor_enabled=0;for(unsigned i=0;i<3;++i)s->floor_rgb[i]=128;}
int c3_wzx_growl_step(C3WzxGrowl *s,uint32_t ms){
 if(!s||!valid(&s->program))return 0;
 if(!s->active||s->stop_requested)return 1;
 uint64_t bound=(uint64_t)C3_WZX_GROWL_MAX_TICKS*1000,advance=(uint64_t)ms*60;if(s->units>bound||advance>bound-s->units)return 0;
 uint64_t next=s->units+advance,stop_units=(uint64_t)s->program.stop_tick*1000;if(next>stop_units)next=stop_units;s->units=next;const C3WzxGrowlProgram *p=&s->program;
 /* Entry90 is normalization0: MotionStart duration0 calls fn801DF070
  * directly with clip1/frame0/rate.5. Do not use NORMAL244 to offset61. */
 s->owner_seconds=(float)((double)next/60000.*.5);s->owner_normal_complete=next>=(uint64_t)p->owner_normal*1000;s->owner_minimum_complete=next>=(uint64_t)p->owner_minimum*1000;
 if(next>=(uint64_t)p->floor_start*1000){s->floor_starts=1;uint64_t age=(next-(uint64_t)p->floor_start*1000)/1000;float v=128;
  if(age<20)v=128+((float)age/20)*(45-128);else if(age<70)v=45;else if(age<120)v=45+((float)(age-70)/50)*(128-45);
  for(unsigned i=0;i<3;++i)s->floor_rgb[i]=(uint8_t)v;
  s->floor_enabled=age<p->floor_duration;}
 if(next>=(uint64_t)p->model_start*1000){s->model_starts=1;uint64_t age=next-(uint64_t)p->model_start*1000,duration=(uint64_t)p->model_end*2000;s->model_finished=age>=duration;if(age>duration)age=duration;s->model_seconds=(float)((double)age/60000.*p->model_rate);s->model_visible=!s->model_finished;}
 if(next>=(uint64_t)p->particle_start*1000)s->particle_starts=1;
 if(next>=(uint64_t)p->sound_start*1000)s->sound_requests=1;
 if(next>=(uint64_t)p->control_start*1000)s->control_starts=1;
 if(next>=stop_units)stop(s);
 return 1;
}
int c3_wzx_growl_acknowledge(C3WzxGrowl *s,int family,C3WzxGrowlAudio audio,int owner,int camera){if(!s||!valid(&s->program)||!s->stop_requested||!family||!owner||!camera||(audio!=C3_GROWL_AUDIO_STOPPED&&audio!=C3_GROWL_AUDIO_FINISHED))return 0;if(s->teardown_acknowledged)return s->audio_status==audio;s->audio_status=audio;s->teardown_acknowledged=1;return 1;}
int c3_wzx_growl_finish(C3WzxGrowl *s){if(!s||!valid(&s->program))return 0;if(s->completed)return 1;if(!s->active||s->cancelled||!s->stop_requested||!s->teardown_acknowledged)return 0;s->active=0;s->completed=1;return 1;}
void c3_wzx_growl_cancel(C3WzxGrowl *s){if(s){if(s->active)stop(s);s->active=0;s->cancelled=1;}}
int c3_wzx_growl_anchors(const C3WzxGrowlProgram *p,const float root[3],float yaw,float scale,float model[3],float emitter[3],char *e,size_t cap){
 if(!valid(p)||!root||!model||!emitter||!isfinite(yaw)||!isfinite(scale)||scale<=0||scale>1000)return fail(e,cap,"Invalid Growl anchor inputs.");
 for(unsigned i=0;i<3;++i)if(!isfinite(root[i])||fabsf(root[i])>100000)return fail(e,cap,"Invalid Growl root.");
 float c=cosf(yaw),s=sinf(yaw),m[3],v[3];const float *a=p->owner_anchor,*b=p->emitter_anchor;
 m[0]=root[0]+scale*(c*a[0]+s*a[2]);m[1]=root[1]+scale*a[1];m[2]=root[2]+scale*(-s*a[0]+c*a[2]);
 v[0]=m[0]+p->class_scale*(c*b[0]+s*b[2]);v[1]=m[1]+p->class_scale*b[1];v[2]=m[2]+p->class_scale*(-s*b[0]+c*b[2]);
 memcpy(model,m,sizeof m);memcpy(emitter,v,sizeof v);if(e&&cap)e[0]=0;return 1;
}
