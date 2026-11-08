/* Private real-card round trip; never writes the input GCI. */
#define main c3_inventory_real_fixture_main
#include "test_game_state.c"
#undef main
#include "colosseum3ds/inventory.h"

int main(int argc,char **argv)
{
    if(argc!=4){fprintf(stderr,"usage: %s input.gci game.c3g pda.c3pd\n",argv[0]);return 2;}
    size_t image_n,pack_n,body_n,again_n;
    uint8_t *image=read_file(argv[1],&image_n),*pack=read_file(argv[3],&pack_n);
    C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,pack,pack_n));
    C3GameState state={0},reload={0};char error[192];
    CHECK(c3_game_state_decode(&state,image,image_n,argv[2],error));
    uint8_t *body=(uint8_t*)c3_game_state_body(&state,&body_n);
    CHECK(body_n==C3_SAVE_BODY_SIZE);
    uint8_t *original=malloc(body_n),*expected=malloc(body_n);CHECK(original&&expected);
    memcpy(original,body,body_n);memcpy(expected,body,body_n);
    unsigned first=6,second=6;
    for(unsigned i=0;i<6;++i)if(r16(body+PARTY+i*312)){if(first==6)first=i;else{second=i;break;}}
    CHECK(second<6);
    memcpy(expected+PARTY+first*312,original+PARTY+second*312,312);
    memcpy(expected+PARTY+second*312,original+PARTY+first*312,312);
    C3InventoryRequest q={.action=C3_INVENTORY_REORDER,.pocket=6,.slot=first,.destination=second};
    C3InventoryResult result;CHECK(c3_inventory_apply(&state,&assets,&q,&result)==C3_INVENTORY_OK);
    CHECK(!memcmp(expected,body,body_n));
    /* Explicit fixture edits only in the disposable decoded clone: ensure a
     * genuine medicine transaction even when the original bag is empty. */
    unsigned target=first;uint8_t *mon=body+PARTY+target*312;
    CHECK(!mon[0xcb]&&r16(mon+0x8c)>1);
    memset(body+0x7f0,0,80);w16(body+0x7f0,13);w16(body+0x7f2,2);
    w16(mon+0x8a,1);
    memcpy(expected,body,body_n);
    q=(C3InventoryRequest){.action=C3_INVENTORY_USE,.party=target,.move=-1,.replacement=-1,.evolution=-1};
    CHECK(c3_inventory_apply(&state,&assets,&q,&result)==C3_INVENTORY_OK);
    CHECK(r16(body+0x7f2)==1&&r16(mon+0x8a)>1);
    /* Only HP, friendship and the consumed quantity may differ from fixture. */
    memcpy(expected+PARTY+target*312+0x8a,mon+0x8a,2);
    memcpy(expected+PARTY+target*312+0xb0,mon+0xb0,2);
    memcpy(expected+0x7f2,body+0x7f2,2);
    CHECK(!memcmp(expected,body,body_n));
    char directory[]="/tmp/c3-inventory-real-XXXXXX",output[1024];CHECK(mkdtemp(directory));
    snprintf(output,sizeof output,"%s/modified.gci",directory);
    w32(expected+0x10,c3_pda_be32(expected+0x10)+1u);
    memset(expected+4,0,4);memset(expected+0x18,0,4);
    CHECK(c3_game_state_save(&state,output,error));
    CHECK(c3_game_state_load(&reload,output,argv[2],error));
    CHECK(!memcmp(expected,c3_game_state_body(&reload,NULL),body_n));
    uint8_t *again=read_file(argv[1],&again_n);
    CHECK(image_n==again_n&&!memcmp(image,again,image_n));
    printf("Real GCI: reorder + disposable Potion fixture; all unrelated decoded bytes preserved; explicit save/reopen exact; input bytes unchanged. Output: %s (%u checks)\n",output,checks);
    free(again);free(original);free(expected);free(image);free(pack);
    c3_game_state_free(&state);c3_game_state_free(&reload);return 0;
}
