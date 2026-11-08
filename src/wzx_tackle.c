#include "colosseum3ds/wzx_tackle.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const uint8_t *p){uint32_t n=word(p);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int hexmatch(const uint8_t *p,const char *s,size_t n){for(size_t i=0;i<n;++i){unsigned a=(unsigned)(s[i*2]<='9'?s[i*2]-'0':s[i*2]-'a'+10),b=(unsigned)(s[i*2+1]<='9'?s[i*2+1]-'0':s[i*2+1]-'a'+10);if(p[i]!=(uint8_t)(a*16+b))return 0;}return 1;}
static int valid(const C3WzxTackleProgram *p){
 static const float anchors[2][3]={{1.492099244160272e-08f,.3874979615211487f,.2519835829734802f},{.08478743582963943f,.3640747368335724f,.13167962431907654f}};
 if(!p||p->attack_start!=130||p->attack_stop!=133||p->attack_sound_id!=0x4A2||p->damage_sound_id!=0xDE||p->particle_flags!=2||p->bank_flags!=1||p->root_generator||p->hp_start||p->owner_rate!=.5f||p->world_scale!=.75f||!hexmatch(p->particles_sha1,"34e7da926099fcdcfdee31a56d0be2e354f9ad27",20)||!hexmatch(p->caster_pkx_sha1,"69d22676ac5b639f22ae0032fb6a07ac3e1478bb",20)||!hexmatch(p->caster_hsd_sha1,"d33d3d404433a48d7cc7863ac365178c47506d62",20))return 0;
 for(unsigned i=0;i<2;++i){const C3WzxTackleOwner *o=p->owners+i;
  if(o->species!=(i?196u:197u)||o->slot!=i||o->owner_clip!=(i?2u:3u)||o->owner_normal!=(i?100u:170u)||o->owner_minimum!=(i?176u:298u)||o->body_part!=(i?62u:42u)||o->owner_end!=(i?88u:149u)||!hexmatch(o->pkx_sha1,i?"8755031c912d7d965ec352b20cd11d9385ef0250":"6ef86a8fadf7c8b97aeb86d602b88c6ad1adc838",20)||!hexmatch(o->hsd_sha1,i?"d1fd5ff047537d95f3291119ccfdcdf786b22840":"a0121db064f81bac15efda57d805573e5a3e87f0",20))return 0;
  for(unsigned j=0;j<3;++j)if(!isfinite(o->anchor[j])||o->anchor[j]!=anchors[i][j])return 0;
 }return 1;
}
int c3_wzx_tackle_decode(C3WzxTackleProgram *out,const void *data,size_t bytes,char *e,size_t cap){
 const uint8_t *b=data;C3WzxTackleProgram p={0};uint8_t digest[20];
 if(!out||!b||bytes!=1024||memcmp(b,"C3TK",4)||word(b+4)!=1||word(b+8)!=1024||word(b+12)!=33||
 !hexmatch(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20)||
 !hexmatch(b+52,"5754c2ac8e4e3caef0bad6486d7cb6a06f91869daae4e2e336a969fb65318439",32)||
 !hexmatch(b+84,"c144e8784d610c38960b67b5dcb4c1776ff25ec0195df0e0b3c4b2ea06951f97",32)||
 !hexmatch(b+116,"40269fb5a7111267999a9e9d9fd72f18c24a4b54a9b452267c782b7123f8bdef",32)||
 word(b+176)!=91680||word(b+184)!=60||word(b+188)!=2||word(b+192)!=4||word(b+196)!=120||word(b+200)!=240||word(b+204)!=53)return fail(e,cap,"Unsupported Tackle source/profile.");
 c3_save_codec_sha1(digest,b,1004);if(memcmp(digest,b+1004,20))return fail(e,cap,"Tackle descriptor checksum mismatch.");
 for(unsigned i=216;i<256;++i)if(b[i])return fail(e,cap,"Unknown Tackle header extension.");
 for(unsigned i=492;i<512;++i)if(b[i])return fail(e,cap,"Unknown Tackle owner extension.");
 for(unsigned i=960;i<1004;++i)if(b[i])return fail(e,cap,"Unknown Tackle extension.");
 for(unsigned i=0;i<4;++i)for(unsigned j=0;j<28;++j){uint32_t expected=0;
  if(j==0)expected=(i&1)+1;
  if(j==1)expected=i==0?1:i==2?3:5;
  if(j==2&&i==2)expected=2;
  if(j==3&&i==2)expected=1;
  if(j==4&&(i&1))expected=1;
  if(j==6&&i==0)expected=2;
  if(j==7||j==26)expected=2;
  if(j==24)expected=(i&1)?1:i==2?2:0;
  if(word(b+512+i*112+j*4)!=expected)return fail(e,cap,"Tackle typed node table changed.");
 }
 p.attack_start=word(b+16);p.attack_stop=word(b+20);p.attack_sound_id=word(b+24);p.damage_sound_id=word(b+28);
 memcpy(p.particles_sha1,b+148,20);p.particle_flags=word(b+168);p.root_generator=word(b+172);p.hp_start=word(b+180);p.bank_flags=word(b+488);p.owner_rate=real(b+208);p.world_scale=real(b+212);
 memcpy(p.caster_pkx_sha1,b+448,20);memcpy(p.caster_hsd_sha1,b+468,20);
 for(unsigned i=0;i<2;++i){const uint8_t *r=b+256+i*96;C3WzxTackleOwner *o=p.owners+i;
  o->species=word(r);o->slot=word(r+4);o->owner_clip=word(r+8);o->owner_normal=word(r+12);o->owner_minimum=word(r+16);o->body_part=word(r+20);o->owner_end=word(r+24);
  if(word(r+28)!=8)return fail(e,cap,"Tackle owner entry changed.");
  memcpy(o->pkx_sha1,r+32,20);memcpy(o->hsd_sha1,r+52,20);for(unsigned j=0;j<3;++j)o->anchor[j]=real(r+72+j*4);
  for(unsigned j=84;j<96;++j)if(r[j])return fail(e,cap,"Unknown Tackle target extension.");
 }
 if(!valid(&p))return fail(e,cap,"Unsupported Tackle timing/owner profile.");
 *out=p;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_tackle_load(C3WzxTackleProgram *out,const char *path,char *e,size_t cap){
 uint8_t b[1024];if(!out||!path)return fail(e,cap,"Missing Tackle descriptor path.");FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Tackle descriptor.");size_t n=fread(b,1,sizeof b,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;if(bad||n!=sizeof b||extra!=EOF)return fail(e,cap,"Invalid Tackle descriptor length.");return c3_wzx_tackle_decode(out,b,sizeof b,e,cap);
}
int c3_wzx_tackle_begin(C3WzxTackle *s,const C3WzxTackleProgram *p,unsigned target){
 if(!s||!valid(p)||target>=2)return 0;
 C3WzxTackle next={0};next.program=*p;next.active=1;next.phase=1;next.target=(uint8_t)target;*s=next;return 1;
}
int c3_wzx_tackle_step(C3WzxTackle *s,uint32_t ms){
 if(!s||!valid(&s->program))return 0;
 if(!s->active)return 1;
 uint64_t advance=(uint64_t)ms*60,bound=(uint64_t)C3_WZX_TACKLE_MAX_TICKS*1000;
 uint64_t used=s->units+(s->phase==1?s->handoff_units:0);
 if(used>bound||advance>bound-used)return 0;
 uint64_t next=s->units+advance;
 if(s->phase==1){if(next>=130000)s->attack_sound_requests=1;if(next>=133000){s->attack_stops=1;s->attack_sound_stops=s->attack_sound_requests;s->attack_complete=1;s->handoff_units+=next-133000;next=133000;}}
 s->units=next;
 uint64_t actor=next;
 if(s->phase==2&&actor>(uint64_t)s->program.owners[s->target].owner_minimum*1000)actor=(uint64_t)s->program.owners[s->target].owner_minimum*1000;
 s->owner_seconds=(float)((double)actor/60000.)*s->program.owner_rate;return 1;
}
int c3_wzx_tackle_defend(C3WzxTackle *s,int hit){
 if(!s||!valid(&s->program)||!s->active||s->phase!=1||!s->attack_complete||(hit!=0&&hit!=1))return 0;
 if(!hit){s->miss=1;s->active=0;s->completed=1;return 1;}
 s->phase=2;s->units=s->handoff_units;s->defender_starts=1;s->reset_except_requests=1;s->particle_starts=1;s->damage_sound_requests=1;s->hp_gates=1;
 return c3_wzx_tackle_step(s,0);
}
void c3_wzx_tackle_cancel(C3WzxTackle *s){if(s&&s->active){if(s->particle_starts)s->family_stop_requests=1;if(s->attack_sound_requests)s->attack_sound_stops=1;if(s->damage_sound_requests)s->damage_sound_stops=1;s->active=0;}}
int c3_wzx_tackle_finish(C3WzxTackle *s,int particles_done,int sound_done,int motion_busy,int sequence_enabled){
 if(!s||!valid(&s->program))return 0;
 if(s->completed)return 1;
 if(!s->active||s->phase!=2)return 0;
 const C3WzxTackleOwner *o=s->program.owners+s->target;
 int early=!motion_busy&&sequence_enabled&&s->units>(uint64_t)o->owner_normal*1000;
 if(!early&&(s->units<(uint64_t)o->owner_minimum*1000||!particles_done||!sound_done))return 0;
 c3_wzx_tackle_cancel(s);s->completed=1;return 1;
}
