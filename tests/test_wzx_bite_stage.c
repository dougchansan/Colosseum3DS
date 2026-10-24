/* Optional private-asset test. Executes production loaders/sampling/timeline;
 * only GPU submission is replaced by an explicitly bounded ownership fixture.
 * No original assets are embedded. Usage: test_wzx_bite_stage PRIVATE_SCENE_DIR. */
#include "battle_stage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counts,static_count,particles_bound,audio85,audio86,fail_audio;static unsigned char enabled[16];static int busy;
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)e;(void)c;counts=n;unsigned v=0;for(unsigned i=0;i<n;i++)v+=m[i]->vertex_count;assert(n<=16&&v<=131072);return 0;}
int c3_stage_renderer_set_static(size_t i,bool value){assert(i<counts);static_count+=value;return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<counts);enabled[i]=(rgb!=NULL);return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t n){(void)e;(void)n;assert(p&&p->texture_count==6);return C3_RENDER_OK;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){if(s){assert(a&&s->position_attachment);assert(a[0]==.1f&&a[5]==.1f&&a[10]==.1f&&a[3]==0&&a[7]==0&&a[11]==0);++particles_bound;}return 1;}
static int sound(uint32_t sequence,uint32_t id,char *e,size_t n){assert(sequence);if(fail_audio){snprintf(e,n,"Injected audio queue failure");return 0;}if(id==0x85)++audio85;else{assert(id==0x86);++audio86;}return 1;}
bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}
static void damage_cases(C3BattleStage *s,char error[192],const void *immutable,size_t immutable_bytes)
{
 uint16_t initial[4]={80,75,50,50};
 for(unsigned target=2;target<4;++target){
  c3_battle_presentation_init(&s->presentation,initial);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));s->sound_cue=sound;
  C3BattlePreviewEvent events[3]={{0},{0},{0}};
  for(unsigned i=0;i<3;++i){events[i].sequence=1+i;events[i].group=1;events[i].actor=0;events[i].target=(uint8_t)target;events[i].move=44;memcpy(events[i].hp,initial,sizeof initial);memcpy(events[i].hp_before,initial,sizeof initial);}
  events[0].kind=C3_PREVIEW_EVENT_MOVE;events[1].kind=C3_PREVIEW_EVENT_HP;events[1].hp[target]=0;events[2].kind=C3_PREVIEW_EVENT_FAINT;events[2].hp[target]=0;
  assert(c3_battle_stage_submit(s,events,3));unsigned gates=0,live=0,changed=0;
  unsigned old85=audio85,old86=audio86;
  for(unsigned elapsed=0;elapsed<18000&&s->presentation.cursor<s->presentation.count;elapsed+=17){
   if(!c3_battle_stage_step(s,17,error,192)){fprintf(stderr,"damage fixture: %s\n",error);for(unsigned k=0;k<s->count;++k)if(!c3_title_animate(&s->models[k],s->clips[k],s->clocks[k]))fprintf(stderr,"sampling failed model%u slot%u clip%u seconds%.9g\n",k,s->slots[k],s->clips[k],s->clocks[k]);assert(0);}
   assert(!memcmp(immutable,s->models[1].draw_vertices,immutable_bytes));
   for(unsigned i=0;i<s->count;++i)if(enabled[i])assert(s->slots[i]==UINT32_MAX);
   if(s->damage.active){
    assert(s->transforms[s->damage_model][4]==.75f);
    if(s->particles_started){
     const float *t=s->transforms[s->damage_model],*b=s->damage_particle_local;float co=cosf(t[3]),si=sinf(t[3]);
     float expected[3]={10*(t[0]+.75f*(co*b[0]+si*b[2])),10*(t[1]+.75f*b[1]),10*(t[2]+.75f*(-si*b[0]+co*b[2]))};
     assert(s->particles.position_attachment);
     for(unsigned k=0;k<C3_PARTICLE_MAX_EMITTERS;++k)if(s->particles.emitters[k].active&&s->particles.emitters[k].depth==0){
      for(unsigned axis=0;axis<3;++axis)assert(fabsf(s->particles.emitters[k].position[axis]-expected[axis])<.001f);
      assert(s->particles.emitters[k].attachment_scale==.75f&&s->particles.emitters[k].radius_scale==.75f);
     }
    }
    gates+=s->damage.hp_gates!=0;live+=s->particles_started!=0;
    if(s->damage.units<(uint64_t)s->damage_program.hp_start*1000u)assert(!s->damage.hp_gates&&s->presentation.hp[target]==initial[target]);
    assert(s->presentation.visible[target]); /* No faint before effect consumers finish. */
   }
   if(s->presentation.hp[target]==0){changed=1;assert(s->damage.hp_gates);}
  }
  assert(gates&&live&&changed&&s->presentation.cursor==s->presentation.count);
  assert(!s->presentation.visible[target]&&s->damage.completed&&!s->damage.active);
  assert(s->particles.emitted==82&&c3_particle_finished(&s->particles));
  assert(audio85==old85+1&&audio86==old86+1&&particles_bound);
 }
 /* Missing defender HP commits are a miss: no defender effects or sound86. */
 c3_battle_presentation_init(&s->presentation,initial);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));C3BattlePreviewEvent miss={0};miss.kind=C3_PREVIEW_EVENT_MOVE;miss.sequence=1;miss.group=2;miss.actor=0;miss.target=2;miss.move=44;
 assert(c3_battle_stage_submit(s,&miss,1));unsigned old86=audio86;for(unsigned i=0;i<200;++i)assert(c3_battle_stage_step(s,17,error,192));assert(!s->damage_hit&&!s->damage_pending&&audio86==old86);
 /* A playback failure propagates; it cannot masquerade as effect completion. */
 c3_battle_presentation_init(&s->presentation,initial);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));assert(c3_battle_stage_submit(s,&miss,1));fail_audio=1;int failed=0;
 for(unsigned i=0;i<200;++i)if(!c3_battle_stage_step(s,17,error,192)){failed=1;break;}
 assert(failed&&strstr(error,"audio queue"));fail_audio=0;
 /* Corrupt only the owned in-memory bank after admission, never the source
  * file. A reached bad particle opcode must fail the stage explicitly. */
 c3_wzx_bite_cancel(&s->bite);c3_wzx_damage_cancel(&s->damage);c3_particle_cancel(&s->particles);
 c3_battle_presentation_init(&s->presentation,initial);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));C3BattlePreviewEvent hit[2]={miss,miss};hit[0].sequence=1;hit[0].group=3;hit[1].sequence=2;hit[1].group=3;hit[1].kind=C3_PREVIEW_EVENT_HP;memcpy(hit[1].hp,initial,sizeof initial);hit[1].hp[2]=20;
 assert(c3_battle_stage_submit(s,hit,2));unsigned char *raw=(unsigned char *)s->particle_pack.bank.bytes;size_t bad=s->particle_pack.bank.generators[3].script_offset+1;unsigned char original=raw[bad];int corrupted=0;failed=0;
 for(unsigned i=0;i<800;++i){
  if(s->particles_started&&!corrupted){raw[bad]=0x99;corrupted=1;}
  if(!c3_battle_stage_step(s,17,error,192)){failed=1;break;}
 }
 raw[bad]=original;assert(corrupted&&failed&&strstr(error,"opcode 0x99"));

}
int main(int argc,char **argv){assert(argc==2);C3BattleStage *s=calloc(1,sizeof *s);assert(s);char error[192];uint16_t hp[4]={80,75,50,50};
if(!c3_battle_stage_load(s,argv[1],hp,stdout,error,sizeof error)){puts(error);return 1;}
assert(s->bite_available&&s->count==(s->damage_available?11u:10u)&&static_count==1);
assert(c3_battle_stage_step(s,0,error,sizeof error));
size_t bytes=s->models[1].vertex_count*sizeof(C3TitleDrawVertex);void *copy=malloc(bytes);assert(copy);memcpy(copy,s->models[1].draw_vertices,bytes);
for(unsigned repeat=0;repeat<2&&!s->damage_available;++repeat){
C3BattlePreviewEvent e={0};e.kind=C3_PREVIEW_EVENT_MOVE;e.move=44;e.actor=0;e.target=2;e.sequence=repeat+1;
assert(c3_battle_stage_submit(s,&e,1));assert(c3_battle_stage_step(s,17,error,sizeof error));assert(s->bite.active&&s->presentation.waiting);
unsigned seen=0,dim=0;
for(unsigned time=0;time<3000;time+=17){
 if(!c3_battle_stage_step(s,17,error,sizeof error)){puts(error);return 1;}
 seen+=s->bite.model_visible;dim+=s->bite.floor_enabled;
 assert(!memcmp(copy,s->models[1].draw_vertices,bytes));
 for(unsigned i=0;i<s->count;i++)if(enabled[i])assert(s->slots[i]==UINT32_MAX);
}
assert(seen>0&&dim>0&&!s->bite.active&&!s->bite.model_visible&&!s->bite.floor_enabled);
assert(s->presentation.cursor==s->presentation.count&&s->bite.model_starts==1&&s->bite.sound_requests==1);
for(unsigned i=0;i<16;i++)assert(!enabled[i]);
printf("Actual scene repeat%u passed: %u effect-visible steps / %u floor-modulated steps; static arena unchanged\n",repeat,seen,dim);
}
if(s->damage_available)damage_cases(s,error,copy,bytes);
busy=1;assert(!c3_battle_stage_close(s));assert(s->models[1].file_data);busy=0;assert(c3_battle_stage_close(s));assert(s->count==0);free(copy);free(s);return 0;}
