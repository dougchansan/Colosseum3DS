/* Focused math fixtures call production-private emit to isolate RNG draws from
 * generator setup. Public admission/run coverage uses the original bank below. */
#include "../src/particle.c"
#include "colosseum3ds/particle_line.h"
#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <assert.h>
#include <stdlib.h>
static void near_value(float a,float b){assert(fabsf(a-b)<.0002f);}
static void shape4(void)
{
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);uint8_t script[]={31,255};
 for(unsigned test=0;test<3;++test){memset(s,0,sizeof *s);s->bank.profile=4;s->bank.generator_count=8;s->bank.bytes=script;s->bank.length=2;
  C3ParticleGenerator *g=&s->bank.generators[6];g->type=4;g->life=20;g->size=1;g->script_bytes=2;g->velocity[2]=2;g->angle=PI/3;g->radius=test==0?0:test==1?8:-8;g->param[0]=g->param[1]=1;
  s->emitters[0].generator=6;s->emitters[0].attachment_scale=s->emitters[0].radius_scale=1;s->cosmetic_seed=3782276673u;
  float angle=0;assert(emit(s,0,0,&angle));const C3Particle *p=&s->particles[0];float rho=test==2?1:.5f;
  near_value(p->position[0]-p->velocity[0],(test==0?0:test==1?4:8)*cosf(1));
  near_value(p->velocity[0],2*sinf(PI/3*rho)*cosf(1));near_value(p->velocity[2],2*cosf(PI/3*rho));
  near_value(sqrtf(p->velocity[0]*p->velocity[0]+p->velocity[1]*p->velocity[1]+p->velocity[2]*p->velocity[2]),2);
  assert(s->cosmetic_draws==(test==2?1:2));
 }
 memset(s,0,sizeof *s);s->bank.profile=4;s->bank.bytes=script;s->bank.length=2;C3ParticleGenerator *g=&s->bank.generators[6];g->type=4;g->life=20;g->script_bytes=2;g->velocity[2]=2;g->radius=-8;g->angle=-PI/2;
 s->emitters[0].generator=6;float angle=PI/4;for(unsigned i=0;i<4;++i){assert(emit(s,0,PI/2,&angle));near_value(s->particles[i].position[0]-s->particles[i].velocity[0],8*cosf(3*PI/4+i*PI/2));}assert(!s->cosmetic_draws);free(s);
}
static void rotate_scale(void)
{
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);uint8_t script[]={31,255};s->bank.profile=4;s->bank.bytes=script;s->bank.length=2;s->position_attachment=1;s->cosmetic_seed=3782276673u;
 C3ParticleGenerator *g=&s->bank.generators[6];g->type=4;g->life=20;g->script_bytes=2;g->velocity[1]=2;g->radius=8;g->angle=PI/3;g->param[0]=g->param[1]=1;
 C3ParticleEmitter *e=&s->emitters[0];e->generator=6;e->radius_scale=1;e->attachment_scale=.75f;e->attachment_yaw=.7f;e->position[0]=100;e->position[1]=200;e->position[2]=-30;
 float angle=0;assert(emit(s,0,0,&angle));const C3Particle *p=&s->particles[0];float c=cosf(1),sn=sinf(1),yawc=cosf(.7f),yaws=sinf(.7f);
 near_value(p->position[0]-p->velocity[0],100+4*c*yawc-4*sn*yaws);near_value(p->position[1]-p->velocity[1],200);near_value(p->position[2]-p->velocity[2],-30-4*c*yaws-4*sn*yawc);
 near_value(p->velocity[0],.75f*(c*yawc-sn*yaws));near_value(p->velocity[1],.75f*sqrtf(3));near_value(p->velocity[2],.75f*(-c*yaws-sn*yawc));free(s);
}
static void order(void)
{
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);s->bank.profile=4;uint16_t slots[1536];size_t n;char e[192];
 for(unsigned i=0;i<4;++i){s->particles[i].active=1;s->particles[i].serial=i+1;}
 s->particles[0].kind=0x06000000;s->particles[1].kind=0x02000000;s->particles[2].kind=8|0x40000000;s->particles[3].kind=0x02000000;
 assert(c3_particle_render_indices(s,slots,1536,&n,e,sizeof e)&&n==4);assert(slots[0]==2&&slots[1]==3&&slots[2]==1&&slots[3]==0);
 s->particles[0].serial=5;s->particles[0].kind=0x02000000|0x40000000;
 assert(c3_particle_render_indices(s,slots,1536,&n,e,sizeof e));assert(slots[0]==2&&slots[1]==0&&slots[2]==3&&slots[3]==1);
 /* POINT/quad alternating order remains serial-owned, not primitive-owned. */
 for(unsigned i=0;i<4;++i)s->particles[i].kind=(i&1)?0x40000000:0;
 assert(c3_particle_render_indices(s,slots,1536,&n,e,sizeof e));assert(slots[0]==0&&slots[1]==3&&slots[2]==2&&slots[3]==1);
 s->bank.profile=0;assert(c3_particle_render_indices(s,slots,1536,&n,e,sizeof e));for(unsigned i=0;i<4;++i)assert(slots[i]==i);assert(!c3_particle_render_indices(s,slots,3,&n,e,sizeof e));free(s);
}
static void update_order(void)
{
 uint8_t program[]={0xac,0,0,0,0,0,0x3f,0x80,0,0,1,255};char error_text[192];
 C3ParticleSystem *head=calloc(1,sizeof *head),*sorted=calloc(1,sizeof *sorted);assert(head&&sorted);
 head->bank.profile=4;head->bank.bytes=program;head->bank.length=sizeof program;head->bank.generators[0].life=20;head->bank.generators[0].script_bytes=sizeof program;
 initial_particle(head,&head->particles[0],0,0);initial_particle(head,&head->particles[1],0,0);
 head->particles[0].kind=8;head->particles[1].kind=0x0e000000;*sorted=*head;
 uint16_t slots[1536];size_t n;assert(c3_particle_render_indices(sorted,slots,1536,&n,error_text,sizeof error_text));assert(slots[0]==0&&slots[1]==1);
 assert(tick(head)&&tick(sorted));assert(head->cosmetic_draws==2&&sorted->cosmetic_draws==2);
 near_value(head->particles[1].size,sorted->particles[0].size);near_value(head->particles[0].size,sorted->particles[1].size);assert(head->particles[0].size!=sorted->particles[0].size);
 /* Birth after a display prepends ahead of old sorted buckets until display. */
 head->particles[1].active=0;initial_particle(head,&head->particles[1],0,0);assert(head->source_order_count==2&&head->source_order[0]==1&&head->source_order[1]==0);
 free(head);free(sorted);
}
static void trail_zero(void)
{
 C3Particle p={0};p.active=1;p.kind=0x40500002u;p.size=1;p.position[0]=3;p.position[1]=2;p.position[2]=-5;p.velocity[0]=1;p.velocity[1]=.5f;p.velocity[2]=1;p.prim[3]=128;
 const float attachment[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0},viewport[2]={400,240};
 C3ParticleCamera camera={{1,0,0},{0,1,0},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};C3ParticleLineRaster raster={{.625f,.5f},-1,0};C3ParticleLine line;C3ParticleLineMesh mesh;char e[192];
 assert(c3_particle_line(&p,attachment,&camera,&raster,&line,e,sizeof e)==1);near_value(line.source[0].position[0],.2f);near_value(line.source[1].position[0],.3f);assert(line.source[0].rgba[3]==0&&line.source[1].rgba[3]==128);
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1&&!mesh.degenerate);
 p.velocity[0]=p.velocity[1]=p.velocity[2]=0;assert(c3_particle_line(&p,attachment,&camera,&raster,&line,e,sizeof e)==1);assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1&&mesh.degenerate);
}
static void source_units(void)
{
 C3ParticleSystem *a=calloc(1,sizeof *a),*b=calloc(1,sizeof *b);assert(a&&b);char e[192];
 assert(c3_particle_step_units(a,20,0,e,sizeof e)&&a->remainder==20&&!a->ticks);
 assert(c3_particle_step_units(a,40,0,e,sizeof e)&&a->remainder==60&&!a->ticks);
 assert(c3_particle_step(b,1,0,e,sizeof e)&&!memcmp(a,b,sizeof *a));
 assert(c3_particle_step_units(a,940,0,e,sizeof e)&&a->ticks==1&&!a->remainder);
 assert(c3_particle_step_units(a,UINT64_MAX,1,e,sizeof e)&&a->ticks==1&&!a->remainder&&!a->failed);
 memset(a,0,sizeof *a);assert(c3_particle_step_units(a,600999,0,e,sizeof e)&&a->ticks==600&&a->remainder==999);
 assert(!c3_particle_step_units(a,UINT64_MAX,0,e,sizeof e)&&a->failed&&a->ticks==600&&a->remainder==999);
 memset(a,0,sizeof *a);assert(!c3_particle_step_units(a,601000,0,e,sizeof e)&&a->failed&&!a->ticks&&!a->remainder);
 free(a);free(b);
}
static void display_run(const C3ParticleBank *bank)
{
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);char e[192];float pos[3]={0};
 const float attachment[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0},viewport[2]={400,240};
 C3ParticleCamera camera={{1,0,0},{0,1,0},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};C3ParticleLineRaster raster={{.625f,.5f},-1,0};
 assert(c3_particle_begin_position_flags(s,bank,0,123,pos,.4f,.75f,1,1,e,sizeof e));unsigned points=0,quads=0,nondegenerate=0;
 for(unsigned frame=0;frame<600&&!c3_particle_finished(s);++frame){assert(c3_particle_step_ticks(s,1,0,e,sizeof e));uint16_t indices[1536];size_t count;uint64_t draws=s->cosmetic_draws,ticks=s->ticks;assert(c3_particle_render_indices(s,indices,1536,&count,e,sizeof e));assert(draws==s->cosmetic_draws&&ticks==s->ticks);
  for(size_t i=0;i<count;++i){const C3Particle *p=&s->particles[indices[i]];if(p->kind&0x40000000u){C3ParticleLine line;C3ParticleLineMesh mesh;assert(p->kind&0x100000u);int result=c3_particle_line(p,attachment,&camera,&raster,&line,e,sizeof e);if(result<0){puts(e);abort();}if(!result)continue;assert(line.source[0].rgba[3]==0);assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);++points;nondegenerate+=!mesh.degenerate;}else{C3ParticleQuad q;int result=c3_particle_quad(p,attachment,&camera,&q,e,sizeof e);assert(result>=0);quads+=(result==1);}}
 }
 assert(c3_particle_finished(s)&&s->emitted==134&&s->spawned_emitters==8&&s->peak_particles==133&&s->ticks==104&&s->cosmetic_draws==964&&points&&quads&&nondegenerate);
 printf("Tackle display samples: points=%u quads=%u nondegenerate-lines=%u\n",points,quads,nondegenerate);free(s);
}
static uint8_t *load(const char *path,size_t *n){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long z=ftell(f);assert(z>0);*n=(size_t)z;rewind(f);uint8_t *p=malloc(*n);assert(p&&fread(p,1,*n,f)==*n);fclose(f);return p;}
int main(int argc,char **argv)
{
 shape4();rotate_scale();order();update_order();trail_zero();source_units();if(argc>1){size_t n;uint8_t *raw=load(argv[1],&n);char e[192];C3ParticleBank bank;
  assert(c3_particle_bank_decode_reachable(&bank,raw,n,0,4,e,sizeof e));display_run(&bank);C3ParticleBank before=bank;raw[940]^=1;assert(!c3_particle_bank_decode_reachable(&bank,raw,n,0,4,e,sizeof e)&&!memcmp(&bank,&before,sizeof bank));raw[940]^=1;
  C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);float pos[3]={0};assert(c3_particle_begin_position_flags(s,&bank,0,123,pos,.4f,.75f,1,1,e,sizeof e));
  assert(s->source_attachment_flags==1&&s->emitters[0].radius_scale==1);assert(!c3_particle_begin_position_flags(s,&bank,0,123,pos,.4f,.75f,1,0x10a1,e,sizeof e));
  assert(!c3_particle_position(s,pos,e,sizeof e));assert(!c3_particle_position_root(s,s->root_emitter_serial,pos,e,sizeof e));
  assert(c3_particle_step_ticks(s,1,0,e,sizeof e));static const unsigned visit[]={7,3,2,1,6,4,5};unsigned last=0,which=0;for(unsigned i=0;i<1536;++i)if(s->particles[i].active&&s->particles[i].generator!=last){assert(which<7&&s->particles[i].generator==visit[which++]);last=s->particles[i].generator;}assert(which==7);
  for(unsigned i=0;i<600&&!c3_particle_finished(s);++i){if(!c3_particle_step_ticks(s,1,0,e,sizeof e)){puts(e);abort();}}
  assert(c3_particle_finished(s));printf("Tackle emitted=%u emitters=%u peak=%u ticks=%llu rng=%llu\n",s->emitted,s->spawned_emitters,s->peak_particles,(unsigned long long)s->ticks,(unsigned long long)s->cosmetic_draws);free(s);
  if(argc>2){size_t pn;uint8_t *pack=load(argv[2],&pn);uint8_t hash[20];c3_save_codec_sha1(hash,pack,(uint32_t)pn);C3ParticlePack decoded;assert(c3_particle_pack_decode_reachable(&decoded,pack,pn,hash,4,e,sizeof e));assert(decoded.texture_count==7);uint32_t bank_at=(uint32_t)pack[16]|(uint32_t)pack[17]<<8|(uint32_t)pack[18]<<16|(uint32_t)pack[19]<<24;pack[bank_at+940]^=1;c3_save_codec_sha1(hash,pack,(uint32_t)pn);assert(!c3_particle_pack_decode_reachable(&decoded,pack,pn,hash,4,e,sizeof e));assert(strstr(e,"source GPT1 SHA-1"));free(pack);}free(raw);
 }puts("particle tackle: shape4 RNG/speed, negative-angle preincrement, source buckets/birth/slot reuse, primitive order, legacy order passed");return 0;
}
