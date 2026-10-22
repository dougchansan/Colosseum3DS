#include "battle_preview_scene.h"
#include "n3ds_platform.h"
#include "colosseum3ds/battle_preview_menu.h"
#include "colosseum3ds/game_state.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_ROOT "sdmc:/3ds/Colosseum3DS/boot/"
#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif

static void health(unsigned current,unsigned maximum)
{
    unsigned filled=maximum?current*20u/maximum:0;
    printf("[");for(unsigned i=0;i<20;++i)putchar(i<filled?'#':'-');printf("] %u/%u\n",current,maximum);
}
static void draw_top(PrintConsole *top,const C3BattlePreview *b,const C3BattlePreviewMenu *m)
{
    consoleSelect(top);consoleClear();
    printf("COLOSSEUM3DS | WILLIE COMBAT PREVIEW\n");
    printf("Turn %lu  Build %.12s\n\n",(unsigned long)b->turn,C3_BUILD_ID);
    const C3BattlePreviewEvent *event=m->page==C3_PREVIEW_MENU_EVENTS&&m->event<b->event_count?&b->events[m->event]:NULL;
    static const unsigned order[]={2,3,0,1};
    for(unsigned k=0;k<4;++k){unsigned i=order[k];const C3BattlePreviewMon *p=b->mons+i;
        unsigned hp=event?event->hp[i]:p->hp;
        printf("%s Lv.%u%s\n",c3_battle_preview_name(i),p->level,hp?"":"  FAINTED");health(hp,p->maximum_hp);
        if(!event)printf("Atk %+d Def %+d Acc %+d%s%s\n",p->stages[0],p->stages[1],p->stages[5],p->taunt?" TAUNT":"",p->confusion?" CONFUSED":"");
        else printf("\n");
        printf("\n");
    }
    printf("NONPERSISTENT DEVELOPMENT UI\n");
    printf("Preview enemy policy, not original AI.\n");
    printf("No experience, rewards, or story writeback.\n");
    printf("START + SELECT: exit application\n");
}
static void draw_bottom(PrintConsole *bottom,const C3BattlePreview *b,const C3BattlePreviewTables *t,
    const C3BattlePreviewMenu *m,const char *error)
{
    consoleSelect(bottom);consoleClear();
    if(error[0]){printf("PREVIEW STOPPED\n\n%s\n\nBattle and saved game unchanged.\n\nB / X: return\n",error);return;}
    printf("WILLIE | 2 vs 2\n\n");
    switch(m->page){
    case C3_PREVIEW_MENU_EVENTS:
        if(m->event<b->event_count)printf("%s\n\n",b->events[m->event].text);
        printf("A / B: next message\nX: leave preview\n");break;
    case C3_PREVIEW_MENU_MOVE:{
        const C3BattlePreviewMon *p=&b->mons[m->actor];uint8_t mask=c3_battle_preview_legal_moves(b,t,m->actor);
        printf("Choose %s's move\n\n",c3_battle_preview_name(m->actor));
        if(!mask)printf("> STRUGGLE (no usable moves)\n");
        else for(unsigned i=0;i<4;++i)printf("%s %-13s %2u/%2u PP%s\n",m->selection==i?">":" ",
            c3_battle_preview_move_name(p->moves[i].id),p->moves[i].pp,p->moves[i].maximum_pp,mask&(1u<<i)?"":" -");
        printf("\nD-pad: select   A: confirm\nB: back         X: leave\n");break;
    }
    case C3_PREVIEW_MENU_TARGET:
        printf("Choose a target for\n%s\n\n",m->selection==4?"STRUGGLE":c3_battle_preview_move_name(b->mons[m->actor].moves[m->selection].id));
        for(unsigned a=2;a<4;++a)printf("%s %-12s %u HP%s\n",m->target==a?">":" ",c3_battle_preview_name(a),b->mons[a].hp,b->mons[a].hp?"":" FAINTED");
        printf("\nD-pad: target   A: confirm\nB: change move  X: leave\n");break;
    case C3_PREVIEW_MENU_CONFIRM:
        printf("Execute these commands?\n\n");
        for(unsigned a=0;a<2;++a)if(b->mons[a].hp){const C3BattlePreviewCommand *c=&m->commands[a];
            uint16_t id=c->slot==4?165:b->mons[a].moves[c->slot].id;
            printf("%s: %s\n",c3_battle_preview_name(a),c3_battle_preview_move_name(id));
            if(id!=115&&id!=270&&id!=289)printf("  -> %s\n",c3_battle_preview_name(c->target));}
        printf("\nA: execute turn\nB: revise command\nX: leave preview\n");break;
    case C3_PREVIEW_MENU_RESULT:
        printf("PREVIEW %s\n\n",b->outcome==C3_PREVIEW_PLAYER_WON?"WIN":b->outcome==C3_PREVIEW_PLAYER_LOST?"LOSS":"DRAW");
        printf("Your commands completed this preview.\nThe original story battle is pending.\n\nNo HP, PP, money, experience or story\nflags were written to the game state.\n\nA / B: return\n");break;
    case C3_PREVIEW_MENU_LEAVE:
        printf("Leave the nonpersistent preview?\n\nAll preview changes will be discarded.\n\nA: leave\nB: continue playing\n");break;
    default:break;
    }
}
static void trace_turn(FILE *f,const C3BattlePreview *b)
{
    if(!f)return;
    fprintf(f,"preview_turn turn=%lu seed=%08lx draws=%llu outcome=%u save_writes=0 gsvm_resumed=0\n",
        (unsigned long)b->turn,(unsigned long)b->rng.seed,(unsigned long long)b->rng.draws,(unsigned)b->outcome);
    for(unsigned i=0;i<b->event_count;++i){const C3BattlePreviewEvent *e=&b->events[i];
        fprintf(f,"preview_event index=%u hp=%u,%u,%u,%u draws=%llu text=%s\n",i,e->hp[0],e->hp[1],e->hp[2],e->hp[3],(unsigned long long)e->draws,e->text);}
    fflush(f);
}
int c3_battle_preview_scene_run(const C3BattleRequest *request,FILE *trace)
{
    C3BattlePreview *battle=calloc(1,sizeof *battle);C3BattleInitTables init;C3BattlePreviewTables tables;
    C3BattlePreviewMenu menu;C3N3dsInput input={0};char error[192]={0};int result=0,dirty=1;
    /* Capture the old console pointer before initializing the independent top
     * console. No output occurs while its zeroed context is uninitialized. */
    PrintConsole top={0};PrintConsole *bottom=consoleSelect(&top);
    consoleInit(GFX_TOP,&top);consoleSelect(bottom);
    c3_battle_preview_menu_init(&menu);
    if(!battle)snprintf(error,sizeof error,"Preview allocation failed.");
    else if(c3_battle_init_load(&init,BOOT_ROOT "willie.c3i",error)&&
            c3_battle_preview_load(&tables,BOOT_ROOT "willie.c3p",error)&&
            c3_battle_preview_begin(battle,request,&init,&tables,0x12345678u,error)){
        if(trace){fprintf(trace,"preview_begin build=%s seed=12345678 enemy_policy=uniform_legal experimental=1\n",C3_BUILD_ID);fflush(trace);}
        trace_turn(trace,battle);
    }
    unsigned last_turn=0;C3BattlePreviewMenuPage previous=menu.page;unsigned previous_event=0;
    while(c3_n3ds_poll(&input)){
        if(error[0]){if(input.pressed&(C3_PAD_B|C3_PAD_X)){break;}}
        else if(input.pressed){
            if(!c3_battle_preview_menu_step(&menu,battle,&tables,input.pressed,error))dirty=1;
            if(menu.page==C3_PREVIEW_MENU_EXIT)break;
            if(battle->turn!=last_turn){last_turn=battle->turn;trace_turn(trace,battle);}
            if(trace&&(menu.page!=previous||menu.event!=previous_event)){
                fprintf(trace,"preview_ui page=%u actor=%u slot=%u target=%u event=%u input=%04x\n",(unsigned)menu.page,menu.actor,menu.selection,menu.target,menu.event,input.pressed);fflush(trace);}
            previous=menu.page;previous_event=menu.event;dirty=1;
        }
        if(dirty){
            if(!error[0])draw_top(&top,battle,&menu);
            else{consoleSelect(&top);consoleClear();printf("COLOSSEUM3DS\n\nCombat preview unavailable.\nNo saved state was changed.\n");}
            draw_bottom(bottom,battle,&tables,&menu,error);gfxFlushBuffers();dirty=0;
        }
        gspWaitForVBlank();
    }
    if(input.exit_requested)result=1;
    /* aptMainLoop cancellation may not fill input.exit_requested. Check it
     * without issuing additional controller actions before returning. */
    if(!aptMainLoop())result=1;
    consoleSelect(bottom);
    gfxSetScreenFormat(GFX_TOP,GSP_BGR8_OES);gfxSetDoubleBuffering(GFX_TOP,true);gfxSwapBuffersGpu();
    if(trace){fprintf(trace,"preview_exit app_exit=%d error=%s save_writes=0 gsvm_resumed=0\n",result,error);fflush(trace);}
    if(error[0]&&!result)result=-1;
    free(battle);return result;
}
int c3_battle_preview_from_game_state(const C3GameState *state,FILE *trace)
{
    C3BattleData data;C3BattleRequest request;char error[192];
    size_t bytes=0;const uint8_t *body=c3_game_state_body(state,&bytes);
    const uint32_t args[]={10,1,0};
    if(c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)&&
       c3_battle_request(&request,&data,args,body,bytes,error))
        return c3_battle_preview_scene_run(&request,trace);
    consoleClear();printf("PREVIEW UNAVAILABLE\n\n%s\n\nB: return\n",error);
    if(trace){fprintf(trace,"preview_admission_failed error=%s\n",error);fflush(trace);}
    C3N3dsInput input={0};
    while(c3_n3ds_poll(&input)){if(input.pressed&C3_PAD_B)return -1;gspWaitForVBlank();}
    return 1;
}

int c3_battle_preview_probe_run(void)
{
    C3GameState state={0};C3BattleData data;C3BattleRequest request;char error[192]={0};int result=1;
    FILE *trace=fopen("sdmc:/3ds/Colosseum3DS/preview-probe.log","w");
    if(c3_game_state_new(&state,"WES",0x12345678u,BOOT_ROOT "newgame.c3g",error)&&
       c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)){
        size_t bytes;const uint8_t *body=c3_game_state_body(&state,&bytes);const uint32_t args[]={10,1,0};
        if(c3_battle_request(&request,&data,args,body,bytes,error)){
            if(trace){fprintf(trace,"preview_probe source=synthetic_entry real_tables=1 fresh_game=1 persisted_save_loaded=0 build=%s\n",C3_BUILD_ID);fflush(trace);}
            result=c3_battle_preview_scene_run(&request,trace)<0?1:0;
        }
    }
    if(error[0]){consoleClear();printf("BATTLE PREVIEW PROBE\n\n%s\n\nSTART + SELECT: exit\n",error);c3_n3ds_wait_for_exit();}
    if(trace){fprintf(trace,"preview_probe_end result=%d error=%s save_writes=0\n",result,error);fclose(trace);}
    c3_game_state_free(&state);return result;
}
