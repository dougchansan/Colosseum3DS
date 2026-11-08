#include "colosseum3ds/battle_presentation.h"
#include <assert.h>
#include <string.h>
static unsigned starts,ends;
static void observe(const C3BattlePreviewEvent *e,int finished,void *user)
{(void)e;(void)user;if(finished)++ends;else ++starts;}
static void hold_hp(const C3BattlePreviewEvent *e,int finished,void *user)
{observe(e,finished,0);if(finished&&e->kind==C3_PREVIEW_EVENT_HP)c3_battle_presentation_wait(user,1);}
int main(void)
{
    C3BattlePresentation a,b;uint16_t hp[4]={100,100,100,100};
    C3BattlePreviewEvent events[3];memset(events,0,sizeof events);
    for(unsigned i=0;i<3;++i){events[i].sequence=i+1;events[i].target=2;memcpy(events[i].hp,hp,sizeof hp);}
    events[0].kind=C3_PREVIEW_EVENT_MOVE;events[0].move=44;
    events[1].kind=C3_PREVIEW_EVENT_HP;events[1].hp[2]=0;
    events[2].kind=C3_PREVIEW_EVENT_FAINT;events[2].hp[2]=0;
    c3_battle_presentation_init(&a,hp);c3_battle_presentation_init(&b,hp);
    assert(c3_battle_presentation_submit(&a,events,3));assert(c3_battle_presentation_submit(&b,events,3));
    assert(!c3_battle_presentation_submit(&a,events,3));
    c3_battle_presentation_pause(&a,1);assert(c3_battle_presentation_step(&a,9000,observe,0));assert(a.cursor==0);
    c3_battle_presentation_pause(&a,0);assert(c3_battle_presentation_step(&a,700,observe,0));assert(a.hp[2]==100);
    assert(c3_battle_presentation_step(&a,450,observe,0));assert(a.hp[2]==0&&a.visible[2]);
    assert(c3_battle_presentation_step(&a,600,observe,0));assert(!a.visible[2]);assert(starts==3&&ends==3);
    for(unsigned i=0;i<1750;++i)assert(c3_battle_presentation_step(&b,1,0,0));
    assert(!memcmp(a.hp,b.hp,sizeof a.hp)&&a.cursor==b.cursor);
    assert(!c3_battle_presentation_submit(&a,events,3));
    c3_battle_presentation_init(&a,hp);assert(c3_battle_presentation_submit(&a,events,3));
    c3_battle_presentation_wait(&a,1);assert(c3_battle_presentation_step(&a,4999,0,0));
    assert(!c3_battle_presentation_step(&a,1,0,0)&&a.failed);
    c3_battle_presentation_init(&a,hp);starts=ends=0;
    assert(c3_battle_presentation_submit(&a,events,3));
    assert(c3_battle_presentation_wait_limit(&a,12000));
    assert(!c3_battle_presentation_wait_limit(&a,60001));
    assert(c3_battle_presentation_step(&a,1150,hold_hp,&a));
    assert(a.cursor==1&&a.hp[2]==0&&a.visible[2]&&starts==2&&ends==2);
    assert(c3_battle_presentation_step(&a,6000,hold_hp,&a));
    assert(a.cursor==1&&ends==2);
    c3_battle_presentation_pause(&a,1);
    assert(c3_battle_presentation_step(&a,60000,hold_hp,&a)&&a.wait_ms==6000);
    c3_battle_presentation_pause(&a,0);c3_battle_presentation_wait(&a,0);
    assert(c3_battle_presentation_step(&a,600,hold_hp,&a));
    assert(a.cursor==3&&!a.visible[2]&&starts==3&&ends==3);
    return 0;
}
