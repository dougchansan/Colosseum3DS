#include "colosseum3ds/particle_line.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const float attachment[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};
static C3ParticleCamera camera={{1,0,0},{0,1,0},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
static const C3ParticleLineRaster raster={{400.f/640,240.f/480},-1,1};
static void near(float a,float b){assert(fabsf(a-b)<.0001f);}
static const float viewport[2]={400,240};
static float sx(const C3ParticleLineTriangleVertex *v){return (v->clip[0]/v->clip[3]+1)*200;}
static float sy(const C3ParticleLineTriangleVertex *v){return (1-v->clip[1]/v->clip[3])*120;}
/* Independent floating half-plane oracle, screen y down, top/left inclusive.
 * Exercises geometric coverage, not GX's 1/16-pixel snapping emulation. */
static int contains(const C3ParticleLineTriangleVertex *v,float x,float y)
{
 double ax[3],ay[3];for(unsigned j=0;j<3;++j){ax[j]=sx(&v[j]);ay[j]=sy(&v[j]);}
 double area=(ax[1]-ax[0])*(ay[2]-ay[0])-(ay[1]-ay[0])*(ax[2]-ax[0]);
 if(area==0)return 0;
 if(area<0){double tmp=ax[1];ax[1]=ax[2];ax[2]=tmp;tmp=ay[1];ay[1]=ay[2];ay[2]=tmp;}
 for(unsigned j=0;j<3;++j){unsigned k=(j+1)%3;double dx=ax[k]-ax[j],dy=ay[k]-ay[j],edge=dx*(y-ay[j])-dy*(x-ax[j]);
  if(edge<-.00001)return 0;
  if(fabs(edge)<.00001&&!(dy<0||(dy==0&&dx>0)))return 0;}
 return 1;
}
static void triangles(void)
{
 C3ParticleLine line={0};line.destination_per_source_pixel[0]=line.destination_per_source_pixel[1]=1;
 line.destination_width_pixels[0]=line.destination_width_pixels[1]=2;line.textured=1;
 /* screen endpoints (20,50) -> (80,50), with w=1. */
 for(unsigned j=0;j<2;++j){line.visible[j].clip[0]=j?-.6f:-.9f;line.visible[j].clip[1]=1-50.f/120;line.visible[j].clip[3]=1;line.visible[j].rgba[3]=j?255:0;line.visible[j].uv[1]=(float)(1-j);}
 C3ParticleLineMesh mesh;char e[160];
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);
 assert(!mesh.source_major_axis&&!mesh.degenerate);near(sx(&mesh.vertices[0]),20);near(sx(&mesh.vertices[1]),80);
 near(sy(&mesh.vertices[0]),51);near(sy(&mesh.vertices[2]),49);
 assert(mesh.vertices[0].uv[0]==1&&mesh.vertices[2].uv[0]==0);
 assert(mesh.vertices[0].rgba[3]==0&&mesh.vertices[1].rgba[3]==255);
 unsigned coverage=0;for(unsigned y=47;y<53;++y)for(unsigned x=18;x<83;++x){
  int count=contains(mesh.vertices,x+.5f,y+.5f)+contains(mesh.vertices+3,x+.5f,y+.5f);
  assert(count<=1);assert(count==((x>=20&&x<80)&&(y>=49&&y<51)));coverage+=(unsigned)count;}
 assert(coverage==120); /* 60 longitudinal pixels x 2 width; no cap extension. */
 assert(contains(mesh.vertices,50,50)+contains(mesh.vertices+3,50,50)==1); /* shared diagonal */
 /* Perspective: each endpoint's clip offset scales with its own w. */
 for(unsigned j=0;j<2;++j){float w=j?4:2;for(unsigned k=0;k<3;++k)line.visible[j].clip[k]*=w;line.visible[j].clip[3]=w;}
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);
 near(sy(&mesh.vertices[0]),51);near(sy(&mesh.vertices[2]),49);near(mesh.vertices[0].clip[3],2);near(mesh.vertices[2].clip[3],4);
 /* Vertical segment: offsets along x only and horizontal endpoint caps. */
 line.visible[0].clip[0]=line.visible[1].clip[0]=0;line.visible[0].clip[1]=1;line.visible[1].clip[1]=-2;
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);assert(mesh.source_major_axis);
 near(fabsf(sx(&mesh.vertices[0])-sx(&mesh.vertices[5])),2);near(sy(&mesh.vertices[0]),sy(&mesh.vertices[5]));
 /* Source dx=dy=50: software tie chooses x expansion. Destination
  * anisotropy cannot silently change this source-major choice. */
 line.visible[0].clip[0]=line.visible[0].clip[1]=0;line.visible[0].clip[3]=1;
 const float square_viewport[2]={400,400};
 line.visible[1].clip[0]=.25f;line.visible[1].clip[1]=-.25f;line.visible[1].clip[3]=1;
 assert(c3_particle_line_triangles(&line,square_viewport,&mesh,e,sizeof e)==1);assert(mesh.source_major_axis);
 line.destination_per_source_pixel[0]=2;line.destination_width_pixels[0]=4;
 line.visible[1].clip[0]=.5f;
 assert(c3_particle_line_triangles(&line,square_viewport,&mesh,e,sizeof e)==1);assert(mesh.source_major_axis);near(fabsf(sx(&mesh.vertices[0])-sx(&mesh.vertices[5])),4);
 /* Slope under 45 degrees has an axis-aligned, not perpendicular width. */
 line.visible[1].clip[1]=-.125f;
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);assert(!mesh.source_major_axis);near(sx(&mesh.vertices[0]),sx(&mesh.vertices[5]));
 line.visible[1]=line.visible[0];assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);assert(mesh.degenerate);
 line.destination_width_pixels[0]=line.destination_width_pixels[1]=0;
 assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);assert(mesh.degenerate);
 line.visible[0].clip[3]=0;assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==-1);
}
static void synthetic(void)
{
 C3Particle p={0};p.active=1;p.kind=0x40100480u;p.size=2;p.trail=.5f;
 p.position[0]=10;p.velocity[0]=5;memset(p.prim,200,4);
 C3ParticleLine q,unchanged;memset(&unchanged,0x55,sizeof unchanged);char e[160];
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);
 near(q.source[0].position[0],.5f);near(q.source[1].position[0],1);
 near(q.source[0].clip[0],.5f);near(q.source_width_pixels,2);
 near(q.destination_width_pixels[0],1.25f);near(q.destination_width_pixels[1],1);
 assert(q.gx_width_sixths==12&&q.source[0].rgba[3]==127&&q.source[1].rgba[3]==255);
 assert(q.source[0].uv[1]==1&&q.source[1].uv[1]==0&&q.gx_texture_offset==5);
 p.kind|=0xc0060u;
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);
 near(q.source[0].uv[0],0);near(q.source[0].uv[1],3);near(q.source[1].uv[1],1);
 p.kind&=~0x400u; /* psSetupTev clears PrimEnv without texture. */
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);
 assert(!q.prim_env&&!q.textured&&q.source[0].rgba[3]==100&&q.source[1].rgba[0]==200);
 for(unsigned b=0;b<4;++b){p.kind=(p.kind&~0xc00000u)|(b<<22)|8u|0x10000000u;
  assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);
  assert(q.blend==b&&!q.depth_test&&q.depth_write&&q.alpha_reference==(b>=2?0:1));}
 p.size=100;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);assert(q.gx_width_sixths==255);near(q.source_width_pixels,42.5f);
 p.size=42.49f;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);assert(q.gx_width_sixths==254);
 p.size=42.5f;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);assert(q.gx_width_sixths==255);
 p.size=.1f;memset(p.velocity,0,sizeof p.velocity);
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);assert(q.gx_width_sixths==0);near(q.source[0].position[0],q.source[1].position[0]);
 p.size=2;p.kind=0x40100480u;p.position[2]=0;p.velocity[2]=20;
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);
 near(q.source_interval[0],.5f);near(q.visible[0].clip[2],-1);near(q.visible[0].rgba[3],191);
 p.position[2]=-30;q=unchanged;
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==0);assert(!memcmp(&q,&unchanged,sizeof q));
 p.position[2]=0;p.velocity[2]=0;p.position[0]=10000;
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1); /* no erroneous centerline side cull */
 p.kind|=4;p.tornado_radius=10;p.velocity[0]=0;p.velocity[1]=1;
 assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==1);near(q.source[0].position[0],1);near(q.source[0].position[1],0);
 p.kind&=~4u;p.size=NAN;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==-1);
 p.size=2;p.kind&=~0x40000000u;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==-1);
 p.kind|=0xc0000000u;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==-1);
 p.kind=0x40100480u;p.trail=2;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==-1);
 p.kind|=0x1000000u;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==-1);p.kind&=~0x1000000u;
 p.trail=.5f;C3ParticleCamera invalid=camera;invalid.clip[3]=INFINITY;
 assert(c3_particle_line(&p,attachment,&invalid,&raster,&q,e,sizeof e)==-1);
 C3ParticleLineRaster bad=raster;bad.destination_per_source_pixel[0]=0;
 assert(c3_particle_line(&p,attachment,&camera,&bad,&q,e,sizeof e)==-1);
 bad=raster;bad.z_max=bad.z_min;assert(c3_particle_line(&p,attachment,&camera,&bad,&q,e,sizeof e)==-1);
 /* Perspective with actual homogeneous w, near clipping at z=-w. */
 C3ParticleCamera perspective=camera;perspective.clip[10]=0;perspective.clip[11]=-1;perspective.clip[14]=1;perspective.clip[15]=0;
 p.position[0]=0;p.position[2]=20;p.velocity[2]=15;p.velocity[1]=0;
 assert(c3_particle_line(&p,attachment,&perspective,&raster,&q,e,sizeof e)==1);
 near(q.source_interval[0],1.f/3);near(q.visible[0].clip[3],1);near(q.source[1].clip[3],2);
 C3ParticleLineMesh mesh;assert(c3_particle_line_triangles(&q,viewport,&mesh,e,sizeof e)==1);near(mesh.vertices[0].clip[3],1);near(mesh.vertices[2].clip[3],2);
 C3ParticleLineRaster pica=raster;pica.z_max=0;
 assert(c3_particle_line(&p,attachment,&perspective,&pica,&q,e,sizeof e)==1);
 p.active=0;assert(c3_particle_line(&p,attachment,&camera,&raster,&q,e,sizeof e)==0);
}
static void actual(const char *path)
{
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);rewind(f);
 void *bytes=malloc((size_t)length);assert(bytes);assert(fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);
 C3ParticleBank bank;C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);char e[160];
 assert(c3_particle_bank_decode(&bank,bytes,(size_t)length,e,sizeof e));assert(c3_particle_begin(s,&bank,4,123,e,sizeof e));
 /* Broad orthographic camera retains full source trajectories; actual source
  * samples still run through homogeneous clip and explicit native conversion. */
 C3ParticleCamera c=camera;c.clip[0]=c.clip[5]=c.clip[10]=.001f;
 unsigned samples=0,seen=0,source_hidden=0;uint32_t serials[512]={0};
 for(unsigned tick=0;tick<300;++tick){assert(c3_particle_step_ticks(s,1,0,e,sizeof e));
  for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(s->particles[i].active){
   const C3Particle *p=&s->particles[i];C3ParticleLine q;
   int result=c3_particle_line(p,attachment,&c,&raster,&q,e,sizeof e);
   if(result==0&&p->size<1.19209290e-7f){++source_hidden;continue;}
   if(result!=1){fprintf(stderr,"root4 tick %u kind %08x: %d %s\n",tick,p->kind,result,e);assert(0);}
   assert(q.blend==1&&!q.depth_write&&q.depth_test);++samples;
   C3ParticleLineMesh mesh;assert(c3_particle_line_triangles(&q,viewport,&mesh,e,sizeof e)==1);
   for(unsigned vertex=0;vertex<6;++vertex)for(unsigned component=0;component<4;++component)assert(isfinite(mesh.vertices[vertex].clip[component]));
   unsigned j=0;while(j<seen&&serials[j]!=p->serial)++j;if(j==seen){assert(seen<512);serials[seen++]=p->serial;}
   for(unsigned k=0;k<3;++k){near(q.source[1].position[k],p->position[k]*.1f);near(q.source[0].position[k],(p->position[k]-p->velocity[k])*.1f);}
  }
 }
 assert(s->emitted==512&&s->peak_particles==512&&seen==512&&c3_particle_finished(s));
 printf("root4: %u distinct source particles, %u line samples, %u source size-hidden samples, peak %u; all retained\n",seen,samples,source_hidden,s->peak_particles);
 free(s);free(bytes);
}
static void system_mode(void)
{
 C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);
 assert(c3_particle_line_system_mode(NULL)==0);assert(c3_particle_line_system_mode(s)==0);
 s->particles[0].active=1;s->particles[0].kind=0x40000000u;
 assert(c3_particle_line_system_mode(s)==1);
 s->particles[1].active=1;assert(c3_particle_line_system_mode(s)==-1);
 s->particles[0].active=0;assert(c3_particle_line_system_mode(s)==0);free(s);
}
static void mixed_bank(const char *path)
{
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);rewind(f);
 void *bytes=malloc((size_t)length);assert(bytes);assert(fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);
 C3ParticleBank bank;C3ParticleSystem *s=calloc(1,sizeof *s);assert(s);char e[160];float pos[3]={0,0,0};
 assert(c3_particle_bank_decode(&bank,bytes,(size_t)length,e,sizeof e));
 assert(c3_particle_begin_position_flags(s,&bank,4,123,pos,0,1,1,1,e,sizeof e));
 const unsigned roots[]={7,3,2,8};
 for(unsigned j=0;j<4;++j){uint32_t handle;assert(c3_particle_append_position_flags(s,&bank,roots[j],pos,0,1,1,1,&handle,e,sizeof e));}
 C3ParticleCamera c=camera;c.clip[0]=c.clip[5]=c.clip[10]=.001f;
 unsigned point_samples=0,quad_samples=0,mixed_frames=0;
 for(unsigned tick=0;tick<300;++tick){assert(c3_particle_step_ticks(s,1,0,e,sizeof e));
  if(c3_particle_line_system_mode(s)==-1)++mixed_frames;
  unsigned retained=0,previous_slot=0;
  for(unsigned slot=0;slot<C3_PARTICLE_MAX_PARTICLES;++slot){
   const C3Particle *p=&s->particles[slot];int result;
   if(p->kind&0x40000000u){C3ParticleLine line;C3ParticleLineMesh mesh;result=c3_particle_line(p,attachment,&c,&raster,&line,e,sizeof e);if(result==1){assert(c3_particle_line_triangles(&line,viewport,&mesh,e,sizeof e)==1);++point_samples;}}
   else{C3ParticleQuad quad;result=c3_particle_quad(p,attachment,&c,&quad,e,sizeof e);if(result==1)++quad_samples;}
   if(result<0){fprintf(stderr,"mixed tick%u slot%u root%u kind%08x: %s\n",tick,slot,p->generator,p->kind,e);assert(0);}
   if(result==1){if(retained)assert(slot>previous_slot);previous_slot=slot;++retained;}
  }
 }
 assert(point_samples&&quad_samples&&mixed_frames&&c3_particle_finished(s));
 printf("Actual mixed roots4/7/3/2/8: %u POINT samples, %u quad samples, %u mixed frames, peak%u; source-slot order retained\n",point_samples,quad_samples,mixed_frames,s->peak_particles);
 free(s);free(bytes);
}
int main(int argc,char **argv){synthetic();triangles();system_mode();if(argc>1){actual(argv[1]);mixed_bank(argv[1]);}puts("POINT+TRAIL source geometry checks passed");return 0;}
