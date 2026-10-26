#include "colosseum3ds/battle_presentation.h"
#include <string.h>
static uint32_t duration(C3BattlePreviewEventKind kind)
{
    switch(kind){
    case C3_PREVIEW_EVENT_MOVE:return 700;
    case C3_PREVIEW_EVENT_HP:return 450;
    case C3_PREVIEW_EVENT_FAINT:return 600;
    case C3_PREVIEW_EVENT_INTRO:case C3_PREVIEW_EVENT_SEND_OUT:return 900;
    default:return 250;
    }
}
void c3_battle_presentation_init(C3BattlePresentation *p,const uint16_t hp[4])
{
    if(!p||!hp)return;
    memset(p,0,sizeof *p);memcpy(p->hp,hp,sizeof p->hp);
    for(unsigned i=0;i<4;++i)p->visible[i]=(uint8_t)(hp[i]!=0);
    p->outcome=C3_PREVIEW_RUNNING;
    p->wait_limit_ms=5000;
}
int c3_battle_presentation_submit(C3BattlePresentation *p,const C3BattlePreviewEvent *events,uint32_t count)
{
    if(!p||!events||!count||count>C3_PREVIEW_EVENT_LIMIT||p->failed||p->cursor<p->count)return 0;
    uint32_t sequence=p->last_sequence;
    for(uint32_t i=0;i<count;++i){
        const C3BattlePreviewEvent *e=&events[i];
        if(e->kind<C3_PREVIEW_EVENT_MESSAGE||e->kind>C3_PREVIEW_EVENT_OUTCOME||
           e->sequence<=sequence||(e->actor>=4&&e->actor!=C3_PREVIEW_NO_SLOT)||
           (e->target>=4&&e->target!=C3_PREVIEW_NO_SLOT))return 0;
        sequence=e->sequence;
    }
    memcpy(p->events,events,count*sizeof *events);p->count=count;p->cursor=0;
    p->elapsed_ms=0;p->started=0;p->waiting=0;p->wait_ms=0;p->applied=0;return 1;
}
void c3_battle_presentation_pause(C3BattlePresentation *p,int paused)
{if(p)p->paused=(uint8_t)(paused!=0);}
void c3_battle_presentation_wait(C3BattlePresentation *p,int waiting)
{if(p){p->waiting=(uint8_t)(waiting!=0);if(!waiting)p->wait_ms=0;}}
int c3_battle_presentation_wait_limit(C3BattlePresentation *p,uint32_t ms)
{if(!p||!ms||ms>60000||p->wait_ms>=ms)return 0;p->wait_limit_ms=ms;return 1;}
int c3_battle_presentation_step(C3BattlePresentation *p,uint32_t ms,C3BattlePresentationNotify notify,void *user)
{
    if(!p||p->failed)return 0;
    if(p->paused)return 1;
    while(p->cursor<p->count){
        const C3BattlePreviewEvent *e=&p->events[p->cursor];
        if(!p->started){p->started=1;p->duration_ms=duration(e->kind);if(notify)notify(e,0,user);}
        if(p->waiting){
            if(p->wait_ms>=p->wait_limit_ms||ms>=p->wait_limit_ms-p->wait_ms){p->failed=1;return 0;}
            p->wait_ms+=ms;return 1;
        }
        uint32_t remaining=p->applied?0:p->duration_ms-p->elapsed_ms;
        if(ms<remaining){p->elapsed_ms+=ms;return 1;}
        ms-=remaining;
        if(!p->applied){
        /* HP changes only on HP events; a zero HP actor stays visible until faint. */
        if(e->kind==C3_PREVIEW_EVENT_MOVE)memcpy(p->pp,e->pp,sizeof p->pp);
        if(e->kind==C3_PREVIEW_EVENT_HP)memcpy(p->hp,e->hp,sizeof p->hp);
        if(e->kind==C3_PREVIEW_EVENT_FAINT&&e->target<4)p->visible[e->target]=0;
        if(e->kind==C3_PREVIEW_EVENT_STATUS){
            memcpy(p->stages,e->stages,sizeof p->stages);
            memcpy(p->confusion,e->confusion,sizeof p->confusion);
            memcpy(p->taunt,e->taunt,sizeof p->taunt);memcpy(p->reflect,e->reflect,sizeof p->reflect);
        }
        if(e->kind==C3_PREVIEW_EVENT_OUTCOME)p->outcome=e->outcome;
        p->elapsed_ms=p->duration_ms;p->applied=1;
        if(notify)notify(e,1,user);
        if(p->waiting)continue;
        }
        p->last_sequence=e->sequence;++p->cursor;p->elapsed_ms=0;p->started=0;p->applied=0;
        if(!ms)break;
    }
    return 1;
}
