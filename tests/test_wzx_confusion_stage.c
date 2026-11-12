/* Optional private-asset test. Executes production loaders/sampling/timeline;
 * only GPU submission is replaced by an explicitly bounded ownership fixture.
 * No original assets are embedded. Usage: test_wzx_bite_stage PRIVATE_SCENE_DIR. */
#include "battle_stage.h"
#include "../platform/3ds/battle_stage.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counts,static_count,particles_bound,reflect_audio,last_cue,audio_hold;static unsigned char enabled[16];static int busy;
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)e;(void)c;counts=n;unsigned v=0;for(unsigned i=0;i<n;i++)v+=m[i]->vertex_count;assert(n<=16&&v<=131072);return 0;}
uint64_t c3_stage_renderer_gpu_skin_mask(size_t m){(void)m;return 0;}
int c3_stage_renderer_set_static(size_t i,bool value){assert(i<counts);static_count+=value;return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<counts);enabled[i]=(rgb!=NULL);return 1;}
int c3_stage_renderer_get_modulation(size_t i,uint8_t rgb[3],int *enabled_out){(void)i;memset(rgb,128,3);*enabled_out=0;return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t n){(void)e;(void)n;assert(p&&p->texture_count);return C3_RENDER_OK;}
int c3_stage_renderer_particles_source_order(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){if(s){assert(a);++particles_bound;}return 1;}
static unsigned cues[4];
static uint32_t emitted_sound[128];
static int sound(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(serial>last_cue);last_cue=serial;assert(serial<128);emitted_sound[serial]=id;unsigned i=id==0x443?0:id==0xE9?1:id==0x472?2:3;assert(id==0x443||id==0xE9||id==0x472||id==0x473);++cues[i];++reflect_audio;return 1;}
static int done(uint32_t serial,uint32_t id,char *e,size_t n){assert(serial&&serial<=last_cue);if(emitted_sound[serial]!=id){snprintf(e,n,"Audio completion cue identity unknown: serial%u emitted%u requested%u",serial,emitted_sound[serial],id);fprintf(stderr,"%s\n",e);return -1;}return !audio_hold;}
bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}


static void submit(C3BattleStage *s,unsigned target,int hit){uint16_t hp[4]={80,75,50,50};c3_battle_presentation_init(&s->presentation,hp);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));
 C3BattlePreviewEvent q[3]={{0},{0},{0}};for(unsigned i=0;i<3;++i){q[i].sequence=i+1;q[i].group=1;q[i].actor=1;q[i].target=(uint8_t)target;q[i].move=93;memcpy(q[i].hp,hp,sizeof hp);memcpy(q[i].hp_before,hp,sizeof hp);}
 q[0].kind=C3_PREVIEW_EVENT_MOVE;q[1].kind=C3_PREVIEW_EVENT_HP;q[1].hp[target]=0;q[2].kind=C3_PREVIEW_EVENT_FAINT;q[2].hp[target]=0;
 assert(c3_battle_stage_submit(s,q,hit?3:1));}
static void follow(C3BattleStage *base)
{
 C3BattleStage *a=malloc(sizeof *a),*b=malloc(sizeof *b);assert(a&&b);*a=*base;a->trace=NULL;a->confusion_damage_particles=calloc(1,sizeof *a->confusion_damage_particles);assert(a->confusion_damage_particles);a->sound_cue=sound;a->sound_done=done;last_cue=0;reflect_audio=0;submit(a,2,1);assert(c3_battle_stage_step(a,0,a->error,sizeof a->error));
 while(!a->confusion_phase||a->confusion_units<45000)assert(c3_battle_stage_step(a,1,a->error,sizeof a->error));
 *b=*a;b->confusion_damage_particles=malloc(sizeof *b->confusion_damage_particles);assert(b->confusion_damage_particles);*b->confusion_damage_particles=*a->confusion_damage_particles;
 b->transforms[b->confusion_target][0]+=3;
 assert(c3_battle_stage_step(b,0,b->error,sizeof b->error));
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(a->confusion_damage_particles->particles[i].active)assert(!memcmp(a->confusion_damage_particles->particles[i].position,b->confusion_damage_particles->particles[i].position,3*sizeof(float)));
 unsigned live=0;for(unsigned r=0;r<5;++r)for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(a->confusion_damage_particles->emitters[i].active&&a->confusion_damage_particles->emitters[i].serial==a->confusion_roots[r]){
  assert(fabsf(b->confusion_damage_particles->emitters[i].position[0]-a->confusion_damage_particles->emitters[i].position[0]-30)<.001f);++live;
 }assert(live);
 unsigned before=a->confusion_damage_particles->emitted;assert(c3_battle_stage_step(a,17,a->error,sizeof a->error)&&c3_battle_stage_step(b,17,b->error,sizeof b->error));unsigned future=0;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i){const C3Particle *x=&a->confusion_damage_particles->particles[i],*y=&b->confusion_damage_particles->particles[i];if(x->active&&x->serial>before){assert(x->serial==y->serial&&fabsf(y->position[0]-x->position[0]-30)<.001f);++future;}}
 assert(future);free(a->confusion_damage_particles);free(b->confusion_damage_particles);free(a);free(b);last_cue=0;reflect_audio=0;memset(cues,0,sizeof cues);
}
static void partitions(C3BattleStage *base)
{
 const unsigned chunks[]={1,17,100,1000,4000};C3ParticleSystem *reference=malloc(sizeof *reference);assert(reference);
 uint64_t units=0;float center[3]={0};uint32_t seen=0;C3BattleCamera expected_camera={0};C3TitleCamera inherited=base->camera;
 for(unsigned variant=0;variant<5;++variant){C3BattleStage *s=malloc(sizeof *s);assert(s);*s=*base;s->trace=NULL;s->confusion_damage_particles=calloc(1,sizeof *s->confusion_damage_particles);assert(s->confusion_damage_particles);s->sound_cue=sound;s->sound_done=done;last_cue=0;reflect_audio=0;memset(cues,0,sizeof cues);
  submit(s,2,1);assert(c3_battle_stage_step(s,0,s->error,sizeof s->error));
  unsigned elapsed=0;while(elapsed<4000){unsigned amount=chunks[variant];if(amount>4000-elapsed)amount=4000-elapsed;assert(c3_battle_stage_step(s,amount,s->error,sizeof s->error));elapsed+=amount;}
  assert(s->confusion_phase==1&&s->confusion_hp_gate);
  if(!variant){*reference=*s->confusion_damage_particles;units=s->confusion_units;seen=s->confusion_seen;memcpy(center,s->confusion_type12.center,sizeof center);expected_camera=s->confusion_camera;}
  else{assert(s->confusion_units==units&&s->confusion_seen==seen);if(memcmp(reference,s->confusion_damage_particles,sizeof *reference)){const unsigned char *a=(const unsigned char *)reference,*b=(const unsigned char *)s->confusion_damage_particles;for(size_t off=0;off<sizeof *reference;++off)if(a[off]!=b[off]){fprintf(stderr,"partition%u units%llu firstdiff%zu %u/%u ticks%llu/%llu remainder%llu/%llu emitted%u/%u\n",variant,(unsigned long long)s->confusion_units,off,a[off],b[off],(unsigned long long)reference->ticks,(unsigned long long)s->confusion_damage_particles->ticks,(unsigned long long)reference->remainder,(unsigned long long)s->confusion_damage_particles->remainder,reference->emitted,s->confusion_damage_particles->emitted);break;}assert(0);}assert(!memcmp(center,s->confusion_type12.center,sizeof center));assert(!memcmp(&expected_camera,&s->confusion_camera,sizeof expected_camera));}
  while(elapsed<10000){unsigned amount=chunks[variant];if(amount>10000-elapsed)amount=10000-elapsed;assert(c3_battle_stage_step(s,amount,s->error,sizeof s->error));elapsed+=amount;}
  assert(!s->confusion_camera.active&&!memcmp(&s->camera,&inherited,sizeof inherited));
  assert(!s->confusion_active&&s->confusion_units==402000&&s->confusion_damage_particles->emitted==738&&s->confusion_damage_particles->cosmetic_draws==3302);
  free(s->confusion_damage_particles);free(s);
 }
 free(reference);last_cue=0;reflect_audio=0;memset(cues,0,sizeof cues);
}
int main(int argc,char **argv){assert(argc>=2&&argc<=5);C3BattleStage *s=calloc(1,sizeof *s);assert(s);char e[192];uint16_t hp[4]={80,75,50,50};
 if(!c3_battle_stage_load(s,argv[1],hp,stdout,e,sizeof e)){puts(e);return 1;}assert(s->confusion_available);s->sound_cue=sound;s->sound_done=done;follow(s);partitions(s);
 C3TitleCamera inherited=s->camera;C3BattleCamera bite_before=s->source_camera;
 for(unsigned target=2;target<4;++target){submit(s,target,1);unsigned before=reflect_audio;audio_hold=1;assert(c3_battle_stage_step(s,0,e,sizeof e));assert(s->confusion_active&&!s->confusion_particles_started);
 if(s->confusion_camera_available){assert(s->confusion_camera.active&&s->confusion_camera.owner==1&&s->confusion_camera.target==target&&s->confusion_camera.units==0&&s->confusion_camera.track_delay==0);assert(!memcmp(s->confusion_camera.origin,s->transforms[s->confusion_owner],3*sizeof(float)));assert(s->confusion_camera.motion==2||s->confusion_camera.motion==3);if(s->confusion_camera.motion==3)assert(s->confusion_camera.yaw_start==s->confusion_camera.yaw_end);}
 C3BattleCamera paused_camera=s->confusion_camera;
 uint64_t units=s->confusion_units;s->presentation.paused=1;assert(c3_battle_stage_step(s,4000,e,sizeof e)&&units==s->confusion_units);if(s->confusion_camera_available){paused_camera.paused=1;assert(!memcmp(&paused_camera,&s->confusion_camera,sizeof paused_camera));}s->presentation.paused=0;
 for(unsigned i=0;i<300;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}}
 assert(s->confusion_active&&!s->confusion_phase&&reflect_audio==before+1&&s->presentation.hp[target]==50);audio_hold=0;
 unsigned roots=0,type12=0,hp_gate=0;
 for(unsigned i=0;i<1000&&s->presentation.cursor<s->presentation.count;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}
  type12+=s->confusion_type12.active;hp_gate+=s->confusion_hp_gate;
  if(s->confusion_phase){if(s->confusion_camera_available)assert(s->confusion_camera.active||!s->confusion_active);if(s->confusion_camera.active){assert(s->confusion_camera.owner==target&&s->confusion_camera.target==1&&s->confusion_camera.units==s->confusion_units&&s->confusion_camera.track_delay==44);assert(s->confusion_camera.dynamic_fov&&isfinite(s->camera.fov_y_degrees)&&s->camera.fov_y_degrees>=15&&s->camera.fov_y_degrees<=85);assert(!memcmp(s->confusion_camera.origin,s->transforms[s->confusion_target],3*sizeof(float)));}unsigned count=0;for(unsigned j=0;j<5;++j)count+=s->confusion_roots[j]!=0;if(count>roots)roots=count;
   if(s->confusion_units<44000)assert(s->presentation.hp[target]==50);
  }
  for(unsigned j=0;j<s->count;++j)if(enabled[j])assert(s->slots[j]==UINT32_MAX);
 }
 assert(roots==5&&type12&&hp_gate&&!s->confusion_active&&s->presentation.cursor==s->presentation.count);assert(reflect_audio==before+4&&s->confusion_damage_particles->emitted==738&&s->confusion_damage_particles->cosmetic_draws==3302);assert(!s->presentation.visible[target]);assert(!s->confusion_camera.active&&!memcmp(&s->camera,&inherited,sizeof inherited));assert(!memcmp(&s->source_camera,&bite_before,sizeof bite_before));
 }
 submit(s,2,0);unsigned before=reflect_audio;for(unsigned i=0;i<300;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}}assert(!s->confusion_active&&!s->confusion_hit&&reflect_audio==before+1);
 submit(s,2,1);assert(c3_battle_stage_step(s,17,e,sizeof e));busy=1;assert(!c3_battle_stage_close(s)&&!s->confusion_active&&s->confusion_program.bytes&&!s->confusion_camera.active&&(!s->confusion_camera_available||s->confusion_camera_program.samples)&&!memcmp(&s->camera,&inherited,sizeof inherited));busy=0;assert(c3_battle_stage_close(s)&&!s->confusion_program.bytes);free(s);
 if(argc>=3){C3BattleStage *bad=calloc(1,sizeof *bad);assert(bad);assert(!c3_battle_stage_load(bad,argv[2],hp,NULL,e,sizeof e));assert(!bad->confusion_bytes[0]&&!bad->confusion_bytes[1]&&!bad->confusion_damage_particles);assert(c3_battle_stage_close(bad)&&c3_battle_stage_close(bad));free(bad);}
 if(argc>=4){C3BattleStage *bad=calloc(1,sizeof *bad);assert(bad);assert(!c3_battle_stage_load(bad,argv[3],hp,NULL,e,sizeof e));assert(c3_battle_stage_close(bad)&&c3_battle_stage_close(bad));free(bad);}
 if(argc>=5){C3BattleStage *optional=calloc(1,sizeof *optional);assert(optional);assert(c3_battle_stage_load(optional,argv[4],hp,NULL,e,sizeof e)&&optional->confusion_available&&!optional->confusion_camera_available);assert(c3_battle_stage_close(optional));free(optional);}
 puts("Actual Confusion stage/camera contract passed");return 0;}
