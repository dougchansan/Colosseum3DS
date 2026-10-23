/* Optional private-asset test. Executes production loaders/sampling/timeline;
 * only GPU submission is replaced by an explicitly bounded ownership fixture.
 * No original assets are embedded. Usage: test_wzx_bite_stage PRIVATE_SCENE_DIR. */
#include "battle_stage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counts,static_count;static unsigned char enabled[16];static int busy;
int c3_stage_renderer_init(const C3Title *const *m,size_t n,char *e,size_t c){(void)e;(void)c;counts=n;unsigned v=0;for(unsigned i=0;i<n;i++)v+=m[i]->vertex_count;assert(n<=16&&v<=131072);return 0;}
int c3_stage_renderer_set_static(size_t i,bool value){assert(i<counts);static_count+=value;return 1;}
int c3_stage_renderer_set_modulation(size_t i,const uint8_t rgb[3]){assert(i<counts);enabled[i]=(rgb!=NULL);return 1;}
bool c3_stage_renderer_try_shutdown(void){return !busy;}
int c3_stage_renderer_draw(const C3Title *const *m,size_t n,const C3TitleCamera *c,float f,C3TitleRenderStats *st,char *e,size_t z){(void)m;(void)n;(void)c;(void)f;(void)st;(void)e;(void)z;return 0;}
int main(int argc,char **argv){assert(argc==2);C3BattleStage *s=calloc(1,sizeof *s);assert(s);char error[192];uint16_t hp[4]={80,75,50,50};
if(!c3_battle_stage_load(s,argv[1],hp,stdout,error,sizeof error)){puts(error);return 1;}
assert(s->bite_available&&s->count==10&&static_count==1);
assert(c3_battle_stage_step(s,0,error,sizeof error));
size_t bytes=s->models[1].vertex_count*sizeof(C3TitleDrawVertex);void *copy=malloc(bytes);assert(copy);memcpy(copy,s->models[1].draw_vertices,bytes);
for(unsigned repeat=0;repeat<2;++repeat){
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
busy=1;assert(!c3_battle_stage_close(s));assert(s->models[1].file_data);busy=0;assert(c3_battle_stage_close(s));assert(s->count==0);free(copy);free(s);return 0;}
