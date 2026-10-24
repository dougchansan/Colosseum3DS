#include "colosseum3ds/particle.h"
#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GREEN_BYTES 1644u
#define PI 3.14159265358979323846f
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void putf(uint8_t *p,float f){uint32_t v;memcpy(&v,&f,4);put32(p,v);}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned at(unsigned i){return i==0?56:i==1?128:288;}
static void fixture(uint8_t b[GREEN_BYTES])
{
 memset(b,0,GREEN_BYTES);memcpy(b,"GPT1",4);put32(b+4,32);put32(b+8,448);put32(b+12,1184);put32(b+16,1632);put16(b+32,0x43);put32(b+40,3);
 const float params[3][12]={{0,0,0,0,0,0,0,-1,1,0,2*PI,0},{0,0,0,1,0,-10,-PI/2,-8,1,-2*PI,2*PI,0},{0,0,0,1,0,-8,PI/2,-4,1,-2*PI,2*PI,0}};
 const uint16_t life[3][2]={{1,1000},{32,16},{28,26}};
 for(unsigned i=0;i<3;++i){unsigned p=at(i);put32(b+44+4*i,p-32);put32(b+1632+4*i,i);put16(b+p,i?7:0);put16(b+p+2,i==2?1:0);put16(b+p+4,life[i][0]);put16(b+p+6,life[i][1]);put32(b+p+8,i?0x400000:0x1000000);for(unsigned j=0;j<12;++j)putf(b+p+12+4*j,params[i][j]);}
 const uint8_t root[]={0xf0,0,1,7,0xf0,0,2,7,0xff};memcpy(b+116,root,sizeof root);
 const uint8_t child[]={0x40,0,0x8f,0,0,0,0,0x41,0xb0,0,0,0,0,0,0,0xad,2,0xff};
 memcpy(b+188,child,sizeof child);memcpy(b+348,child,sizeof child);
 put32(b+448,2);put32(b+452,12);put32(b+456,64);
 for(unsigned i=0;i<2;++i){unsigned p=i?512:460;put32(b+p,i?2:1);put32(b+p+4,i?0:1);put32(b+p+12,i?8:32);put32(b+p+16,i?8:32);}
}
static float random_value(uint32_t *state){*state=*state*214013u+2531011u;return(float)(*state>>16)/65536.f;}
static void near(float a,float b){assert(fabsf(a-b)<.0002f);}
static void synthetic(void)
{
 uint8_t raw[GREEN_BYTES];fixture(raw);char e[192];C3ParticleBank bank,old;
 assert(c3_particle_bank_decode(&old,raw,sizeof raw,e,sizeof e));assert(!old.profile);
 assert(c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,C3_PARTICLE_PROFILE_GREEN_DOWN_V1,e,sizeof e));assert(bank.generator_count==3&&bank.source_generator_count==3&&bank.profile==2);
 for(unsigned i=0;i<3;++i){unsigned slot;assert(c3_particle_bank_source_slot(&bank,i,&slot)&&slot==i);}
 C3ParticleBank unchanged=bank;
 assert(!c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,1,2,e,sizeof e));assert(!memcmp(&bank,&unchanged,sizeof bank));
 raw[188+2]=0x8e;assert(!c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,2,e,sizeof e));fixture(raw);
 putf(raw+188+7,21);assert(!c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,2,e,sizeof e));fixture(raw);
 put16(raw+128,6);assert(!c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,2,e,sizeof e));fixture(raw);
 putf(raw+128+32,-9);assert(!c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,2,e,sizeof e));fixture(raw);
 C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);float pos[3]={100,200,-30};
 assert(!c3_particle_begin_position_flags(a,&old,0,123,pos,0,.75f,.75f,0xa1,e,sizeof e));
 assert(c3_particle_begin(a,&old,0,123,e,sizeof e));assert(!c3_particle_step_ticks(a,1,0,e,sizeof e)); /* old admission still rejects shape7 */
 assert(c3_particle_bank_decode_reachable(&bank,raw,sizeof raw,0,2,e,sizeof e));
 assert(!c3_particle_begin_position_flags(a,&bank,0,123,pos,0,.75f,.75f,0x10a1,e,sizeof e));
 assert(c3_particle_begin_position_flags(a,&bank,0,123,pos,0,.75f,.75f,0xa1,e,sizeof e));
 assert(!c3_particle_position(a,pos,e,sizeof e));assert(!c3_particle_position_root(a,a->root_emitter_serial,pos,e,sizeof e));
 assert(c3_particle_begin_position_flags(b,&bank,0,123,pos,PI/2,.75f,.75f,0xa1,e,sizeof e));
 assert(c3_particle_step_ticks(a,1,0,e,sizeof e)&&c3_particle_step_ticks(b,1,0,e,sizeof e));
 assert(a->emitted==13&&a->cosmetic_draws==19); /* root2 + shape7 seed1 +8height +4*(angle,height) */
 C3Particle *p=&a->particles[0],*q=&b->particles[0];assert(p->generator==1&&!p->app_srt_handle);
 uint32_t rng=123;random_value(&rng);random_value(&rng);float azimuth=-1.5f*PI+(3*PI/8)*(1+random_value(&rng));
 near(p->position[0],100+7.5f*cosf(azimuth));near(p->position[2],-30-7.5f*sinf(azimuth));
 near(p->position[1],200+22-.75f);near(p->velocity[1],-.75f);
 near(q->position[0]-100,p->position[2]+30);near(q->position[2]+30,-(p->position[0]-100));near(q->position[1],p->position[1]);
 /* Pause/partitioning and lifetime are source-clock driven, not a fixed FX cut. */
 assert(c3_particle_begin_position_flags(b,&bank,0,123,pos,0,.75f,.75f,0xa1,e,sizeof e));
 assert(c3_particle_step_ticks(a,5,1,e,sizeof e));assert(a->ticks==1&&a->cosmetic_draws==19);
 assert(c3_particle_step_ticks(a,39,0,e,sizeof e));for(unsigned tick=0;tick<40;++tick)assert(c3_particle_step_ticks(b,1,0,e,sizeof e));
 assert(!memcmp(a,b,sizeof *a)&&c3_particle_finished(a));assert(a->emitted==369&&a->peak_particles==24&&a->cosmetic_draws==518);
 assert(c3_particle_begin_position_flags(a,&bank,0,123,pos,0,.75f,.75f,0xa1,e,sizeof e));assert(c3_particle_step_ticks(a,1,0,e,sizeof e));c3_particle_cancel(a);assert(c3_particle_finished(a));
 free(a);free(b);
}
static uint8_t *read_file(const char *path,size_t *length)
{FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);uint8_t *p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*length=(size_t)n;return p;}
static void actual(const char *raw_path,const char *pack_path)
{
 size_t length;uint8_t *raw=read_file(raw_path,&length);C3ParticleBank bank;char e[192];
 if(!c3_particle_bank_decode_reachable(&bank,raw,length,0,2,e,sizeof e)){fprintf(stderr,"%s\n",e);assert(0);}
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);float pos[3]={100,200,-30};
 assert(c3_particle_begin_position_flags(s,&bank,0,123,pos,.7f,.75f,.75f,0xa1,e,sizeof e));
 unsigned ticks=0;while(!c3_particle_finished(s)){assert(ticks<300);assert(c3_particle_step_ticks(s,1,0,e,sizeof e));++ticks;
  if(ticks==1){assert(s->emitted==13&&s->cosmetic_draws==47);C3Particle *p=&s->particles[0];
   uint32_t rng=123;random_value(&rng);random_value(&rng);float angle=-1.5f*PI+(3*PI/8)*(1+random_value(&rng));random_value(&rng);float speed=1+7*random_value(&rng);
   float x=7.5f*cosf(angle),z=-7.5f*sinf(angle);near(p->position[0],100+cosf(.7f)*x+sinf(.7f)*z);near(p->position[2],-30-sinf(.7f)*x+cosf(.7f)*z);near(p->position[1],200+22-speed*.74f);
  }
 }
 assert(ticks==54&&s->emitted==369&&s->peak_particles==232&&s->cosmetic_draws==1772);
 printf("Original Green-down root0: natural_end%u emitted%u peak%u RNG%llu\n",ticks,s->emitted,s->peak_particles,(unsigned long long)s->cosmetic_draws);
 if(pack_path){size_t bytes;uint8_t *data=read_file(pack_path,&bytes),hash[20];C3ParticlePack pack;c3_save_codec_sha1(hash,data,(uint32_t)bytes);
  assert(c3_particle_pack_decode_reachable(&pack,data,bytes,hash,2,e,sizeof e));assert(pack.bank.profile==2&&pack.root_generator==0&&pack.texture_count==3&&pack.bank.bytes==data+get32(data+16));
  /* Re-authenticating a modified whole pack must not bypass the source pin. */
  data[get32(data+16)+600]^=1;c3_save_codec_sha1(hash,data,(uint32_t)bytes);
  assert(!c3_particle_pack_decode_reachable(&pack,data,bytes,hash,2,e,sizeof e));assert(strstr(e,"source GPT1 SHA-1"));free(data);
 }
 free(s);free(raw);
}
int main(int argc,char **argv){synthetic();if(argc>1)actual(argv[1],argc>2?argv[2]:NULL);puts("Qualified Green-down shape7/8F/one-shot checks passed");return 0;}
