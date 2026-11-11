#include "colosseum3ds/battle_preview_menu.h"
#include <stdio.h>
#include <string.h>

static unsigned first_target(const C3BattlePreview *b){return b->mons[2].hp?2:3;}
static void choose(C3BattlePreviewMenu *m,const C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned actor)
{
    while(actor<2&&!b->mons[actor].hp)++actor;
    if(actor==2){m->page=C3_PREVIEW_MENU_CONFIRM;return;}
    m->actor=actor;m->page=C3_PREVIEW_MENU_MOVE;m->selection=4;m->target=first_target(b);
    uint8_t mask=c3_battle_preview_legal_moves(b,t,actor);
    for(unsigned i=0;i<4;++i)if(mask&(1u<<i)){m->selection=i;break;}
}
void c3_battle_preview_menu_init(C3BattlePreviewMenu *m)
{if(m){memset(m,0,sizeof *m);m->page=C3_PREVIEW_MENU_EVENTS;m->target=2;}}
static int needs_target(uint16_t move)
{return move!=115&&move!=270&&move!=289&&move!=39&&move!=45;}
int c3_battle_preview_menu_step(C3BattlePreviewMenu *m,C3BattlePreview *b,
    const C3BattlePreviewTables *t,uint16_t pressed,char *error)
{
    if(!m||!b||!t||m->page>C3_PREVIEW_MENU_EXIT||m->actor>1||m->selection>4||m->target<2||m->target>3){
        if(error)snprintf(error,C3_BATTLE_ERROR_SIZE,"Invalid preview menu state.");
        return 0;
    }
    if(error)error[0]=0;
    if(m->page==C3_PREVIEW_MENU_EXIT)return 1;
    if((pressed&C3_PREVIEW_X)&&m->page!=C3_PREVIEW_MENU_LEAVE){m->previous=m->page;m->page=C3_PREVIEW_MENU_LEAVE;return 1;}
    switch(m->page){
    case C3_PREVIEW_MENU_EVENTS:
        if(pressed&(C3_PREVIEW_A|C3_PREVIEW_B)){
            if(m->event+1<b->event_count)++m->event;
            else if(b->outcome!=C3_PREVIEW_RUNNING)m->page=C3_PREVIEW_MENU_RESULT;
            else choose(m,b,t,0);
        }break;
    case C3_PREVIEW_MENU_MOVE:{
        uint8_t mask=c3_battle_preview_legal_moves(b,t,m->actor);
        if(pressed&C3_PREVIEW_B){
            if(m->actor&&b->mons[0].hp)choose(m,b,t,0);
            else{m->previous=m->page;m->page=C3_PREVIEW_MENU_LEAVE;}break;
        }
        if(mask&&(pressed&(C3_PREVIEW_UP|C3_PREVIEW_DOWN|C3_PREVIEW_LEFT|C3_PREVIEW_RIGHT))){
            unsigned delta=pressed&(C3_PREVIEW_UP|C3_PREVIEW_LEFT)?3:1;
            for(unsigned k=0;k<4;++k){m->selection=(m->selection+delta)%4;if(mask&(1u<<m->selection))break;}
        }
        if(pressed&C3_PREVIEW_A){
            uint16_t move=m->selection==4?165:b->mons[m->actor].moves[m->selection].id;
            if(needs_target(move)){m->page=C3_PREVIEW_MENU_TARGET;m->target=first_target(b);}
            else{m->commands[m->actor]=(C3BattlePreviewCommand){(uint8_t)m->selection,(uint8_t)first_target(b)};choose(m,b,t,m->actor+1);}
        }break;
    }
    case C3_PREVIEW_MENU_TARGET:
        if(pressed&C3_PREVIEW_B){m->page=C3_PREVIEW_MENU_MOVE;break;}
        if(pressed&(C3_PREVIEW_UP|C3_PREVIEW_DOWN|C3_PREVIEW_LEFT|C3_PREVIEW_RIGHT)){
            unsigned other=m->target^1u;if(b->mons[other].hp)m->target=other;}
        if(pressed&C3_PREVIEW_A){
            m->commands[m->actor]=(C3BattlePreviewCommand){(uint8_t)m->selection,(uint8_t)m->target};choose(m,b,t,m->actor+1);}
        break;
    case C3_PREVIEW_MENU_CONFIRM:
        if(pressed&C3_PREVIEW_B){choose(m,b,t,b->mons[1].hp?1:0);break;}
        if(pressed&C3_PREVIEW_A){
            if(!c3_battle_preview_turn(b,t,m->commands,error))return 0;
            m->page=C3_PREVIEW_MENU_EVENTS;m->event=0;}
        break;
    case C3_PREVIEW_MENU_RESULT:
        if(pressed&(C3_PREVIEW_A|C3_PREVIEW_B)){m->page=C3_PREVIEW_MENU_EXIT;}
        break;
    case C3_PREVIEW_MENU_LEAVE:
        if(pressed&C3_PREVIEW_B)m->page=m->previous;
        else if(pressed&C3_PREVIEW_A)m->page=C3_PREVIEW_MENU_EXIT;
        break;
    default:break;
    }
    return 1;
}

uint16_t c3_battle_preview_menu_touch(C3BattlePreviewMenu *m,const C3BattlePreview *b,
    const C3BattlePreviewTables *t,unsigned x,unsigned y)
{
    if(!m||!b||!t||x>=320||y>=240||m->actor>1)return 0;
    switch(m->page){
    case C3_PREVIEW_MENU_EVENTS:case C3_PREVIEW_MENU_RESULT:return y<220?C3_PREVIEW_A:0;
    case C3_PREVIEW_MENU_MOVE:{
        uint8_t mask=c3_battle_preview_legal_moves(b,t,m->actor);
        if(x<15||x>=305)return 0;
        if(!mask)return y>=120&&y<143?C3_PREVIEW_A:0; /* the single Struggle row */
        if(y<116||y>=116+4*23)return 0;
        unsigned row=(y-116)/23;
        if(!(mask&(1u<<row)))return 0;
        m->selection=row;return C3_PREVIEW_A;}
    case C3_PREVIEW_MENU_TARGET:{
        if(x<15||x>=305||y<125||y>=125+2*35)return 0;
        unsigned target=2+(y-125)/35;
        if(!b->mons[target].hp)return 0;
        m->target=target;return C3_PREVIEW_A;}
    case C3_PREVIEW_MENU_CONFIRM:return x>=7&&x<313&&y>=111&&y<213?C3_PREVIEW_A:0;
    default:return 0;
    }
}
