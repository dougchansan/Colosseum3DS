/* Render the production controller/views against an actual original new-game
 * pack or GCI. --demo modifies only this process's freshly-created state to
 * exercise populated pages; it never loads/saves a user's GCI in demo mode. */
#include "ingame_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<1||n>8*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    void *data=malloc((size_t)n);int ok=data&&fread(data,1,(size_t)n,f)==(size_t)n&&!ferror(f);fclose(f);
    if(!ok){free(data);return NULL;}*size=(size_t)n;return data;
}
static void put16(uint8_t *p,unsigned value){p[0]=(uint8_t)(value>>8);p[1]=(uint8_t)value;}
static void put32(uint8_t *p,uint32_t value){p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;}
static void tap(C3IngameMenu *m,C3GameState *s,uint16_t button)
{
    C3N3dsInput input={.held=button,.pressed=button};(void)c3_ingame_update(m,s,&input,1.0f/60.0f);
    input.held=input.pressed=0;(void)c3_ingame_update(m,s,&input,1.0f/60.0f);
}
static void home(C3IngameMenu *m,C3GameState *s)
{for(unsigned i=0;i<10&&m->page!=C3_GAME_HUB;++i)tap(m,s,C3_PAD_B);m->selection=m->hub_selection=m->pda_selection=0;}
static int snapshot(C3IngameMenu *m,C3GameState *s,const C3UiAssets *assets,const char *directory,const char *name)
{
    uint8_t *pixels=calloc(320u*240u,3u);if(!pixels)return 0;C3BottomUi ui;
    c3_ui_begin(&ui,pixels,0,assets,NULL);c3_ingame_draw_to(m,s,&ui);
    char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",directory,name);FILE *f=fopen(path,"wb");if(!f){free(pixels);return 0;}
    fprintf(f,"P6\n320 240\n255\n");int ok=fwrite(pixels,3u,320u*240u,f)==320u*240u;if(fclose(f))ok=0;free(pixels);return ok;
}
int main(int argc,char **argv)
{
    if(argc<5||argc>6){fprintf(stderr,"usage: render_ingame_menu_preview ui.c3ui pda.c3pd newgame.c3g output-directory [--demo|save.gci]\n");return 1;}
    size_t bytes=0;void *ui_data=read_file(argv[1],&bytes);C3UiAssets assets={0};if(!c3_ui_assets_open(&assets,ui_data,bytes)){free(ui_data);return 1;}
    C3GameState state={0};char error[192];int demo=argc==6&&!strcmp(argv[5],"--demo");
    int ok=argc==6&&!demo?c3_game_state_load(&state,argv[5],argv[3],error):c3_game_state_new(&state,"WES",0x12345678u,argv[3],error);
    if(!ok){fprintf(stderr,"%s\n",error);free(ui_data);return 1;}
    C3IngameMenu menu;c3_ingame_init(&menu,&state);if(!c3_ingame_load(&menu,argv[2])){c3_game_state_free(&state);free(ui_data);return 1;}
    if(demo){
        uint8_t *body=(uint8_t*)c3_game_state_body(&state,NULL);static const uint16_t mail[]={0,4,10,22,23,29};
        for(unsigned i=0;i<6;++i)put16(body+0x7d20+i*2,mail[i]);
        put16(body+0x7d20+0x400,6);body[0x7d20+0x402]=0x80;
        c3_game_state_set_flag(&state,0x3f0,1);
        /* Source marker cleared here only in this explicit preview fixture.
         * Runtime browsing never performs the aide's memo update operation. */
        put16(body+0x82a8+4,197);put16(body+0x82a8+16,196);
        put16(body+0x82a8,3);put16(body+0x82a8+28,335|0x8000);put32(body+0x82a8+32,1);put32(body+0x82a8+36,2);
        C3PdaSnag snag;if(c3_pda_snag(&menu.assets,&state,0,&snag)){
            c3_game_state_set_flag(&state,snag.seen_flag,1);c3_game_state_set_flag(&state,snag.snag_flag,1);
            c3_game_state_set_flag(&state,snag.hint_flag,3);
        }
    }
    ok=snapshot(&menu,&state,&assets,argv[4],"hub");
    tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"party");tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"pokemon");tap(&menu,&state,C3_PAD_R);ok&=snapshot(&menu,&state,&assets,argv[4],"stats");
    tap(&menu,&state,C3_PAD_R);ok&=snapshot(&menu,&state,&assets,argv[4],"moves");tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_B);
    tap(&menu,&state,C3_PAD_RIGHT);tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"items");tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"item");tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"trainer");tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_LEFT);tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"pda");tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"snag");tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"snag-entry");tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"memo");tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"memo-entry");
    if(menu.page==C3_GAME_MEMO_ENTRY){tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_Y);ok&=snapshot(&menu,&state,&assets,argv[4],"memo-search");}
    home(&menu,&state);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);
    ok&=snapshot(&menu,&state,&assets,argv[4],"mail");tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"letter");
    if(menu.page==C3_GAME_LETTER){tap(&menu,&state,C3_PAD_X);ok&=snapshot(&menu,&state,&assets,argv[4],"picture");if(menu.page==C3_GAME_PICTURE)tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_B);}
    if(menu.page==C3_GAME_MAIL){tap(&menu,&state,C3_PAD_Y);ok&=snapshot(&menu,&state,&assets,argv[4],"tones");}
    home(&menu,&state);menu.selection=1;tap(&menu,&state,C3_PAD_A);
    if(menu.count){tap(&menu,&state,C3_PAD_X);ok&=snapshot(&menu,&state,&assets,argv[4],"item-actions");
        tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"item-target");tap(&menu,&state,C3_PAD_B);
        menu.selection=2;tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"item-quantity");
        tap(&menu,&state,C3_PAD_A);ok&=snapshot(&menu,&state,&assets,argv[4],"item-confirm");tap(&menu,&state,C3_PAD_B);
        menu.page=C3_GAME_ITEM_CHOICE;menu.selection=0;menu.inventory_choice=C3_INVENTORY_NEED_MOVE;
        menu.inventory_result.choice_count=4;for(unsigned i=0;i<4;++i)menu.inventory_result.choices[i]=(uint16_t)i;
        menu.inventory.party=0;ok&=snapshot(&menu,&state,&assets,argv[4],"item-move");
        menu.inventory_choice=C3_INVENTORY_NEED_REPLACEMENT;menu.inventory_result.choice_count=5;menu.inventory_result.choices[4]=4;
        ok&=snapshot(&menu,&state,&assets,argv[4],"item-forget");
        menu.inventory_choice=C3_INVENTORY_NEED_EVOLUTION;menu.inventory_result.choice_count=1;menu.inventory_result.choices[0]=2;
        ok&=snapshot(&menu,&state,&assets,argv[4],"item-evolution");
    }
    home(&menu,&state);menu.selection=0;tap(&menu,&state,C3_PAD_A);tap(&menu,&state,C3_PAD_X);
    ok&=snapshot(&menu,&state,&assets,argv[4],"party-actions");
    printf("Rendered native in-game pages (%s); no save writes.\n",demo?"explicit populated preview fixture":argc==6?"actual GCI":"actual new-game state");
    c3_ingame_close(&menu);c3_game_state_free(&state);free(ui_data);return ok?0:1;
}
