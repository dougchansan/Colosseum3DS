/* Exercise production action routing with the shared synthetic save fixture. */
#define main c3_inventory_fixture_main
#include "test_game_state.c"
#undef main
#include "ingame_menu.h"
static void tap_inventory(C3IngameMenu *m,C3GameState *s,uint16_t button)
{
    C3N3dsInput input={.pressed=button,.held=button};c3_ingame_update(m,s,&input,1.0f/60.0f);
    input.pressed=input.held=0;c3_ingame_update(m,s,&input,1.0f/60.0f);
}
static void original_choices(const char *pda,const char *game)
{
    size_t bytes;uint8_t *data=read_file(pda,&bytes);C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,data,bytes));
    C3GameState state={0};char error[192];CHECK(c3_game_state_new(&state,"WES",7,game,error));
    uint8_t *body=(uint8_t*)c3_game_state_body(&state,NULL),*p=body+PARTY;
    /* Original Bulbasaur learns PoisonPowder and SleepPowder at level15. */
    w16(p,1);p[0x60]=14;p[0xcb]=0;w16(p+0xd8,0);w16(p+0x88,0);
    for(unsigned i=0;i<4;++i){w16(p+0x78+i*4,10+i*10);p[0x7a+i*4]=5;p[0x7b+i*4]=0;}
    memcpy(p+312,p,312);
    memset(body+0x7f0,0,80);w16(body+0x7f0,68);w16(body+0x7f2,2);
    uint8_t *before=malloc(C3_SAVE_BODY_SIZE);CHECK(before);memcpy(before,body,C3_SAVE_BODY_SIZE);
    C3IngameMenu m;c3_ingame_init(&m,&state);c3_ingame_set_assets(&m,&assets);m.selection=1;tap_inventory(&m,&state,C3_PAD_A);
    tap_inventory(&m,&state,C3_PAD_X);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_choice==C3_INVENTORY_NEED_REPLACEMENT&&m.inventory_result.learning_index==0);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_result.learning_index==1);CHECK(m.inventory.replacement_count==1&&m.inventory.replacements[0]==0);
    CHECK(c3_pda_be16(m.inventory_result.pokemon+0x78)==77);
    CHECK(m.inventory_result.pokemon[0x7a]>0&&c3_pda_be16(p+0x78)==10);
    /* Abandon A's second move prompt, then use the candy on B. B must get
     * its own first prompt, without A's staged replacement being reused. */
    tap_inventory(&m,&state,C3_PAD_B);CHECK(m.page==C3_GAME_ITEM_ACTIONS);
    tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_DOWN);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_result.learning_index==0&&m.inventory.party==1);
    CHECK(!m.inventory.replacement_count&&c3_pda_be16(m.inventory_result.pokemon+0x78)==10);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));
    tap_inventory(&m,&state,C3_PAD_B);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_result.learning_index==1);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));tap_inventory(&m,&state,C3_PAD_DOWN);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_RESULT&&m.inventory_result.code==C3_INVENTORY_OK);
    CHECK(p[0x60]==15&&r16(p+0x78)==77&&r16(p+0x7c)==79&&r16(body+0x7f2)==1);
    CHECK(m.inventory.replacement_count==2&&m.inventory.replacements[1]==1);
    /* The next original level prompts Bulbasaur's evolution to Ivysaur. */
    tap_inventory(&m,&state,C3_PAD_A);memcpy(before,body,C3_SAVE_BODY_SIZE);
    tap_inventory(&m,&state,C3_PAD_X);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_choice==C3_INVENTORY_NEED_EVOLUTION&&m.inventory_result.choices[0]==2);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));tap_inventory(&m,&state,C3_PAD_B);CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));
    tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_RESULT&&m.inventory_result.code==C3_INVENTORY_OK&&r16(p)==2&&p[0x60]==16);
    /* Declining evolution retains the candy's level gain and consumes once. */
    w16(p,1);p[0x60]=15;w16(body+0x7f0,68);w16(body+0x7f2,2);tap_inventory(&m,&state,C3_PAD_A);
    tap_inventory(&m,&state,C3_PAD_X);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_choice==C3_INVENTORY_NEED_EVOLUTION);
    tap_inventory(&m,&state,C3_PAD_DOWN);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.inventory_result.code==C3_INVENTORY_OK&&r16(p)==1&&p[0x60]==16&&r16(body+0x7f2)==1);
    /* PP selection uses source item34 and only consumes after selection. */
    w16(body+0x7f0,34);w16(body+0x7f2,2);p[0x7a]=0;memcpy(before,body,C3_SAVE_BODY_SIZE);
    tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_X);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_CHOICE&&m.inventory_choice==C3_INVENTORY_NEED_MOVE);CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.inventory_result.code==C3_INVENTORY_OK&&p[0x7a]>0&&r16(body+0x7f2)==1);
    free(before);c3_ingame_close(&m);c3_game_state_free(&state);free(data);
}
int main(int argc,char **argv)
{
    char dir[]="/tmp/c3-inventory-ui-XXXXXX",path[1024],error[192];CHECK(mkdtemp(dir));
    uint8_t *pack=synthetic_pack();sign_pack(pack);snprintf(path,sizeof path,"%s/game.c3g",dir);write_file(path,pack,PACK_SIZE);
    C3GameState state={0};CHECK(c3_game_state_new(&state,"WES",7,path,error));
    size_t bytes;uint8_t *data=read_file("build/pda_fixture.c3pd",&bytes);C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,data,bytes));
    C3IngameMenu m;c3_ingame_init(&m,&state);c3_ingame_set_assets(&m,&assets);
    uint8_t *body=(uint8_t*)c3_game_state_body(&state,NULL);w16(body+0x7f0,13);w16(body+0x7f2,5);
    m.selection=1;tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEMS&&m.count);
    tap_inventory(&m,&state,C3_PAD_X);CHECK(m.page==C3_GAME_ITEM_ACTIONS);CHECK(m.inventory.move==-1&&m.inventory.replacement==-1&&m.inventory.evolution==-1);
    m.selection=2;tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_QUANTITY&&m.inventory.quantity==1);
    tap_inventory(&m,&state,C3_PAD_UP);CHECK(m.inventory.quantity==2);
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_CONFIRM&&m.selection==0);
    uint8_t *before=malloc(C3_SAVE_BODY_SIZE);CHECK(before);memcpy(before,body,C3_SAVE_BODY_SIZE);
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_ACTIONS);CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));
    m.selection=2;tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_DOWN);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_RESULT);CHECK(m.inventory_result.code==C3_INVENTORY_OK);CHECK(r16(body+0x7f2)==3);
    tap_inventory(&m,&state,C3_PAD_B);CHECK(m.page==C3_GAME_ITEMS);
    m.page=C3_GAME_ITEMS;m.count=1;m.selection=0;m.order[0]=6;tap_inventory(&m,&state,C3_PAD_X);
    CHECK(m.inventory.slot==6&&m.inventory.party==0);tap_inventory(&m,&state,C3_PAD_A);
    CHECK(m.page==C3_GAME_ITEM_TARGET);memcpy(before,body,C3_SAVE_BODY_SIZE);tap_inventory(&m,&state,C3_PAD_B);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));tap_inventory(&m,&state,C3_PAD_B);

    tap_inventory(&m,&state,C3_PAD_B);m.selection=0;tap_inventory(&m,&state,C3_PAD_A);tap_inventory(&m,&state,C3_PAD_X);CHECK(m.page==C3_GAME_PARTY_ACTIONS);
    m.selection=2;tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_TARGET&&m.inventory.pocket==6);
    tap_inventory(&m,&state,C3_PAD_DOWN);tap_inventory(&m,&state,C3_PAD_A);CHECK(m.inventory_result.code==C3_INVENTORY_OK);
    CHECK(r16(body+PARTY)==196);CHECK(r16(body+PARTY+312)==197);
    m.page=C3_GAME_POKEMON;m.selection=1;m.detail=1;c3_ingame_error(&m,"The selected model is unavailable.");
    CHECK(m.page==C3_GAME_NOTICE);tap_inventory(&m,&state,C3_PAD_B);CHECK(m.page==C3_GAME_POKEMON&&m.selection==1&&m.detail==1);
    m.page=C3_GAME_MEMO_ENTRY;m.selection=2;m.detail=3;c3_ingame_error(&m,"The selected model is unavailable.");
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_MEMO_ENTRY&&m.selection==2&&m.detail==3);
    memcpy(before,body,C3_SAVE_BODY_SIZE);m.page=C3_GAME_ITEM_CHOICE;m.inventory_origin=C3_GAME_ITEMS;
    m.inventory.move=2;m.inventory.replacement=1;m.inventory.evolution=2;
    m.inventory.replacement_count=1;m.inventory.replacements[0]=3;
    tap_inventory(&m,&state,C3_PAD_B);CHECK(m.page==C3_GAME_ITEM_ACTIONS);
    CHECK(m.inventory.move==-1&&m.inventory.replacement==-1&&m.inventory.evolution==-1&&!m.inventory.replacement_count);
    tap_inventory(&m,&state,C3_PAD_A);CHECK(m.page==C3_GAME_ITEM_TARGET);
    CHECK(!memcmp(before,body,C3_SAVE_BODY_SIZE));
    free(before);c3_ingame_close(&m);c3_game_state_free(&state);free(data);free(pack);unlink(path);rmdir(dir);
    if(argc==3)original_choices(argv[1],argv[2]);
    printf("Inventory UI: %u checks passed.\n",checks);return 0;
}
