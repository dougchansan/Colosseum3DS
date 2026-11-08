#include "colosseum3ds/particle.h"
#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FIXTURE_BYTES 15556u
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void putf(uint8_t *p,float f){uint32_t v;memcpy(&v,&f,4);put32(p,v);}
static unsigned at(unsigned id){return 512+128*id;}
static void fixture(uint8_t *b)
{
 memset(b,0,FIXTURE_BYTES);memcpy(b,"GPT1",4);put32(b+4,32);put32(b+8,14976);put32(b+16,15104);put16(b+32,0x43);put32(b+40,113);
 for(unsigned i=0;i<113;++i){put32(b+44+4*i,at(i)-32);put32(b+15104+4*i,i);unsigned p=at(i);put16(b+p+4,1);put16(b+p+6,1000);put32(b+p+8,0x1000000);putf(b+p+40,-1);b[p+60]=0xff;}
 put32(b+14976,2);for(unsigned i=0;i<2;++i){put32(b+14980+4*i,16+32*i);unsigned t=14992+32*i;put32(b+t,8);put32(b+t+12,8);put32(b+t+16,8);}
 const unsigned ids[3]={93,111,112};const uint32_t kinds[3]={0x1420100,0x1420000,0x1400001};
 const uint8_t child[]={0x40,0,0xa0,0,0x3f,0x80,0,0,2,0xff};
 for(unsigned i=0;i<3;++i){unsigned p=at(ids[i]);put16(b+p+2,i==1?0:1);put32(b+p+8,kinds[i]);memcpy(b+p+60,child,sizeof child);}
 put16(b+at(112),8);putf(b+at(112)+32,-1);putf(b+at(93)+32,-.2f);
 const uint8_t root[]={0xf0,0,93,7,0xf0,0,111,7,0xf0,0,112,7,0xff};memcpy(b+at(18)+60,root,sizeof root);
}
static C3ParticleCamera camera={{1,0,0},{0,1,0},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
static void near(float a,float b){assert(fabsf(a-b)<.0001f);}
static void synthetic(void)
{
 uint8_t *raw=malloc(FIXTURE_BYTES);assert(raw);fixture(raw);char e[192];C3ParticleBank bank,unchanged;memset(&unchanged,0x55,sizeof unchanged);bank=unchanged;
 assert(!c3_particle_bank_decode(&bank,raw,FIXTURE_BYTES,e,sizeof e));assert(!memcmp(&bank,&unchanged,sizeof bank));
 assert(!c3_particle_bank_decode_reachable(NULL,raw,FIXTURE_BYTES,18,1,e,sizeof e));
 assert(!c3_particle_bank_decode_reachable((C3ParticleBank *)raw,raw,FIXTURE_BYTES,18,1,e,sizeof e));
 assert(c3_particle_bank_decode_reachable(&bank,raw,FIXTURE_BYTES,18,C3_PARTICLE_PROFILE_GROWL_V1,e,sizeof e));
 assert(bank.generator_count==4&&bank.source_generator_count==113&&bank.bytes==raw);unsigned slot;
 assert(c3_particle_bank_source_slot(&bank,111,&slot)&&slot==2);assert(!c3_particle_bank_source_slot(&bank,19,&slot));
 assert(!c3_particle_bank_decode_reachable(&bank,raw,FIXTURE_BYTES,93,C3_PARTICLE_PROFILE_GROWL_V1,e,sizeof e));
 raw[at(93)+60]=0xf5;assert(!c3_particle_bank_decode_reachable(&bank,raw,FIXTURE_BYTES,18,C3_PARTICLE_PROFILE_GROWL_V1,e,sizeof e));fixture(raw);
 put32(raw+15104+4*93,111);assert(!c3_particle_bank_decode_reachable(&bank,raw,FIXTURE_BYTES,18,C3_PARTICLE_PROFILE_GROWL_V1,e,sizeof e));fixture(raw);
 assert(c3_particle_bank_decode_reachable(&bank,raw,FIXTURE_BYTES,18,1,e,sizeof e));
 C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);float pos[3]={100,20,-30};
 assert(!c3_particle_begin_position_flags(a,&bank,18,123,pos,0,1,1,1,e,sizeof e));
 assert(c3_particle_begin_position_flags(a,&bank,18,123,pos,0,1,1,0x10a1,e,sizeof e));
 assert(c3_particle_begin_position_flags(b,&bank,18,123,pos,1.5f,1,1,0x10a1,e,sizeof e));
 assert(c3_particle_step_ticks(a,1,0,e,sizeof e)&&c3_particle_step_ticks(b,1,0,e,sizeof e));
 assert(a->spawned_emitters==4&&a->emitted==4&&a->cosmetic_draws==7);
 unsigned found=0;for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(a->particles[i].active&&a->particles[i].app_srt_handle){
  C3Particle *p=&a->particles[i];assert(!memcmp(p->position,b->particles[i].position,sizeof p->position));
  C3ParticleAppSRT *srt=&a->app_srt[p->app_srt_handle&15];assert(srt->active&&srt->references==2);near(srt->translate[0],100);
  C3Particle copy=*p;copy.position[0]=1;copy.position[1]=2;copy.position[2]=3;copy.velocity[0]=4;copy.velocity[1]=5;copy.velocity[2]=6;
  C3ParticleCamera roll=camera;roll.right[0]=0;roll.right[1]=1;roll.up[0]=-1;roll.up[1]=0;
  float out[3],velocity[3];assert(c3_particle_app_srt_geometry(a,&copy,&roll,7,out,velocity,e,sizeof e));near(out[0],98);near(out[1],21);near(out[2],-27);near(velocity[0],-5);near(velocity[1],4);near(velocity[2],6);
  srt->translate[0]=110;assert(c3_particle_app_srt_geometry(a,&copy,&roll,7,out,velocity,e,sizeof e));near(out[0],98);
  assert(c3_particle_app_srt_geometry(a,&copy,&roll,8,out,velocity,e,sizeof e));near(out[0],108);
  srt->status=1;assert(!c3_particle_app_srt_geometry(a,&copy,&roll,9,out,velocity,e,sizeof e));srt->status=0;
  assert(c3_particle_app_srt_geometry(a,&copy,&roll,9,out,velocity,e,sizeof e));srt->matrix[0]=2;assert(!c3_particle_app_srt_geometry(a,&copy,&roll,9,out,velocity,e,sizeof e));srt->matrix[0]=1;
  copy.app_srt_handle+=16;assert(!c3_particle_app_srt_geometry(a,&copy,&roll,9,out,velocity,e,sizeof e));copy=*p;
  C3ParticleQuad quad;float native[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};assert(c3_particle_quad(&copy,native,&camera,&quad,e,sizeof e)==-1);
  assert(c3_particle_quad_app_srt(a,&copy,&camera,10,.1f,&quad,e,sizeof e)==1);++found;
 }
 assert(found==2);assert(c3_particle_step_ticks(a,1,0,e,sizeof e));assert(a->cosmetic_draws==10&&!c3_particle_finished(a));
 C3Particle old=a->particles[0];assert(c3_particle_step_ticks(a,1,0,e,sizeof e));assert(a->cosmetic_draws==13&&c3_particle_finished(a));
 float out[3],velocity[3];assert(!c3_particle_app_srt_geometry(a,&old,&camera,12,out,velocity,e,sizeof e));
 for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i)assert(!a->app_srt[i].active&&!a->app_srt[i].references);
 assert(c3_particle_step_ticks(b,2,0,e,sizeof e));assert(b->cosmetic_draws==a->cosmetic_draws);
 assert(c3_particle_begin_position_flags(a,&bank,18,123,pos,0,1,1,0x10a1,e,sizeof e));assert(c3_particle_step_ticks(a,1,0,e,sizeof e));old=a->particles[0];
 unsigned references=0;for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i)references+=a->app_srt[i].references;assert(references==4);
 c3_particle_cancel(a);assert(!c3_particle_app_srt_geometry(a,&old,&camera,0,out,velocity,e,sizeof e));
 for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i)assert(!a->app_srt[i].active&&!a->app_srt[i].references);
 C3ParticleAppSRT cancelled[C3_PARTICLE_MAX_APP_SRT];memcpy(cancelled,a->app_srt,sizeof cancelled);c3_particle_cancel(a);assert(!memcmp(cancelled,a->app_srt,sizeof cancelled));assert(c3_particle_finished(a));
 free(a);free(b);free(raw);
}
static void *read_file(const char *path,size_t *length)
{FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);void *p=malloc((size_t)n);assert(p);assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);*length=(size_t)n;return p;}
static void actual(const char *raw_path,const char *pack_path)
{
 size_t n;void *raw=read_file(raw_path,&n);C3ParticleBank bank;char e[192];assert(!c3_particle_bank_decode(&bank,raw,n,e,sizeof e));
 if(!c3_particle_bank_decode_reachable(&bank,raw,n,18,1,e,sizeof e)){fprintf(stderr,"%s\n",e);assert(0);}
 C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);float pos[3]={10,20,-30};
 assert(c3_particle_begin_position_flags(a,&bank,18,123,pos,.7f,.75f,.75f,0x10a1,e,sizeof e));assert(c3_particle_begin_position_flags(b,&bank,18,123,pos,.7f,.75f,.75f,0x10a1,e,sizeof e));
 unsigned srt_samples=0,world_samples=0;
 for(unsigned tick=0;tick<180;++tick){assert(c3_particle_step_ticks(a,1,0,e,sizeof e));
  for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(a->particles[i].active){C3Particle *p=&a->particles[i];C3ParticleQuad q;
   if(p->app_srt_handle){if(c3_particle_quad_app_srt(a,p,&camera,tick,.1f,&q,e,sizeof e)<0){fprintf(stderr,"%s\n",e);assert(0);}++srt_samples;}
   else ++world_samples;
  }
 }
 assert(c3_particle_finished(a)&&srt_samples&&world_samples);assert(c3_particle_step(b,3000,0,e,sizeof e));
 /* Rendering updates caches only; compare simulation and RNG independently. */
 assert(a->cosmetic_draws==b->cosmetic_draws&&a->emitted==b->emitted&&a->ticks==b->ticks);
 printf("Growl original113/decoded4: emitted%u peak%u RNG%llu AppSRTsamples%u worldsamples%u\n",a->emitted,a->peak_particles,(unsigned long long)a->cosmetic_draws,srt_samples,world_samples);
 if(pack_path){size_t bytes;void *data=read_file(pack_path,&bytes);uint8_t hash[20];c3_save_codec_sha1(hash,data,(uint32_t)bytes);C3ParticlePack pack;
  assert(!c3_particle_pack_decode(&pack,data,bytes,hash,e,sizeof e));
  if(!c3_particle_pack_decode_reachable(&pack,data,bytes,hash,1,e,sizeof e)){fprintf(stderr,"%s\n",e);assert(0);}
  assert(pack.root_generator==18&&pack.bank.generator_count==4&&pack.bank.source_generator_count==113&&pack.texture_count==25);free(data);
 }
 free(a);free(b);free(raw);
}
int main(int argc,char **argv){synthetic();if(argc>1)actual(argv[1],argc>2?argv[2]:NULL);puts("Qualified sparse Growl AppSRT foundation checks passed");return 0;}
