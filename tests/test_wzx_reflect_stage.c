/* Optional private-asset test. Executes production loaders/sampling/timeline;
 * only GPU submission is replaced by an explicitly bounded ownership fixture.
 * No original assets are embedded. Usage: test_wzx_bite_stage PRIVATE_SCENE_DIR. */
#include "battle_stage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counts,static_count,particles_bound,reflect_audio,last_cue,audio_hold;static unsigned char enabled[16];static int busy;
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *p,char *e,size_t n){(void)p;(void)e;(void)n;return 1;}
int c3_stage_renderer_confusion(const C3ConfusionType12 *s){(void)s;return 1;}
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)e;(void)c;counts=n;unsigned v=0;for(unsigned i=0;i<n;i++)v+=m[i]->vertex_count;assert(n<=16&&v<=131072);return 0;}
int c3_stage_renderer_set_static(size_t i,bool value){assert(i<counts);static_count+=value;return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<counts);enabled[i]=(rgb!=NULL);return 1;}
int c3_stage_renderer_particle_pack(const C3ParticlePack *p,char *e,size_t n){(void)e;(void)n;assert(p&&p->texture_count);return C3_RENDER_OK;}
int c3_stage_renderer_particles(const C3ParticleSystem *s,const float a[12]){if(s){assert(a);++particles_bound;}return 1;}
static int sound(uint32_t sequence,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(sequence>last_cue);last_cue=sequence;assert(id==0x220);++reflect_audio;return 1;}
static int done(uint32_t sequence,uint32_t id,char *e,size_t n){(void)e;(void)n;assert(sequence==last_cue&&id==0x220);return !audio_hold;}
bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}

static void model_position(C3BattleStage *s,float out[3])
{
 unsigned i=s->reflect_model;const float *owner=s->transforms[s->reflect_owner],*t=s->transforms[i];
 assert(t[4]==.75f);assert(c3_wzx_reflect_position(&s->reflect_program,s->reflect.species,s->reflect.owner_seconds,owner,owner[3],owner[4],out,NULL,0));
 for(unsigned k=0;k<3;++k)assert(fabsf(t[k]-out[k])<.00001f);
 C3Title *local=calloc(1,sizeof *local);assert(local);assert(c3_title_instance(local,&s->models[i],NULL,0));assert(c3_title_animate(local,0,s->reflect.model_seconds));
 const float *v=local->draw_vertices[0].pos,*actual=s->models[i].draw_vertices[0].pos;float co=cosf(t[3]),si=sinf(t[3]);
 float expected[3]={out[0]+.75f*(co*v[0]+si*v[2]),out[1]+.75f*v[1],out[2]+.75f*(-si*v[0]+co*v[2])};
 for(unsigned k=0;k<3;++k)assert(fabsf(expected[k]-actual[k])<.0001f);
 assert(fabsf(out[0]-owner[0])+fabsf(out[1]-owner[1])+fabsf(out[2]-owner[2])>.01f);
 c3_title_close(local);free(local);
}
static void events(C3BattleStage *s,int ally,int successful){
 uint16_t hp[4]={ally?80:0,75,50,50};c3_battle_presentation_init(&s->presentation,hp);assert(c3_battle_presentation_wait_limit(&s->presentation,20000));
 C3BattlePreviewEvent q[2]={{0},{0}};for(unsigned i=0;i<2;++i){q[i].sequence=i+1;q[i].group=1;q[i].actor=1;q[i].move=115;memcpy(q[i].hp,hp,sizeof hp);memcpy(q[i].hp_before,hp,sizeof hp);}
 q[0].kind=C3_PREVIEW_EVENT_MOVE;q[1].kind=C3_PREVIEW_EVENT_STATUS;q[1].reflect[0]=successful?5:0;
 assert(c3_battle_stage_submit(s,q,2));
}
int main(int argc,char **argv){assert(argc==2);C3BattleStage *s=calloc(1,sizeof *s);assert(s);char e[192];uint16_t hp[4]={80,75,50,50};
 if(!c3_battle_stage_load(s,argv[1],hp,stdout,e,sizeof e)){puts(e);return 1;}assert(s->reflect_available&&static_count==1);s->sound_cue=sound;s->sound_done=done;
 for(unsigned ally=0;ally<2;++ally){events(s,ally,1);unsigned before=reflect_audio;audio_hold=1;assert(c3_battle_stage_step(s,0,e,sizeof e));assert(s->reflect.active&&reflect_audio==before+1);
 float first[3],second[3];model_position(s,first);assert(c3_battle_stage_step(s,500,e,sizeof e));model_position(s,second);assert(fabsf(first[0]-second[0])+fabsf(first[1]-second[1])+fabsf(first[2]-second[2])>.00001f);
 for(unsigned i=0;i<600;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}for(unsigned j=0;j<s->count;++j)if(enabled[j])assert(s->slots[j]==UINT32_MAX);}
 assert(s->reflect.active&&s->slots[s->reflect_owner]==1&&s->presentation.waiting);uint64_t units=s->reflect.units;s->presentation.paused=1;assert(c3_battle_stage_step(s,4000,e,sizeof e)&&s->reflect.units==units);s->presentation.paused=0;
 audio_hold=0;unsigned ally_checked=0;for(unsigned i=0;i<1200&&s->presentation.cursor<s->presentation.count;++i){if(!c3_battle_stage_step(s,17,e,sizeof e)){puts(e);assert(0);}if(ally&&!ally_checked&&s->reflect.active&&s->slots[s->reflect_owner]==0&&s->reflect.model_visible){model_position(s,second);ally_checked=1;}}assert(!ally||ally_checked);
 assert(!s->reflect.active&&s->presentation.cursor==s->presentation.count&&reflect_audio==before+1+ally);assert(s->reflect.completed&&c3_particle_finished(&s->particles));
 }
 events(s,1,0);unsigned before=reflect_audio;for(unsigned i=0;i<100;++i)assert(c3_battle_stage_step(s,17,e,sizeof e));assert(!s->reflect.active&&reflect_audio==before);
 events(s,1,1);s->presentation.events[1].kind=C3_PREVIEW_EVENT_MISS;for(unsigned i=0;i<100;++i)assert(c3_battle_stage_step(s,17,e,sizeof e));assert(!s->reflect.active&&reflect_audio==before);
 events(s,1,1);s->presentation.events[0].reflect[0]=5;for(unsigned i=0;i<100;++i)assert(c3_battle_stage_step(s,17,e,sizeof e));assert(!s->reflect.active&&reflect_audio==before);
 /* Audio unavailable and real completion errors fail closed. */
 events(s,1,1);s->sound_done=NULL;assert(!c3_battle_stage_step(s,0,e,sizeof e));assert(strstr(e,"audio"));c3_wzx_reflect_cancel(&s->reflect);c3_particle_cancel(&s->particles);s->sound_done=done;
 events(s,1,1);assert(c3_battle_stage_step(s,0,e,sizeof e));busy=1;assert(!c3_battle_stage_close(s));assert(!s->reflect.active&&s->reflect_program.parts_data);busy=0;assert(c3_battle_stage_close(s));assert(!s->reflect_program.parts_data);free(s);puts("Reflect production stage fixture passed");return 0;}
