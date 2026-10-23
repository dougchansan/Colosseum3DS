#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/game_items.h"
#include "colosseum3ds/save.h"
#include "../src/game_state_internal.h"
#include "../platform/3ds/bag_menu.h"
#include "../src/save_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
static void w16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void w32(unsigned char *p,unsigned v)
{p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
#include "field_items_fixture.h"

static void write_file(const char *path,const void *data,size_t size)
{
    FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(data,1,size,f)==size);CHECK(fclose(f)==0);
}
static unsigned char *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);
    long end=ftell(f);CHECK(end>=0);CHECK(fseek(f,0,SEEK_SET)==0);
    unsigned char *p=malloc((size_t)end+1);CHECK(p);
    CHECK(fread(p,1,(size_t)end,f)==(size_t)end);CHECK(fclose(f)==0);*size=(size_t)end;return p;
}
static void sign_catalog(unsigned char *p)
{ c3_save_codec_sha1(p+84,p+128,C3_ITEM_CATALOG_SIZE-128); }
static unsigned char *synthetic_catalog(void)
{
    unsigned char *p=calloc(1,C3_ITEM_CATALOG_SIZE);CHECK(p);
    const unsigned char pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    const unsigned char dol[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
    const unsigned char rel[20]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
    memcpy(p,"C3IT",4);w32(p+4,1);w32(p+8,C3_ITEM_CATALOG_SIZE);w32(p+12,548);w32(p+16,128);w32(p+20,128);
    memcpy(p+24,pin,20);memcpy(p+44,dol,20);memcpy(p+64,rel,20);
    static const unsigned heal[17]={20,0,0,0,0,0,255,255,200,50,0,254,255,50,60,80,100};
    static const unsigned cure[17]={0,16,8,4,32,2,63,0,0,0,63,0,0,0,0,0,0};
    for(unsigned id=13;id<=29;++id){
        unsigned char *r=p+128+id*128;r[0]=2;r[4]=(unsigned char)(id-12);
        w32(r+16,id);w32(r+28,0x80023b9c);r[40+3]=(unsigned char)cure[id-13];
        r[40+10]=(unsigned char)heal[id-13];if(id==24||id==25)r[44]=64;
        snprintf((char*)r+56,64,"SYNTHETIC %u",id);
    }
    static const unsigned ids[]={1,300,400,500,510},kinds[]={1,3,4,5,6};
    for(unsigned i=0;i<5;++i){
        unsigned char *r=p+128+ids[i]*128;r[0]=(unsigned char)kinds[i];
        w32(r+16,ids[i]);strcpy((char*)r+56,"UNSUPPORTED FIXTURE");
    }
    sign_catalog(p);return p;
}
static void check_unchanged(const C3GameState *state,const unsigned char *before)
{
    CHECK(!memcmp(c3_game_state_body(state,NULL),before,C3_SAVE_BODY_SIZE));
}
static void set_fixture(C3GameState *state,unsigned item,unsigned quantity,unsigned hp,unsigned maxhp,unsigned condition)
{
    GameStateImpl *s=state->impl;unsigned char *p=s->body+0xa0;
    memset(s->body+0x7f0,0,80);w16(s->body+0x7f0,item);w16(s->body+0x7f2,quantity);
    w16(p+0x8a,hp);w16(p+0x8c,maxhp);memset(p+0x64,0,16);w16(p+0x64,condition);
    /* Unknown bytes/padding must survive even a full status cure. */
    p[0x6b]=0xa5;p[0x72]=0x5a;p[0x73]=0xf3;p[0x74]=0x93;
    p[0xcb]=p[0xcd]=0;w16(p+0xe8,0);s->random=0x81726354u;
}
static void coverage(C3GameState *state,C3ItemCatalog *catalog)
{
    char err[192];C3ItemUseResult effect,unchanged;C3BagView view;C3BagPokemon party[6];
    unsigned char *before=malloc(C3_SAVE_BODY_SIZE),*expected=malloc(C3_SAVE_BODY_SIZE);CHECK(before&&expected);
    GameStateImpl *s=state->impl;
    /* Every supported item against each ordinary condition, several HP states,
     * and both dry-run/apply, using an independent small semantic oracle. */
    static const unsigned heal[17]={20,0,0,0,0,0,255,255,200,50,0,254,255,50,60,80,100};
    static const unsigned cure[17]={0,16,8,4,32,2,63,0,0,0,63,0,0,0,0,0,0};
    const unsigned hps[]={0,1,49,50,99,100};
    for(unsigned id=13;id<=29;++id)for(unsigned cond=0;cond<=8;++cond)
    for(unsigned h=0;h<sizeof hps/sizeof hps[0];++h){
        unsigned hp=hps[h],after=hp,flag=cond==3||cond==4?16:cond==5?2:cond==6?8:cond==7?4:cond==8?32:0;
        int cured=(cure[id-13]&flag)!=0,revive=id==24||id==25;
        if((hp!=0&&!revive)||(hp==0&&revive)){
            unsigned add=heal[id-13]==255?100:heal[id-13]==254?50:heal[id-13];
            after=hp+add;if(after>100)after=100;
        }
        set_fixture(state,id,2,hp,100,cond);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
        memset(&effect,0x5a,sizeof effect);unchanged=effect;
        C3ItemUseStatus wanted=(cured||after!=hp)?C3_ITEM_APPLIED:C3_ITEM_NO_EFFECT;
        CHECK(c3_game_item_use(state,catalog,2,0,(uint16_t)id,0,1,&effect,err)==wanted);
        check_unchanged(state,before);CHECK(s->random==0x81726354u);
        if(wanted==C3_ITEM_NO_EFFECT)CHECK(!memcmp(&effect,&unchanged,sizeof effect));
        else {CHECK(effect.hp_after==after);CHECK(effect.cured==cured);}
        CHECK(c3_game_item_use(state,catalog,2,0,(uint16_t)id,0,0,&effect,err)==wanted);
        memcpy(expected,before,C3_SAVE_BODY_SIZE);
        if(wanted==C3_ITEM_APPLIED){
            w16(expected+0xa0+0x8a,after);w16(expected+0x7f2,1);
            if(cured){memset(expected+0xa0+0x64,0,7);memset(expected+0xa0+0x6c,0,6);}
            CHECK(effect.quantity_before==2&&effect.quantity_after==1);
        }
        CHECK(!memcmp(s->body,expected,C3_SAVE_BODY_SIZE));CHECK(s->random==0x81726354u);
    }
    /* One-HP revive rounds up; living/fainted selection follows source. */
    set_fixture(state,24,1,0,1,0);
    CHECK(c3_game_item_use(state,catalog,2,0,24,0,0,&effect,err)==C3_ITEM_APPLIED);
    CHECK(effect.hp_after==1&&effect.revived&&effect.quantity_after==0);
    /* Stable normal-bag compaction preserves remaining slot bytes/order. */
    set_fixture(state,13,1,1,100,0);
    w16(s->body+0x7f0+8,22);w16(s->body+0x7f0+10,3);
    w16(s->body+0x7f0+12,13);w16(s->body+0x7f0+14,2);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,&effect,err)==C3_ITEM_APPLIED);
    CHECK(c3_game_bag_view(state,catalog,2,&view,err));
    CHECK(view.occupied==2&&view.slots[0].id==22&&view.slots[0].quantity==3);
    CHECK(view.slots[1].id==13&&view.slots[1].quantity==2&&view.slots[2].id==0);
    /* Deliberately malformed selectors/state do not partially apply. */
    set_fixture(state,13,2,1,100,0);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(c3_game_item_use(state,catalog,2,20,13,0,0,&effect,err)==C3_ITEM_ERROR);
    CHECK(c3_game_item_use(state,catalog,2,0,14,0,0,&effect,err)==C3_ITEM_ERROR);
    CHECK(c3_game_item_use(state,catalog,2,0,13,6,0,&effect,err)==C3_ITEM_ERROR);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,2,&effect,err)==C3_ITEM_ERROR);
    CHECK(c3_game_item_use(state,catalog,2,0,13,2,0,&effect,err)==C3_ITEM_ERROR);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,NULL,err)==C3_ITEM_ERROR);
    check_unchanged(state,before);
    s->body[0xa0+0xcb]=1;memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,&effect,err)==C3_ITEM_ERROR);check_unchanged(state,before);
    s->body[0xa0+0xcb]=0;w16(s->body+0xa0+0xe8,0x3e);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,&effect,err)==C3_ITEM_NO_EFFECT);check_unchanged(state,before);
    w16(s->body+0xa0+0xe8,0);w16(s->body+0x7f2,100);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(!c3_game_bag_view(state,catalog,2,&view,err));
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,&effect,err)==C3_ITEM_ERROR);check_unchanged(state,before);
    set_fixture(state,13,2,1,100,0);w16(s->body+0x7f0+6,5);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(c3_game_item_use(state,catalog,2,0,13,0,0,&effect,err)==C3_ITEM_ERROR);check_unchanged(state,before);
    set_fixture(state,13,2,1,100,0);
    CHECK(c3_game_bag_party(state,party,err));CHECK(party[0].species==197&&party[1].species==196);
    CHECK(party[0].hp==1&&party[0].maximum_hp==100&&!party[2].species);
    free(expected);free(before);
}

static void menu_checks(C3GameState *state,C3ItemCatalog *catalog)
{
    C3BagMenu menu;C3ItemUseResult used={0};GameStateImpl *s=state->impl;
    unsigned char *before=malloc(C3_SAVE_BODY_SIZE);CHECK(before);
    set_fixture(state,13,2,5,100,0);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    c3_bag_menu_init(&menu,state,catalog);CHECK(menu.page==C3_BAG_LIST&&menu.pocket==2);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used)==C3_BAG_EVENT_NONE);
    CHECK(menu.page==C3_BAG_TARGET);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used)==C3_BAG_EVENT_NONE);
    CHECK(menu.page==C3_BAG_CONFIRM&&!menu.yes);
    check_unchanged(state,before);
    /* Confirming the default NO returns without consuming. */
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used)==C3_BAG_EVENT_NONE);
    CHECK(menu.page==C3_BAG_TARGET);check_unchanged(state,before);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_UP|C3_PAD_A,&used);
    CHECK(menu.page==C3_BAG_CONFIRM&&menu.yes);check_unchanged(state,before);
    /* Cancel takes precedence over simultaneous confirmation. */
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A|C3_PAD_B,&used);
    CHECK(menu.page==C3_BAG_TARGET);check_unchanged(state,before);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_UP,&used);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used)==C3_BAG_EVENT_USED);
    CHECK(menu.page==C3_BAG_NOTICE&&used.hp_after==25&&used.quantity_after==1);
    CHECK(s->random==0x81726354u);
    memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    /* Held/no-edge frames cannot repeat consumption. */
    for(unsigned i=0;i<100;++i)CHECK(c3_bag_menu_step(&menu,state,catalog,0,&used)==C3_BAG_EVENT_NONE);
    check_unchanged(state,before);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);CHECK(menu.page==C3_BAG_LIST);
    /* Inventory browsing wraps all six pockets without mutation. */
    for(unsigned i=0;i<6;++i)(void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_RIGHT,&used);
    CHECK(menu.pocket==2);check_unchanged(state,before);
    for(unsigned i=0;i<6;++i)(void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_LEFT,&used);
    CHECK(menu.pocket==2);
    /* No-effect from a full-health Pokemon consumes no item. */
    set_fixture(state,13,1,100,100,0);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    c3_bag_menu_init(&menu,state,catalog);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    CHECK(menu.page==C3_BAG_NOTICE&&strstr(menu.notice,"no effect"));check_unchanged(state,before);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_X|C3_PAD_A,&used)==C3_BAG_EVENT_CLOSED);
    check_unchanged(state,before);
    /* Revalidate changed quantities before final confirmation. */
    set_fixture(state,13,2,1,100,0);c3_bag_menu_init(&menu,state,catalog);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_UP,&used);
    w16(s->body+0x7f2,3);memcpy(before,s->body,C3_SAVE_BODY_SIZE);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used)==C3_BAG_EVENT_NONE);
    CHECK(menu.page==C3_BAG_NOTICE&&!menu.yes);check_unchanged(state,before);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);CHECK(menu.page==C3_BAG_CONFIRM);
    CHECK(c3_bag_menu_step(&menu,state,catalog,C3_PAD_START,&used)==C3_BAG_EVENT_CLOSED);
    check_unchanged(state,before);
    /* Unsupported item, empty bag, and malformed catalog exit safely. */
    set_fixture(state,13,0,1,100,0);w16(s->body+0x7f0,0);
    c3_bag_menu_init(&menu,state,catalog);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_DOWN,&used);
    (void)c3_bag_menu_step(&menu,state,catalog,C3_PAD_A,&used);
    CHECK(menu.page==C3_BAG_NOTICE&&strstr(menu.notice,"empty"));
    c3_bag_menu_init(&menu,state,NULL);CHECK(menu.page==C3_BAG_NOTICE);
    CHECK(c3_bag_menu_step(&menu,state,NULL,C3_PAD_B,&used)==C3_BAG_EVENT_CLOSED);
    free(before);
}
int main(int argc,char **argv)
{
    C3GameState state={0},loaded={0};C3ItemCatalog catalog={0};
    C3ItemUseResult effect;C3ItemInfo info;C3BagView view;char error[192],dir[]="/tmp/c3-items-XXXXXX";
    char packpath[512],cardpath[512],badpath[512];CHECK(mkdtemp(dir));
    snprintf(packpath,sizeof packpath,"%s/synthetic.c3g",dir);snprintf(cardpath,sizeof cardpath,"%s/test.gci",dir);
    snprintf(badpath,sizeof badpath,"%s/truncated.c3it",dir);
    unsigned char *pack=synthetic_pack(),*table=synthetic_catalog();
    write_file(packpath,pack,PACK_SIZE);free(pack);
    const char *gamepack=argc>2?argv[2]:packpath;
    CHECK(c3_game_state_new(&state,"WES",0x12345678,gamepack,error));
    if(argc>1){CHECK(c3_item_catalog_load(&catalog,argv[1],error));}
    else CHECK(c3_item_catalog_decode(&catalog,table,C3_ITEM_CATALOG_SIZE,error));
    for(unsigned id=13;id<=29;++id){CHECK(c3_item_info(&catalog,(uint16_t)id,&info));CHECK(info.field_supported&&info.pocket==2);}
    CHECK(c3_item_info(&catalog,1,&info)&&!info.field_supported);
    CHECK(!c3_item_info(&catalog,0,&info));CHECK(!c3_item_info(&catalog,548,&info));
    for(unsigned k=1;k<=6;++k){CHECK(c3_game_bag_view(&state,&catalog,k,&view,error));CHECK(view.capacity>0);}
    if(argc<=1){
        static const unsigned ids[]={1,300,400,500,510},kinds[]={1,3,4,5,6},offs[]={0x87c,0x9bc,0x8bc,0x7d0,0xa74};
        GameStateImpl *s=state.impl;
        for(unsigned i=0;i<5;++i){
            w16(s->body+0x70+offs[i],ids[i]);w16(s->body+0x70+offs[i]+2,i==1?999:99);
            CHECK(c3_game_bag_view(&state,&catalog,kinds[i],&view,error));
            CHECK(view.occupied==1);
            CHECK(c3_game_item_use(&state,&catalog,kinds[i],0,(uint16_t)ids[i],0,0,&effect,error)==C3_ITEM_UNSUPPORTED);
        }
    }
    coverage(&state,&catalog);
    menu_checks(&state,&catalog);
    /* Exact catalog replacement failure preserves the previously loaded table. */
    void *old=catalog.impl;
    table[84]^=1;CHECK(!c3_item_catalog_decode(&catalog,table,C3_ITEM_CATALOG_SIZE,error));
    CHECK(catalog.impl==old);table[84]^=1;
    CHECK(!c3_item_catalog_decode(&catalog,table,C3_ITEM_CATALOG_SIZE-1,error));
    table[128+13*128+56]=1;sign_catalog(table);
    CHECK(!c3_item_catalog_decode(&catalog,table,C3_ITEM_CATALOG_SIZE,error));CHECK(catalog.impl==old);
    write_file(badpath,table,100);CHECK(!c3_item_catalog_load(&catalog,badpath,error));CHECK(catalog.impl==old);
    /* Production Save/load round trip, in isolated temporary storage only. */
    set_fixture(&state,13,5,1,100,0);
    CHECK(c3_game_item_use(&state,&catalog,2,0,13,0,0,&effect,error)==C3_ITEM_APPLIED);
    CHECK(effect.hp_after==21);
    CHECK(c3_game_state_save(&state,cardpath,error));
    CHECK(c3_game_state_load(&loaded,cardpath,gamepack,error));
    CHECK(!memcmp(c3_game_state_body(&state,NULL),c3_game_state_body(&loaded,NULL),C3_SAVE_BODY_SIZE));
    CHECK(c3_game_bag_view(&loaded,&catalog,2,&view,error)&&view.slots[0].quantity==4);
    size_t a,b;unsigned char *saved=read_file(cardpath,&a);
    CHECK(c3_game_bag_view(&loaded,&catalog,2,&view,error));
    CHECK(c3_game_item_use(&loaded,&catalog,2,0,13,0,1,&effect,error)==C3_ITEM_APPLIED);
    unsigned char *after=read_file(cardpath,&b);CHECK(a==b&&!memcmp(saved,after,a));
    free(after);free(saved);c3_game_state_free(&state);c3_game_state_free(&loaded);c3_item_catalog_free(&catalog);
    free(table);CHECK(unlink(cardpath)==0);CHECK(unlink(packpath)==0);CHECK(unlink(badpath)==0);CHECK(rmdir(dir)==0);
    printf("Field items: %u checks passed (%s); no battle result or automatic disk writes.\n",checks,argc>1?"canonical private catalog":"synthetic catalog");
    return 0;
}
