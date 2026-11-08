#include "colosseum3ds/particle_line.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static int fail(char *e,size_t n,const char *s)
{if(e&&n)snprintf(e,n,"%s",s);return -1;}
int c3_particle_line_system_mode(const C3ParticleSystem *s)
{
 if(!s)return 0;
 unsigned points=0,other=0;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(s->particles[i].active){
  if(s->particles[i].kind&0x40000000u)++points;else ++other;
 }
 return points?(other?-1:1):0;
}
static void transform(const float m[12],const float p[3],float q[3])
{for(unsigned j=0;j<3;++j)q[j]=m[4*j]*p[0]+m[4*j+1]*p[1]+m[4*j+2]*p[2]+m[4*j+3];}
static int plane(double a,double b,float *lo,float *hi)
{
 if(a<0&&b<0)return 0;
 if(a<0||b<0){float t=(float)(a/(a-b));if(a<0){if(t>*lo)*lo=t;}else if(t<*hi)*hi=t;}
 return *lo<=*hi;
}
int c3_particle_line(const C3Particle *p,const float m[12],const C3ParticleCamera *c,
 const C3ParticleLineRaster *r,C3ParticleLine *out,char *e,size_t n)
{
 if(!p||!m||!c||!r||!out)return fail(e,n,"Missing particle line arguments.");
 if(!isfinite(p->size))return fail(e,n,"Nonfinite line size.");
 if(!p->active||p->size<FLT_EPSILON||(p->kind&0x20000000u))return 0;
 if((p->kind&0x40100000u)!=0x40100000u||(p->kind&0x80000000u))
  return fail(e,n,"Requires unlit POINT+TRAIL source branch.");
 if(p->kind&0x01000000u)return fail(e,n,"Fog requires a separate source fog adapter.");
 if(!isfinite(p->trail)||p->trail<0||p->trail>1)
  return fail(e,n,"Trail alpha outside qualified source range.");
 for(unsigned j=0;j<12;++j)if(!isfinite(m[j]))return fail(e,n,"Nonfinite attachment.");
 for(unsigned j=0;j<16;++j)if(!isfinite(c->clip[j]))return fail(e,n,"Nonfinite clip camera.");
 if(!isfinite(r->z_min)||!isfinite(r->z_max)||r->z_min>=r->z_max)
  return fail(e,n,"Invalid camera depth interval.");
 for(unsigned j=0;j<2;++j)if(!isfinite(r->destination_per_source_pixel[j])||r->destination_per_source_pixel[j]<=0)
  return fail(e,n,"Invalid explicit source pixel ratio.");
 float previous[3];
 for(unsigned j=0;j<3;++j){
  if(!isfinite(p->position[j])||!isfinite(p->velocity[j]))return fail(e,n,"Nonfinite source line.");
  previous[j]=p->position[j]-p->velocity[j];
 }
 if(p->kind&4u){
  const float scalars[]={p->tornado_radius,p->tornado_angle,p->tornado_speed,p->tornado_turn,p->grav,p->fric};
  for(unsigned j=0;j<6;++j)if(!isfinite(scalars[j]))return fail(e,n,"Nonfinite tornado snapshot.");
  float vz=p->velocity[2]-p->tornado_speed,phase=p->velocity[0]-p->tornado_turn;
  float radius=(fabsf(p->tornado_radius)+vz*tanf(fabsf(p->tornado_angle)))*p->velocity[1];
  float x=radius*cosf(phase),y=radius*sinf(phase),sa=sinf(p->grav),ca=cosf(p->grav),sb=sinf(p->fric),cb=cosf(p->fric);
  previous[0]=x*cb+vz*sb+p->tornado_origin[0];
  previous[1]=-x*sa*sb+y*ca+vz*sa*cb+p->tornado_origin[1];
  previous[2]=-x*ca*sb-y*sa+vz*ca*cb+p->tornado_origin[2];
 }
 C3ParticleLine q={0};
 q.gx_width_sixths=p->size>42.5f?255:(uint8_t)(6*p->size);
 q.source_width_pixels=(float)q.gx_width_sixths/6;
 for(unsigned j=0;j<2;++j){q.destination_per_source_pixel[j]=r->destination_per_source_pixel[j];q.destination_width_pixels[j]=q.source_width_pixels*r->destination_per_source_pixel[j];}
 for(unsigned j=0;j<2;++j)if(!isfinite(q.destination_width_pixels[j]))return fail(e,n,"Raster width overflow.");
 memcpy(q.prim,p->prim,4);memcpy(q.env,p->env,4);
 q.textured=(p->kind&0x400u)!=0;q.prim_env=q.textured&&(p->kind&0x80u)!=0;
 q.blend=(uint8_t)((p->kind>>22)&3);q.depth_test=(p->kind&0x10000000u)==0;q.depth_write=(p->kind&8u)!=0;
 q.filter_nearest=(p->kind&0x200u)!=0;q.mirror_s=(p->kind&0x20u)!=0;q.mirror_t=(p->kind&0x40u)!=0;
 q.texture_group=p->tex_group;q.texture_frame=p->pose;q.alpha_reference=q.blend>=2?0:1;q.gx_texture_offset=5;
 for(unsigned i=0;i<2;++i){
  C3ParticleLineVertex *v=&q.source[i];transform(m,i?p->position:previous,v->position);
  for(unsigned j=0;j<3;++j)if(!isfinite(v->position[j]))return fail(e,n,"Line position overflow.");
  for(unsigned j=0;j<4;++j){v->clip[j]=c->clip[4*j]*v->position[0]+c->clip[4*j+1]*v->position[1]+c->clip[4*j+2]*v->position[2]+c->clip[4*j+3];if(!isfinite(v->clip[j]))return fail(e,n,"Line projection overflow.");v->rgba[j]=(float)(q.prim_env?255:p->prim[j]);}
  if(i==0)v->rgba[3]=(float)(uint8_t)(v->rgba[3]*p->trail);
  /* psDispSubPointTrail always submits indices 0=(0,1), 1=(0,0).
   * Flip flags are not applied in this source branch. */
  v->uv[0]=0;v->uv[1]=(float)(1-i)*(q.mirror_t?2:1)+(q.mirror_t?1:0);
 }
 float lo=0,hi=1;const float *a=q.source[0].clip,*b=q.source[1].clip;
 if(!plane(a[3]-FLT_MIN,b[3]-FLT_MIN,&lo,&hi)||
    !plane(a[2]-(double)r->z_min*a[3],b[2]-(double)r->z_min*b[3],&lo,&hi)||
    !plane((double)r->z_max*a[3]-a[2],(double)r->z_max*b[3]-b[2],&lo,&hi))return 0;
 q.source_interval[0]=lo;q.source_interval[1]=hi;
 for(unsigned i=0;i<2;++i){
  float t=q.source_interval[i];C3ParticleLineVertex *v=&q.visible[i];
  for(unsigned j=0;j<3;++j)v->position[j]=q.source[0].position[j]*(1-t)+q.source[1].position[j]*t;
  for(unsigned j=0;j<4;++j){v->clip[j]=a[j]*(1-t)+b[j]*t;v->rgba[j]=q.source[0].rgba[j]*(1-t)+q.source[1].rgba[j]*t;}
  for(unsigned j=0;j<2;++j)v->uv[j]=q.source[0].uv[j]*(1-t)+q.source[1].uv[j]*t;
 }
 *out=q;if(e&&n)e[0]=0;return 1;
}
int c3_particle_line_triangles(const C3ParticleLine *line,const float viewport[2],
 C3ParticleLineMesh *out,char *e,size_t n)
{
 if(!line||!viewport||!out)return fail(e,n,"Missing line expansion arguments.");
 for(unsigned j=0;j<2;++j)
  if(!isfinite(viewport[j])||viewport[j]<=0||
     !isfinite(line->destination_per_source_pixel[j])||line->destination_per_source_pixel[j]<=0||
     !isfinite(line->destination_width_pixels[j])||line->destination_width_pixels[j]<0)
   return fail(e,n,"Invalid raster dimensions for line expansion.");
 for(unsigned i=0;i<2;++i){
  const C3ParticleLineVertex *v=&line->visible[i];
  for(unsigned j=0;j<4;++j)if(!isfinite(v->clip[j])||!isfinite(v->rgba[j]))return fail(e,n,"Nonfinite line expansion endpoint.");
  for(unsigned j=0;j<2;++j)if(!isfinite(v->uv[j]))return fail(e,n,"Nonfinite line expansion texture coordinate.");
  if(v->clip[3]<=0)return fail(e,n,"Line expansion requires positive clipped w.");
 }
 const C3ParticleLineVertex *a=&line->visible[0],*b=&line->visible[1];
 /* Compare longitudinal distances in SOURCE pixels. Anisotropic destination
  * scaling must not change which source raster axis determines the caps. */
 double dx=((double)b->clip[0]/b->clip[3]-(double)a->clip[0]/a->clip[3])*viewport[0]/line->destination_per_source_pixel[0];
 double dy=((double)b->clip[1]/b->clip[3]-(double)a->clip[1]/a->clip[3])*viewport[1]/line->destination_per_source_pixel[1];
 C3ParticleLineMesh mesh={0};mesh.source_major_axis=(uint8_t)(fabs(dy)>=fabs(dx));
 double offset[2]={0,0};
 if(mesh.source_major_axis)offset[0]=(dy>0?-1:1)*(double)line->destination_width_pixels[0]/viewport[0];
 else offset[1]=(dx>0?1:-1)*(double)line->destination_width_pixels[1]/viewport[1];
 mesh.degenerate=(uint8_t)((dx==0&&dy==0)||(offset[0]==0&&offset[1]==0));
 /* Corner order: previous minus/plus offset, current minus/plus offset.
  * This splits the parallelogram without changing either endpoint depth. */
 static const unsigned corners[6]={0,2,3,0,3,1};
 for(unsigned i=0;i<6;++i){
  unsigned corner=corners[i];const C3ParticleLineVertex *v=corner<2?a:b;
  double side=(corner&1u)?1:-1;C3ParticleLineTriangleVertex *t=&mesh.vertices[i];
  memcpy(t->clip,v->clip,sizeof t->clip);memcpy(t->rgba,v->rgba,sizeof t->rgba);memcpy(t->uv,v->uv,sizeof t->uv);
  for(unsigned j=0;j<2;++j){
   double value=(double)v->clip[j]+side*offset[j]*v->clip[3];
   if(!isfinite(value)||fabs(value)>FLT_MAX)return fail(e,n,"Line expansion overflow.");
   t->clip[j]=(float)value;
  }
  /* The opposite source raster edge receives GX_TO_ONE's normalized S +1.
   * It is applied after the mirror texgen matrix, not multiplied by it. */
  if(line->textured&&!(corner&1u))t->uv[0]+=1;
  if(!isfinite(t->uv[0]))return fail(e,n,"Line texture offset overflow.");
 }
 *out=mesh;if(e&&n)e[0]=0;return 1;
}
