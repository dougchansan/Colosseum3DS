#include "colosseum3ds/wzx_stat_down.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float real(const uint8_t *p){uint32_t n=word(p);float f;memcpy(&f,&n,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int hex(const uint8_t *b,const char *s,size_t n){for(size_t i=0;i<n;++i){char t[3]={s[i*2],s[i*2+1],0};if(b[i]!=(uint8_t)strtoul(t,NULL,16))return 0;}return 1;}
typedef struct {
 uint32_t special,stat_id,sequence_flags,attachment_flags,mode,sound,color;
 const char *member_sha256,*bank_sha256,*pack_sha1;
 uint32_t records[3][12],payloads[3][19];
} StatProfile;
/* Explicit paired profiles: a RED identity never admits GREEN resources,
 * one-shot attachment or node order (and vice versa). */
static const StatProfile profiles[2]={
 {0x29,2,0,2,0xA1,0x19B,0x80437A3D,
  "5e08695ee116a4a5aaded4deef4fd1af71e5f4178992e187610a7e92a63e7c1e",
  "7f42a1c5a723ed7563e290ede2feb2b1828f90498553feaea96ec1e650bdefeb",
  "daf3b931c7dda451a6c275588735609b92feb25b",
  {{1,3,0,0,1,0,0,0,2,2,0,0},{2,5,0,0,0,1,0,0,2,1,0,0},{3,4,0,0,0,0,0,0,2,1,0,0}},
  {{0,0xA1,1644,4},{0x19B,3,0},{0,50,2,2,3,3,0,0x80808080,0x80437A3D,10,1,0x80437A3D,0x80437A3D,10,1,0x80437A3D,0x80808080,30,1}}},
 {0x27,1,0x78,0,1,0x19A,0x80454681,
  "0e0103bdcf6f2dca317e8ae86cfbc5294de2bcd2d7f1af4bf42c04a01bdf08de",
  "9b54c2d13b9c50949ca13d7fec95ba1d49effb8268490ac1a65d4b04dcd27d97",
  "e81bc82423aa2b31ab7aa49ed08c881a8d630b6f",
  {{1,3,0,0,1,0,0,0,2,0,0,0},{2,4,0,0,0,0,0,0,2,1,0,0},{3,5,0,0,0,0,0,0,2,1,0,0}},
  {{0,1,1644,4},{0,50,2,2,3,3,0,0x80808080,0x80454681,10,1,0x80454681,0x80454681,10,1,0x80454681,0x80808080,30,1},{0x19A,3,0}}}
};
static const StatProfile *profile(uint32_t special){for(unsigned i=0;i<2;++i)if(profiles[i].special==special)return profiles+i;return NULL;}
static int valid(const C3WzxStatDownProgram *p){
 const StatProfile *v=p?profile(p->special):NULL;
 if(!v||p->stat_id!=v->stat_id||p->sequence_flags!=v->sequence_flags||p->attachment_flags!=v->attachment_flags||p->sound_id!=v->sound||p->root_generator||p->particle_mode!=v->mode||p->color_duration!=50||p->target_model!=2||p->class_scale!=.75f||p->owner_rate!=.5f||!hex(p->particles_sha1,v->pack_sha1,20))return 0;
 for(unsigned i=0;i<3;++i)if(p->color_rgb[i]!=(uint8_t)(v->color>>(8*i)))return 0;
 for(unsigned i=0;i<2;++i){const C3WzxStatDownOwner *o=p->owners+i;if(o->species!=(i?196u:197u)||o->slot!=i||o->entry||o->clip!=i||o->authored_end!=(i?90u:59u)||o->minimum!=(i?180u:118u)||o->root_part||o->camera_part!=(i?62u:42u)||!hex(o->owner_sha1,i?"d1fd5ff047537d95f3291119ccfdcdf786b22840":"a0121db064f81bac15efda57d805573e5a3e87f0",20)||!hex(o->member_sha1,i?"8755031c912d7d965ec352b20cd11d9385ef0250":"6ef86a8fadf7c8b97aeb86d602b88c6ad1adc838",20))return 0;for(unsigned j=0;j<3;++j)if(o->root_translation[j]!=0)return 0;}
 for(unsigned i=0;i<3;++i){const C3WzxStatDownNode *n=p->nodes+i;const uint32_t fields[12]={n->key,n->kind,n->start,n->attachment,n->position_type,n->link,n->source,n->target,n->timing_index,n->flags,n->part,n->state};if(memcmp(fields,v->records[i],sizeof fields)||memcmp(n->resource,v->payloads[i],sizeof n->resource))return 0;for(unsigned j=0;j<16;++j)if(n->timing[j])return 0;}
 return 1;
}
int c3_wzx_stat_down_decode(C3WzxStatDownProgram *out,const void *data,size_t bytes,char *e,size_t cap){
 const uint8_t *b=data;uint8_t copy[1024],digest[20];C3WzxStatDownProgram p={0};
 const StatProfile *v=out&&b&&bytes==1024?profile(word(b+12)):NULL;
 if(!v||memcmp(b,"C3SG",4)||word(b+4)!=1||word(b+8)!=1024||word(b+16)!=4||word(b+20)!=v->stat_id||word(b+24)!=2||word(b+28)!=3||!hex(b+32,"51bfe4314a218dfaf9592fc80723c50498d0ee55",20)||!hex(b+52,v->member_sha256,32)||!hex(b+84,v->bank_sha256,32))return fail(e,cap,"Unsupported stat-down source/profile.");
 memcpy(copy,b,bytes);memset(copy+236,0,20);c3_save_codec_sha1(digest,copy,bytes);if(memcmp(digest,b+236,20))return fail(e,cap,"Stat-down descriptor checksum mismatch.");
 if(word(b+148)!=60||word(b+168)||word(b+172)!=0x803763CE||word(b+176)!=448||word(b+180)!=256||word(b+184)!=1644||word(b+188)||word(b+192)!=1||word(b+196)!=v->attachment_flags||word(b+200)!=v->mode)return fail(e,cap,"Stat-down source attachment/table bounds mismatch.");
 for(unsigned i=204;i<236;++i)if(b[i])return fail(e,cap,"Unknown Stat-down descriptor extension.");
 memcpy(p.particles_sha1,b+116,20);p.sound_id=word(b+136);p.root_generator=word(b+140);p.sequence_flags=word(b+144);p.special=v->special;p.stat_id=word(b+20);p.attachment_flags=word(b+196);for(unsigned i=0;i<3;++i)p.color_rgb[i]=(uint8_t)(v->color>>(8*i));p.particle_mode=word(b+200);p.color_duration=word(b+152);p.target_model=word(b+156);p.class_scale=real(b+160);p.owner_rate=real(b+164);
 for(unsigned i=0;i<2;++i){const uint8_t *r=b+256+i*96;C3WzxStatDownOwner *o=p.owners+i;o->species=word(r);o->slot=word(r+4);o->entry=word(r+8);o->clip=word(r+12);o->authored_end=word(r+16);o->minimum=word(r+20);o->root_part=word(r+24);o->camera_part=word(r+28);memcpy(o->owner_sha1,r+32,20);memcpy(o->member_sha1,r+52,20);for(unsigned j=0;j<3;++j)o->root_translation[j]=real(r+72+j*4);if(word(r+84)!=UINT32_MAX||word(r+88)||word(r+92))return fail(e,cap,"Unknown Stat-down owner selector/extension.");}
 for(unsigned i=0;i<3;++i){const uint8_t *r=b+448+i*192;C3WzxStatDownNode *n=p.nodes+i;n->key=word(r);n->kind=word(r+4);n->start=word(r+8);n->attachment=word(r+12);n->position_type=word(r+16);n->link=word(r+20);n->source=word(r+24);n->target=word(r+28);n->timing_index=word(r+32);n->flags=word(r+36);n->part=word(r+40);n->state=word(r+44);for(unsigned j=0;j<16;++j)n->timing[j]=word(r+48+j*4);for(unsigned j=0;j<19;++j)n->resource[j]=word(r+112+j*4);if(word(r+188))return fail(e,cap,"Unknown Stat-down node extension.");}
 if(!valid(&p))return fail(e,cap,"Stat-down typed node/owner/resource profile mismatch.");
 *out=p;if(e&&cap)e[0]=0;return 1;
}
int c3_wzx_stat_down_load(C3WzxStatDownProgram *out,const char *path,char *e,size_t cap){uint8_t b[1024];if(!out||!path)return fail(e,cap,"Missing Stat-down path/output.");FILE *f=fopen(path,"rb");if(!f)return fail(e,cap,"Cannot open Stat-down descriptor.");size_t n=fread(b,1,sizeof b,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;if(bad||n!=sizeof b||extra!=EOF)return fail(e,cap,"Invalid Stat-down descriptor length.");return c3_wzx_stat_down_decode(out,b,n,e,cap);}
void c3_wzx_stat_down_program_close(C3WzxStatDownProgram *p){if(p)memset(p,0,sizeof *p);}
static void teardown(C3WzxStatDown *s){s->teardown_requested=1;s->family_kills=s->sound_keyoffs=s->restore_requests=1;s->color_enabled=0;memcpy(s->color_rgb,s->prior_rgb,3);}
static void natural(C3WzxStatDown *s){if(s->owner_minimum_complete&&s->color_finished&&s->particles_finished&&s->audio_finished)teardown(s);}
int c3_wzx_stat_down_begin(C3WzxStatDown *out,const C3WzxStatDownProgram *p,unsigned slot,const uint8_t prior[3]){if(!out||!valid(p)||slot>1||!prior)return 0;C3WzxStatDown s={0};s.program=*p;s.slot=slot;s.owner_clip=p->owners[slot].clip;s.owner_minimum=p->owners[slot].minimum;s.active=s.color_enabled=1;s.particle_starts=s.color_starts=s.sound_requests=1;memcpy(s.prior_rgb,prior,3);memcpy(s.color_rgb,prior,3);*out=s;return 1;}
int c3_wzx_stat_down_step(C3WzxStatDown *s,uint32_t ms){
 if(!s||!valid(&s->program))return 0;
 if(!s->active||s->teardown_requested)return 1;
 uint64_t bound=(uint64_t)C3_WZX_STAT_DOWN_MAX_TICKS*1000,advance=(uint64_t)ms*60;if(s->units>bound||advance>bound-s->units)return 0;
 uint64_t next=s->units+advance,minimum=(uint64_t)s->owner_minimum*1000;
 if(s->particles_finished&&s->audio_finished&&s->units<minimum&&next>minimum)next=minimum;
 s->units=next;
 /* fnDE698 restarts same idle at source59/90, then loops rate.5.
  * Preserve absolute completion units, but omit the sampled endpoint dwell. */
 uint64_t owner_units=s->units%((uint64_t)s->owner_minimum*1000);
 s->owner_seconds=(float)((double)owner_units/60000.*s->program.owner_rate);s->owner_minimum_complete=s->units>=(uint64_t)s->owner_minimum*1000;
 unsigned age=(unsigned)(s->units/1000);s->color_finished=age>=s->program.color_duration;s->color_enabled=!s->color_finished;
 if(s->color_finished)memcpy(s->color_rgb,s->prior_rgb,3);
 else {float fraction=age<10?(float)age/10:age<20?1:1-(float)(age-20)/30;
  for(unsigned i=0;i<3;++i){/* tracefx low-byteRGBA ->fn13AD9C truncation, then13B268 adds captured base-128. */uint8_t color=(uint8_t)(128+fraction*((int)s->program.color_rgb[i]-128));int v=(int)s->prior_rgb[i]+color-128;if(v<0)v=0;if(v>255)v=255;s->color_rgb[i]=(uint8_t)v;}}
 natural(s);return 1;
}
int c3_wzx_stat_down_observe(C3WzxStatDown *s,int particles,C3WzxStatDownAudio audio){if(!s||!valid(&s->program)||s->cancelled||(audio!=C3_STAT_DOWN_AUDIO_ACTIVE&&audio!=C3_STAT_DOWN_AUDIO_FINISHED))return 0;if(!s->active||s->teardown_requested)return 1;if(particles)s->particles_finished=1;if(audio==C3_STAT_DOWN_AUDIO_FINISHED)s->audio_finished=1;natural(s);return 1;}
int c3_wzx_stat_down_acknowledge(C3WzxStatDown *s,int family,C3WzxStatDownAudio audio,int actor,int camera){if(!s||!valid(&s->program)||!s->teardown_requested||!family||!actor||!camera||(audio!=C3_STAT_DOWN_AUDIO_FINISHED&&audio!=C3_STAT_DOWN_AUDIO_STOPPED))return 0;if(s->teardown_acknowledged)return s->cleanup_audio_status==audio;s->teardown_acknowledged=1;s->cleanup_audio_status=audio;return 1;}
int c3_wzx_stat_down_finish(C3WzxStatDown *s){if(!s||!valid(&s->program))return 0;if(s->completed)return 1;if(!s->active||s->cancelled||!s->teardown_requested||!s->teardown_acknowledged||!s->audio_finished||!s->particles_finished)return 0;s->completed=1;s->active=0;return 1;}
void c3_wzx_stat_down_cancel(C3WzxStatDown *s){if(s){if(s->active)teardown(s);s->active=0;s->cancelled=1;}}
