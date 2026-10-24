#include "colosseum3ds/particle.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void putf(uint8_t *p,float f){uint32_t v;memcpy(&v,&f,4);put32(p,v);}
static unsigned at(unsigned gen){return 96+128*gen;}
static void fixture(uint8_t b[2048])
{
 memset(b,0,2048);memcpy(b,"GPT1",4);put32(b+4,32);put32(b+8,1024);put32(b+16,1984);put16(b+32,0x43);put32(b+40,6);
 for(unsigned i=0;i<6;++i){unsigned p=at(i);put32(b+44+4*i,p-32);put16(b+p+2,i==5?0:1);put16(b+p+4,1);put16(b+p+6,100);put32(b+p+8,0x100);putf(b+p+40,-1);putf(b+p+44,2);b[p+60]=0xff;put32(b+1984+4*i,i);}
 put32(b+1024,2);put32(b+1028,16);put32(b+1032,48);
 for(unsigned i=0;i<2;++i){put32(b+1040+32*i,3);put32(b+1052+32*i,8);put32(b+1056+32*i,8);}
 /* Synthetic control/spawn program; different timing and scalar profile from retail. */
 const uint8_t root[]={2,0xf0,0,0,7,0xf0,0,5,7,0xff};memcpy(b+at(3)+60,root,sizeof root);
 /* AC consumes TWO floats before the next wait: regression for importer discrepancy. */
 const uint8_t child[]={0x40,1,0xcf,0,10,20,30,0,0xac,2,0x3f,0x80,0,0,0x3f,0x80,0,0,0xcf,2,10,20,30,255,2,0xff};memcpy(b+at(0)+60,child,sizeof child);
 const uint8_t child2[]={0x40,2,0xad,0xdf,0,20,30,40,0,0xe6,0xe8,0x3f,0x80,0,0,3,0xff};memcpy(b+at(5)+60,child2,sizeof child2);
}
static void synthetic(void)
{
 uint8_t bytes[2048];fixture(bytes);C3ParticleBank bank;char error[160];C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);
 assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(bank.generators[3].life==100);assert(bank.generators[3].kind==0x08000100);
 assert(c3_particle_begin(a,&bank,3,42,error,sizeof error));assert(c3_particle_begin(b,&bank,3,42,error,sizeof error));
 assert(c3_particle_step_ticks(a,2,0,error,sizeof error));assert(a->emitted==1&&a->spawned_emitters==1);
 assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(a->emitted==3&&a->spawned_emitters==3);
 unsigned child=128;for(unsigned i=0;i<128;++i)if(a->particles[i].active&&a->particles[i].generator==0)child=i;
 assert(child<128);assert(a->particles[child].size_count==2);assert(a->particles[child].size_target>=1&&a->particles[child].size_target<2);assert(a->particles[child].pose==1);assert(a->particles[child].prim[3]==0);
 assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(a->particles[child].prim[3]==127);
 assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(!a->particles[child].active);
 assert(c3_particle_step_ticks(a,55,0,error,sizeof error));
 for(unsigned i=0;i<1000;++i)assert(c3_particle_step(b,1,0,error,sizeof error));
 assert(!memcmp(a,b,sizeof *a));assert(c3_particle_finished(a));
 C3ParticleSystem saved=*b;assert(c3_particle_step(b,9999,1,error,sizeof error));assert(!memcmp(&saved,b,sizeof saved));
 c3_particle_cancel(b);assert(c3_particle_finished(b));unsigned emitted=b->emitted;assert(c3_particle_step(b,99999,0,error,sizeof error));assert(b->emitted==emitted);
 /* Unknown reached instruction reports the source generator and byte offset. */
 fixture(bytes);bytes[at(3)+60]=0x99;assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin(b,&bank,3,2,error,sizeof error));assert(!c3_particle_step_ticks(b,1,0,error,sizeof error));assert(strstr(error,"generator 3")&&strstr(error,"opcode 0x99"));
 fixture(bytes);assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin(b,&bank,3,2,error,sizeof error));assert(!c3_particle_step_ticks(b,601,0,error,sizeof error));assert(strstr(error,"catch-up"));
 fixture(bytes);put32(bytes+44,0xfffffff0u);assert(!c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));
 fixture(bytes);putf(bytes+at(3)+12,NAN);assert(!c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));
 /* Zero-wait instruction stream and allocation/nesting limits fail explicitly. */
 fixture(bytes);memset(bytes+at(5)+60,0,1024-at(5)-60);assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin(b,&bank,5,2,error,sizeof error));assert(!c3_particle_step_ticks(b,1,0,error,sizeof error));assert(strstr(error,"instruction budget"));
 fixture(bytes);put16(bytes+at(3)+4,10);putf(bytes+at(3)+40,-(float)C3_PARTICLE_MAX_PARTICLES);bytes[at(3)+60]=127;bytes[at(3)+61]=255;assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin(b,&bank,3,2,error,sizeof error));assert(c3_particle_step_ticks(b,1,0,error,sizeof error));assert(!c3_particle_step_ticks(b,10,0,error,sizeof error));assert(strstr(error,"particle pool"));
 fixture(bytes);const uint8_t recurse[]={0xf0,0,3,7,0xff};memcpy(bytes+at(3)+60,recurse,sizeof recurse);assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin(b,&bank,3,2,error,sizeof error));assert(!c3_particle_step_ticks(b,20,0,error,sizeof error));assert(strstr(error,"nesting"));
 free(a);free(b);
}
static void position_attachment(void)
{
 uint8_t bytes[2048];fixture(bytes);unsigned pos=at(1);
 put16(bytes+pos,1);put16(bytes+pos+4,5);putf(bytes+pos+20,2);putf(bytes+pos+24,0);putf(bytes+pos+28,0);putf(bytes+pos+48,4);bytes[pos+60]=127;
 C3ParticleBank bank;char error[160];C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);
 assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));float origin[3]={10,20,30};
 assert(c3_particle_begin_position(a,&bank,1,42,origin,1.57079632679f,.75f,.75f,error,sizeof error));
 assert(c3_particle_begin_position(b,&bank,1,42,origin,1.57079632679f,.75f,.75f,error,sizeof error));
 assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(c3_particle_step(b,17,0,error,sizeof error));
 assert(a->particles[0].active);assert(fabsf(a->particles[0].velocity[0])<.00001f);assert(fabsf(a->particles[0].velocity[2]+1.5f)<.00001f);assert(a->particles[0].attachment_scale==.75f);
 float old[3];memcpy(old,a->particles[0].position,sizeof old);origin[0]=100;
 assert(c3_particle_position(a,origin,error,sizeof error));assert(c3_particle_position(b,origin,error,sizeof error));assert(!memcmp(old,a->particles[0].position,sizeof old));
 assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(c3_particle_step(b,17,0,error,sizeof error));assert(a->particles[1].position[0]>99.9f);assert(a->particles[0].position[0]<11);
 C3ParticleSystem saved=*a;assert(c3_particle_step_ticks(a,100,1,error,sizeof error));assert(!memcmp(&saved,a,sizeof saved));
 origin[0]=NAN;assert(!c3_particle_position(a,origin,error,sizeof error));assert(!memcmp(&saved,a,sizeof saved));
 assert(a->cosmetic_draws==b->cosmetic_draws);assert(!memcmp(a->particles,b->particles,sizeof a->particles));free(a);free(b);
}
static void position_children(void)
{
 uint8_t bytes[2048];fixture(bytes);unsigned root=at(3),child=at(2);put16(bytes+root,1);put16(bytes+root+4,2);putf(bytes+root+20,2);putf(bytes+child+20,4);bytes[child+60]=127;
 const uint8_t program[]={0xf1,0,2,0xf0,0,2,7,31};memcpy(bytes+root+60,program,sizeof program);
 C3ParticleBank bank;char error[160];C3ParticleSystem *a=calloc(1,sizeof *a);assert(a);float pos[3]={10,20,30};
 assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin_position(a,&bank,3,42,pos,1.57079632679f,.75f,.75f,error,sizeof error));
 if(!c3_particle_step_ticks(a,1,0,error,sizeof error)){fprintf(stderr,"%s\n",error);assert(0);}assert(a->spawned_emitters==2);
 assert(a->particles[1].generator==2&&a->particles[1].depth==1);assert(fabsf(a->particles[1].velocity[2]+4)<.00001f);
 assert(a->emitters[1].attachment_scale==.75f&&a->emitters[1].radius_scale==.75f);assert(fabsf(a->particles[2].velocity[2]+3)<.00001f);
 float child_x=a->emitters[1].position[0];pos[0]=100;assert(c3_particle_position(a,pos,error,sizeof error));assert(a->emitters[1].position[0]==child_x);
 pos[0]=10;assert(c3_particle_begin_position_flags(a,&bank,3,42,pos,1.57079632679f,.75f,.75f,0x10a1,error,sizeof error));assert(c3_particle_step_ticks(a,1,0,error,sizeof error));
 assert(fabsf(a->particles[0].velocity[2]+1.5f)<.00001f);assert(a->particles[1].velocity[0]==4&&a->particles[1].velocity[2]==0);assert(a->particles[1].attachment_scale==.75f);assert(fabsf(a->particles[2].velocity[2]+3)<.00001f);
 assert(!c3_particle_begin_position_flags(a,&bank,3,42,pos,0,.75f,.75f,0,error,sizeof error));
 /* BD random magnitude scales for both profiles; F1 only rotates in Reflect. */
 const uint8_t bd[]={0xbd,0x41,0,0,0,0,0,0,0,31};memcpy(bytes+child+60,bd,sizeof bd);
 assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));
 for(unsigned profile=0;profile<2;++profile){assert(c3_particle_begin_position_flags(a,&bank,3,42,pos,1.57079632679f,.75f,.75f,profile?0x18a9:0x10a1,error,sizeof error));assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(fabsf(a->particles[1].velocity[profile?2:0]-(profile?-6:6))<.00001f);assert(fabsf(a->particles[2].velocity[2]+6)<.00001f);}
 free(a);
}
static void position_tornado(void)
{
 uint8_t bytes[2048];fixture(bytes);unsigned gen=at(2);put16(bytes+gen,2);putf(bytes+gen+28,2);putf(bytes+gen+32,4);bytes[gen+60]=31;
 C3ParticleBank bank;char error[160];C3ParticleSystem *a=calloc(1,sizeof *a);assert(a);float pos[3]={10,20,30};
 assert(c3_particle_bank_decode(&bank,bytes,sizeof bytes,error,sizeof error));assert(c3_particle_begin_position(a,&bank,2,42,pos,1.57079632679f,.75f,.75f,error,sizeof error));assert(c3_particle_step_ticks(a,1,0,error,sizeof error));
 assert(a->particles[0].tornado_speed==2&&a->particles[0].tornado_radius==3);assert(a->particles[0].tornado_origin[0]==10);
 pos[0]=100;assert(c3_particle_position(a,pos,error,sizeof error));assert(c3_particle_step_ticks(a,1,0,error,sizeof error));assert(a->particles[0].tornado_origin[0]==10);assert(a->particles[0].position[0]<20);free(a);
}
static void quad(void)
{
 C3Particle p={0};p.active=1;p.size=2;p.kind=0x480|0x60|0x400000;p.trail=1;memset(p.prim,255,4);
 C3ParticleCamera c={{1,0,0},{0,1,0},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
 float attachment[12]={1,0,0,3,0,1,0,4,0,0,1,5};C3ParticleQuad q;char error[160];
 assert(c3_particle_quad(&p,attachment,&c,&q,error,sizeof error)==1);
 assert(q.vertices[0].position[0]==1&&q.vertices[0].position[0+1]==2&&q.vertices[0].position[2]==5);
 assert(q.vertices[2].position[0]==5&&q.vertices[2].position[1]==6);
 assert(q.vertices[0].uv[0]==0&&q.vertices[0].uv[1]==3);assert(q.vertices[2].uv[0]==2&&q.vertices[2].uv[1]==1);
 assert(q.blend==1&&q.depth_test&&!q.depth_write&&q.prim_env&&q.mirror_s&&q.mirror_t);
 p.kind=0x300480;p.velocity[0]=1;p.trail=.5f;
 assert(c3_particle_quad(&p,attachment,&c,&q,error,sizeof error)==1);
 assert(fabsf(q.vertices[0].position[0])<.00001f&&fabsf(q.vertices[0].position[1]-6)<.00001f);
 assert(fabsf(q.vertices[1].position[0]-5)<.00001f&&fabsf(q.vertices[1].position[1]-6)<.00001f);
 assert(q.vertices[0].rgba[3]==127&&q.vertices[1].rgba[3]==255);
 attachment[0]=2;assert(c3_particle_quad(&p,attachment,&c,&q,error,sizeof error)==-1);assert(strstr(error,"nonuniform"));
 p.kind=0x480;p.attachment_scale=.75f;attachment[0]=1;assert(c3_particle_quad(&p,attachment,&c,&q,error,sizeof error)==1);assert(fabsf(q.vertices[0].position[0]-1.5f)<.00001f);
 p.active=0;assert(c3_particle_quad(&p,attachment,&c,&q,error,sizeof error)==0);
}
static void private_bank(const char *path,unsigned root)
{
 FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long n=ftell(file);assert(n>0);rewind(file);uint8_t *bytes=malloc((size_t)n);assert(bytes);assert(fread(bytes,1,(size_t)n,file)==(size_t)n);fclose(file);
 C3ParticleBank bank;char error[160];assert(c3_particle_bank_decode(&bank,bytes,(size_t)n,error,sizeof error));C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);assert(c3_particle_begin(a,&bank,root,123,error,sizeof error));assert(c3_particle_begin(b,&bank,root,123,error,sizeof error));
 if(!c3_particle_step(a,5000,0,error,sizeof error)){fprintf(stderr,"%s\n",error);assert(0);}for(unsigned i=0;i<5000;++i)assert(c3_particle_step(b,1,0,error,sizeof error));assert(!memcmp(a,b,sizeof *a));assert(c3_particle_finished(a));if(root==3&&bank.generator_count==6)assert(a->emitted==82&&a->spawned_emitters==3);if(root==0&&bank.generator_count==10)assert(a->emitted==2938&&a->peak_particles==1369);if(root==4&&bank.generator_count==13)assert(a->emitted==512&&a->peak_particles==512);/* Same retail closure through POSITION attachment, partitioned time, and pause. */
 float pos[3]={100,20,-30};uint16_t flags=root==3&&bank.generator_count==6?0x10a1:0x18a9;assert(c3_particle_begin_position_flags(a,&bank,root,123,pos,.7f,.75f,.75f,flags,error,sizeof error));assert(c3_particle_begin_position_flags(b,&bank,root,123,pos,.7f,.75f,.75f,flags,error,sizeof error));
 assert(c3_particle_step(a,5000,0,error,sizeof error));for(unsigned i=0;i<5000;++i)assert(c3_particle_step(b,1,0,error,sizeof error));assert(!memcmp(a,b,sizeof *a));assert(c3_particle_finished(a));
 printf("Original GPT1 root %u executed %u particles, peak %u, %llu private cosmetic draws\n",root,a->emitted,a->peak_particles,(unsigned long long)a->cosmetic_draws);free(a);free(b);free(bytes);
}
int main(int argc,char **argv){synthetic();position_attachment();position_children();position_tornado();quad();if(argc>=2)private_bank(argv[1],argc>=3?(unsigned)strtoul(argv[2],NULL,0):3);puts("Bounded GPT1 source execution, timing, color, spawn, cancellation checks passed");return 0;}
