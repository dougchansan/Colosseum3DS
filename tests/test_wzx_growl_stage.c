/* Optional original-resource stage fixture. Production load/timeline/pose code;
 * GPU submission and sound completion are explicit mocks. No game bytes.
 * Run through tests/test_growl_stage.py with C3_GROWL_STAGE_FIXTURE. */
#include "battle_stage.h"
#include "../platform/3ds/battle_stage.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counts,static_count,particles_bound;static unsigned char enabled[16];static int busy;
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)e;(void)c;counts=n;unsigned v=0;for(unsigned i=0;i<n;i++)v+=m[i]->vertex_count;assert(n<=16&&v<=131072);return 0;}
uint64_t c3_stage_renderer_gpu_skin_mask(size_t m){(void)m;return 0;}void c3_stage_renderer_request_gpu_skin(uint64_t m){(void)m;}
int c3_stage_renderer_set_static(size_t i,bool value){assert(i<counts);static_count+=value;return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<counts);enabled[i]=(rgb!=NULL);return 1;}
int c3_stage_renderer_get_modulation(size_t i,uint8_t rgb[3],int *enabled_out){(void)i;memset(rgb,128,3);*enabled_out=0;return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t n){(void)e;(void)n;assert(p&&p->texture_count);return C3_RENDER_OK;}

int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){if(s){assert(a);++particles_bound;}return 1;}

bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}

static C3BattleStage *observed;
static unsigned cue_count,last_cue,stop_calls,force_finished,finished_status[128],stopped_status[128],srt_samples,world_samples;
static uint64_t render_frame;
static int stop_pending,stop_failure,status_failure,status_invalid;
static int sound(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(id==0x210&&serial>last_cue&&serial<128);last_cue=serial;++cue_count;return 1;}
static int status(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(id==0x210&&serial==last_cue);if(status_failure)return -1;if(status_invalid)return 99;return finished_status[serial]?C3_BATTLE_AUDIO_CUE_FINISHED:stopped_status[serial]?C3_BATTLE_AUDIO_CUE_STOPPED:C3_BATTLE_AUDIO_CUE_ACTIVE;}
static int stop_sound(uint32_t serial,uint32_t id,char *e,size_t n){assert(id==0x210&&serial==last_cue);++stop_calls;if(stop_failure){snprintf(e,n,"mock stop failure");return 0;}if(stop_pending)return 1;if(force_finished)finished_status[serial]=1;else stopped_status[serial]=1;return status(serial,id,e,n)!=C3_BATTLE_AUDIO_CUE_ACTIVE;}
int c3_stage_renderer_particles_source_order(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){assert(observed&&s->bank.profile==C3_PARTICLE_PROFILE_GROWL_V1);C3ParticleCamera camera={0};float z[3],x[3],up[3];float zz=0,xx=0;
 for(unsigned k=0;k<3;++k){z[k]=observed->camera.eye[k]-observed->camera.target[k];zz+=z[k]*z[k];}assert(zz>0);for(unsigned k=0;k<3;++k)z[k]/=sqrtf(zz);x[0]=z[2];x[1]=0;x[2]=-z[0];for(unsigned k=0;k<3;++k)xx+=x[k]*x[k];assert(xx>0);for(unsigned k=0;k<3;++k)x[k]/=sqrtf(xx);for(unsigned k=0;k<3;++k)up[k]=z[(k+1)%3]*x[(k+2)%3]-z[(k+2)%3]*x[(k+1)%3];memcpy(camera.right,x,12);memcpy(camera.up,up,12);camera.clip[0]=camera.clip[5]=camera.clip[10]=camera.clip[15]=1;
 uint64_t ticks=s->ticks,draws=s->cosmetic_draws;float identity[12]={.1,0,0,0,0,.1,0,0,0,0,.1,0};++render_frame;
 for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i){const C3Particle *p=&s->particles[i];C3Particle saved=*p;C3ParticleQuad q;char error[192];int result=p->app_srt_handle?c3_particle_quad_app_srt(s,p,&camera,render_frame,.1f,&q,error,sizeof error):c3_particle_quad(p,identity,&camera,&q,error,sizeof error);if(result<0){puts(error);assert(0);}assert(!memcmp(p,&saved,sizeof saved));if(result){unsigned found=0;for(unsigned j=0;j<observed->growl_pack.texture_count;++j)found|=observed->growl_pack.textures[j].group==q.texture_group&&observed->growl_pack.textures[j].pose==q.texture_frame;assert(found);for(unsigned v=0;v<4;++v)for(unsigned k=0;k<3;++k)assert(isfinite(q.vertices[v].position[k]));if(p->app_srt_handle)++srt_samples;else ++world_samples;}}
 assert(s->ticks==ticks&&s->cosmetic_draws==draws);return 1;}
static unsigned owner_index(C3BattleStage *s,unsigned slot){for(unsigned i=0;i<s->count;++i)if(s->slots[i]==slot)return i;assert(0);return 0;}
static void submit(C3BattleStage *s,unsigned slot){uint16_t hp[4]={89,69,52,52};c3_battle_presentation_init(&s->presentation,hp);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));C3BattlePreviewEvent ev={0};ev.kind=C3_PREVIEW_EVENT_MOVE;ev.sequence=1;ev.group=1;ev.actor=(uint8_t)slot;ev.target=0;ev.target_mask=3;ev.move=45;memcpy(ev.hp,hp,sizeof hp);memcpy(ev.hp_before,hp,sizeof hp);assert(c3_battle_stage_submit(s,&ev,1));}
static unsigned refs(C3ParticleSystem *s){unsigned n=0;for(unsigned i=0;i<C3_PARTICLE_MAX_APP_SRT;++i)n+=s->app_srt[i].active;return n;}
int main(int argc,char **argv){assert(argc==2);char e[192];uint16_t hp[4]={89,69,52,52};C3BattleStage *s=calloc(1,sizeof *s);assert(s);if(!c3_battle_stage_load(s,argv[1],hp,NULL,e,sizeof e)){puts(e);assert(0);}assert(s->growl_available);observed=s;s->sound_cue=sound;s->sound_status=status;s->sound_stop=stop_sound;C3BattleCamera bite=s->source_camera;C3TitleCamera camera=s->camera;
 for(unsigned turn=0;turn<3;++turn){unsigned slot=turn==1?3:2,owner=owner_index(s,slot);unsigned before=cue_count,stops=stop_calls;force_finished=turn==2;submit(s,slot);assert(c3_battle_stage_step(s,0,e,sizeof e));assert(s->growl.active&&s->growl_owner==owner&&s->presentation.waiting&&cue_count==before);assert(c3_battle_stage_step(s,1400,e,sizeof e));assert(!s->growl.floor_enabled&&!s->growl.model_starts&&s->clocks[owner]==.7f);
 uint64_t time=s->growl.units;s->presentation.paused=1;assert(c3_battle_stage_step(s,4000,e,sizeof e)&&s->growl.units==time);s->presentation.paused=0;unsigned sawfloor=0,sawmodel=0,sawparticle=0;
 for(unsigned i=0;i<300&&s->presentation.cursor<s->presentation.count;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}sawfloor|=s->growl.floor_enabled;sawmodel|=s->growl.model_visible;sawparticle|=s->growl_particles_started;for(unsigned j=0;j<s->count;++j)if(enabled[j])assert(s->slots[j]==UINT32_MAX);}
 assert(sawfloor&&sawmodel&&sawparticle&&s->growl.completed&&!s->growl.active&&s->growl.units==230000&&s->presentation.cursor==1);assert(!s->growl.owner_normal_complete&&!s->growl.owner_minimum_complete);assert(cue_count==before+1&&stop_calls==stops+1);assert(s->growl.audio_status==(force_finished?C3_GROWL_AUDIO_FINISHED:C3_GROWL_AUDIO_STOPPED));assert(c3_particle_finished(&s->particles)&&!refs(&s->particles));assert(s->clips[owner]==s->roles[owner][2]&&s->growl_motion[owner]==1&&s->clocks[owner]>=115.f/60.f&&!s->growl.floor_enabled&&!s->growl.model_visible);assert(!memcmp(&camera,&s->camera,sizeof camera)&&!memcmp(&bite,&s->source_camera,sizeof bite));
 }
 assert(srt_samples&&world_samples);
 /* Owner callback continues source clip1/.5 independently of WZX wait. */
 unsigned owner=owner_index(s,2);force_finished=0;submit(s,2);assert(c3_battle_stage_step(s,3834,e,sizeof e));assert(s->growl.completed&&s->presentation.cursor==1&&s->growl_motion[owner]==1);uint64_t carry=s->growl_motion_units[owner];
 C3BattlePreviewEvent action={0};action.kind=C3_PREVIEW_EVENT_MOVE;action.sequence=2;action.group=2;action.actor=0;action.target=2;action.move=269;memcpy(action.hp,hp,sizeof hp);memcpy(action.hp_before,hp,sizeof hp);assert(c3_battle_stage_submit(s,&action,1));assert(c3_battle_stage_step(s,1000,e,sizeof e));assert(s->presentation.cursor==1&&s->growl_motion[owner]==1&&s->growl_motion_units[owner]>carry);
 uint64_t remaining=366000-s->growl_motion_units[owner];unsigned ms=(unsigned)((remaining-1)/60);assert(c3_battle_stage_step(s,ms,e,sizeof e));assert(s->growl_motion[owner]==1&&s->clips[owner]==s->roles[owner][2]&&s->growl_motion_units[owner]<366000);assert(c3_battle_stage_step(s,1,e,sizeof e));assert(s->growl_motion[owner]==2&&s->clips[owner]==s->roles[owner][0]);uint64_t idle=s->growl_motion_units[owner];assert(c3_battle_stage_step(s,100,e,sizeof e));assert(s->growl_motion_units[owner]==idle+6000&&fabsf(s->clocks[owner]-(float)((double)(idle+6000)/60000*.5))<1e-6f);
 /* New typed move/hit/faint takes ownership of the actor immediately. */
 const C3BattlePreviewEventKind overrides[3]={C3_PREVIEW_EVENT_MOVE,C3_PREVIEW_EVENT_HP,C3_PREVIEW_EVENT_FAINT};
 for(unsigned k=0;k<3;++k){submit(s,2);assert(c3_battle_stage_step(s,3834,e,sizeof e));assert(s->growl_motion[owner]==1);action.kind=overrides[k];action.sequence=3+k;action.actor=2;action.target=2;action.move=33;assert(c3_battle_stage_submit(s,&action,1));assert(c3_battle_stage_step(s,0,e,sizeof e));assert(!s->growl_motion[owner]&&!s->growl_motion_units[owner]);assert(s->clips[owner]==s->roles[owner][k==0?1:k==1?3:4]);/* New submit requires completed prior event. */for(unsigned i=0;i<20&&s->presentation.cursor<s->presentation.count;++i)assert(c3_battle_stage_step(s,4000,e,sizeof e));assert(s->presentation.cursor==1);}
 /* Keyoff acceptance may stay ACTIVE; immediate teardown already happened. */
 stop_pending=1;submit(s,2);unsigned stops=stop_calls;assert(c3_battle_stage_step(s,3834,e,sizeof e));assert(s->growl.active&&!s->growl.completed&&s->presentation.waiting&&s->growl_teardown_started&&s->growl_keyoff_sent&&stop_calls==stops+1);assert(c3_particle_finished(&s->particles)&&!refs(&s->particles)&&!s->growl.model_visible&&!s->growl.floor_enabled&&!s->growl_camera.active&&s->growl_motion[owner]==1);
 uint32_t waited=s->growl_audio_wait_ms;uint64_t units=s->growl_motion_units[owner];assert(!c3_battle_stage_step(s,4001,e,sizeof e));assert(s->growl_audio_wait_ms==waited&&s->growl_motion_units[owner]==units&&stop_calls==stops+1);s->presentation.paused=1;assert(c3_battle_stage_step(s,4000,e,sizeof e));assert(s->growl_audio_wait_ms==waited&&s->growl_motion_units[owner]==units&&stop_calls==stops+1);s->presentation.paused=0;assert(c3_battle_stage_step(s,17,e,sizeof e));assert(s->growl_audio_wait_ms==waited+17&&stop_calls==stops+1);finished_status[last_cue]=1;assert(c3_battle_stage_step(s,1,e,sizeof e));assert(s->growl.completed&&s->growl.audio_status==C3_GROWL_AUDIO_FINISHED&&s->presentation.cursor==1&&stop_calls==stops+1);stop_pending=0;
 /* Before source tick130 cancellation requests no cue or keyoff. */
 unsigned cues=cue_count;stops=stop_calls;submit(s,2);assert(c3_battle_stage_step(s,1000,e,sizeof e));growl_cancel(s);assert(cue_count==cues&&stop_calls==stops&&!s->growl_cue&&!s->growl.active);c3_battle_presentation_wait(&s->presentation,0);s->presentation.cursor=s->presentation.count;
 /* Failed stop/status/invalid status and bounded ACTIVE timeout are surfaced. */
 for(unsigned failure=0;failure<4;++failure){stop_failure=failure==0;status_failure=failure==1;status_invalid=failure==2;stop_pending=failure==3;submit(s,2);stops=stop_calls;int result=c3_battle_stage_step(s,3834,e,sizeof e);if(failure==3){assert(result&&s->growl.active);assert(c3_battle_stage_step(s,4000,e,sizeof e));assert(c3_battle_stage_step(s,4000,e,sizeof e));result=c3_battle_stage_step(s,3000,e,sizeof e);assert(strstr(e,"timed out"));}assert(!result&&s->growl_teardown_started&&c3_particle_finished(&s->particles)&&!refs(&s->particles));assert(stop_calls==stops+1);if(failure==0)assert(strstr(e,"mock stop failure"));stop_failure=status_failure=status_invalid=stop_pending=0;growl_cancel(s);s->presentation.cursor=s->presentation.count;s->presentation.waiting=0;s->error[0]=0;}
 /* BUSY renderer keeps source storage alive, but family/keyoff teardown once. */
 submit(s,2);for(unsigned i=0;i<300&&!refs(&s->particles);++i)assert(c3_battle_stage_step(s,17,e,sizeof e));assert(s->growl.active&&refs(&s->particles));stops=stop_calls;busy=1;assert(!c3_battle_stage_close(s)&&!s->growl.active&&s->growl_bytes);assert(!refs(&s->particles)&&stop_calls==stops+1&&!s->growl_camera.active);busy=0;assert(c3_battle_stage_close(s)&&!s->growl_bytes&&!s->growl_camera_program.samples);assert(c3_battle_stage_close(s)&&stop_calls==stops+1);
 printf("Growl stage models=%u srt_samples=%u world_samples=%u owner_carry/audio_pending/failures passed\n",counts,srt_samples,world_samples);free(s);puts("Actual Growl stage contract passed");return 0;}
