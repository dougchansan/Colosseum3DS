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
int c3_particle_bank_decode(C3ParticleBank *out,const void *data,size_t n,char *e,size_t cap)
{
 C3ParticleBank b={0};const uint8_t *p=data;
 if(!out||!p||n<32||n>1048576||overlap(out,sizeof *out,p,n)||memcmp(p,"GPT1",4))return error(e,cap,"Invalid GPT1 bank header/size.");
 uint32_t desc=u32(p+4),txg=u32(p+8),refs=u32(p+16);
 if(!range(n,desc,12)||u16(p+desc)!=0x43||u32(p+desc+4)||!range(n,txg,4)||desc>=txg)return error(e,cap,"Unsupported GPT1 description version/sections.");
 b.generator_count=u32(p+desc+8);b.texture_group_count=u32(p+txg);
 if(!b.generator_count||b.generator_count>C3_PARTICLE_MAX_GENERATORS||!b.texture_group_count||b.texture_group_count>C3_PARTICLE_MAX_TEXTURE_GROUPS||!range(n,desc+12,4*b.generator_count)||!range(n,txg+4,4*b.texture_group_count)||!range(n,refs,4*b.generator_count))return error(e,cap,"GPT1 table count/offset bound.");
 for(unsigned i=0;i<b.texture_group_count;++i){
  uint32_t rel=u32(p+txg+4+i*4);size_t at=(size_t)txg+rel;
  if(at<txg||rel<4+4*b.texture_group_count||!range(n,at,24)||!(b.texture_frames[i]=u32(p+at))||b.texture_frames[i]>256||!u32(p+at+12)||!u32(p+at+16)||u32(p+at+12)>1024||u32(p+at+16)>1024)return error(e,cap,"GPT1 texture group/frame bound.");
 }
 for(unsigned i=0;i<b.generator_count;++i){
  size_t at=(size_t)desc+u32(p+desc+12+i*4),end=i+1<b.generator_count?(size_t)desc+u32(p+desc+16+i*4):txg;
  if(at<(size_t)desc+12+4*b.generator_count||end>txg||end<=at||!range(end,at,61)||end-at>4096)return error(e,cap,"GPT1 generator/script offset bound.");
  C3ParticleGenerator *g=&b.generators[i];g->type=u16(p+at);g->tex_group=u16(p+at+2);g->gen_life=u16(p+at+4);g->life=u16(p+at+6);g->kind=(u32(p+at+8)&0xf1ffffffu)|0x08000000u;
  if(g->tex_group>=b.texture_group_count||!g->life||g->life==65535)return error(e,cap,"GPT1 generator texture/lifetime bound.");
  float v[12];for(unsigned j=0;j<12;++j){v[j]=f32(p+at+12+4*j);if(!isfinite(v[j])||fabsf(v[j])>1000000)return error(e,cap,"Nonfinite/excessive GPT1 generator scalar.");}
  g->grav=v[0];g->fric=v[1];memcpy(g->velocity,v+2,12);g->radius=v[5];g->angle=v[6];g->rate=v[7];g->size=v[8];memcpy(g->param,v+9,12);
  g->script_offset=(uint32_t)at+60;g->script_bytes=(uint32_t)(end-at-60);b.refs[i]=u32(p+refs+4*i);
  if(b.refs[i]>=b.generator_count)return error(e,cap,"GPT1 reference handle out of range.");
 }
 b.bytes=p;b.length=n;*out=b;if(e&&cap)e[0]=0;
 return 1;
}
static int fault(C3ParticleSystem *s,unsigned gen,unsigned pc,unsigned op,const char *why)
{snprintf(s->diagnostic,sizeof s->diagnostic,"GPT1 generator %u pc 0x%X opcode 0x%02X: %s",gen,pc,op,why);s->failed=1;return 0;}
static float rnd(C3ParticleSystem *s)
{s->cosmetic_seed=s->cosmetic_seed*214013u+2531011u;++s->cosmetic_draws;return (float)(s->cosmetic_seed>>16)/65536.f;}
/* POSITION uses checked wazaSequenceEntry flags. Reflect 0x18A9 enables velocity-local rotation,
 * texture scaling and random-velocity scaling; no local-offset or tornado
 * height/radius scaling. Snapshots survive native emitter-slot reuse. */
static void yaw_rotate(float yaw,float v[3])
{float x=v[0],z=v[2],c=cosf(yaw),sn=sinf(yaw);v[0]=c*x+sn*z;v[2]=-sn*x+c*z;}
static int emitter(C3ParticleSystem *s,unsigned g,unsigned depth,const float pos[3],unsigned flags)
{
 if(g>=s->bank.generator_count||depth>C3_PARTICLE_MAX_DEPTH)return fault(s,g,0,0xF0,"reference/nesting bound");
 const C3ParticleGenerator *d=&s->bank.generators[g];
 if((d->type!=0&&d->type!=1&&d->type!=2&&d->type!=8)||d->rate>C3_PARTICLE_MAX_PARTICLES||d->rate< -(float)C3_PARTICLE_MAX_PARTICLES||d->kind&0x30000)return fault(s,g,0,0,"unsupported generator shape/camera basis/rate");

 for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(!s->emitters[i].active){
  C3ParticleEmitter *v=&s->emitters[i];memset(v,0,sizeof *v);v->active=1;v->generator=(uint16_t)g;v->depth=(uint8_t)depth;v->life=d->gen_life;v->kind=d->kind;
  if(flags<8)v->kind=(v->kind&~0x0e000000u)|(flags<<25);
  memcpy(v->position,pos,sizeof v->position);
  v->attachment_scale=v->radius_scale=1;
  if(d->kind&0x100)v->count=d->rate<0?(1.f+d->rate>FLT_EPSILON?1.f:0.f):1.f-FLT_EPSILON;
  else v->count=d->rate<0?0:rnd(s);
  v->serial=++s->spawned_emitters;return 1;
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
 p->serial=++s->emitted;unsigned live=0;for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)live+=s->particles[i].active!=0;if(live>s->peak_particles)s->peak_particles=live;
}
static int child_particle(C3ParticleSystem *s,C3Particle *parent,unsigned generator)
{
 if(parent->depth>=C3_PARTICLE_MAX_DEPTH)return fault(s,generator,0,0xf1,"particle nesting bound");
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(!s->particles[i].active){
  C3Particle *p=&s->particles[i];initial_particle(s,p,generator,parent->depth+1);p->emitter=parent->emitter;p->attachment_yaw=parent->attachment_yaw;p->attachment_scale=parent->attachment_scale;p->radius_scale=parent->radius_scale;if(s->position_attachment&&(s->source_attachment_flags&8))yaw_rotate(p->attachment_yaw,p->velocity);memcpy(p->position,parent->position,sizeof p->position);
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
    case 0xbd:{float a,b;if(!scalar(s,p,&a)||!scalar(s,p,&b))return 0;if(s->position_attachment){a*=p->attachment_scale;b*=p->attachment_scale;}a+=b*rnd(s);float mag=sqrtf(p->velocity[0]*p->velocity[0]+p->velocity[1]*p->velocity[1]+p->velocity[2]*p->velocity[2]);if(mag>1e-10f)for(unsigned j=0;j<3;++j)p->velocity[j]*=a/mag;break;}
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
    case 0xf0:{unsigned hi,lo,flags;if(!byte(s,p,&hi)||!byte(s,p,&lo)||!byte(s,p,&flags))return 0;unsigned id=(hi<<8)|lo;if(id>=s->bank.generator_count)return fault(s,p->generator,pc,op,"spawn reference out of range");if(!emitter(s,s->bank.refs[id],p->depth+1,p->position,flags&7))return 0;
     if(s->position_attachment)for(unsigned j=0;j<C3_PARTICLE_MAX_EMITTERS;++j)if(s->emitters[j].serial==s->spawned_emitters){s->emitters[j].attachment_yaw=p->attachment_yaw;s->emitters[j].attachment_scale=p->attachment_scale;s->emitters[j].radius_scale=p->radius_scale;}
     break;}
    case 0xf1:{unsigned hi,lo;if(!byte(s,p,&hi)||!byte(s,p,&lo))return 0;unsigned id=(hi<<8)|lo;if(id>=s->bank.generator_count)return fault(s,p->generator,pc,op,"child particle reference bound");if(!child_particle(s,p,s->bank.refs[id]))return 0;break;}
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
 if(!--p->life){p->active=0;return 1;}
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
 if(s->position_attachment){adjusted=*g;adjusted.radius*=e->radius_scale;for(unsigned j=0;j<3;++j)adjusted.param[j]*=e->radius_scale;g=&adjusted;}
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(!s->particles[i].active){
  C3Particle *p=&s->particles[i];initial_particle(s,p,e->generator,e->depth);p->emitter=(uint16_t)index;p->kind=e->kind;p->attachment_yaw=e->attachment_yaw;p->attachment_scale=e->attachment_scale;p->radius_scale=e->radius_scale;memcpy(p->position,e->position,sizeof p->position);
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
   float radius=g->radius<0?1:rnd(s),dist=g->radius<0?-g->radius:radius*g->radius;
   float min=g->param[0],max=g->param[1];if(min==0&&max==0)max=2*PI;
   float angle;if(g->angle<0){*cur_angle+=angle_step;angle=*cur_angle;}else angle=(max-min)*rnd(s)+min;
   float local[3]={dist*cosf(angle),dist*sinf(angle),0};rotate(g->velocity,local);for(unsigned j=0;j<3;++j)p->position[j]+=local[j];
   float speed=sqrtf(g->velocity[0]*g->velocity[0]+g->velocity[1]*g->velocity[1]+g->velocity[2]*g->velocity[2]),cone=fabsf(g->angle)*(s->position_attachment?radius:1);
   float v[3]={speed*sinf(cone)*cosf(angle),speed*sinf(cone)*sinf(angle),speed*cosf(cone)};rotate(g->velocity,v);memcpy(p->velocity,v,sizeof v);
  }
  if(s->position_attachment&&g->type!=2){
   float offset[3];for(unsigned j=0;j<3;++j)offset[j]=p->position[j]-e->position[j];
   yaw_rotate(e->attachment_yaw,offset);yaw_rotate(e->attachment_yaw,p->velocity);
   for(unsigned j=0;j<3;++j){p->position[j]=e->position[j]+offset[j];p->velocity[j]*=e->attachment_scale;}
  }
  return interpret(s,p);
 }
 return fault(s,e->generator,0,0,"particle pool exhausted");
}
static int tick(C3ParticleSystem *s)
{
 /* Source snapshots particle next before interpreting; F1 children initialize
  * immediately but must not receive a second update in this particle pass. */
 uint32_t particles[C3_PARTICLE_MAX_PARTICLES];for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)particles[i]=s->particles[i].active?s->particles[i].serial:0;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(particles[i]&&s->particles[i].active&&s->particles[i].serial==particles[i])if(!interpret(s,&s->particles[i]))return 0;
 /* Child emitters created by zero-wait particle initialization are eligible
  * in this generator pass. Checked native handle order owns cosmetic ordering. */
 uint32_t visited[C3_PARTICLE_MAX_EMITTERS]={0};unsigned passes=0;
 for(;;){int any=0;for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i){C3ParticleEmitter *e=&s->emitters[i];if(!e->active||visited[i]==e->serial)continue;any=1;visited[i]=e->serial;
   if(++passes>C3_PARTICLE_MAX_EMITTERS*(C3_PARTICLE_MAX_DEPTH+1))return fault(s,e->generator,0,0,"per-tick emitter work bound");
   const C3ParticleGenerator *g=&s->bank.generators[e->generator];e->count+=g->rate<0?-g->rate:g->rate*rnd(s);
   float step=0,angle=0;if(g->angle<0&&e->count>=1){if(g->type==0){float min=g->param[0],max=g->param[1];if(s->position_attachment){min*=e->radius_scale;max*=e->radius_scale;}if(min==0&&max==0)max=2*PI;step=(max-min)/(int)e->count;angle=step*rnd(s)+min;}else if(g->type==2){step=2*PI/(int)e->count;angle=step*rnd(s);}}
   unsigned emitted=0;while(e->count>=1){if(++emitted>C3_PARTICLE_MAX_PARTICLES)return fault(s,e->generator,0,0,"emission budget exceeded");if(!emit(s,i,step,&angle))return 0;e->count-=1;}
   if(e->life&&!--e->life)e->active=0;
  }if(!any)break;
 }
 ++s->ticks;return 1;
}
int c3_particle_begin(C3ParticleSystem *out,const C3ParticleBank *bank,unsigned gen,uint32_t seed,char *e,size_t cap)
{
 if(!out||!bank||!bank->bytes||gen>=bank->generator_count||overlap(out,sizeof *out,bank->bytes,bank->length))return error(e,cap,"Invalid particle begin arguments.");
 /* Avoid a large temporary on the 3DS stack; validate before clearing output. */
 C3ParticleBank validated;if(!c3_particle_bank_decode(&validated,bank->bytes,bank->length,e,cap))return 0;
 const C3ParticleGenerator *g=&validated.generators[gen];
 if((g->type!=0&&g->type!=1&&g->type!=2&&g->type!=8)||g->rate>C3_PARTICLE_MAX_PARTICLES||g->rate< -(float)C3_PARTICLE_MAX_PARTICLES||g->kind&0x30000)return error(e,cap,"Unsupported particle root emission shape/rate.");
 memset(out,0,sizeof *out);out->bank=validated;out->cosmetic_seed=seed;float zero[3]={0,0,0};
 if(!emitter(out,gen,0,zero,8))return error(e,cap,out->diagnostic);
 if(e&&cap)e[0]=0;
 return 1;
}
static int valid_position(const float v[3])
{return v&&isfinite(v[0])&&isfinite(v[1])&&isfinite(v[2])&&fabsf(v[0])<=1000000&&fabsf(v[1])<=1000000&&fabsf(v[2])<=1000000;}
int c3_particle_position(C3ParticleSystem *s,const float pos[3],char *e,size_t cap)
{
 if(!s||!s->position_attachment||s->failed||!valid_position(pos))return error(e,cap,"Invalid POSITION particle attachment.");
 if(!s->cancelled)for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->emitters[i].serial==s->root_emitter_serial)memcpy(s->emitters[i].position,pos,sizeof s->emitters[i].position);
 if(e&&cap)e[0]=0;
 return 1;
}
int c3_particle_begin_position_flags(C3ParticleSystem *s,const C3ParticleBank *bank,unsigned gen,uint32_t seed,const float pos[3],float yaw,float scale,float radius_scale,uint16_t flags,char *e,size_t cap)
{
 if(flags!=0x10a1&&flags!=0x18a9)return error(e,cap,"Unsupported source POSITION attachment profile.");
 if(!valid_position(pos)||!isfinite(yaw)||fabsf(yaw)>1000000||!isfinite(scale)||scale<=0||scale>1000||!isfinite(radius_scale)||radius_scale<=0||radius_scale>1000)return error(e,cap,"Invalid POSITION particle start transform.");
 float position[3];memcpy(position,pos,sizeof position);
 if(!c3_particle_begin(s,bank,gen,seed,e,cap))return 0;
 s->position_attachment=1;s->source_attachment_flags=flags;s->root_emitter_serial=s->emitters[0].serial;
 s->emitters[0].attachment_yaw=yaw;s->emitters[0].attachment_scale=scale;s->emitters[0].radius_scale=radius_scale;
 return c3_particle_position(s,position,e,cap);
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
int c3_particle_step(C3ParticleSystem *s,uint32_t ms,int paused,char *e,size_t cap)
{
 if(!s||s->failed)return error(e,cap,s?s->diagnostic:"Missing particle system.");
 if(s->cancelled||paused)return 1;
 uint64_t elapsed=s->remainder+(uint64_t)ms*60,ticks=elapsed/1000;
 if(ticks>C3_PARTICLE_MAX_CATCHUP){fault(s,0,0,0,"catch-up exceeds 600 source ticks");return error(e,cap,s->diagnostic);}
 s->remainder=elapsed%1000;
 if(!c3_particle_step_ticks(s,(uint32_t)ticks,0,e,cap))return 0;
 if(e&&cap)e[0]=0;
 return 1;
}
void c3_particle_cancel(C3ParticleSystem *s)
{if(s){memset(s->particles,0,sizeof s->particles);memset(s->emitters,0,sizeof s->emitters);s->cancelled=1;s->remainder=0;}}
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

 float half=p->size*scale[0]*(p->attachment_scale>0?p->attachment_scale:1),angle=p->rotation;
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
