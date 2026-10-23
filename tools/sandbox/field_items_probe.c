/* Explicit standalone diagnostic, not linked by the normal application.
 * A fresh PRIVATE test state is deliberately injured to exercise restoration.
 * Never loads/writes the player's normal save path or completed-save original. */
#include <3ds.h>
#include "bag_menu.h"
#include "colosseum3ds/game_state.h"
#include "game_state_internal.h"
#include "save_codec.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif
#define ROOT "sdmc:/3ds/Colosseum3DS/"
#define SAVE ROOT "test-field-items-01/items.gci"

static uint16_t map(uint32_t keys)
{
    uint16_t p=0;
    if(keys&KEY_A)p|=C3_PAD_A;
    if(keys&KEY_B)p|=C3_PAD_B;
    if(keys&KEY_X)p|=C3_PAD_X;
    if(keys&KEY_DUP)p|=C3_PAD_UP;
    if(keys&KEY_DDOWN)p|=C3_PAD_DOWN;
    if(keys&KEY_DLEFT)p|=C3_PAD_LEFT;
    if(keys&KEY_DRIGHT)p|=C3_PAD_RIGHT;
    if(keys&KEY_L)p|=C3_PAD_L;
    if(keys&KEY_R)p|=C3_PAD_R;
    return p;
}
static void digest(FILE *f,const C3GameState *state)
{
    size_t n=0;const uint8_t *p=c3_game_state_body(state,&n);uint8_t hash[20];
    if(!f||!p)return;
    c3_save_codec_sha1(hash,p,(uint32_t)n);
    fprintf(f,"body_sha1=");
    for(unsigned i=0;i<20;++i)fprintf(f,"%02x",hash[i]);
    fprintf(f,"\n");
}
int main(void)
{
    C3GameState state={0};C3ItemCatalog items={0};C3GameSummary summary={0};C3BagMenu menu;
    C3ItemUseResult used={0};PrintConsole top,bottom;char error[192]={0};
    unsigned writes=0,uses=0;int loaded=0,ready=0;
    gfxInitDefault();gfxSetDoubleBuffering(GFX_TOP,false);gfxSetDoubleBuffering(GFX_BOTTOM,false);
    consoleInit(GFX_TOP,&top);consoleInit(GFX_BOTTOM,&bottom);
    mkdir("sdmc:/3ds",0777);mkdir(ROOT,0777);
    if(mkdir(ROOT "test-field-items-01",0777)!=0&&errno!=EEXIST)snprintf(error,sizeof error,"Cannot create isolated test directory.");
    FILE *trace=fopen(ROOT "test-field-items-01/trace.log","w");
    struct stat existing;
    if(!error[0]&&c3_item_catalog_load(&items,ROOT "boot/items.c3it",error)){
        if(stat(SAVE,&existing)==0){
            loaded=c3_game_state_load(&state,SAVE,ROOT "boot/newgame.c3g",error);ready=loaded;
        }else if(errno!=ENOENT)snprintf(error,sizeof error,"Cannot inspect isolated test save.");
        else if(c3_game_state_new(&state,"WES",0x12345678u,ROOT "boot/newgame.c3g",error)){
            /* Intentionally synthetic damage, ONLY in this standalone fixture.
             * No battle or source gameplay damage is being claimed. */
            GameStateImpl *s=state.impl;s->body[0xa0+0x8a]=0;s->body[0xa0+0x8b]=5;
            ready=1;
        }
    }
    if(ready){c3_bag_menu_init(&menu,&state,&items);c3_game_state_summary(&state,&summary);}
    if(trace){
        fprintf(trace,"field_items_probe build=%s fixture_damage=1 loaded=%d ready=%d normal_save_access=0\n",C3_BUILD_ID,loaded,ready);
        if(ready)digest(trace,&state);
        fflush(trace);
    }
    for(unsigned frame=0;aptMainLoop();++frame){
        hidScanInput();uint32_t down=hidKeysDown(),held=hidKeysHeld();
        if((held&(KEY_START|KEY_SELECT))==(KEY_START|KEY_SELECT))break;
        if(ready){
            if(down&KEY_Y){
                if(c3_game_state_save(&state,SAVE,error)){++writes;if(trace)fprintf(trace,"explicit_test_save ok=1\n");}
            }else if(menu.page==C3_BAG_CLOSED){
                if(down&KEY_A)c3_bag_menu_init(&menu,&state,&items);
            }else{
                C3BagEvent e=c3_bag_menu_step(&menu,&state,&items,map(down),&used);
                if(e==C3_BAG_EVENT_USED){
                    ++uses;
                    if(trace)fprintf(trace,"item_used id=%u hp=%u->%u quantity=%u->%u writes=%u\n",used.item,used.hp_before,used.hp_after,used.quantity_before,used.quantity_after,writes);
                }
            }
            c3_game_state_summary(&state,&summary);
        }
        if(frame==0||down||frame%30==0){
            consoleSelect(&top);consoleClear();
            printf("COLOSSEUM3DS | FIELD ITEM TEST\nBuild %.12s\n\n",C3_BUILD_ID);
            printf("ISOLATED TEST FIXTURE, NOT A BATTLE\nFresh fixture starts Umbreon at 5 HP.\nNormal/completed saves never opened.\n\n");
            printf("Ready: %d  Loaded: %d\nUses: %u  Writes: %u  Save count: %lu\n",ready,loaded,uses,writes,(unsigned long)summary.saved_count);
            printf("WES party:\nUMBREON %u/%u HP\nESPEON %u/%u HP\n\n",summary.party[0].hp,summary.party[0].max_hp,summary.party[1].hp,summary.party[1].max_hp);
            printf("Y: explicitly save ISOLATED test\nSTART+SELECT: exit\n%s\n",error);
            consoleSelect(&bottom);
            if(ready&&menu.page!=C3_BAG_CLOSED)c3_bag_menu_draw(&menu,&items);
            else{consoleClear();printf("FIELD ITEM TEST\n\n%s\nA: reopen Bag\n",ready?"Bag closed.":error);}
            if(trace){
                fprintf(trace,"frame=%u input=%08lx page=%u hp=%u/%u uses=%u writes=%u loaded=%d count=%lu error=%s\n",
                    frame,(unsigned long)down,ready?(unsigned)menu.page:99u,summary.party[0].hp,summary.party[0].max_hp,uses,writes,loaded,(unsigned long)summary.saved_count,error);
                if(down&&ready)digest(trace,&state);
                fflush(trace);
            }
        }
        gfxFlushBuffers();gspWaitForVBlank();
    }
    if(trace){fprintf(trace,"probe_exit ready=%d uses=%u writes=%u loaded=%d error=%s\n",ready,uses,writes,loaded,error);fclose(trace);}
    c3_item_catalog_free(&items);c3_game_state_free(&state);gfxExit();
    return ready&&!error[0]?0:1;
}
