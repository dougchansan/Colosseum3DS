/* Optional original-resource stage regression. Only GPU ownership and external
 * audio status are mocked; production stage, actual poses and mixed geometry
 * run unchanged. No original source game bytes are embedded. */
#include "battle_stage.h"
#include "../platform/3ds/battle_stage.c"
#include "colosseum3ds/particle_line.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static C3BattleStage *observed;
static unsigned models,pack_count,line_samples,quad_samples,ordered_samples;
static int busy,pack_busy,bind_failure,cue_failure,stop_failure,status_failure,premature,voice_finished;
static uint8_t modulation[16][3],modulated[16];
static unsigned cue_count,stop_count,last_serial;
static uint32_t ids[8192],stopped[8192];
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *p){(void)p;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t z){(void)e;(void)z;models=(unsigned)n;assert(n<=16);unsigned vertices=0;for(unsigned i=0;i<n;++i)vertices+=m[i]->vertex_count;assert(vertices<=131072);memset(modulation,128,sizeof modulation);memset(modulated,0,sizeof modulated);pack_count=0;return C3_RENDER_OK;}
int c3_stage_renderer_set_static(size_t i,bool value){(void)value;assert(i<models);return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<models);modulated[i]=(uint8_t)(rgb!=NULL);if(rgb)memcpy(modulation[i],rgb,3);else memset(modulation[i],128,3);return 1;}
int c3_stage_renderer_get_modulation(size_t i,uint8_t rgb[3],int *enabled){assert(i<models);memcpy(rgb,modulation[i],3);*enabled=modulated[i];return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t n){(void)e;(void)n;assert(p&&p->texture_count);++pack_count;return pack_busy?C3_RENDER_BUSY:C3_RENDER_OK;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
static unsigned bucket(const C3Particle *p){return ((p->kind>>25)&7)+((p->kind&8)?0:8);}
int c3_stage_renderer_particles_source_order(C3ParticleSystem *s){
 if(!s)return 1;
 assert(s->bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1);
 uint16_t slots[C3_PARTICLE_MAX_PARTICLES];size_t n=0;char error[192];
 uint64_t ticks=s->ticks,draws=s->cosmetic_draws;
 assert(c3_particle_render_indices(s,slots,C3_PARTICLE_MAX_PARTICLES,&n,error,sizeof error));
 for(size_t i=0;i<n;++i){assert(slots[i]<C3_PARTICLE_MAX_PARTICLES&&s->particles[slots[i]].active);if(i){const C3Particle *a=s->particles+slots[i-1],*b=s->particles+slots[i];assert(bucket(a)<=bucket(b));if(bucket(a)==bucket(b))assert(a->serial>b->serial);}}
 assert(ticks==s->ticks&&draws==s->cosmetic_draws);++ordered_samples;const float attachment[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};return c3_stage_renderer_particles(s,attachment);
}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){
 if(!s||s->bank.profile!=C3_PARTICLE_PROFILE_TACKLE_V1)return 1;
 if(bind_failure)return 0;
 assert(observed&&a&&a[0]==.1f&&a[5]==.1f&&a[10]==.1f&&a[3]==0&&a[7]==0&&a[11]==0);
 C3ParticleCamera camera={0};camera.right[0]=camera.up[1]=1;camera.clip[0]=camera.clip[5]=.01f;camera.clip[10]=-.0001f;camera.clip[11]=-.5f;camera.clip[15]=1;
 C3ParticleLineRaster raster={{1,1},-1,0};uint64_t ticks=s->ticks,draws=s->cosmetic_draws;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i)if(s->particles[i].active){const C3Particle *p=s->particles+i;C3Particle saved=*p;unsigned group=0,pose=0;int result;
  char error[192];
  if(p->kind&0x40000000u){C3ParticleLine line;result=c3_particle_line(p,a,&camera,&raster,&line,error,sizeof error);assert(result>=0);if(result){C3ParticleLineMesh mesh;assert(c3_particle_line_triangles(&line,(float[]){400,240},&mesh,error,sizeof error)==1);assert(line.source[0].rgba[3]==0);for(unsigned v=0;v<6;++v)for(unsigned k=0;k<4;++k)assert(isfinite(mesh.vertices[v].clip[k]));group=line.texture_group;pose=line.texture_frame;++line_samples;}}
  else{C3ParticleQuad quad;result=c3_particle_quad(p,a,&camera,&quad,error,sizeof error);assert(result>=0);if(result){for(unsigned v=0;v<4;++v)for(unsigned k=0;k<3;++k)assert(isfinite(quad.vertices[v].position[k]));group=quad.texture_group;pose=quad.texture_frame;++quad_samples;}}
  if(result){unsigned found=0;for(unsigned t=0;t<observed->tackle_pack.texture_count;++t)found|=observed->tackle_pack.textures[t].group==group&&observed->tackle_pack.textures[t].pose==pose;assert(found);}
  assert(!memcmp(&saved,p,sizeof saved));
 }
 assert(s->ticks==ticks&&s->cosmetic_draws==draws);return 1;
}
bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *s,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)s;(void)e;(void)z;return 0;}
static int sound(uint32_t serial,uint32_t id,char *e,size_t n){if(cue_failure&&(cue_failure==1||id==0xDE)){snprintf(e,n,"injected Tackle wave failure");return 0;}assert(serial>last_serial&&serial<8192&&(id==0x4A2||id==0xDE));ids[serial]=id;last_serial=serial;++cue_count;return 1;}
static int status(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(serial&&serial<8192&&ids[serial]==id);if(status_failure)return -1;if(stopped[serial])return C3_BATTLE_AUDIO_CUE_STOPPED;return premature?C3_BATTLE_AUDIO_CUE_STOPPED:voice_finished?C3_BATTLE_AUDIO_CUE_FINISHED:C3_BATTLE_AUDIO_CUE_ACTIVE;}
static int stop_sound(uint32_t serial,uint32_t id,char *e,size_t n){assert(serial&&serial<8192&&ids[serial]==id);++stop_count;if(stop_failure){snprintf(e,n,"injected Tackle keyoff failure");return 0;}stopped[serial]=1;return 1;}
static unsigned index_for(C3BattleStage *s,unsigned slot){for(unsigned i=0;i<s->count;++i)if(s->slots[i]==slot)return i;assert(0);return 0;}
static void step(C3BattleStage *s,unsigned ms){char e[192];if(!c3_battle_stage_step(s,ms,e,sizeof e)){fprintf(stderr,"Tackle step%u: %s\n",ms,e);assert(0);}}
static void load(C3BattleStage *s,const char *path){char e[192];uint16_t hp[4]={89,69,52,52};observed=s;last_serial=cue_count=stop_count=0;memset(ids,0,sizeof ids);memset(stopped,0,sizeof stopped);cue_failure=stop_failure=status_failure=premature=voice_finished=bind_failure=0;if(!c3_battle_stage_load(s,path,hp,NULL,e,sizeof e)){fprintf(stderr,"Tackle load: %s\n",e);assert(0);}assert(s->tackle_available&&s->tackle_camera_available&&pack_count==8);s->sound_cue=sound;s->sound_status=status;s->sound_stop=stop_sound;}
static C3BattlePreviewEvent move(unsigned actor,unsigned target,unsigned sequence){C3BattlePreviewEvent e={0};e.kind=C3_PREVIEW_EVENT_MOVE;e.sequence=sequence;e.group=sequence;e.move=33;e.actor=(uint8_t)actor;e.target=(uint8_t)target;e.target_mask=(uint8_t)(1u<<target);e.draws=0x12345678u;uint16_t hp[4]={89,69,52,52};memcpy(e.hp,hp,sizeof hp);memcpy(e.hp_before,hp,sizeof hp);return e;}
static void submit(C3BattleStage *s,unsigned actor,unsigned target,int hit,int faint){uint16_t hp[4]={89,69,52,52};c3_battle_presentation_init(&s->presentation,hp);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));s->error[0]=0;for(unsigned i=0;i<s->count;++i)if(s->slots[i]<4){s->clips[i]=s->roles[i][0];s->clocks[i]=0;s->growl_motion[i]=0;s->growl_motion_units[i]=0;s->owner_motion_end[i]=0;}C3BattlePreviewEvent e[3];e[0]=move(actor,target,1);e[1]=e[0];e[1].sequence=2;e[1].kind=hit?C3_PREVIEW_EVENT_HP:C3_PREVIEW_EVENT_MISS;e[1].hp[target]=(uint16_t)(faint?0:hp[target]-11);e[2]=e[1];e[2].sequence=3;e[2].kind=C3_PREVIEW_EVENT_FAINT;assert(c3_battle_stage_submit(s,e,(uint32_t)(hit&&faint?3:2)));}
static void finish(C3BattleStage *s,unsigned partition){for(unsigned elapsed=0;elapsed<18000&&s->presentation.cursor<s->presentation.count;elapsed+=partition)step(s,partition);assert(s->presentation.cursor==s->presentation.count);}
static void partitions(C3BattleStage *s){
 const unsigned parts[3]={1,17,1000};uint64_t units[3],draws[3],ticks[3];unsigned emitted[3],peak[3];
 for(unsigned p=0;p<3;++p){voice_finished=1;submit(s,2,0,1,0);C3BattlePreviewEvent committed[2];memcpy(committed,s->presentation.events,sizeof committed);finish(s,parts[p]);assert(s->tackle.completed&&s->tackle.defender_starts==1&&s->tackle.hp_gates==1&&s->tackle.handoff_units==20&&s->presentation.hp[0]==78&&s->tackle.attack_sound_requests==1&&s->tackle.damage_sound_requests==1);assert(!memcmp(committed,s->presentation.events,sizeof committed));units[p]=s->tackle.units;draws[p]=s->particles.cosmetic_draws;ticks[p]=s->particles.ticks;emitted[p]=s->particles.emitted;peak[p]=s->particles.peak_particles;printf("partition%u units=%llu births=%u peak=%u draws=%llu ticks=%llu\n",parts[p],(unsigned long long)units[p],emitted[p],peak[p],(unsigned long long)draws[p],(unsigned long long)ticks[p]);fflush(stdout);assert(emitted[p]==134&&peak[p]==133&&draws[p]==964);}
 for(unsigned p=1;p<3;++p)assert(units[p]==units[0]&&draws[p]==draws[0]&&ticks[p]==ticks[0]&&emitted[p]==emitted[0]&&peak[p]==peak[0]);
 /* Source display mutates its particle list. Attributes can legitimately differ
  * across these draw cadences; no whole-pool/pixel partition parity claim. */
 printf("Tackle partitions1/17/1000 phase_units=%llu emitted134/peak133/draws964 (first natural particle finish104 qualified separately)\n",(unsigned long long)units[0]);
}
static void guard_cases(C3BattleStage *s){
 char e[192];unsigned caster=index_for(s,2),other=index_for(s,1),trainer=index_for(s,4);
 for(unsigned mode=0;mode<6;++mode){voice_finished=0;submit(s,2,0,1,0);step(s,2216);s->clocks[other]=s->clocks[trainer]=0;
  if(mode==0)s->presentation.visible[1]=0;
  if(mode==1)s->presentation.hp[1]=0;
  if(mode==2)s->clips[other]=s->roles[other][4];
  if(mode==3){unsigned unknown=s->models[other].clip_count;for(unsigned clip=0;clip<s->models[other].clip_count;++clip){int known=0;for(unsigned role=0;role<5;++role)known|=clip==s->roles[other][role];if(!known){unknown=clip;break;}}assert(unknown<s->models[other].clip_count);s->clips[other]=unknown;}
  if(mode==4){assert(s->models[trainer].clip_count>1);s->clips[trainer]=s->roles[trainer][0]?0:1;}
  if(mode==5){s->presentation.hp[1]=0;s->presentation.visible[1]=0;s->clips[other]=s->roles[other][4];s->clocks[other]=0;}
  int result=c3_battle_stage_step(s,1,e,sizeof e);printf("guard%u result%d error=%s\n",mode,result,result?"none":e);fflush(stdout);assert(!result&&strstr(e,"Tackle reset"));assert(s->clips[caster]==4&&!s->tackle_particles_started&&!s->tackle_cues[1]);tackle_cancel(s);assert(!s->tackle.active&&c3_particle_finished(&s->particles));s->clips[trainer]=s->roles[trainer][0];
 }
 submit(s,2,0,1,0);step(s,2216);s->clocks[other]=.25f;step(s,1);assert(s->clocks[other]>=.25f);tackle_cancel(s);
 submit(s,2,0,1,0);step(s,2216);s->presentation.hp[1]=0;s->presentation.visible[1]=0;s->clips[other]=s->roles[other][4];s->clocks[other]=(float)s->models[other].clips[s->clips[other]].frame_count/s->models[other].fps;step(s,1);assert(s->tackle.phase==2&&s->tackle.particle_starts&&s->clips[other]==s->roles[other][4]&&!s->presentation.visible[1]);tackle_cancel(s);
 puts("Tackle reset guards hidden/fainted/unknown/trainer refusal, completed-faint skip and existing idle preservation passed");
}
static void failures(C3BattleStage *s,const char *fixture){
 char e[192];
 /* Fail after the owned bank was created but before the DE cue was accepted. */
 submit(s,2,0,1,0);step(s,2216);cue_failure=2;assert(!c3_battle_stage_step(s,1,e,sizeof e)&&strstr(e,"wave failure"));assert(!s->tackle_cues[1]&&s->particles.bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1);cue_failure=0;assert(c3_battle_stage_close(s)&&!s->tackle_bytes&&c3_particle_finished(&s->particles));assert(c3_battle_stage_close(s));load(s,fixture);
 /* STOPPED cannot impersonate natural FINISHED; invalid callback and binding
  * failures also propagate, and close tears down the partially active family. */
 for(unsigned mode=0;mode<3;++mode){submit(s,3,1,1,0);step(s,2217);if(mode==0)premature=1;else if(mode==1)status_failure=1;else bind_failure=1;assert(!c3_battle_stage_step(s,17,e,sizeof e));if(mode==0)assert(strstr(e,"FINISHED"));premature=status_failure=bind_failure=0;assert(c3_battle_stage_close(s)&&!s->tackle_bytes&&c3_particle_finished(&s->particles));assert(c3_battle_stage_close(s));load(s,fixture);}
 /* Failure to keyoff after pure clock completion still leaves a recoverable
  * accepted cue identity for cancellation, rather than skipping cleanup. */
 submit(s,2,0,1,0);voice_finished=1;step(s,2217);stop_failure=1;step(s,4000);assert(!c3_battle_stage_step(s,2000,e,sizeof e)&&strstr(e,"keyoff failure"));stop_failure=0;unsigned stopped_before=stop_count;tackle_cancel(s);assert(stop_count==stopped_before+1&&s->tackle_keyoff[1]&&c3_particle_finished(&s->particles));tackle_cancel(s);assert(stop_count==stopped_before+1);
 puts("Tackle partial bank/cue/status/STOPPED/binding/keyoff failure cleanup passed");
}
int main(int argc,char **argv){
 assert(argc==2);C3BattleStage *s=calloc(1,sizeof *s);assert(s);char e[192];load(s,argv[1]);
 C3BattleAudio audio={0};char path[1024];assert(snprintf(path,sizeof path,"%s/audio.c3u",argv[1])<(int)sizeof path);assert(c3_battle_audio_load(&audio,path,e,sizeof e)&&audio.count==13&&c3_battle_audio_find(&audio,0x4A2)&&c3_battle_audio_find(&audio,0xDE));c3_battle_audio_close(&audio);
 /* Same typed source move for either Zigzagoon and both committed targets. */
 for(unsigned actor=2;actor<4;++actor)for(unsigned target=0;target<2;++target){unsigned a=index_for(s,actor),d=index_for(s,target);voice_finished=0;unsigned cues=cue_count,stops=stop_count;submit(s,actor,target,1,1);C3BattlePreviewEvent committed[3];memcpy(committed,s->presentation.events,sizeof committed);step(s,0);assert(s->tackle.active&&s->tackle_owner==a&&s->tackle_target==d&&s->clips[a]==4&&!s->tackle.particle_starts);step(s,2166);assert(cue_count==cues&&!s->tackle.attack_sound_requests&&s->presentation.hp[target]==committed[0].hp[target]);step(s,1);assert(cue_count==cues+1&&s->tackle.attack_sound_requests);step(s,49);assert(!s->tackle.attack_complete&&s->clips[a]==4);step(s,1);assert(s->tackle.attack_complete&&s->tackle.phase==2&&s->tackle.reset_except_requests&&s->tackle.hp_gates&&s->tackle.particle_starts&&cue_count==cues+2&&stop_count==stops+1);assert(s->clips[a]==s->roles[a][0]&&s->clips[d]==s->tackle_program.owners[target].owner_clip);assert(s->presentation.hp[target]==0&&s->presentation.visible[target]);
  const C3WzxTackleOwner *owner=s->tackle_program.owners+target;const float *t=s->transforms[d];float co=cosf(t[3]),si=sinf(t[3]),expected[3]={10*(t[0]+t[4]*(co*owner->anchor[0]+si*owner->anchor[2])),10*(t[1]+t[4]*owner->anchor[1]),10*(t[2]+t[4]*(-si*owner->anchor[0]+co*owner->anchor[2]))};assert(owner->body_part==(target?62u:42u));unsigned compact_diff=0;for(unsigned k=0;k<3;++k)compact_diff|=fabsf(s->models[d].pose.matrices[0][4*k+3]-owner->anchor[k])>1e-5f;assert(compact_diff);for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(s->particles.emitters[i].active&&s->particles.emitters[i].depth==0){for(unsigned k=0;k<3;++k)assert(fabsf(s->particles.emitters[i].position[k]-expected[k])<.0001f);}
  uint64_t units=s->tackle.units,ticks=s->particles.ticks,draws=s->particles.cosmetic_draws;s->presentation.paused=1;step(s,4000);assert(s->tackle.units==units&&s->particles.ticks==ticks&&s->particles.cosmetic_draws==draws);s->presentation.paused=0;for(unsigned frame=0;frame<120;++frame)step(s,17);step(s,4000);assert(s->tackle.active&&s->presentation.waiting&&s->presentation.visible[target]&&c3_particle_finished(&s->particles));assert(!memcmp(committed,s->presentation.events,sizeof committed));voice_finished=1;step(s,0);assert(s->tackle.completed&&!s->tackle.active&&s->tackle.family_stop_requests&&stop_count==stops+2);finish(s,17);assert(!s->presentation.visible[target]);printf("Tackle actor%u target%u emitted=%u generators=%u peak=%u draws=%llu\n",actor,target,s->particles.emitted,s->particles.spawned_emitters,s->particles.peak_particles,(unsigned long long)s->particles.cosmetic_draws);
 }
 partitions(s);guard_cases(s);failures(s,argv[1]);
 /* Miss has no committed HP callback, bank, soundDE or shared reset. */
 unsigned before=cue_count;uint32_t old_defenders=s->tackle.defender_starts;submit(s,2,0,0,0);voice_finished=1;finish(s,17);assert(!s->tackle.active&&s->tackle.defender_starts==old_defenders&&cue_count==before);
 /* Consumer failures remain actionable and cancellation is idempotent. */
 submit(s,2,0,1,0);cue_failure=1;assert(!c3_battle_stage_step(s,2167,e,sizeof e)&&strstr(e,"wave failure"));cue_failure=0;tackle_cancel(s);assert(!s->tackle.active);tackle_cancel(s);
 submit(s,3,1,1,0);step(s,2217);assert(s->tackle.active&&s->tackle_particles_started);before=stop_count;busy=1;assert(!c3_battle_stage_close(s)&&s->tackle_bytes&&!s->tackle.active&&stop_count==before+1);assert(c3_particle_finished(&s->particles)&&!s->tackle_camera.active);busy=0;assert(c3_battle_stage_close(s)&&!s->tackle_bytes&&!s->tackle_camera_program.samples);assert(c3_battle_stage_close(s)&&stop_count==before+1);
 pack_busy=busy=1;uint16_t hp[4]={89,69,52,52};assert(!c3_battle_stage_load(s,argv[1],hp,NULL,e,sizeof e)&&s->initialized&&s->tackle_bytes);assert(!c3_battle_stage_close(s)&&s->tackle_bytes);pack_busy=busy=0;assert(c3_battle_stage_close(s)&&!s->tackle_bytes);assert(c3_battle_stage_close(s));
 assert(line_samples&&quad_samples&&ordered_samples);printf("Tackle geometry lines=%u quads=%u ordered=%u\n",line_samples,quad_samples,ordered_samples);free(s);puts("Actual Tackle stage contract passed");return 0;
}
