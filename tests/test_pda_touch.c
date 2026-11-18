/* P*DA / in-game menu stylus: hit boxes against the drawn layout on every
 * page, the Back button (exactly B), press/drag/release, original button
 * sounds, and folding the HID touch ring for taps shorter than a frame. */
#define main c3_game_state_fixture_main
#include "test_game_state.c"
#undef main
#include "ingame_menu.h"
#include "colosseum3ds/touch_ring.h"

static unsigned char *pixels;

static void draw(C3IngameMenu *m,C3GameState *state)
{C3BottomUi ui;c3_ui_begin(&ui,pixels,0,NULL,NULL);c3_ingame_draw_to(m,state,&ui);}
static uint32_t at(int x,int y)
{const unsigned char *p=pixels+((size_t)y*320u+(unsigned)x)*3u;return (uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2];}
/* Bounding box of one colour; 0 when absent. */
static int box(uint32_t color,int *l,int *t,int *r,int *b)
{
    *l=320;*t=240;*r=-1;*b=-1;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(at(x,y)==color){
        if(x<*l)*l=x;
        if(y<*t)*t=y;
        if(x>*r)*r=x;
        if(y>*b)*b=y;
    }
    return *r>=0;
}
static uint16_t touch(C3IngameMenu *m,int pressed,int held,int released,int x,int y)
{C3N3dsInput in={0};in.touch_pressed=pressed;in.touch_held=held;in.touch_released=released;in.touch_x=(uint16_t)x;in.touch_y=(uint16_t)y;return c3_ingame_touch(m,&in);}
static C3IngameEvent press(C3IngameMenu *m,C3GameState *state,uint16_t buttons)
{
    C3N3dsInput in={.held=buttons,.pressed=buttons};C3IngameEvent e=c3_ingame_update(m,state,&in,1.0f/60.0f);
    in.held=in.pressed=0;c3_ingame_update(m,state,&in,1.0f/60.0f);return e;
}
/* A whole tap the way field_scene runs it: touch pass, then update. */
static C3IngameEvent tap_at(C3IngameMenu *m,C3GameState *state,int x,int y)
{
    uint16_t down=touch(m,1,1,0,x,y);CHECK(!down);
    C3N3dsInput in={0};c3_ingame_update(m,state,&in,1.0f/60.0f);
    in.pressed=touch(m,0,0,1,x,y);C3IngameEvent e=c3_ingame_update(m,state,&in,1.0f/60.0f);
    in.pressed=0;c3_ingame_update(m,state,&in,1.0f/60.0f);return e;
}

typedef struct { C3IngamePage page; int top,pitch,shown; unsigned count; uint32_t highlight; } ListPage;

static void prepare(C3IngameMenu *m,C3IngamePage page,unsigned count)
{
    m->page=page;m->selection=0;m->detail=0;m->pocket=0;m->sort=0;m->tab=0;m->count=count;m->pager=(C3UiPager){0};
    m->touch_active=0;m->sound=0;m->inventory.action=C3_INVENTORY_USE;m->inventory_origin=C3_GAME_ITEMS;
    m->inventory_result.choice_count=3;m->inventory.quantity=1;
    for(unsigned i=0;i<512;++i)m->order[i]=i;
}

static void ring_checks(void)
{
    C3TouchRing ring={0};C3TouchEdges e;C3TouchSection s;memset(&s,0,sizeof s);
    /* Ticks: entry 0 of this cycle at 8000, of the previous at 0: one sample
     * per 1000 ticks. */
    s.tick0=8000;s.tick1=1;s.index=2;
    c3_touch_ring_fold(&ring,&s,0,0,0,&e);CHECK(!e.pressed&&!e.released&&!e.held); /* first poll: no history */
    /* Five samples later (index 7); samples 4..6 touched and the stylus is up
     * again at the poll: a tap entirely between two frames. */
    s.index=7;for(unsigned i=4;i<7;++i){s.entry[i][0]=40u|(120u<<16);s.entry[i][1]=1;}
    s.entry[4][0]=41u|(121u<<16);
    c3_touch_ring_fold(&ring,&s,0,0,0,&e);
    CHECK(e.pressed&&e.released&&!e.held&&e.x==41&&e.y==121);
    /* The same ring again (nothing new written): no second tap. */
    c3_touch_ring_fold(&ring,&s,0,0,0,&e);CHECK(!e.pressed&&!e.released);
    /* Held across polls: one press, positions follow, one release at the
     * last touched sample. */
    s.tick1=s.tick0;s.tick0=16000;s.index=1;memset(s.entry,0,sizeof s.entry);
    s.entry[0][0]=200u|(30u<<16);s.entry[0][1]=1;s.entry[1][0]=201u|(31u<<16);s.entry[1][1]=1;
    c3_touch_ring_fold(&ring,&s,1,201,31,&e);CHECK(e.pressed&&!e.released&&e.held&&e.x==201&&e.y==31);
    s.index=3;s.entry[2][0]=205u|(35u<<16);s.entry[2][1]=1;s.entry[3][1]=0;
    c3_touch_ring_fold(&ring,&s,0,0,0,&e);CHECK(!e.pressed&&e.released&&!e.held&&e.x==205&&e.y==35);
    /* Without the ring (or unusable ticks) the current state still works. */
    C3TouchRing plain={0};c3_touch_ring_fold(&plain,NULL,1,10,20,&e);CHECK(e.pressed&&e.held&&e.x==10&&e.y==20);
    c3_touch_ring_fold(&plain,NULL,0,0,0,&e);CHECK(e.released&&e.x==10&&e.y==20);
    memset(&s,0,sizeof s);c3_touch_ring_fold(&plain,&s,0,0,0,&e);CHECK(!e.pressed&&!e.released);
    /* More than eight samples elapsed: the eight in the ring are read once. */
    C3TouchRing late={0};memset(&s,0,sizeof s);s.tick0=8000;s.tick1=1;s.index=0;c3_touch_ring_fold(&late,&s,0,0,0,&e);
    s.tick1=8000;s.tick0=16000;s.index=7;s.entry[3][0]=7u|(8u<<16);s.entry[3][1]=1;
    late.newest=1000;c3_touch_ring_fold(&late,&s,0,0,0,&e);CHECK(e.pressed&&e.released&&e.x==7&&e.y==8);
}

int main(void)
{
    char directory[]="/tmp/c3-pda-touch-XXXXXX",pack_path[1024],error[192];CHECK(mkdtemp(directory)!=NULL);
    uint8_t *pack=synthetic_pack();
    unsigned bit=0;for(unsigned i=0;i<3593;++i){unsigned width=i>=1500&&i<=1596?8:1;pack[FLAGS+i*8]=(uint8_t)(0xc0u|width);w16(pack+FLAGS+i*8+4,bit);bit+=width;}
    w16(pack+BODY+0x7d20+0x400,6);for(unsigned i=0;i<6;++i)w16(pack+BODY+0x7d20+i*2,i);
    sign_pack(pack);snprintf(pack_path,sizeof pack_path,"%s/game.c3g",directory);write_file(pack_path,pack,PACK_SIZE);
    C3GameState state={0};CHECK(c3_game_state_new(&state,"WES",7,pack_path,error));
    size_t bytes;uint8_t *data=read_file("build/pda_fixture.c3pd",&bytes);C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,data,bytes));
    pixels=calloc(1,320u*240u*3u);CHECK(pixels);
    ring_checks();
    C3IngameMenu menu;c3_ingame_init(&menu,&state);c3_ingame_set_assets(&menu,&assets);

    /* 1. Every list page: each drawn row's highlight lies inside that row's
     * hit box (all four corners and the centre), the rows' hit boxes leave no
     * gap between them, and nothing outside the rows is a target. */
    const ListPage lists[]={
        {C3_GAME_PDA,68,32,4,4,0x30494bu},{C3_GAME_ITEM_ACTIONS,65,25,5,5,0x30494bu},{C3_GAME_PARTY_ACTIONS,65,25,4,4,0x30494bu},
        {C3_GAME_ITEM_TARGET,63,24,6,6,0x30494bu},{C3_GAME_ITEM_QUANTITY,90,24,1,1,0x30494bu},{C3_GAME_ITEM_CONFIRM,85,30,2,2,0x30494bu},
        {C3_GAME_ITEM_CHOICE,63,24,4,4,0x30494bu},{C3_GAME_PARTY,62,25,6,6,0x30494bu},{C3_GAME_ITEMS,63,24,6,6,0x30494bu},
        {C3_GAME_MAIL,65,29,5,6,0x12502cu},{C3_GAME_TONES,65,24,6,10,0x30494bu},{C3_GAME_MEMO,65,24,2,2,0x30494bu},
        {C3_GAME_SNAG,65,24,6,48,0x30494bu},{C3_GAME_MEMO_FILTER,65,29,5,5,0x30494bu},{C3_GAME_OPTIONS,72,37,3,3,0x30494bu},
    };
    for(unsigned p=0;p<sizeof lists/sizeof *lists;++p){const ListPage *L=lists+p;
        for(int row=0;row<L->shown;++row){
            prepare(&menu,L->page,L->count);menu.selection=(unsigned)row;draw(&menu,&state);
            int l,t,r,b;CHECK(box(L->highlight,&l,&t,&r,&b));
            int corners[5][2]={{l,t},{r,t},{l,b},{r,b},{(l+r)/2,(t+b)/2}};
            for(unsigned c=0;c<5;++c){int hit=c3_ingame_hit(&menu,corners[c][0],corners[c][1]);
                if(hit!=row){fprintf(stderr,"page %u row %d corner %u (%d,%d) hit %d\n",(unsigned)L->page,row,c,corners[c][0],corners[c][1],hit);CHECK(0);}}
        }
        prepare(&menu,L->page,L->count);
        for(int y=L->top;y<L->top+(L->shown-1)*L->pitch+19;++y){int hit=c3_ingame_hit(&menu,160,y);CHECK(hit>=0&&hit<L->shown);}
        CHECK(c3_ingame_hit(&menu,20,L->top+5)==C3_INGAME_HIT_NONE);CHECK(c3_ingame_hit(&menu,300,L->top+5)==C3_INGAME_HIT_NONE);
        CHECK(c3_ingame_hit(&menu,160,30)==C3_INGAME_HIT_NONE);
    }
    /* Lists that page by six/five map rows to the shown page's entries. */
    prepare(&menu,C3_GAME_SNAG,48);menu.selection=13;CHECK(c3_ingame_hit(&menu,160,65+5)==12&&c3_ingame_hit(&menu,160,65+24*5+5)==17);
    prepare(&menu,C3_GAME_MAIL,6);menu.selection=5;CHECK(c3_ingame_hit(&menu,160,70)==5&&c3_ingame_hit(&menu,160,100)==C3_INGAME_HIT_NONE);
    prepare(&menu,C3_GAME_PARTY,2);CHECK(c3_ingame_hit(&menu,160,62+25*2+5)==C3_INGAME_HIT_NONE);
    /* Hub: each drawn button inside its cell; cells meet without gaps. */
    for(unsigned i=0;i<8;++i){prepare(&menu,C3_GAME_HUB,0);menu.idle=0;menu.selection=i;draw(&menu,&state);
        int l,t,r,b;CHECK(box(0x175134u,&l,&t,&r,&b));
        CHECK(c3_ingame_hit(&menu,l,t)==(int)i&&c3_ingame_hit(&menu,r,b)==(int)i&&c3_ingame_hit(&menu,l,b)==(int)i&&c3_ingame_hit(&menu,r,t)==(int)i);}
    CHECK(c3_ingame_hub_hit(160,80)>=0&&c3_ingame_hub_hit(100,100)>=0&&c3_ingame_hub_hit(5,100)==-1);

    /* 2. Back button: drawn on every page and hit as BACK over all of it,
     * with priority over any row or panel under it. */
    for(unsigned p=0;p<=C3_GAME_ITEM_RESULT;++p){
        prepare(&menu,(C3IngamePage)p,p==C3_GAME_PARTY||p==C3_GAME_MEMO?2:p==C3_GAME_SNAG?48:0);menu.idle=0;draw(&menu,&state);
        int l,t,r,b;CHECK(box(0x0b3a22u,&l,&t,&r,&b));
        CHECK(l>=C3_INGAME_BACK_X&&r<C3_INGAME_BACK_X+C3_INGAME_BACK_W&&t>=C3_INGAME_BACK_Y&&b<C3_INGAME_BACK_Y+C3_INGAME_BACK_H);
        CHECK(r>=300&&b>=230); /* bottom-right corner */
        for(int y=C3_INGAME_BACK_Y;y<C3_INGAME_BACK_Y+C3_INGAME_BACK_H;++y)for(int x=C3_INGAME_BACK_X;x<C3_INGAME_BACK_X+C3_INGAME_BACK_W;++x)
            CHECK(c3_ingame_hit(&menu,x,y)==C3_INGAME_HIT_BACK);
        /* Pressed: the lighter fill shows while the stylus is down on it. */
        touch(&menu,1,1,0,C3_INGAME_BACK_X+30,C3_INGAME_BACK_Y+9);draw(&menu,&state);CHECK(box(0x2a7a4cu,&l,&t,&r,&b));
        touch(&menu,0,1,0,100,30);CHECK(!menu.touch_active); /* slid off: cancelled */
    }
    /* Detail pages: the panel is one target. */
    const C3IngamePage details[]={C3_GAME_ITEM,C3_GAME_POKEMON,C3_GAME_MEMO_ENTRY,C3_GAME_SNAG_ENTRY,C3_GAME_LETTER,C3_GAME_PICTURE,
                                  C3_GAME_TRAINER,C3_GAME_NOTICE,C3_GAME_ITEM_RESULT};
    for(unsigned i=0;i<sizeof details/sizeof *details;++i){prepare(&menu,details[i],0);
        CHECK(c3_ingame_hit(&menu,160,120)==C3_INGAME_HIT_PANEL);CHECK(c3_ingame_hit(&menu,160,20)==C3_INGAME_HIT_NONE);}

    /* 3. Back is B on every page: the touch pass returns exactly the B edge,
     * and the page, selection, event and sound match a real B press. */
    CHECK(c3_game_state_set_flag(&state,0x3f0,1));
    uint8_t *body=malloc(C3_SAVE_BODY_SIZE);CHECK(body);memcpy(body,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);
    for(unsigned p=0;p<=C3_GAME_ITEM_RESULT;++p){
        C3IngameMenu with_b,with_back;
        prepare(&menu,(C3IngamePage)p,p==C3_GAME_PARTY||p==C3_GAME_MEMO?2:p==C3_GAME_SNAG?48:p==C3_GAME_MAIL?6:0);
        menu.selection=p==C3_GAME_SNAG?7:0;menu.hub_selection=2;menu.pda_selection=1;menu.parent=C3_GAME_PDA;menu.idle=0;
        menu.inventory_origin=p==C3_GAME_PARTY_ACTIONS?C3_GAME_PARTY:C3_GAME_ITEMS;
        memcpy(&with_b,&menu,sizeof menu);memcpy(&with_back,&menu,sizeof menu);
        C3N3dsInput in={.held=C3_PAD_B,.pressed=C3_PAD_B};C3IngameEvent eb=c3_ingame_update(&with_b,&state,&in,1.0f/60.0f);
        CHECK(!touch(&with_back,1,1,0,C3_INGAME_BACK_X+30,C3_INGAME_BACK_Y+9));
        uint16_t edge=touch(&with_back,0,0,1,C3_INGAME_BACK_X+30,C3_INGAME_BACK_Y+9);CHECK(edge==C3_PAD_B);
        C3N3dsInput tin={.pressed=edge};C3IngameEvent et=c3_ingame_update(&with_back,&state,&tin,1.0f/60.0f);
        if(with_b.page!=with_back.page||with_b.selection!=with_back.selection||eb.kind!=et.kind||with_b.sound!=with_back.sound){
            fprintf(stderr,"page %u: B -> page %u sel %u ev %u snd %x; Back -> page %u sel %u ev %u snd %x\n",p,with_b.page,with_b.selection,eb.kind,with_b.sound,
                    with_back.page,with_back.selection,et.kind,with_back.sound);CHECK(0);}
        CHECK(!memcmp(body,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    }
    /* The specific B results the P*DA promises: one page back, close at the top. */
    prepare(&menu,C3_GAME_HUB,0);CHECK(tap_at(&menu,&state,290,230).kind==C3_INGAME_RESUME&&menu.sound==C3_INGAME_SOUND_CANCEL);
    prepare(&menu,C3_GAME_PDA,0);menu.hub_selection=2;tap_at(&menu,&state,290,230);CHECK(menu.page==C3_GAME_HUB&&menu.selection==2);
    prepare(&menu,C3_GAME_SNAG,48);tap_at(&menu,&state,290,230);CHECK(menu.page==C3_GAME_PDA&&menu.selection==0);
    prepare(&menu,C3_GAME_MAIL,6);tap_at(&menu,&state,290,230);CHECK(menu.page==C3_GAME_PDA&&menu.selection==2);

    /* 4. Taps on rows: press highlights (cursor sound), release in the same
     * row acts; sliding off cancels; a sub-frame tap still acts. */
    prepare(&menu,C3_GAME_HUB,0);menu.idle=1;
    CHECK(!touch(&menu,1,1,0,32+135+40,65+36+10));CHECK(menu.selection==3&&menu.sound==C3_INGAME_SOUND_CURSOR&&!menu.idle);
    menu.sound=0;CHECK(!touch(&menu,0,1,0,32+40,65+10));CHECK(!menu.touch_active); /* slid onto another button */
    CHECK(!touch(&menu,0,0,1,32+40,65+10));CHECK(menu.selection==3);           /* release there does nothing */
    prepare(&menu,C3_GAME_HUB,0);tap_at(&menu,&state,32+40,65+36+10);CHECK(menu.page==C3_GAME_PDA&&menu.sound==C3_INGAME_SOUND_DECIDE);
    /* P*DA page: every row opens what A opens. */
    tap_at(&menu,&state,160,68+5);CHECK(menu.page==C3_GAME_SNAG);tap_at(&menu,&state,290,230);
    tap_at(&menu,&state,160,68+32+5);CHECK(menu.page==C3_GAME_MEMO);tap_at(&menu,&state,290,230);
    tap_at(&menu,&state,160,68+64+5);CHECK(menu.page==C3_GAME_MAIL&&menu.count==6);
    C3IngameEvent e=tap_at(&menu,&state,160,65+29*2+5);CHECK(menu.page==C3_GAME_LETTER&&e.kind==C3_INGAME_READ_MAIL&&menu.selection==2);
    tap_at(&menu,&state,290,230);CHECK(menu.page==C3_GAME_MAIL&&menu.selection==2);
    tap_at(&menu,&state,290,230);tap_at(&menu,&state,160,68+96+5);CHECK(menu.page==C3_GAME_HUB); /* CANCEL row */
    /* Pressed and released within one frame (touch_ring.h) acts once. */
    prepare(&menu,C3_GAME_PDA,0);
    C3N3dsInput quick={0};quick.touch_pressed=quick.touch_released=1;quick.touch_x=160;quick.touch_y=68+64+5;
    quick.pressed=c3_ingame_touch(&menu,&quick);CHECK(quick.pressed==C3_PAD_A);c3_ingame_update(&menu,&state,&quick,1.0f/60.0f);CHECK(menu.page==C3_GAME_MAIL);
    /* Options: rows toggle, END applies. */
    prepare(&menu,C3_GAME_OPTIONS,0);C3GameSummary summary;CHECK(c3_game_state_summary(&state,&summary));menu.option_mono=summary.sound_mono;
    tap_at(&menu,&state,160,72+5);CHECK(menu.option_mono!=summary.sound_mono&&menu.page==C3_GAME_OPTIONS&&menu.sound==C3_INGAME_SOUND_CURSOR);
    CHECK(tap_at(&menu,&state,160,146+5).kind==C3_INGAME_OPTIONS);
    CHECK(c3_game_state_summary(&state,&summary)&&summary.sound_mono==menu.option_mono);
    CHECK(c3_game_state_set_options(&state,0,0));
    /* Tones: the first tap auditions, a tap on the highlighted tone keeps it. */
    prepare(&menu,C3_GAME_TONES,0);
    e=tap_at(&menu,&state,160,65+24*3+5);CHECK(e.kind==C3_INGAME_PLAY_TONE&&e.tone==3&&menu.page==C3_GAME_TONES&&!menu.sound);
    e=tap_at(&menu,&state,160,65+24*3+5);unsigned sort,tone;CHECK(e.kind==C3_INGAME_STOP_TONE&&menu.page==C3_GAME_MAIL);
    CHECK(c3_game_state_mail_settings(&state,&sort,&tone)&&tone==3);
    /* Memo search: rows step their value; SEARCH applies. */
    prepare(&menu,C3_GAME_MEMO_FILTER,0);menu.filter_types[0]=0;tap_at(&menu,&state,160,65+5);CHECK(menu.filter_types[0]==1);
    tap_at(&menu,&state,160,181+5);CHECK(menu.page==C3_GAME_MEMO&&!menu.filter_types[0]);
    /* Detail panel: A (pages text), Pokemon summary turns tabs once read. */
    prepare(&menu,C3_GAME_POKEMON,0);tap_at(&menu,&state,160,120);CHECK(menu.tab==1);
    prepare(&menu,C3_GAME_NOTICE,0);menu.parent=C3_GAME_PDA;tap_at(&menu,&state,160,120);CHECK(menu.page==C3_GAME_PDA);
    /* Buttons sound the same way. */
    prepare(&menu,C3_GAME_PDA,0);press(&menu,&state,C3_PAD_DOWN);CHECK(menu.sound==C3_INGAME_SOUND_CURSOR);
    menu.sound=0;press(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MEMO&&menu.sound==C3_INGAME_SOUND_DECIDE);
    menu.sound=0;press(&menu,&state,C3_PAD_B);CHECK(menu.page==C3_GAME_PDA&&menu.sound==C3_INGAME_SOUND_CANCEL);

    c3_ingame_close(&menu);c3_game_state_free(&state);free(body);free(pack);free(data);free(pixels);
    CHECK(unlink(pack_path)==0);CHECK(rmdir(directory)==0);
    printf("PDA touch: %u checks passed; hit boxes match the drawn rows on every page, Back is B everywhere, taps and sub-frame taps act once.\n",checks);
    return 0;
}
