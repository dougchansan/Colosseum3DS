#include "game_ui_views.h"
#include <stdio.h>

static const char *pokemon_name(uint16_t species)
{
    switch(species) {case 197:return "UMBREON";case 196:return "ESPEON";default:return "POKEMON";}
}

static void experience(C3BottomUi *ui,const C3GamePokemon *pokemon,int x,int y,int width,int label)
{
    uint32_t progress,range;
    if(!c3_game_pokemon_experience(pokemon,&progress,&range))return;
    if(label){
        c3_ui_text(ui,x,y-6,32,1,"EXP",C3_UI_GOLD);x+=35;width-=35;
        if(pokemon->level==100){c3_ui_text_right(ui,x+width,y-6,32,"MAX",C3_UI_MUTED);width-=35;}
    }
    c3_ui_experience(ui,x,y,width,progress,range);
}

uint32_t c3_field_ui_identity(PausePage page,const C3FieldStoryView *story)
{
    uint32_t id=(uint32_t)page*0x100000u+(story?story->message_id:0)+1u;
    if(page==PAUSE_CLOSED&&story){
        if(story->blocked[0])id^=0x40000000u;
        else if(!story->dialogue)id=0;
    }
    return id;
}

void c3_field_ui_draw_to(C3BottomUi *ui,const C3GameSummary *summary,FieldPhase phase,PausePage page,
                         unsigned selection,int yes,const char *message,const C3FieldStoryView *story,
                         C3UiPager *pager)
{
    char text[160],value[48];
    c3_ui_pager_sync(pager,c3_field_ui_identity(page,story));
    snprintf(text,sizeof text,"%s   $%lu   %lu:%02lu",summary->name,(unsigned long)summary->money,
        (unsigned long)(summary->play_seconds/3600u),(unsigned long)(summary->play_seconds/60u%60u));
    c3_ui_header(ui,summary->floor==2?"OUTSKIRT STAND - INSIDE":"OUTSKIRT STAND",text);
    if(phase==FIELD_SETUP||phase==FIELD_ARRIVAL||phase==FIELD_ARRIVAL_OUT){
        c3_ui_panel(ui,7,64,306,90);
        c3_ui_text(ui,20,80,280,3,phase==FIELD_SETUP?"Loading the original area...":"Arriving at Outskirt Stand...",C3_UI_WHITE);
    }else if(page==PAUSE_CLOSED&&story&&story->blocked[0]){
        c3_ui_dialogue(ui,pager,story->blocked,0,0);
        if(!pager->more)c3_ui_footer(ui,story->battle_id==10?"A: combat preview   X / START: menu":"X / START: main menu");
    }else if(page==PAUSE_CLOSED&&story&&story->dialogue){
        c3_ui_dialogue(ui,pager,story->message,story->choice,!story->choice_no);
    }else if(page==PAUSE_CLOSED){
        for(unsigned i=0;i<2;++i){
            snprintf(text,sizeof text,"%s  Lv.%u",pokemon_name(summary->party[i].species),summary->party[i].level);
            c3_ui_hp(ui,9,65+(int)i*58,302,text,summary->party[i].hp,summary->party[i].max_hp);
            experience(ui,&summary->party[i],19,113+(int)i*58,282,1);
        }
        c3_ui_text(ui,18,184,284,1,story&&!story->control?"An event is running...":"Circle Pad / D-pad: move",C3_UI_MUTED);
        c3_ui_footer(ui,"A: talk / inspect   X / START: menu");
    }else if(page==PAUSE_MAIN){
        static const char *const labels[]={"RESUME","POKEMON","SAVE","MAIN MENU"};
        c3_ui_panel(ui,9,58,302,151);
        for(unsigned i=0;i<4;++i)c3_ui_row(ui,68+(int)i*32,labels[i],i==selection,0);
        c3_ui_footer(ui,"A: choose   B: resume");
    }else if(page==PAUSE_PARTY){
        c3_ui_panel(ui,9,56,302,157);
        for(unsigned i=0,row=0;i<6;++i)if(summary->party[i].species){
            snprintf(text,sizeof text,"%s Lv.%u",pokemon_name(summary->party[i].species),summary->party[i].level);
            snprintf(value,sizeof value,"%u/%u HP",summary->party[i].hp,summary->party[i].max_hp);
            int y=62+(int)row++*24;
            c3_ui_row_value(ui,y,text,value,0,0);
            experience(ui,&summary->party[i],26,y+18,273,1);
        }
        c3_ui_footer(ui,"B: back");
    }else if(page==PAUSE_SAVE||page==PAUSE_LEAVE){
        c3_ui_dialogue(ui,pager,page==PAUSE_SAVE?
            "Save your game here? Continue resumes at this area's entrance. Your party and progress will be kept.":
            "Return to the main menu? Unsaved progress will be lost.",1,yes);
        if(!pager->more)c3_ui_footer(ui,"A: confirm   B: cancel");
    }else{
        c3_ui_dialogue(ui,pager,message,0,0);
        if(!pager->more)c3_ui_footer(ui,"A / B: back");
    }
}

void c3_battle_ui_draw_to(C3BottomUi *ui,const C3BattlePreview *b,const C3BattlePreviewTables *t,
    const C3BattlePreviewMenu *m,const C3BattlePresentation *presentation,const char *error,C3UiPager *pager)
{
    char text[192];
    unsigned hp[4];for(unsigned i=0;i<4;++i)hp[i]=presentation?presentation->hp[i]:b->mons[i].hp;
    if(presentation){
        const C3BattlePresentation *p=presentation;
        if(p->cursor<p->count&&p->events[p->cursor].kind==C3_PREVIEW_EVENT_HP&&p->duration_ms){
            const C3BattlePreviewEvent *e=&p->events[p->cursor];
            for(unsigned i=0;i<4;++i){int32_t before=e->hp_before[i],after=e->hp[i];
                hp[i]=(unsigned)(before+(after-before)*(int32_t)p->elapsed_ms/(int32_t)p->duration_ms);}
        }
    }
    for(unsigned i=0;i<4;++i){
        snprintf(text,sizeof text,"%s",c3_battle_preview_name(i));
        c3_ui_hp(ui,5+(int)(i%2)*159,4+(int)(i/2)*46,151,text,hp[i],b->mons[i].maximum_hp);
        if(i<2){
            const unsigned char *p=b->original_request.player_party[i];
            C3GamePokemon pokemon={.species=b->mons[i].species,.level=b->mons[i].level,
                .experience=(uint32_t)p[0x5c]<<24|(uint32_t)p[0x5d]<<16|(uint32_t)p[0x5e]<<8|p[0x5f],
                .shadow_id=(uint16_t)((uint16_t)p[0xd8]<<8|p[0xd9])};
            experience(ui,&pokemon,14+(int)i*159,47,133,0);
        }
    }
    c3_ui_pager_sync(pager,(uint32_t)m->page*0x10000u+(uint32_t)m->event+1u);
    if(error[0]){
        c3_ui_message(ui,pager,error,105,5);c3_ui_footer(ui,"B / X: return - saved game unchanged");
        return;
    }
    switch(m->page){
    case C3_PREVIEW_MENU_EVENTS:
        if(m->event<b->event_count)c3_ui_message(ui,pager,b->events[m->event].text,105,5);
        c3_ui_footer(ui,pager->more?"A / B: more   X: leave":"A / B: continue   X: leave preview");break;
    case C3_PREVIEW_MENU_MOVE:{
        const C3BattlePreviewMon *p=&b->mons[m->actor];uint8_t mask=c3_battle_preview_legal_moves(b,t,m->actor);
        snprintf(text,sizeof text,"%s - choose a move",c3_battle_preview_name(m->actor));
        c3_ui_text(ui,15,94,290,1,text,C3_UI_GOLD);c3_ui_panel(ui,7,111,306,102);
        if(!mask)c3_ui_row(ui,120,"STRUGGLE (no usable moves)",1,0);
        else for(unsigned i=0;i<4;++i){
            snprintf(text,sizeof text,"%u/%u PP",p->moves[i].pp,p->moves[i].maximum_pp);
            c3_ui_row_value(ui,116+(int)i*23,c3_battle_preview_move_name(p->moves[i].id),text,m->selection==i,!(mask&(1u<<i)));
        }
        c3_ui_footer(ui,"A: confirm   B: back   X: leave");break;
    }
    case C3_PREVIEW_MENU_TARGET:
        c3_ui_text(ui,15,94,290,1,"Choose a target",C3_UI_GOLD);
        c3_ui_panel(ui,7,111,306,102);
        for(unsigned i=2;i<4;++i){
            snprintf(text,sizeof text,b->mons[i].hp?"%u HP":"FAINTED",b->mons[i].hp);
            c3_ui_row_value(ui,125+(int)(i-2)*35,c3_battle_preview_name(i),text,m->target==i,!b->mons[i].hp);
        }
        c3_ui_footer(ui,"A: confirm   B: change move   X: leave");break;
    case C3_PREVIEW_MENU_CONFIRM:
        c3_ui_text(ui,15,94,290,1,"Execute these commands?",C3_UI_GOLD);
        c3_ui_panel(ui,7,111,306,102);
        for(unsigned i=0;i<2;++i)if(b->mons[i].hp){
            const C3BattlePreviewCommand *c=&m->commands[i];uint16_t id=c->slot==4?165:b->mons[i].moves[c->slot].id;
            snprintf(text,sizeof text,"%s: %s",c3_battle_preview_name(i),c3_battle_preview_move_name(id));
            c3_ui_text(ui,19,116+(int)i*43,280,1,text,C3_UI_WHITE);
            if(id!=115&&id!=270&&id!=289){
                snprintf(text,sizeof text,"Target: %s",c3_battle_preview_name(c->target));
                c3_ui_text(ui,28,133+(int)i*43,270,1,text,C3_UI_MUTED);
            }
        }
        c3_ui_footer(ui,"A: execute turn   B: revise   X: leave");break;
    case C3_PREVIEW_MENU_RESULT:
        snprintf(text,sizeof text,"PREVIEW %s\nYour commands completed this preview. The Story battle is pending. No HP, PP, money, experience or story flags were saved.",
            b->outcome==C3_PREVIEW_PLAYER_WON?"WIN":b->outcome==C3_PREVIEW_PLAYER_LOST?"LOSS":"DRAW");
        c3_ui_message(ui,pager,text,105,5);c3_ui_footer(ui,pager->more?"A / B: more":"A / B: return");break;
    case C3_PREVIEW_MENU_LEAVE:
        c3_ui_message(ui,pager,"Leave the preview? All preview changes will be discarded.",105,5);
        c3_ui_footer(ui,"A: leave   B: continue playing");break;
    default:break;
    }
}
