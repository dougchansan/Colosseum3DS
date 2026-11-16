/* Optional original-resource stage fixture. Production load/timeline/pose code;
 * GPU submission and sound completion are explicit mocks. No game bytes.
 * Run through tests/test_tail_whip_stage.py with C3_TAIL_WHIP_STAGE_FIXTURE. */
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
int c3_stage_renderer_particles_source_order(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *s){(void)s;return 1;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){if(s){assert(a);++particles_bound;}return 1;}

bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}
static unsigned cue_count,cue_serial,audio_hold=1;
static int sound(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(id==0x1DB&&serial>cue_serial);cue_serial=serial;++cue_count;return 1;}
static int done(uint32_t serial,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(id==0x1DB&&serial==cue_serial);return !audio_hold;}
static unsigned owner_index(C3BattleStage *s,unsigned slot){for(unsigned i=0;i<s->count;++i)if(s->slots[i]==slot)return i;assert(0);return 0;}
static void submit(C3BattleStage *s,unsigned slot){uint16_t hp[4]={89,69,52,52};c3_battle_presentation_init(&s->presentation,hp);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));C3BattlePreviewEvent ev={0};ev.kind=C3_PREVIEW_EVENT_MOVE;ev.sequence=1;ev.group=1;ev.actor=(uint8_t)slot;ev.target=0;ev.target_mask=3;ev.move=39;memcpy(ev.hp,hp,sizeof hp);memcpy(ev.hp_before,hp,sizeof hp);assert(c3_battle_stage_submit(s,&ev,1));}
int main(int argc,char **argv){assert(argc==2);char e[192];uint16_t hp[4]={89,69,52,52};C3BattleStage *s=calloc(1,sizeof *s);assert(s);if(!c3_battle_stage_load(s,argv[1],hp,NULL,e,sizeof e)){puts(e);assert(0);}assert(s->tail_available);s->sound_cue=sound;s->sound_done=done;C3BattleCamera bite=s->source_camera;C3TitleCamera camera=s->camera;
 for(unsigned slot=2;slot<4;++slot){unsigned owner=owner_index(s,slot);float base[5];memcpy(base,s->transforms[owner],sizeof base);unsigned before=cue_count;audio_hold=1;submit(s,slot);assert(c3_battle_stage_step(s,0,e,sizeof e));assert(s->tail.active&&s->tail_owner==owner&&s->presentation.waiting&&cue_count==before+1);assert(c3_battle_stage_step(s,300,e,sizeof e));assert(s->tail.owner_attached&&fabsf(s->transforms[owner][3]-base[3])>.001f);assert(s->transforms[owner][4]==base[4]);assert(s->transforms[s->tail_model][3]==base[3]&&s->transforms[s->tail_model][4]==.75f);
 uint64_t time=s->tail.units;float pose[5];memcpy(pose,s->transforms[owner],sizeof pose);s->presentation.paused=1;assert(c3_battle_stage_step(s,4000,e,sizeof e)&&s->tail.units==time&&!memcmp(pose,s->transforms[owner],sizeof pose));s->presentation.paused=0;
 assert(c3_battle_stage_step(s,4000,e,sizeof e));assert(s->tail.active&&s->tail.controller_ended&&s->tail.effect_ended&&!s->tail.effect_visible&&s->presentation.waiting);assert(!memcmp(base,s->transforms[owner],sizeof base));audio_hold=0;assert(c3_battle_stage_step(s,0,e,sizeof e));assert(!s->tail.active&&s->tail.completed&&cue_count==before+1&&s->presentation.cursor==1);assert(!memcmp(base,s->transforms[owner],sizeof base)&&!memcmp(&camera,&s->camera,sizeof camera));assert(!memcmp(&bite,&s->source_camera,sizeof bite));
 }
 audio_hold=1;submit(s,2);assert(c3_battle_stage_step(s,300,e,sizeof e));unsigned owner=s->tail_owner;float root[3];memcpy(root,s->tail.root,sizeof root);float yaw=s->tail.root_yaw;busy=1;assert(!c3_battle_stage_close(s)&&!s->tail.active&&s->tail_program.motion_data);assert(!memcmp(root,s->transforms[owner],sizeof root)&&s->transforms[owner][3]==yaw);busy=0;assert(c3_battle_stage_close(s)&&!s->tail_program.motion_data&&!s->tail_camera_program.samples);assert(c3_battle_stage_close(s));free(s);puts("Actual Tail Whip stage contract passed");return 0;}
