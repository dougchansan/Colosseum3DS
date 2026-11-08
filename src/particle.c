#include "colosseum3ds/particle.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846f
static int error(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static uint16_t u16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static float f32(const uint8_t *p){uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f;}
static int range(size_t n,size_t p,size_t z){return p<=n&&z<=n-p;}
static int overlap(const void *a,size_t n,const void *b,size_t m)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y?x-y<m:y-x<n;}
static int stat_down_profile(unsigned profile)
{return profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1||profile==C3_PARTICLE_PROFILE_RED_DOWN_V1;}
static int decode_bank(C3ParticleBank *out,const void *data,size_t n,int growl,char *e,size_t cap)
{
 C3ParticleBank b={0};const uint8_t *p=data;
 if(!out||!p||n<32||n>1048576||overlap(out,sizeof *out,p,n)||memcmp(p,"GPT1",4))return error(e,cap,"Invalid GPT1 bank header/size.");
 uint32_t desc=u32(p+4),txg=u32(p+8),refs=u32(p+16);
 if(!range(n,desc,12)||u16(p+desc)!=0x43||u32(p+desc+4)||!range(n,txg,4)||desc>=txg)return error(e,cap,"Unsupported GPT1 description version/sections.");
 b.source_generator_count=u32(p+desc+8);b.texture_group_count=u32(p+txg);
 if(!b.source_generator_count||(growl?b.source_generator_count!=113:b.source_generator_count>C3_PARTICLE_MAX_GENERATORS)||!b.texture_group_count||b.texture_group_count>C3_PARTICLE_MAX_TEXTURE_GROUPS||!range(n,desc+12,4*b.source_generator_count)||!range(n,txg+4,4*b.texture_group_count)||!range(n,refs,4*b.source_generator_count))return error(e,cap,"GPT1 table count/offset bound.");
 b.generator_count=growl?4:b.source_generator_count;b.reference_offset=refs;b.profile=(uint8_t)(growl?C3_PARTICLE_PROFILE_GROWL_V1:0);
 for(unsigned i=0;i<b.source_generator_count;++i)if(u32(p+refs+4*i)>=b.source_generator_count)return error(e,cap,"GPT1 reference handle out of range.");
 for(unsigned i=0;i<b.texture_group_count;++i){
  uint32_t rel=u32(p+txg+4+i*4);size_t at=(size_t)txg+rel;
  if(at<txg||rel<4+4*b.texture_group_count||!range(n,at,24)||!(b.texture_frames[i]=u32(p+at))||b.texture_frames[i]>256||!u32(p+at+12)||!u32(p+at+16)||u32(p+at+12)>1024||u32(p+at+16)>1024)return error(e,cap,"GPT1 texture group/frame bound.");
 }
 for(unsigned i=0;i<b.generator_count;++i){
  static const uint16_t ids[4]={18,93,111,112};unsigned id=growl?ids[i]:i;b.source_ids[i]=(uint16_t)id;
  size_t at=(size_t)desc+u32(p+desc+12+id*4),end=id+1<b.source_generator_count?(size_t)desc+u32(p+desc+16+id*4):txg;
  if(at<(size_t)desc+12+4*b.source_generator_count||end>txg||end<=at||!range(end,at,61)||end-at>4096)return error(e,cap,"GPT1 generator/script offset bound.");
  C3ParticleGenerator *g=&b.generators[i];g->type=u16(p+at);g->tex_group=u16(p+at+2);g->gen_life=u16(p+at+4);g->life=u16(p+at+6);g->kind=(u32(p+at+8)&0xf1ffffffu)|0x08000000u;
  if(g->tex_group>=b.texture_group_count||!g->life||g->life==65535)return error(e,cap,"GPT1 generator texture/lifetime bound.");
  float v[12];for(unsigned j=0;j<12;++j){v[j]=f32(p+at+12+4*j);if(!isfinite(v[j])||fabsf(v[j])>1000000)return error(e,cap,"Nonfinite/excessive GPT1 generator scalar.");}
  g->grav=v[0];g->fric=v[1];memcpy(g->velocity,v+2,12);g->radius=v[5];g->angle=v[6];g->rate=v[7];g->size=v[8];memcpy(g->param,v+9,12);
  g->script_offset=(uint32_t)at+60;g->script_bytes=(uint32_t)(end-at-60);b.refs[i]=u32(p+refs+4*id);
 }
 b.bytes=p;b.length=n;*out=b;if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_bank_decode(C3ParticleBank *b,const void *p,size_t n,char *e,size_t cap)
{return decode_bank(b,p,n,0,e,cap);}
int c3_particle_bank_source_slot(const C3ParticleBank *b,unsigned id,unsigned *slot)
{
 if(!b||!slot||b->generator_count>C3_PARTICLE_MAX_GENERATORS||(b->profile&&b->profile!=C3_PARTICLE_PROFILE_GROWL_V1&&!stat_down_profile(b->profile)&&b->profile!=C3_PARTICLE_PROFILE_TACKLE_V1))return 0;
 for(unsigned i=0;i<b->generator_count;++i)if((b->profile?b->source_ids[i]:i)==id){*slot=i;return 1;}
 return 0;
}
/* Audited Growl bytecode permits identity-RS AppSRT only: no SRT state,
 * detach, inheritance, loops or further dynamic spawn instructions in children. */
static int growl_program(const C3ParticleBank *b,unsigned slot)
{
 const C3ParticleGenerator *g=&b->generators[slot];const uint8_t *p=b->bytes+g->script_offset;size_t at=0;
 if(!slot){static const uint8_t root[]={0xf0,0,93,7,0xf0,0,111,7,0xf0,0,112,7,0xff};return g->script_bytes>=sizeof root&&!memcmp(p,root,sizeof root);}
 unsigned ended=0;
 while(at<g->script_bytes){unsigned op=p[at++],extra=0;
  if(op==0xff){ended=1;break;}
  if(op<0x80){if(op&32)extra++;if(op&64)extra++;}
  else if(op==0xad||op==0xb1)extra=0;
  else if(op==0xa0||op==0xac||op==0xcf||op==0xdf){
   if(at>=g->script_bytes)return 0;
   extra=1+((p[at]&128)?1:0)+(op==0xac?8:4);
  }else if(op==0xed)extra=9;else return 0;
  if(extra>g->script_bytes-at)return 0;
  at+=extra;
 }
 return ended;
}
static int stat_down_program(const C3ParticleBank *b,unsigned slot)
{
 const C3ParticleGenerator *g=&b->generators[slot];const uint8_t *p=b->bytes+g->script_offset;size_t at=0;unsigned offsets=0;
 if(!slot){static const uint8_t root[]={0xf0,0,1,7,0xf0,0,2,7,0xff};return g->script_bytes>=sizeof root&&!memcmp(p,root,sizeof root);}
 while(at<g->script_bytes){unsigned op=p[at++],extra=0;
  if(op==0xff)return offsets==1;
  if(op<0x80){if(op&32)extra++;if(op&64)extra++;}
  else if(op==0xad||op==0xb1)extra=0;
  else if(op==0x8f){if(g->script_bytes-at<12||f32(p+at)!=0||f32(p+at+4)!=22||f32(p+at+8)!=0)return 0;++offsets;extra=12;}
  else if(op==0xa0||op==0xac||op==0xcf||op==0xdf){if(at>=g->script_bytes)return 0;extra=1+((p[at]&128)?1:0)+(op==0xac?8:4);}
  else if(op==0xa3||op==0xe8)extra=4;
  else if(op==0xbd)extra=8;
  else if(op==0xbc)extra=2;
  else return 0;
  if(extra>g->script_bytes-at)return 0;
  at+=extra;
 }
 return 0;
}
static int stat_down_bank(C3ParticleBank *b,unsigned profile)
{
 if(b->generator_count!=3||b->texture_group_count!=2||b->texture_frames[0]!=1||b->texture_frames[1]!=2)return 0;
 const float parameters[3][12]={{0,0,0,0,0,0,0,-1,1,0,2*PI,0},
  {0,0,0,1,0,-10,-PI/2,-8,1,-2*PI,2*PI,0},
  {0,0,0,1,0,-8,PI/2,-4,1,-2*PI,2*PI,0}};
 const uint16_t life[3][2]={{1,1000},{32,16},{28,26}};
 for(unsigned i=0;i<3;++i){const C3ParticleGenerator *g=&b->generators[i];
  if(g->type!=(i?7:0)||g->tex_group!=(i==2?1:0)||g->gen_life!=life[i][0]||g->life!=life[i][1]||g->kind!=(i?0x08400000u:0x09000000u)||b->refs[i]!=i||!stat_down_program(b,i))return 0;
  float actual[12]={g->grav,g->fric,g->velocity[0],g->velocity[1],g->velocity[2],g->radius,g->angle,g->rate,g->size,g->param[0],g->param[1],g->param[2]};
  for(unsigned j=0;j<12;++j)if(actual[j]!=parameters[i][j])return 0;
 }
 b->profile=(uint8_t)profile;return 1;
}
/* Pinned source51bfe Tackle's eight original descriptors and complete padded
 * programs. Exact bytes bound the F0-only/static-key closure: F1 and mutable
 * sort-key instructions cannot enter this opt-in list-order model. */
static int tackle_bank(C3ParticleBank *b)
{
 static const uint8_t generators[1104]={
  0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x0a,0x81,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0xf0,0x00,0x05,0x05,
  0xf0,0x00,0x04,0x04,0xf0,0x00,0x06,0x07,0xf0,0x00,0x01,0x06,0xf0,0x00,0x02,0x06,
  0xf0,0x00,0x03,0x06,0xf0,0x00,0x07,0x03,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x14,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0x40,0x00,0xed,0x00,
  0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x02,0xf3,0x01,0x3e,0xa0,0xd9,0x7c,0xbc,0x64,
  0xc3,0x88,0x14,0xa0,0x00,0x40,0x10,0x00,0x00,0xcf,0x00,0xe8,0xa4,0x95,0xff,0xad,
  0xdf,0x00,0x7b,0x6b,0x4f,0x00,0xa0,0x04,0x40,0x88,0x00,0x00,0xcf,0x04,0xe8,0xa4,
  0x95,0xff,0xad,0xdf,0x04,0x7b,0x6b,0x4f,0x00,0x04,0xa0,0x10,0x40,0xa0,0x00,0x00,
  0xcf,0x10,0xe8,0xa4,0x95,0x00,0xad,0xdf,0x10,0x7b,0x6b,0x4f,0x00,0x10,0xff,0xff,
  0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x20,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0x40,0x01,0xed,0x00,
  0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x02,0xf3,0x00,0x3e,0x8e,0xfa,0x35,0xbb,0xe6,
  0x5a,0x3a,0x24,0xa0,0x00,0x40,0x00,0x00,0x00,0xcf,0x00,0xfb,0xef,0xdb,0xff,0xad,
  0xdf,0x00,0xe7,0xc9,0x98,0x00,0xa0,0x04,0x40,0x20,0x00,0x00,0xcf,0x04,0xfb,0xef,
  0xdb,0xff,0xad,0xdf,0x04,0x53,0x48,0x35,0x00,0x04,0xa0,0x1c,0x40,0x88,0x00,0x00,
  0xcf,0x1c,0xfb,0xf0,0xdb,0x00,0xad,0xdf,0x1c,0x53,0x48,0x35,0x00,0x1c,0xff,0xff,
  0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x24,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0x40,0x02,0xed,0x00,
  0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x02,0xf3,0x01,0x3e,0xc4,0x98,0x09,0xbc,0x26,
  0xce,0x93,0x24,0xa0,0x00,0x3f,0x80,0x00,0x00,0xcf,0x00,0xfb,0xef,0xdb,0xff,0xad,
  0xdf,0x00,0xe7,0xc9,0x98,0x00,0xa0,0x04,0x40,0x20,0x00,0x00,0xcf,0x04,0xfb,0xef,
  0xdb,0xff,0xad,0xdf,0x04,0x9b,0x88,0x64,0x00,0x04,0xa0,0x20,0x40,0x48,0x00,0x00,
  0xcf,0x20,0xfb,0xef,0xdb,0x00,0xad,0xdf,0x20,0x9b,0x88,0x64,0x00,0x20,0x20,0xff,
  0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x10,0x01,0x40,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0x40,0x00,0xb1,0xed,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0xf3,0x01,0x3e,0x0e,0xfa,0x35,0xbb,
  0xe4,0xc3,0x88,0x14,0xa0,0x00,0x3f,0x80,0x00,0x00,0xcf,0x00,0xfb,0xef,0xdb,0xff,
  0xad,0xdf,0x00,0xe7,0xc9,0x98,0x00,0xa0,0x04,0x41,0x00,0x00,0x00,0xcf,0x04,0xfb,
  0xef,0xdb,0xff,0xad,0xdf,0x04,0x61,0x2e,0x1f,0x00,0x04,0xa0,0x10,0x40,0x80,0x00,
  0x00,0xcf,0x0c,0xfb,0xef,0xdb,0x00,0xad,0xdf,0x0c,0x61,0x2e,0x1f,0x00,0x0c,0xff,
  0x00,0x00,0x00,0x02,0x00,0x01,0x00,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xbf,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,
  0x00,0x00,0x00,0x00,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0x40,0x00,0xb1,0xa0,
  0x00,0x40,0x40,0x00,0x00,0xcf,0x00,0xe6,0x59,0x24,0xff,0xad,0xdf,0x00,0x95,0x4d,
  0x59,0x00,0xa0,0x10,0x41,0x00,0x00,0x00,0xcf,0x10,0xe6,0x59,0x24,0xff,0xad,0xdf,
  0x10,0x95,0x4d,0x59,0x00,0x10,0xa0,0x14,0x40,0x00,0x00,0x00,0xcf,0x14,0xe6,0x59,
  0x24,0x00,0xad,0xdf,0x14,0x95,0x4d,0x59,0x00,0x14,0xff,0xff,0xff,0xff,0xff,0xff,
  0x00,0x04,0x00,0x00,0x00,0x04,0x00,0x20,0x40,0x40,0x00,0x02,0x00,0x00,0x00,0x00,
  0x3f,0x5e,0xb8,0x52,0x00,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xc0,0xc9,0x0f,0xdb,0xc1,0x40,0x00,0x00,0x3f,0x80,0x00,0x00,
  0xc0,0xc9,0x0f,0xdb,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0xe8,0x00,0x00,0x00,
  0x00,0xbd,0x3f,0x80,0x00,0x00,0x3f,0xc0,0x00,0x00,0xa0,0x00,0x00,0x00,0x00,0x00,
  0x01,0xac,0x00,0x3f,0x80,0x00,0x00,0x40,0x00,0x00,0x00,0xcf,0x00,0xf8,0xe1,0xad,
  0xff,0x0f,0xcf,0x10,0xf8,0xe1,0xad,0x00,0x10,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
  0x00,0x00,0x00,0x04,0x00,0x08,0x00,0x60,0x00,0x00,0x00,0x03,0xba,0x1d,0x49,0x52,
  0x3f,0x79,0x99,0x9a,0x00,0x00,0x00,0x00,0x38,0xd1,0xb7,0x17,0x00,0x00,0x00,0x00,
  0x40,0x80,0x00,0x00,0x40,0xc9,0x0f,0xdb,0xc1,0x20,0x00,0x00,0x3f,0x80,0x00,0x00,
  0xc0,0xc9,0x0f,0xdb,0x40,0xc9,0x0f,0xdb,0x00,0x00,0x00,0x00,0xbc,0x00,0x02,0xa8,
  0x3f,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,0xed,0x00,0x00,0x00,
  0x00,0x40,0xc9,0x0f,0xdb,0x24,0xf3,0x00,0x3c,0x2b,0x92,0xa7,0xb9,0x02,0xb8,0xe1,
  0x54,0xbd,0x3d,0xa3,0xd7,0x0a,0x3e,0x19,0x99,0x9a,0xcf,0x00,0x76,0x64,0x50,0x00,
  0xad,0xdf,0x00,0x45,0x42,0x36,0x00,0xac,0x14,0x3f,0xc0,0x00,0x00,0x3f,0x40,0x00,
  0x00,0xcf,0x14,0x76,0x64,0x50,0xff,0xad,0xdf,0x14,0x45,0x42,0x36,0x00,0x14,0xac,
  0x4c,0x40,0x13,0x33,0x33,0x3f,0x40,0x00,0x00,0xcf,0x4c,0x42,0x38,0x2d,0x00,0xad,
  0xdf,0x4c,0x16,0x15,0x12,0x00,0x20,0x4c,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
 };
 if(b->generator_count!=8||b->texture_group_count!=5||b->generators[0].script_offset!=140||b->generators[7].script_offset+b->generators[7].script_bytes!=1184||memcmp(b->bytes+80,generators,sizeof generators))return 0;
 static const uint32_t frames[5]={1,3,1,3,2};
 for(unsigned i=0;i<5;++i)if(b->texture_frames[i]!=frames[i])return 0;
 for(unsigned i=0;i<8;++i)if(b->refs[i]!=i)return 0;
 b->profile=C3_PARTICLE_PROFILE_TACKLE_V1;return 1;
}
int c3_particle_bank_decode_reachable(C3ParticleBank *out,const void *p,size_t n,unsigned root,unsigned profile,char *e,size_t cap)
{
 if(!out||!p||overlap(out,sizeof *out,p,n))return error(e,cap,"Unsupported reachable particle profile/root/ownership.");
 if(profile==C3_PARTICLE_PROFILE_TACKLE_V1){C3ParticleBank b;
  if(root||n!=91680||!decode_bank(&b,p,n,0,e,cap)||!tackle_bank(&b))return error(e,cap,"Unqualified Tackle source bank/root/program.");
  *out=b;if(e&&cap)e[0]=0;return 1;
 }
 if(stat_down_profile(profile)){C3ParticleBank b;
  if(root||n!=1644||!decode_bank(&b,p,n,0,e,cap)||!stat_down_bank(&b,profile))return error(e,cap,"Unqualified stat-down source bank/root/program.");
  *out=b;if(e&&cap)e[0]=0;return 1;
 }
 if(root!=18||profile!=C3_PARTICLE_PROFILE_GROWL_V1)return error(e,cap,"Unsupported reachable particle profile/root/ownership.");
 C3ParticleBank b;if(!decode_bank(&b,p,n,1,e,cap))return 0;
 static const uint32_t kinds[4]={0x09000000u,0x09420100u,0x09420000u,0x09400001u};
 for(unsigned i=0;i<4;++i)if(b.generators[i].type!=(i==3?8:0)||b.generators[i].tex_group!=((i==1||i==3)?1:0)||b.generators[i].kind!=kinds[i]||!growl_program(&b,i))return error(e,cap,"Unqualified Growl generator/AppSRT program.");
 for(unsigned i=0;i<113;++i)if(u32(b.bytes+b.reference_offset+4*i)!=i)return error(e,cap,"Growl source reference mapping differs.");
 *out=b;if(e&&cap)e[0]=0;return 1;
}
static int fault(C3ParticleSystem *s,unsigned gen,unsigned pc,unsigned op,const char *why)
{if(s->bank.profile&&gen<s->bank.generator_count)gen=s->bank.source_ids[gen];snprintf(s->diagnostic,sizeof s->diagnostic,"GPT1 generator %u pc 0x%X opcode 0x%02X: %s",gen,pc,op,why);s->failed=1;return 0;}
static float rnd(C3ParticleSystem *s)
{s->cosmetic_seed=s->cosmetic_seed*214013u+2531011u;++s->cosmetic_draws;return (float)(s->cosmetic_seed>>16)/65536.f;}
static C3ParticleAppSRT *app_srt(C3ParticleSystem *s,uint32_t handle)
{
 if(!handle)return NULL;
 C3ParticleAppSRT *a=&s->app_srt[handle&15u];
 return a->active&&a->generation==(handle>>4)?a:NULL;
}
static int app_release(C3ParticleSystem *s,uint32_t handle)
{
 C3ParticleAppSRT *a=app_srt(s,handle);if(!a||!a->references)return fault(s,0,0,0,"stale AppSRT release");
 if(!--a->references)a->active=0;
 return 1;
}
static uint32_t app_create(C3ParticleSystem *s,uint32_t owner,const float pos[3])
{
 if(s->app_srt_generation==0x0fffffffu)return 0;
 for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i)if(!s->app_srt[i].active){
  C3ParticleAppSRT *a=&s->app_srt[i];memset(a,0,sizeof *a);a->active=1;a->references=1;a->owner_serial=owner;a->generation=++s->app_srt_generation;memcpy(a->translate,pos,sizeof a->translate);
  return (a->generation<<4)|i;
 }
 return 0;
}
static int reference_slot(const C3ParticleBank *b,unsigned reference,unsigned *slot)
{
 if(!b->profile){if(reference>=b->generator_count)return 0;*slot=b->refs[reference];return *slot<b->generator_count;}
 if(reference>=b->source_generator_count||!range(b->length,b->reference_offset+4u*reference,4))return 0;
 return c3_particle_bank_source_slot(b,u32(b->bytes+b->reference_offset+4u*reference),slot);
}
/* POSITION uses checked wazaSequenceEntry flags. Reflect 0x18A9 enables velocity-local rotation,
 * texture scaling and random-velocity scaling; no local-offset or tornado
 * height/radius scaling. Snapshots survive native emitter-slot reuse. */
static void yaw_rotate(float yaw,float v[3])
{float x=v[0],z=v[2],c=cosf(yaw),sn=sinf(yaw);v[0]=c*x+sn*z;v[2]=-sn*x+c*z;}
static int emission_shape(const C3ParticleBank *b,const C3ParticleGenerator *g)
{return g->type==0||g->type==1||g->type==2||g->type==8||(stat_down_profile(b->profile)&&g->type==7)||(b->profile==C3_PARTICLE_PROFILE_TACKLE_V1&&g==&b->generators[6]&&g->type==4);}
static int emitter(C3ParticleSystem *s,unsigned g,unsigned depth,const float pos[3],unsigned flags)
{
 if(g>=s->bank.generator_count||depth>C3_PARTICLE_MAX_DEPTH)return fault(s,g,0,0xF0,"reference/nesting bound");
 const C3ParticleGenerator *d=&s->bank.generators[g];
 if(!emission_shape(&s->bank,d)||d->rate>C3_PARTICLE_MAX_PARTICLES||d->rate< -(float)C3_PARTICLE_MAX_PARTICLES||((d->kind&0x30000)&&!(s->bank.profile==C3_PARTICLE_PROFILE_GROWL_V1&&(d->kind&0x30000)==0x20000)))return fault(s,g,0,0,"unsupported generator shape/camera basis/rate");

 for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(!s->emitters[i].active){
  C3ParticleEmitter *v=&s->emitters[i];memset(v,0,sizeof *v);v->active=1;v->generator=(uint16_t)g;v->depth=(uint8_t)depth;v->life=d->gen_life;v->kind=d->kind;
  if(flags<8)v->kind=(v->kind&~0x0e000000u)|(flags<<25);
  memcpy(v->position,pos,sizeof v->position);
  v->attachment_scale=v->radius_scale=1;
  if(d->kind&0x100)v->count=d->rate<0?(1.f+d->rate>FLT_EPSILON?1.f:0.f):1.f-FLT_EPSILON;
  else v->count=d->rate<0?0:rnd(s);
  v->serial=++s->spawned_emitters;
  if(s->bank.profile&&(d->kind&0x20000)){v->app_srt_handle=app_create(s,v->serial,pos);if(!v->app_srt_handle)return fault(s,g,0,0,"AppSRT pool/generation exhausted");memset(v->position,0,sizeof v->position);}
  return 1;
 }
 return fault(s,g,0,0xF0,"emitter pool exhausted");
}
static void color(C3Particle *p,unsigned which)
{
 uint8_t *out=which?p->env:p->prim;unsigned count=p->color_count[which],remain=p->color_remain[which];
 for(unsigned c=0;c<4;++c){
  int target=p->color_target[which][c];int t=count?(int)((remain<<16)/count):0;
  out[c]=(uint8_t)(((target<<16)+t*((int)p->color_base[which][c]-target))>>16);
 }
}
static int byte(C3ParticleSystem *s,C3Particle *p,unsigned *v)
{
 const C3ParticleGenerator *g=&s->bank.generators[p->generator];
 if(p->pc>=g->script_bytes)return fault(s,p->generator,p->pc,0,"truncated instruction");
 *v=s->bank.bytes[g->script_offset+p->pc++];return 1;
}
static int timearg(C3ParticleSystem *s,C3Particle *p,unsigned *v)
{unsigned a,b;if(!byte(s,p,&a))return 0;if(a&128){if(!byte(s,p,&b))return 0;a=((a&127)<<8)|b;}*v=a;return 1;}
static int scalar(C3ParticleSystem *s,C3Particle *p,float *v)
{
 unsigned x[4]={0};for(unsigned i=0;i<4;++i)if(!byte(s,p,&x[i]))return 0;
 uint32_t bits=x[0]<<24|x[1]<<16|x[2]<<8|x[3];memcpy(v,&bits,4);
 return (isfinite(*v)&&fabsf(*v)<=1000000)||fault(s,p->generator,p->pc-4,0,"nonfinite/excessive instruction scalar");
}
static int interpret(C3ParticleSystem *,C3Particle *);
static void initial_particle(C3ParticleSystem *s,C3Particle *p,unsigned generator,unsigned depth)
{
 const C3ParticleGenerator *g=&s->bank.generators[generator];memset(p,0,sizeof *p);p->active=1;p->generator=(uint16_t)generator;p->depth=(uint8_t)depth;p->kind=g->kind;p->tex_group=(uint8_t)g->tex_group;
 p->attachment_scale=p->radius_scale=1;
 p->life=(uint16_t)(g->life+1);p->wait=1;p->trail=1;p->size=g->size;p->grav=g->grav;p->fric=g->fric;
 memcpy(p->velocity,g->velocity,sizeof p->velocity);memset(p->prim,255,4);memset(p->color_base[0],255,4);memset(p->color_target[0],255,4);
 if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){
  unsigned slot=(unsigned)(p-s->particles),n=0;
  for(unsigned j=0;j<s->source_order_count;++j){unsigned old=s->source_order[j];if(old!=slot&&s->particles[old].active)s->source_order[n++]=s->source_order[j];}
  memmove(s->source_order+1,s->source_order,n*sizeof *s->source_order);s->source_order[0]=(uint16_t)slot;s->source_order_count=(uint16_t)(n+1);
 }
 p->serial=++s->emitted;unsigned live=0;for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)live+=s->particles[i].active!=0;if(live>s->peak_particles)s->peak_particles=live;
}
static int child_particle(C3ParticleSystem *s,C3Particle *parent,unsigned generator)
{
 if(parent->depth>=C3_PARTICLE_MAX_DEPTH)return fault(s,generator,0,0xf1,"particle nesting bound");
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(!s->particles[i].active){
  C3Particle *p=&s->particles[i];initial_particle(s,p,generator,parent->depth+1);p->emitter=parent->emitter;p->attachment_yaw=parent->attachment_yaw;p->attachment_scale=parent->attachment_scale;p->radius_scale=parent->radius_scale;p->source_flags=parent->source_flags;if(s->position_attachment&&(p->source_flags&8))yaw_rotate(p->attachment_yaw,p->velocity);memcpy(p->position,parent->position,sizeof p->position);
  if(parent->kind&0x20000000)p->kind|=0x20000000;
  return interpret(s,p);
 }
 return fault(s,generator,0,0xf1,"particle pool exhausted");
}
/* psInterpretParticle0: size/color clocks precede instructions, wait executes
 * only when its decrement reaches zero; lifetime and integration follow it. */
static int interpret(C3ParticleSystem *s,C3Particle *p)
{
 if(p->size_count){p->size+=(p->size_target-p->size)/p->size_count;--p->size_count;}
 for(unsigned k=0;k<2;++k)if(p->color_count[k]){if(--p->color_remain[k]==0){p->color_count[k]=0;memcpy(p->color_base[k],p->color_target[k],4);}color(p,k);}
 if(p->rotate_count){
  if(p->rotate_accel){p->rotation+=p->rotate_target;p->rotate_target+=p->rotate_target>=0?p->rotate_accel:-p->rotate_accel;if(!--p->rotate_count)p->rotate_accel=p->rotate_target=0;}
  else{p->rotation+=(p->rotate_target-p->rotation)/p->rotate_count;--p->rotate_count;}
 }
 if(p->wait&&!--p->wait){
  unsigned instructions=0;
  for(;;){unsigned op,pc=p->pc,t=0,v=0;if(++instructions>C3_PARTICLE_MAX_INSTRUCTIONS)return fault(s,p->generator,pc,0,"instruction budget exceeded");
   if(!byte(s,p,&op))return 0;
   if(op<0x80){t=op&31;if(op&32){if(!byte(s,p,&v))return 0;t=(t<<8)|v;}
    if(op&64){if(!byte(s,p,&v))return 0;if(v>=s->bank.texture_frames[p->tex_group])return fault(s,p->generator,pc,op,"texture frame out of range");p->pose=(uint8_t)v;p->kind|=0x400;}
   }else if((op&0xf0)==0xc0||(op&0xf0)==0xd0){
    unsigned k=(op&0xf0)==0xd0;color(p,k);uint8_t *base=k?p->env:p->prim;memcpy(p->color_base[k],base,4);memcpy(p->color_target[k],base,4);
    if(!timearg(s,p,&t))return 0;
    p->color_count[k]=p->color_remain[k]=(uint16_t)t;
    for(unsigned c=0;c<4;++c)if(op&(1u<<c)){if(!byte(s,p,&v))return 0;p->color_target[k][c]=(uint8_t)v;}
    if(!t)memcpy(p->color_base[k],p->color_target[k],4);
    color(p,k);t=0;
   }else if(op==0x8f&&stat_down_profile(s->bank.profile)){
    float offset[3];for(unsigned j=0;j<3;++j)if(!scalar(s,p,&offset[j]))return 0;
    /* psApplyOffsetLocalRotation: A1 does not set bit8, so the admitted
     * [0,22,0] offset is unscaled. Keep the source gate explicit. */
    if(p->source_flags&8){if(!(p->source_flags&0x10))for(unsigned j=0;j<3;++j)offset[j]*=p->attachment_scale;yaw_rotate(p->attachment_yaw,offset);}
    for(unsigned j=0;j<3;++j)p->position[j]+=offset[j];
   }else if((op&0xf8)==0x98){
    if((op&7)!=7)return fault(s,p->generator,pc,op,"source partial-vector uninitialized operands refused");
    for(unsigned j=0;j<3;++j){float value;if(!scalar(s,p,&value))return 0;p->velocity[j]+=value;}
   }else switch(op){
    case 0xa0:case 0xac:{float value,spread=0;if(!timearg(s,p,&v)||!scalar(s,p,&value))return 0;if(op==0xac){if(!scalar(s,p,&spread))return 0;value+=spread*rnd(s);}p->size_count=(uint16_t)v;p->size_target=value;if(!v)p->size=value;break;}
    case 0xa3:if(!scalar(s,p,&p->fric))return 0;if(p->fric==1)p->kind&=~2u;else p->kind|=2;break;
    case 0xa8:for(unsigned j=0;j<3;++j){float value;if(!scalar(s,p,&value))return 0;p->position[j]+=2*value*rnd(s)-value;}break;
    case 0xad:p->kind|=0x80;break;
    case 0xb1:p->kind|=0x60;break;
    case 0xb6:{float value;if(!timearg(s,p,&v)||!scalar(s,p,&value))return 0;p->rotate_count=(uint16_t)v;p->rotate_target+=value;if(!v)p->rotation=p->rotate_target;break;}
    case 0xbc:{unsigned base,span;if(!byte(s,p,&base)||!byte(s,p,&span))return 0;unsigned frame=base+(unsigned)(span*rnd(s));if(frame>=s->bank.texture_frames[p->tex_group])return fault(s,p->generator,pc,op,"random texture frame bound");p->pose=(uint8_t)frame;p->kind|=0x400;break;}
    case 0xbd:{float a,b;if(!scalar(s,p,&a)||!scalar(s,p,&b))return 0;if(s->position_attachment&&(p->source_flags&0x1000)){a*=p->attachment_scale;b*=p->attachment_scale;}a+=b*rnd(s);float mag=sqrtf(p->velocity[0]*p->velocity[0]+p->velocity[1]*p->velocity[1]+p->velocity[2]*p->velocity[2]);if(mag>1e-10f)for(unsigned j=0;j<3;++j)p->velocity[j]*=a/mag;break;}
    case 0xe9:{
     unsigned flags,steps;if(!byte(s,p,&flags)||!byte(s,p,&steps))return 0;
     for(unsigned k=0;k<2;++k){color(p,k);memcpy(p->color_base[k],k?p->env:p->prim,4);}
     float value=steps?(int)((steps+1)*rnd(s))/(float)steps:rnd(s);
     for(unsigned c=0;c<4;++c)if(flags&(1u<<c)){
      unsigned delta;if(!byte(s,p,&delta))return 0;float change;
      if(c==3)change=(int8_t)delta*2*(steps?(int)((steps+1)*rnd(s))/(float)steps:rnd(s));else change=value*((int8_t)delta*2);
      for(unsigned k=0;k<2;++k)if(flags&(0x10u<<k)){float target=p->color_target[k][c]+change;p->color_target[k][c]=(uint8_t)(target<0?0:target>255?255:(int)target);}
     }
     for(unsigned k=0;k<2;++k){p->color_remain[k]=p->color_count[k];if(!p->color_count[k])memcpy(p->color_base[k],p->color_target[k],4);color(p,k);}break;
    }
    case 0xe6:p->kind|=0x200000;break;
    case 0xe8:if(!scalar(s,p,&p->trail))return 0;if(p->trail<0)p->kind&=~0x100000u;else p->kind|=0x100000;break;
    case 0xed:{float a,b;if(!scalar(s,p,&a)||!scalar(s,p,&b)||!byte(s,p,&v))return 0;float delta=v?a+b*(int)((v+1)*rnd(s))/(float)v:a+b*rnd(s);p->rotation+=delta;p->rotate_target+=delta;break;}
    case 0xf0:{unsigned hi,lo,flags;if(!byte(s,p,&hi)||!byte(s,p,&lo)||!byte(s,p,&flags))return 0;unsigned id=(hi<<8)|lo,slot;if(!reference_slot(&s->bank,id,&slot))return fault(s,p->generator,pc,op,"spawn reference out of range/admitted closure");if(!emitter(s,slot,p->depth+1,p->position,flags&7))return 0;
     if(s->position_attachment)for(unsigned j=0;j<C3_PARTICLE_MAX_EMITTERS;++j)if(s->emitters[j].serial==s->spawned_emitters){s->emitters[j].attachment_yaw=p->attachment_yaw;s->emitters[j].attachment_scale=p->attachment_scale;s->emitters[j].radius_scale=p->radius_scale;s->emitters[j].source_flags=p->source_flags;}
     break;}
    case 0xf1:{unsigned hi,lo;if(!byte(s,p,&hi)||!byte(s,p,&lo))return 0;unsigned id=(hi<<8)|lo,slot;if(!reference_slot(&s->bank,id,&slot))return fault(s,p->generator,pc,op,"child particle reference bound");if(!child_particle(s,p,slot))return 0;break;}
    case 0xf3:{unsigned dir,count;if(!byte(s,p,&dir)||!scalar(s,p,&p->rotate_target)||!scalar(s,p,&p->rotate_accel)||!timearg(s,p,&count))return 0;p->rotate_count=(uint16_t)count;if(count){if(!dir)p->rotate_target+=p->rotate_accel/2;else p->rotate_target=-p->rotate_target-p->rotate_accel/2;}else p->rotate_target=p->rotate_accel=0;break;}
    case 0xfa:if(!byte(s,p,&v))return 0;if(!v)return fault(s,p->generator,pc,op,"zero loop count refused");p->loop_count=(uint8_t)v;p->loop_pc=p->pc;break;
    case 0xfb:if(!p->loop_count)return fault(s,p->generator,pc,op,"loop end without start");if(--p->loop_count)p->pc=p->loop_pc;break;
    case 0xfc:p->mark_pc=p->pc;break;
    case 0xfd:p->pc=p->mark_pc;break;
    case 0xfe:case 0xff:p->life=1;goto done;
    default:return fault(s,p->generator,pc,op,"unsupported reached instruction");
   }
   if(t){p->wait=(uint16_t)t;break;}
  }
 }
 done:
 if(!--p->life){if(p->app_srt_handle&&!app_release(s,p->app_srt_handle))return 0;p->active=0;return 1;}
 if(p->kind&4){
  float sa=sinf(p->grav),sb=sinf(p->fric),ca=cosf(p->grav),cb=cosf(p->fric);p->velocity[2]+=p->tornado_speed;
  float r=fabsf(p->tornado_radius)+p->velocity[2]*tanf(fabsf(p->tornado_angle));r*=p->velocity[1];
  p->velocity[0]+=p->tornado_turn;
  float x=r*cosf(p->velocity[0]),y=r*sinf(p->velocity[0]),z=p->velocity[2];
  p->position[0]=p->tornado_origin[0]+x*cb+z*sb;p->position[1]=p->tornado_origin[1]+sb*(-x*sa)+y*ca+cb*z*sa;p->position[2]=p->tornado_origin[2]+sb*(-x*ca)-y*sa+cb*z*ca;
 }else{
  if(p->kind&1)p->velocity[1]-=p->grav;
  if(p->kind&2)for(unsigned j=0;j<3;++j)p->velocity[j]*=p->fric;
  for(unsigned j=0;j<3;++j)p->position[j]+=p->velocity[j];
 }
 for(unsigned j=0;j<3;++j){if(!isfinite(p->position[j])||fabsf(p->position[j])>1000000)return fault(s,p->generator,p->pc,0,"particle position bound");}
 return 1;
}
/* Source generator.c velocity-alignment matrix (yaw then pitch). */
static void rotate(const float vel[3],float out[3])
{
 float mag=sqrtf(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);if(mag<=FLT_EPSILON)return;
 float x=vel[0]/mag,y=vel[1]/mag,z=vel[2]/mag;
 float yaw=fabsf(z)<FLT_MIN?(y>=0?PI/2:-PI/2):atan2f(y,z),sy=sinf(yaw),cy=cosf(yaw),pz=y*sy+z*cy;
 float pitch=fabsf(pz)<FLT_MIN?(x>=0?PI/2:-PI/2):atan2f(x,pz),sp=sinf(pitch),cp=cosf(pitch);
 x=out[0];y=out[1];z=out[2];out[0]=cp*x+sp*z;out[1]=-sy*sp*x+cy*y+sy*cp*z;out[2]=-cy*sp*x-sy*y+cy*cp*z;
}
static int emit(C3ParticleSystem *s,unsigned index,float angle_step,float *cur_angle)
{
 C3ParticleEmitter *e=&s->emitters[index];const C3ParticleGenerator *g=&s->bank.generators[e->generator];
 C3ParticleGenerator adjusted;
 if(s->position_attachment){adjusted=*g;adjusted.radius*=e->radius_scale;for(unsigned j=0;j<3;++j)adjusted.param[j]*=e->radius_scale;
  /* Growl F0 psCopyGeneratorData invokes AngleRadiusScale(applyToMotion)
   * for source flag0x1000. Keep legacy profiles' sampled behavior unchanged. */
  if(s->bank.profile&&(e->source_flags&0x1000)){adjusted.grav*=e->radius_scale;adjusted.fric*=e->radius_scale;}
  g=&adjusted;}
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(!s->particles[i].active){
  C3Particle *p=&s->particles[i];initial_particle(s,p,e->generator,e->depth);p->emitter=(uint16_t)index;p->kind=e->kind;p->attachment_yaw=e->attachment_yaw;p->attachment_scale=e->attachment_scale;p->radius_scale=e->radius_scale;p->source_flags=e->source_flags;memcpy(p->position,e->position,sizeof p->position);
  if(s->bank.profile){p->grav=g->grav;p->fric=g->fric;p->emitter_serial=e->serial;p->app_srt_handle=e->app_srt_handle;if(p->app_srt_handle){C3ParticleAppSRT *a=app_srt(s,p->app_srt_handle);if(!a||a->references==UINT16_MAX)return fault(s,e->generator,0,0,"invalid AppSRT particle attachment");++a->references;}}
  if(g->type==8){
   float lat=(g->param[0]==0?PI:fabsf(g->param[0]))*rnd(s),lon=2*PI*rnd(s),dist=g->radius<0?-g->radius:g->radius*rnd(s);
   float v[3]={sinf(lat)*cosf(lon),sinf(lat)*sinf(lon),cosf(lat)};rotate(g->velocity,v);
   float speed=sqrtf(g->velocity[0]*g->velocity[0]+g->velocity[1]*g->velocity[1]+g->velocity[2]*g->velocity[2]);if(g->param[0]<0)speed=-speed;
   for(unsigned j=0;j<3;++j){p->position[j]+=dist*v[j];p->velocity[j]=speed*v[j];}
  }else if(g->type==1){float r=rnd(s);for(unsigned j=0;j<3;++j)p->position[j]+=r*g->param[j];
  }else if(g->type==2){
   float dist=g->radius<0?1:rnd(s),angle;if(g->angle<0){*cur_angle+=angle_step;angle=*cur_angle;}else angle=2*PI*rnd(s);
   float basis[3]={0,0,1};rotate(g->velocity,basis);if(s->position_attachment)yaw_rotate(e->attachment_yaw,basis);
   float a1=fabsf(basis[2])<FLT_MIN?(basis[1]>=0?PI/2:-PI/2):atan2f(basis[1],basis[2]);float comb=basis[1]*sinf(a1)+basis[2]*cosf(a1);
   float a3=fabsf(comb)<FLT_MIN?(basis[0]>=0?PI/2:-PI/2):atan2f(basis[0],comb);
   p->kind|=4;p->velocity[0]=angle;p->velocity[1]=dist;p->velocity[2]=0;p->grav=a1;p->fric=a3;p->tornado_radius=g->radius;p->tornado_angle=g->angle;
   p->tornado_turn=g->grav;p->tornado_speed=sqrtf(g->velocity[0]*g->velocity[0]+g->velocity[1]*g->velocity[1]+g->velocity[2]*g->velocity[2]);memcpy(p->tornado_origin,e->position,sizeof p->tornado_origin);
  }else{
   float radius=g->radius<0?1:rnd(s);if(g->type==4)radius=sqrtf(radius);
   float dist=g->radius<0?-g->radius:radius*g->radius;
   float min=g->param[0],max=g->param[1];if(min==0&&max==0)max=2*PI;
   float angle;if(g->angle<0){*cur_angle+=angle_step;angle=*cur_angle;}else angle=(max-min)*rnd(s)+min;
   /* Shape7 keeps a uniform radial fraction and consumes the height RNG even
    * for the audited zero-height disc. Its cone angle is independent of radius. */
   float local[3]={dist*cosf(angle),dist*sinf(angle),g->type==7?rnd(s)*g->param[2]:0};rotate(g->velocity,local);for(unsigned j=0;j<3;++j)p->position[j]+=local[j];
   float speed=sqrtf(g->velocity[0]*g->velocity[0]+g->velocity[1]*g->velocity[1]+g->velocity[2]*g->velocity[2]),cone=fabsf(g->angle)*((s->position_attachment||g->type==4)?radius:1);
   if(g->type==7)cone=g->angle<0?PI/2-g->angle:PI/2+g->angle;
   float v[3]={speed*sinf(cone)*cosf(angle),speed*sinf(cone)*sinf(angle),speed*cosf(cone)};rotate(g->velocity,v);memcpy(p->velocity,v,sizeof v);
  }
  if(s->position_attachment&&g->type!=2&&!p->app_srt_handle){
   float offset[3];for(unsigned j=0;j<3;++j)offset[j]=p->position[j]-e->position[j];
   yaw_rotate(e->attachment_yaw,offset);yaw_rotate(e->attachment_yaw,p->velocity);
   for(unsigned j=0;j<3;++j){p->position[j]=e->position[j]+offset[j];p->velocity[j]*=e->attachment_scale;}
  }
  if(s->position_attachment&&p->app_srt_handle)for(unsigned j=0;j<3;++j)p->velocity[j]*=e->attachment_scale;
  return interpret(s,p);
 }
 return fault(s,e->generator,0,0,"particle pool exhausted");
}
static int tick(C3ParticleSystem *s)
{
 /* Source snapshots particle next before interpreting; F1 children initialize
  * immediately but must not receive a second update in this particle pass. */
 uint32_t particles[C3_PARTICLE_MAX_PARTICLES];for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)particles[i]=s->particles[i].active?s->particles[i].serial:0;
 /* Source display's stable bucket sort mutates the list. Between displays,
  * F0 births prepend; do not re-sort at simulation boundaries. */
 uint16_t update_order[C3_PARTICLE_MAX_PARTICLES];unsigned updates=C3_PARTICLE_MAX_PARTICLES;
 if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){updates=s->source_order_count;memcpy(update_order,s->source_order,updates*sizeof *update_order);}
 for(unsigned visit=0;visit<updates;++visit){unsigned i=s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1?update_order[visit]:visit;if(particles[i]&&s->particles[i].active&&s->particles[i].serial==particles[i])if(!interpret(s,&s->particles[i]))return 0;}
 /* Child emitters created by zero-wait particle initialization are eligible
  * in this generator pass. Checked native handle order owns cosmetic ordering. */
 uint32_t visited[C3_PARTICLE_MAX_EMITTERS]={0};unsigned passes=0;
 for(;;){int any=0;for(unsigned visit=0;visit<C3_PARTICLE_MAX_EMITTERS;++visit){unsigned i=visit;
   if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){unsigned best=C3_PARTICLE_MAX_EMITTERS;for(unsigned j=0;j<C3_PARTICLE_MAX_EMITTERS;++j){const C3ParticleEmitter *candidate=&s->emitters[j];if(!candidate->active||visited[j]==candidate->serial)continue;if(best==C3_PARTICLE_MAX_EMITTERS||candidate->depth==0||(s->emitters[best].depth!=0&&candidate->serial>s->emitters[best].serial))best=j;}if(best==C3_PARTICLE_MAX_EMITTERS)break;i=best;}
   C3ParticleEmitter *e=&s->emitters[i];if(!e->active||visited[i]==e->serial)continue;any=1;visited[i]=e->serial;
   if(++passes>C3_PARTICLE_MAX_EMITTERS*(C3_PARTICLE_MAX_DEPTH+1))return fault(s,e->generator,0,0,"per-tick emitter work bound");
   const C3ParticleGenerator *g=&s->bank.generators[e->generator];float rate=e->deferred?0:g->rate;
   /* Source expired owners set kind0x800 and skip RNG/emission entirely.
    * Retain frozen legacy accounting; Tackle alone admits that dormant path. */
   if(!(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1&&e->deferred))e->count+=rate<0?-rate:rate*rnd(s);
   float step=0,angle=0;if(g->angle<0&&e->count>=1){if(g->type==0||g->type==4||g->type==7){float min=g->param[0],max=g->param[1];if(s->position_attachment){min*=e->radius_scale;max*=e->radius_scale;}if(min==0&&max==0)max=2*PI;step=(max-min)/(int)e->count;angle=step*rnd(s)+min;}else if(g->type==2){step=2*PI/(int)e->count;angle=step*rnd(s);}}
   unsigned emitted=0;while(e->count>=1){if(++emitted>C3_PARTICLE_MAX_PARTICLES)return fault(s,e->generator,0,0,"emission budget exceeded");if(!emit(s,i,step,&angle))return 0;e->count-=1;}
   if(e->life&&!--e->life){
    if(!s->bank.profile)e->active=0;
    else{unsigned live=0;for(unsigned j=0;j<C3_PARTICLE_MAX_PARTICLES;++j)live+=s->particles[j].active&&s->particles[j].emitter_serial==e->serial;
     C3ParticleAppSRT *a=e->app_srt_handle?app_srt(s,e->app_srt_handle):NULL;
     if(e->app_srt_handle&&!a)return fault(s,e->generator,0,0,"stale AppSRT owner");
     if(live||(a&&a->references!=1)){e->life=1;e->deferred=1;}
     else{if(a&&!app_release(s,e->app_srt_handle))return 0;e->active=0;}
    }
   }
  }if(!any)break;
 }
 ++s->ticks;return 1;
}
int c3_particle_begin(C3ParticleSystem *out,const C3ParticleBank *bank,unsigned gen,uint32_t seed,char *e,size_t cap)
{
 if(!out||!bank||!bank->bytes||(!bank->profile&&gen>=bank->generator_count)||(bank->profile&&gen!=(bank->profile==C3_PARTICLE_PROFILE_GROWL_V1?18u:0u))||overlap(out,sizeof *out,bank->bytes,bank->length))return error(e,cap,"Invalid particle begin arguments.");
 /* Avoid a large temporary on the 3DS stack; validate before clearing output. */
 C3ParticleBank validated;if(bank->profile){if(!c3_particle_bank_decode_reachable(&validated,bank->bytes,bank->length,gen,bank->profile,e,cap))return 0;if(!c3_particle_bank_source_slot(&validated,gen,&gen))return error(e,cap,"Missing source root.");}
 else if(!c3_particle_bank_decode(&validated,bank->bytes,bank->length,e,cap))return 0;
 const C3ParticleGenerator *g=&validated.generators[gen];
 if(!emission_shape(&validated,g)||g->rate>C3_PARTICLE_MAX_PARTICLES||g->rate< -(float)C3_PARTICLE_MAX_PARTICLES||g->kind&0x30000)return error(e,cap,"Unsupported particle root emission shape/rate.");
 memset(out,0,sizeof *out);out->bank=validated;out->cosmetic_seed=seed;float zero[3]={0,0,0};
 if(!emitter(out,gen,0,zero,8))return error(e,cap,out->diagnostic);
 if(e&&cap)e[0]=0;
 return 1;
}
static int valid_position(const float v[3])
{return v&&isfinite(v[0])&&isfinite(v[1])&&isfinite(v[2])&&fabsf(v[0])<=1000000&&fabsf(v[1])<=1000000&&fabsf(v[2])<=1000000;}
static int red_constant_position(C3ParticleSystem *s,uint32_t handle,const float pos[3],char *e,size_t cap)
{
 if(!s->position_attachment||s->failed||s->cancelled||!valid_position(pos)||!handle||handle!=s->root_emitter_serial)return error(e,cap,"Invalid RED constant-point LIVE root handle/position.");
 for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i){const C3ParticleEmitter *v=&s->emitters[i];
  if(v->serial==handle&&v->depth==0&&v->generator==0){
   for(unsigned j=0;j<3;++j)if(pos[j]!=v->position[j])return error(e,cap,"Changed RED LIVE root is outside the constant-point profile.");
   if(e&&cap)e[0]=0;
   return 1;
  }
 }
 return error(e,cap,"Expired RED constant-point root record.");
}
int c3_particle_position(C3ParticleSystem *s,const float pos[3],char *e,size_t cap)
{
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1)return error(e,cap,"Tackle POSITION is a one-shot snapshot.");
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1)return error(e,cap,"Green-down POSITION is a one-shot snapshot.");
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_RED_DOWN_V1)return red_constant_position(s,s->root_emitter_serial,pos,e,cap);
 if(!s||!s->position_attachment||s->failed||!valid_position(pos))return error(e,cap,"Invalid POSITION particle attachment.");
 if(!s->cancelled)for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->emitters[i].serial==s->root_emitter_serial)memcpy(s->emitters[i].position,pos,sizeof s->emitters[i].position);
 if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_begin_position_flags(C3ParticleSystem *s,const C3ParticleBank *bank,unsigned gen,uint32_t seed,const float pos[3],float yaw,float scale,float radius_scale,uint16_t flags,char *e,size_t cap)
{
 if(flags!=1&&flags!=0x10a1&&flags!=0x18a9&&!(bank&&bank->profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1&&flags==0xa1))return error(e,cap,"Unsupported source POSITION attachment profile.");
 if(bank&&bank->profile==C3_PARTICLE_PROFILE_TACKLE_V1&&flags!=1)return error(e,cap,"Qualified Tackle POSITION requires source flags0x0001.");
 if(bank&&bank->profile==C3_PARTICLE_PROFILE_GROWL_V1&&flags!=0x10a1)return error(e,cap,"Qualified Growl POSITION requires source flags0x10A1.");
 if(bank&&bank->profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1&&flags!=0xa1)return error(e,cap,"Qualified Green-down POSITION requires source flags0xA1.");
 if(bank&&bank->profile==C3_PARTICLE_PROFILE_RED_DOWN_V1&&flags!=1)return error(e,cap,"Qualified Red-down POSITION requires source flags0x0001.");
 if(!valid_position(pos)||!isfinite(yaw)||fabsf(yaw)>1000000||!isfinite(scale)||scale<=0||scale>1000||!isfinite(radius_scale)||radius_scale<=0||radius_scale>1000)return error(e,cap,"Invalid POSITION particle start transform.");
 float position[3];memcpy(position,pos,sizeof position);
 if(!c3_particle_begin(s,bank,gen,seed,e,cap))return 0;
 s->position_attachment=1;s->source_attachment_flags=flags;s->root_emitter_serial=s->emitters[0].serial;
 s->emitters[0].attachment_yaw=yaw;s->emitters[0].attachment_scale=scale;s->emitters[0].radius_scale=(flags&0x20)?radius_scale:1;s->emitters[0].source_flags=flags;
 if(stat_down_profile(s->bank.profile)||s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){memcpy(s->emitters[0].position,position,sizeof position);if(e&&cap)e[0]=0;return 1;}
 return c3_particle_position(s,position,e,cap);
}
int c3_particle_append_position_flags(C3ParticleSystem *s,const C3ParticleBank *bank,unsigned gen,const float pos[3],float yaw,float scale,float radius_scale,uint16_t flags,uint32_t *handle,char *e,size_t cap)
{
 if(bank&&bank->profile)return error(e,cap,"Qualified particle profile admits a single source root only.");
 if(!s||!s->position_attachment||s->failed||s->cancelled||!bank||bank->bytes!=s->bank.bytes||bank->length!=s->bank.length||gen>=s->bank.generator_count||!handle)return error(e,cap,"Invalid POSITION append system/bank/root.");
 if((flags!=1&&flags!=0x10a1&&flags!=0x18a9)||!valid_position(pos)||!isfinite(yaw)||fabsf(yaw)>1000000||!isfinite(scale)||scale<=0||scale>1000||!isfinite(radius_scale)||radius_scale<=0||radius_scale>1000)return error(e,cap,"Invalid POSITION append transform/profile.");
 const C3ParticleGenerator *g=&s->bank.generators[gen];
 if((g->type!=0&&g->type!=1&&g->type!=2&&g->type!=8)||g->rate>C3_PARTICLE_MAX_PARTICLES||g->rate< -(float)C3_PARTICLE_MAX_PARTICLES||g->kind&0x30000)return error(e,cap,"Unsupported POSITION append root shape/rate.");
 /* Stage inputs before emitter allocation: callers may point into system data. */
 float position[3];memcpy(position,pos,sizeof position);
 if(!emitter(s,gen,0,position,8))return error(e,cap,s->diagnostic);
 for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->emitters[i].serial==s->spawned_emitters){C3ParticleEmitter *v=&s->emitters[i];v->attachment_yaw=yaw;v->attachment_scale=scale;v->radius_scale=(flags&0x20)?radius_scale:1;v->source_flags=flags;}
 *handle=s->spawned_emitters;
 if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_position_root(C3ParticleSystem *s,uint32_t handle,const float pos[3],char *e,size_t cap)
{
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1)return error(e,cap,"Tackle POSITION is a one-shot snapshot.");
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1)return error(e,cap,"Green-down POSITION is a one-shot snapshot.");
 if(s&&s->bank.profile==C3_PARTICLE_PROFILE_RED_DOWN_V1)return red_constant_position(s,handle,pos,e,cap);
 if(!s||!s->position_attachment||s->failed||s->cancelled||!handle||!valid_position(pos))return error(e,cap,"Invalid POSITION root handle/position.");
 for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->emitters[i].serial==handle&&s->emitters[i].depth==0){memmove(s->emitters[i].position,pos,sizeof s->emitters[i].position);if(e&&cap)e[0]=0;return 1;}
 return error(e,cap,"Expired or non-root POSITION handle.");
}
int c3_particle_begin_position(C3ParticleSystem *s,const C3ParticleBank *bank,unsigned gen,uint32_t seed,const float pos[3],float yaw,float scale,float radius_scale,char *e,size_t cap)
{return c3_particle_begin_position_flags(s,bank,gen,seed,pos,yaw,scale,radius_scale,0x18a9,e,cap);}
int c3_particle_step_ticks(C3ParticleSystem *s,uint32_t ticks,int paused,char *e,size_t cap)
{
 if(!s||s->failed)return error(e,cap,s?s->diagnostic:"Missing particle system.");
 if(s->cancelled||paused)return 1;
 if(ticks>C3_PARTICLE_MAX_CATCHUP){fault(s,0,0,0,"catch-up exceeds 600 source ticks");return error(e,cap,s->diagnostic);}
 for(uint32_t i=0;i<ticks;++i)if(!tick(s))return error(e,cap,s->diagnostic);
 if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_step_units(C3ParticleSystem *s,uint64_t units,int paused,char *e,size_t cap)
{
 if(!s||s->failed)return error(e,cap,s?s->diagnostic:"Missing particle system.");
 if(s->cancelled||paused)return 1;
 if(units>UINT64_MAX-s->remainder){fault(s,0,0,0,"source clock fraction overflow");return error(e,cap,s->diagnostic);}
 uint64_t elapsed=s->remainder+units,ticks=elapsed/1000;
 if(ticks>C3_PARTICLE_MAX_CATCHUP){fault(s,0,0,0,"catch-up exceeds 600 source ticks");return error(e,cap,s->diagnostic);}
 s->remainder=elapsed%1000;
 if(!c3_particle_step_ticks(s,(uint32_t)ticks,0,e,cap))return 0;
 if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_step(C3ParticleSystem *s,uint32_t ms,int paused,char *e,size_t cap)
{return c3_particle_step_units(s,(uint64_t)ms*60,paused,e,cap);}
void c3_particle_cancel(C3ParticleSystem *s)
{if(s){memset(s->particles,0,sizeof s->particles);memset(s->emitters,0,sizeof s->emitters);if(s->bank.profile)for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i){s->app_srt[i].active=0;s->app_srt[i].references=0;}s->cancelled=1;s->remainder=0;if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1)s->source_order_count=0;}}
int c3_particle_finished(const C3ParticleSystem *s)
{if(!s||s->failed)return 0;for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->emitters[i].active)return 0;for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(s->particles[i].active)return 0;return 1;}

static void transform(const float m[12],const float p[3],float out[3],int point)
{for(unsigned i=0;i<3;++i)out[i]=m[4*i]*p[0]+m[4*i+1]*p[1]+m[4*i+2]*p[2]+(point?m[4*i+3]:0);}
static int projected(const float m[16],const float p[3],float out[2])
{
 float w=m[12]*p[0]+m[13]*p[1]+m[14]*p[2]+m[15];if(fabsf(w)<FLT_MIN)return 0;
 for(unsigned i=0;i<2;++i)out[i]=(m[4*i]*p[0]+m[4*i+1]*p[1]+m[4*i+2]*p[2]+m[4*i+3])/w;
 return isfinite(out[0])&&isfinite(out[1]);
}
int c3_particle_quad(const C3Particle *p,const float attachment[12],const C3ParticleCamera *camera,C3ParticleQuad *out,char *e,size_t cap)
{
 if(!p||!attachment||!camera||!out){error(e,cap,"Missing particle quad arguments.");return -1;}
 if(!p->active||p->size<FLT_EPSILON||(p->kind&0x20000000))return 0;
 if(p->app_srt_handle){error(e,cap,"AppSRT particles require the checked system-aware adapter.");return -1;}
 if(p->kind&(0x80000000u|0x40000000u)){error(e,cap,"Unqualified particle lighting/point render mode.");return -1;}
 if((p->kind&0x100000)&&!(p->kind&0x80)){error(e,cap,"Unqualified non-PrimEnv particle trail.");return -1;}
 for(unsigned i=0;i<12;++i)if(!isfinite(attachment[i])){error(e,cap,"Nonfinite particle attachment.");return -1;}
 for(unsigned i=0;i<16;++i)if(!isfinite(camera->clip[i])){error(e,cap,"Nonfinite particle camera.");return -1;}
 float scale[3];for(unsigned j=0;j<3;++j)scale[j]=sqrtf(attachment[j]*attachment[j]+attachment[j+4]*attachment[j+4]+attachment[j+8]*attachment[j+8]);
 if(scale[0]<=FLT_EPSILON||fabsf(scale[0]-scale[1])>scale[0]*0.0001f||fabsf(scale[0]-scale[2])>scale[0]*0.0001f){error(e,cap,"Unqualified nonuniform particle attachment scale.");return -1;}
 for(unsigned j=0;j<3;++j)for(unsigned k=j+1;k<3;++k){float dot=attachment[j]*attachment[k]+attachment[j+4]*attachment[k+4]+attachment[j+8]*attachment[k+8];if(fabsf(dot)>scale[0]*scale[0]*0.0001f){error(e,cap,"Unqualified sheared particle attachment.");return -1;}}
 float cr=0,cu=0,cd=0;for(unsigned j=0;j<3;++j){cr+=camera->right[j]*camera->right[j];cu+=camera->up[j]*camera->up[j];cd+=camera->right[j]*camera->up[j];}
 if(!isfinite(cr)||!isfinite(cu)||fabsf(cr-1)>0.001f||fabsf(cu-1)>0.001f||fabsf(cd)>0.001f){error(e,cap,"Particle camera basis must be orthonormal.");return -1;}
 float position[3],velocity[3],r[3],u[3];transform(attachment,p->position,position,1);transform(attachment,p->velocity,velocity,0);
 if(p->kind&4){
  /* psdisp.c calcTornadoLastPos, using the owning source-generator snapshot. */
  float vz=p->velocity[2]-p->tornado_speed,phase=p->velocity[0]-p->tornado_turn;
  float rad=(fabsf(p->tornado_radius)+vz*tanf(fabsf(p->tornado_angle)))*p->velocity[1],x=rad*cosf(phase),y=rad*sinf(phase);
  float sa=sinf(p->grav),ca=cosf(p->grav),sb=sinf(p->fric),cb=cosf(p->fric);
  float prev[3]={x*cb+vz*sb+p->tornado_origin[0],-x*sa*sb+y*ca+vz*sa*cb+p->tornado_origin[1],-x*ca*sb-y*sa+vz*ca*cb+p->tornado_origin[2]},world[3];
  transform(attachment,prev,world,1);for(unsigned j=0;j<3;++j)velocity[j]=position[j]-world[j];
 }

 float half=p->size*scale[0]*((p->source_flags&0x80)&&p->attachment_scale>0?p->attachment_scale:1),angle=p->rotation;
 for(unsigned j=0;j<3;++j){r[j]=(camera->right[j]+camera->up[j])*half;u[j]=(camera->right[j]-camera->up[j])*half;}
 if(p->kind&(0x100000|0x200000)){
  float previous[3],a[2],b[2];for(unsigned j=0;j<3;++j)previous[j]=position[j]-velocity[j];
  if(projected(camera->clip,position,a)&&projected(camera->clip,previous,b)){
   float dx=a[0]-b[0],dy=a[1]-b[1];angle=fabsf(dy)<FLT_MIN?(dx>=0?PI/2:-PI/2):atan2f(dx,dy);
   if(p->kind&0x200000)angle+=p->rotation;
  }
 }
 if(fabsf(angle)>0.01f){
  float axis[3]={r[1]*u[2]-r[2]*u[1],r[2]*u[0]-r[0]*u[2],r[0]*u[1]-r[1]*u[0]};
  float length=sqrtf(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
  if(length<=FLT_MIN){error(e,cap,"Degenerate particle camera basis.");return -1;}
  for(unsigned j=0;j<3;++j)axis[j]/=length;
  float c=cosf(angle),s=sinf(angle);
  for(unsigned n=0;n<2;++n){float *v=n?u:r,original[3];memcpy(original,v,sizeof original);float dot=axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
   for(unsigned j=0;j<3;++j)v[j]=c*original[j]+s*(axis[(j+1)%3]*original[(j+2)%3]-axis[(j+2)%3]*original[(j+1)%3])+(1-c)*axis[j]*dot;
  }
 }
 C3ParticleQuad q={0};memcpy(q.prim,p->prim,4);memcpy(q.env,p->env,4);
 q.blend=(uint8_t)((p->kind>>22)&3);q.depth_test=(p->kind&0x10000000)==0;q.depth_write=(p->kind&8)!=0;
 q.mirror_s=(p->kind&0x20)!=0;q.mirror_t=(p->kind&0x40)!=0;q.filter_nearest=(p->kind&0x200)!=0;q.alpha_reference=q.blend>=2?0:1;
 q.texture_group=p->tex_group;q.texture_frame=p->pose;q.prim_env=(p->kind&0x80)!=0;q.trail=(p->kind&0x100000)!=0;
 if(!q.prim_env)memset(q.env,0,4);
 static const unsigned coords[4][2]={{0,1},{0,0},{1,0},{1,1}};
 for(unsigned i=0;i<4;++i){
  C3ParticleQuadVertex *v=&q.vertices[i];const float *axis=(i&1)?u:r;float sign=i<2?-1:1;
  for(unsigned j=0;j<3;++j){v->position[j]=position[j]+sign*axis[j];if(q.trail&&(i==0||i==3))v->position[j]-=velocity[j];if(!isfinite(v->position[j])){error(e,cap,"Particle quad overflow.");return -1;}}
  unsigned sx=coords[i][0],ty=coords[i][1];if(p->kind&0x40000)sx=1-sx;if(p->kind&0x80000)ty=1-ty;
  v->uv[0]=(float)sx*(q.mirror_s?2:1);v->uv[1]=(float)ty*(q.mirror_t?2:1)+(q.mirror_t?1:0);
  memset(v->rgba,255,4);
  if(q.trail&&(i==0||i==3)){float alpha=255*p->trail;if(alpha<0)alpha=0;if(alpha>255)alpha=255;v->rgba[3]=(uint8_t)alpha;}
 }
 *out=q;if(e&&cap)e[0]=0;return 1;
}
int c3_particle_app_srt_geometry(C3ParticleSystem *s,const C3Particle *p,const C3ParticleCamera *c,uint64_t frame,float position[3],float velocity[3],char *e,size_t cap)
{
 if(!s||!p||!c||!position||!velocity||s->failed||s->cancelled||s->bank.profile!=C3_PARTICLE_PROFILE_GROWL_V1||!p->active||!p->app_srt_handle||
    overlap(position,12,s,sizeof *s)||overlap(velocity,12,s,sizeof *s)||overlap(position,12,velocity,12))return error(e,cap,"Invalid qualified AppSRT geometry arguments.");
 C3ParticleAppSRT *a=app_srt(s,p->app_srt_handle);
 if(!a||!a->references||a->status!=0)return error(e,cap,"Stale AppSRT handle or unsupported SRT status/setter.");
 float rr=0,uu=0,ru=0;
 for(unsigned i=0;i<3;++i){if(!isfinite(c->right[i])||!isfinite(c->up[i])||!isfinite(a->translate[i])||!isfinite(p->position[i])||!isfinite(p->velocity[i]))return error(e,cap,"Nonfinite AppSRT geometry.");rr+=c->right[i]*c->right[i];uu+=c->up[i]*c->up[i];ru+=c->right[i]*c->up[i];}
 if(fabsf(rr-1)>.001f||fabsf(uu-1)>.001f||fabsf(ru)>.001f)return error(e,cap,"AppSRT camera basis must be orthonormal.");
 /* HSD_MtxGetRotationMtx(inverse_view,'z','x'): normalize Z and X,
  * then Y=Z cross X, X=Y cross Z. No actor yaw enters this basis. */
 float z[3],x[3],y[3];
 for(unsigned i=0;i<3;++i){z[i]=c->right[(i+1)%3]*c->up[(i+2)%3]-c->right[(i+2)%3]*c->up[(i+1)%3];x[i]=c->right[i]/sqrtf(rr);}
 float zz=sqrtf(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);if(zz<=FLT_EPSILON)return error(e,cap,"Degenerate AppSRT camera basis.");
 for(unsigned i=0;i<3;++i)z[i]/=zz;
 for(unsigned i=0;i<3;++i)y[i]=z[(i+1)%3]*x[(i+2)%3]-z[(i+2)%3]*x[(i+1)%3];
 for(unsigned i=0;i<3;++i)x[i]=y[(i+1)%3]*z[(i+2)%3]-y[(i+2)%3]*z[(i+1)%3];
 if(!a->cache_valid||a->render_frame!=frame){memset(a->matrix,0,sizeof a->matrix);a->matrix[0]=a->matrix[5]=a->matrix[10]=1;for(unsigned i=0;i<3;++i)a->matrix[4*i+3]=a->translate[i];a->render_frame=frame;a->cache_valid=1;}
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(a->matrix[4*i+j]!=(i==j?1.f:0.f))return error(e,cap,"Unqualified nonidentity AppSRT rotation/scale cache.");
 float world[3],motion[3];
 for(unsigned i=0;i<3;++i){world[i]=a->matrix[4*i+3]+x[i]*p->position[0]+y[i]*p->position[1]+z[i]*p->position[2];motion[i]=x[i]*p->velocity[0]+y[i]*p->velocity[1]+z[i]*p->velocity[2];if(!isfinite(world[i])||!isfinite(motion[i]))return error(e,cap,"AppSRT geometry overflow.");}
 memcpy(position,world,sizeof world);memcpy(velocity,motion,sizeof motion);if(e&&cap)e[0]=0;return 1;
}
int c3_particle_quad_app_srt(C3ParticleSystem *s,const C3Particle *p,const C3ParticleCamera *c,uint64_t frame,float native_scale,C3ParticleQuad *out,char *e,size_t cap)
{
 if(!p||!out||!isfinite(native_scale)||native_scale<=0){error(e,cap,"Invalid AppSRT quad conversion.");return -1;}
 if(!p->active||p->size<FLT_EPSILON||(p->kind&0x20000000u))return 0;
 if(!p->app_srt_handle){error(e,cap,"AppSRT quad requires an AppSRT particle.");return -1;}
 C3Particle converted=*p;
 if(!c3_particle_app_srt_geometry(s,p,c,frame,converted.position,converted.velocity,e,cap))return -1;
 converted.app_srt_handle=0;float attachment[12]={native_scale,0,0,0,0,native_scale,0,0,0,0,native_scale,0};
 return c3_particle_quad(&converted,attachment,c,out,e,cap);
}

static unsigned draw_key(const C3Particle *p)
{return ((p->kind>>25)&7u)+((p->kind&8u)?0u:8u);}
int c3_particle_render_indices(C3ParticleSystem *s,uint16_t *indices,size_t capacity,size_t *count,char *e,size_t cap)
{
 if(!s||!indices||!count||s->failed)return error(e,cap,"Invalid particle draw index arguments.");
 size_t n=0;for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(s->particles[i].active){
  if(n>=capacity)return error(e,cap,"Particle draw index capacity exceeded.");
  size_t at=n++;if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){const C3Particle *p=&s->particles[i];
   while(at){const C3Particle *before=&s->particles[indices[at-1]];if(draw_key(before)<draw_key(p)||(draw_key(before)==draw_key(p)&&before->serial>=p->serial))break;indices[at]=indices[at-1];--at;}}
  indices[at]=(uint16_t)i;
 }
 if(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1){memcpy(s->source_order,indices,n*sizeof *indices);s->source_order_count=(uint16_t)n;}
 *count=n;if(e&&cap)e[0]=0;return 1;
}
