#include "colosseum3ds/bottom_ui.h"
#include "game_menu.h"
#include "game_ui_views.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES (320u*240u*3u)
#define TABLE (32u+C3_UI_SPRITES*16u+95u*8u)
#define PACK (TABLE+C3_UI_SPRITES*8u*8u*4u+95u*256u)
static void put32(unsigned char *p,uint32_t n)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}

static unsigned char *fixture(void)
{
    unsigned char *d=calloc(1,PACK);assert(d);
    memcpy(d,"C3UI",4);put32(d+4,1);put32(d+8,PACK);put32(d+12,C3_UI_SPRITES);put32(d+16,95);
    unsigned next=TABLE;
    for(unsigned i=0;i<C3_UI_SPRITES;++i){
        unsigned char *e=d+32+i*16;put32(e,8);put32(e+4,8);put32(e+8,next);put32(e+12,256);
        for(unsigned k=0;k<64;++k){d[next+k*4]=50;d[next+k*4+1]=100;d[next+k*4+2]=150;d[next+k*4+3]=255;}
        next+=256;
    }
    for(unsigned i=0;i<95;++i){
        unsigned char *e=d+32+C3_UI_SPRITES*16+i*8;put32(e,8);put32(e+4,next);
        for(unsigned k=0;k<8;++k)d[next+k*16+k]=255;
        next+=256;
    }
    assert(next==PACK);return d;
}

static unsigned rgb565_pixel(const unsigned char *p,unsigned x,unsigned y)
{size_t at=((size_t)x*240u+239u-y)*2u;return (unsigned)p[at]|(unsigned)p[at+1]<<8;}
static void rgb565_screen(const C3UiAssets *assets)
{
    const size_t bytes=320u*240u*2u;unsigned char *guard=malloc(bytes+32);assert(guard);
    memset(guard,0xa5,bytes+32);C3BottomUi ui;
    c3_ui_begin(&ui,guard+16,C3_UI_ROTATED_RGB565,assets,NULL);
    c3_ui_rect(&ui,0,0,320,240,0);
    c3_ui_rect(&ui,0,0,1,1,0xff0000);c3_ui_rect(&ui,319,0,1,1,0x00ff00);c3_ui_rect(&ui,0,239,1,1,0x0000ff);
    assert(rgb565_pixel(ui.pixels,0,0)==0xf800&&rgb565_pixel(ui.pixels,319,0)==0x07e0&&rgb565_pixel(ui.pixels,0,239)==0x001f);
    const uint8_t red[]={255,0,0,128};c3_ui_rgba(&ui,red,1,1,2,3,1,1);
    assert(rgb565_pixel(ui.pixels,2,3)==0x8000);
    c3_ui_title(&ui,1,0);C3UiPager pager={0};c3_ui_dialogue(&ui,&pager,"RGB565 native framebuffer bounds",1,1);
    for(unsigned k=0;k<16;++k)assert(guard[k]==0xa5&&guard[bytes+16+k]==0xa5);
    free(guard);
}

int main(void)
{
    unsigned char *data=fixture();C3UiAssets a={0};assert(c3_ui_assets_open(&a,data,PACK));
    rgb565_screen(&a);
    C3UiAssets previous=a;
    assert(!c3_ui_assets_open(&a,data,PACK-1));assert(a.data==previous.data&&a.size==previous.size);
    put32(data+32+8,0);assert(!c3_ui_assets_open(&a,data,PACK));put32(data+32+8,TABLE);
    put32(data+32,7);assert(!c3_ui_assets_open(&a,data,PACK));put32(data+32,8);
    put32(data+20,1);assert(!c3_ui_assets_open(&a,data,PACK));put32(data+20,0);
    put32(data+32+C3_UI_SPRITES*16,17);assert(!c3_ui_assets_open(&a,data,PACK));put32(data+32+C3_UI_SPRITES*16,8);
    for(size_t i=0;i<TABLE;i+=17)assert(!c3_ui_assets_open(&a,data,i));
    unsigned char *guard=malloc(BYTES+32),*rotated=malloc(BYTES);assert(guard&&rotated);
    memset(guard,0xa5,BYTES+32);C3BottomUi ui,r;
    c3_ui_begin(&ui,guard+16,0,&a,NULL);c3_ui_begin(&r,rotated,1,&a,NULL);
    c3_ui_rect(&ui,-8,-9,20,30,0xab3456);c3_ui_rect(&r,-8,-9,20,30,0xab3456);
    c3_ui_rect(&ui,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0);
    c3_ui_sprite(&ui,0,INT_MAX,INT_MIN,100,100);c3_ui_panel(&ui,INT_MAX,INT_MIN,100,100);
    c3_ui_text(&ui,INT_MAX,INT_MIN,100,2,"Safe clipping",0xffffff);
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){
        size_t at=(y*320u+x)*3u,rt=(x*240u+239u-y)*3u;
        assert(ui.pixels[at]==rotated[rt+2]&&ui.pixels[at+1]==rotated[rt+1]&&ui.pixels[at+2]==rotated[rt]);
    }
    C3GameMenu menu;C3GameMenuSave save={.valid=1,.can_continue=1,.name="WES",.location="OUTSKIRT STAND"};
    c3_game_menu_init(&menu,&save,NULL);
    for(unsigned page=0;page<=C3_MENU_ERROR;++page){
        menu.page=(C3GameMenuPage)page;
        for(unsigned selection=0;selection<(page==C3_MENU_MAIN?6u:1u);++selection){
            menu.selection=selection;c3_ui_begin(&ui,guard+16,0,&a,NULL);c3_game_menu_draw_to(&menu,&ui);
            for(unsigned k=0;k<16;++k)assert(guard[k]==0xa5&&guard[BYTES+16+k]==0xa5);
        }
    }
    C3UiPager pager={0};char long_text[1200];memset(long_text,'W',sizeof long_text-1);long_text[sizeof long_text-1]=0;
    c3_ui_pager_sync(&pager,123);c3_ui_dialogue(&ui,&pager,long_text,1,1);assert(pager.more&&pager.next>0);
    uint16_t directions=0x000fu;assert(!c3_ui_pager_advance(&pager,&directions)&&!directions);
    uint16_t pressed=0x030fu;assert(c3_ui_pager_advance(&pager,&pressed));assert(!pressed&&pager.offset>0);
    unsigned pages=1;
    do{
        size_t old=pager.offset;c3_ui_dialogue(&ui,&pager,long_text,1,0);assert(pager.next>old);
        if(!pager.more)break;
        pressed=0x100u;assert(c3_ui_pager_advance(&pager,&pressed));assert(++pages<32);
    }while(1);
    pressed=0x100u;assert(!c3_ui_pager_advance(&pager,&pressed)&&pressed==0x100u);
    c3_ui_pager_sync(&pager,124);assert(!pager.offset&&!pager.next&&!pager.more);
    c3_ui_begin(&ui,guard+16,0,&a,NULL);
    c3_ui_title(&ui,1,0);c3_ui_title(&ui,0,1);c3_ui_title(&ui,0,0);
    unsigned title_left=320,title_right=0,title_top=240,title_bottom=0;
    for(unsigned y=62;y<82;++y)for(unsigned x=0;x<320;++x){
        size_t at=(y*320u+x)*3u;
        if(ui.pixels[at]==(C3_UI_WHITE>>16)&&ui.pixels[at+1]==((C3_UI_WHITE>>8)&255)&&ui.pixels[at+2]==(C3_UI_WHITE&255)){
            if(x<title_left)title_left=x;
            if(x>title_right)title_right=x;
            if(y<title_top)title_top=y;
            if(y>title_bottom)title_bottom=y;
        }
    }
    assert(title_left+title_right>=318&&title_left+title_right<=320&&title_bottom-title_top+1>=10);
    c3_ui_experience(&ui,10,150,102,1,2);
    assert(ui.pixels[(151u*320u+11u)*3u]==0x88&&ui.pixels[(151u*320u+60u)*3u]==0x88);
    assert(ui.pixels[(151u*320u+61u)*3u]==0x15);
    c3_ui_experience(&ui,10,150,102,UINT32_MAX,1);
    assert(ui.pixels[(151u*320u+110u)*3u]==0x88);
    c3_ui_experience(&ui,10,150,102,1,0);
    assert(ui.pixels[(151u*320u+11u)*3u]==0x15);
    size_t consumed=c3_ui_text(&ui,2,2,40,1,"one two three",0xffffff);assert(consumed>0&&consumed<13);
    /* A shorter value must end at the same visible right edge. Measure ink,
     * not padded character counts: this font has proportional advances. */
    const char *values[]={"9 PP","19/20 PP","100/100 PP","1","89/89","100/100"};
    for(unsigned i=0;i<sizeof values/sizeof *values;++i){
        c3_ui_rect(&ui,0,0,320,240,0);
        assert(c3_ui_text_right(&ui,299,20,120,values[i],0xffffff)>0);
        unsigned rightmost=0;
        for(unsigned y=20;y<37;++y)for(unsigned x=0;x<320;++x){
            size_t at=(y*320u+x)*3u;
            if(ui.pixels[at]==255&&ui.pixels[at+1]==255&&ui.pixels[at+2]==255)rightmost=x;
        }
        assert(rightmost==297); /* 298 is the original font's shadow. */
    }
    assert(!c3_ui_text_right(&ui,299,20,8,"100/100 PP",0xffffff));
    assert(!c3_ui_text_right(&ui,INT_MAX,INT_MIN,120,"99",0xffffff));
    c3_ui_hp(&ui,0,0,150,"UMBREON",0,0);c3_ui_hp(&ui,170,180,150,"ESPEON",UINT_MAX,UINT_MAX);
    /* Both letters of the left label must fit beside the numeric field. */
    assert(ui.pixels[((26u*320u)+17u)*3u]==(C3_UI_WHITE>>16));
    C3GameSummary summary={.name="WES",.floor=1,.money=1000};
    summary.party[0].species=197;summary.party[0].hp=summary.party[0].max_hp=89;
    summary.party[1].species=196;summary.party[1].hp=summary.party[1].max_hp=74;
    summary.party[0].level=26;summary.party[0].experience=0x4a6fu;
    summary.party[1].level=25;summary.party[1].experience=0x3ff4u;
    C3GameSummary original_summary=summary;C3FieldStoryView story={.control=1};
    for(unsigned phase=FIELD_SETUP;phase<=FIELD_CONTROL;++phase)for(unsigned page=PAUSE_CLOSED;page<=PAUSE_MESSAGE;++page){
        c3_ui_begin(&ui,guard+16,0,&a,NULL);
        c3_field_ui_draw_to(&ui,&summary,(FieldPhase)phase,(PausePage)page,0,0,"Saved.",&story,&pager);
        if(phase==FIELD_CONTROL&&page==PAUSE_CLOSED){
            /* Regression: 302px field panel previously exceeded the HP width
             * guard and silently vanished despite valid party data. */
            size_t at=(66u*320u+10u)*3u;assert(ui.pixels[at]==50&&ui.pixels[at+1]==100&&ui.pixels[at+2]==150);
            assert(ui.pixels[(114u*320u+55u)*3u]==0x88);
        }
    }
    story.dialogue=1;story.choice=1;story.choice_no=1;story.message_id=99;
    snprintf(story.message,sizeof story.message,"Would you like to battle?");
    C3FieldStoryView original_story=story;
    c3_field_ui_draw_to(&ui,&summary,FIELD_CONTROL,PAUSE_CLOSED,0,0,"",&story,&pager);
    assert(!memcmp(&story,&original_story,sizeof story)&&!memcmp(&summary,&original_summary,sizeof summary));
    C3BattlePreview *battle=calloc(1,sizeof *battle),*original_battle=malloc(sizeof *battle);assert(battle&&original_battle);
    C3BattlePreviewTables tables={0};memcpy(tables.bytes,"C3P1",4);
    battle->magic=0x43504231;battle->event_count=1;
    snprintf(battle->events[0].text,sizeof battle->events[0].text,"UMBREON used BITE!");
    for(unsigned i=0;i<4;++i){battle->mons[i].hp=89;battle->mons[i].maximum_hp=89;}
    for(unsigned i=0;i<4;++i){battle->mons[0].moves[i].id=44;battle->mons[0].moves[i].pp=20;battle->mons[0].moves[i].maximum_pp=25;}
    for(unsigned i=0;i<2;++i){
        battle->mons[i].species=summary.party[i].species;battle->mons[i].level=summary.party[i].level;
        for(unsigned k=0;k<4;++k)battle->original_request.player_party[i][0x5c+k]=(unsigned char)(summary.party[i].experience>>(24u-k*8u));
    }
    memcpy(original_battle,battle,sizeof *battle);
    for(unsigned page=C3_PREVIEW_MENU_EVENTS;page<=C3_PREVIEW_MENU_EXIT;++page){
        C3BattlePreviewMenu battle_menu={.page=(C3BattlePreviewMenuPage)page,.target=2};
        c3_ui_begin(&ui,guard+16,0,&a,NULL);
        c3_battle_ui_draw_to(&ui,battle,&tables,&battle_menu,NULL,"",&pager);
        assert(ui.pixels[(48u*320u+15u)*3u]==0x88);
        assert(!memcmp(original_battle,battle,sizeof *battle));
    }
    free(battle);free(original_battle);
    for(unsigned k=0;k<16;++k)assert(guard[k]==0xa5&&guard[BYTES+16+k]==0xa5);
    c3_game_menu_close(&menu);free(data);free(guard);free(rotated);
    puts("bottom UI: asset bounds, orientation, clipping, all menu pages and pagination passed");return 0;
}
