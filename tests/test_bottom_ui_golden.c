/* Byte-exact output of the bottom-screen software renderer.
 *
 * The hashes below were recorded from src/bottom_ui.c before it gained its
 * division-free fast paths (one software division per pixel channel and per
 * scaled coordinate cost 34 ms per redraw on a New 3DS). Any optimisation must
 * reproduce every pixel in all three framebuffer layouts.
 * Run with an argument to print the hashes instead of checking them. */
#include "colosseum3ds/bottom_ui.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE (32u+C3_UI_SPRITES*16u+95u*8u)

static void put32(unsigned char *p,uint32_t n)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}

/* Sprites of varied size with colour and alpha ramps (0..255 included),
 * glyphs with partial alpha, so blending, scaling and nine-slicing all run. */
static unsigned char *fixture(size_t *bytes)
{
    size_t size=TABLE;
    for(unsigned i=0;i<C3_UI_SPRITES;++i){unsigned w=8u+(i*13u)%40u,h=8u+(i*7u)%24u;size+=w*h*4u;}
    size+=95u*256u;
    unsigned char *d=calloc(1,size);assert(d);
    memcpy(d,"C3UI",4);put32(d+4,1);put32(d+8,(uint32_t)size);put32(d+12,C3_UI_SPRITES);put32(d+16,95);
    size_t next=TABLE;
    for(unsigned i=0;i<C3_UI_SPRITES;++i){
        unsigned w=8u+(i*13u)%40u,h=8u+(i*7u)%24u;unsigned char *e=d+32+i*16;
        put32(e,w);put32(e+4,h);put32(e+8,(uint32_t)next);put32(e+12,w*h*4u);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            unsigned char *p=d+next+((size_t)y*w+x)*4u;
            p[0]=(unsigned char)(x*37u+i*11u);p[1]=(unsigned char)(y*53u+i*29u);p[2]=(unsigned char)((x^y)*19u+i);
            p[3]=(unsigned char)((x+y)%5u==0?0:(x+y)%5u==1?255:(x*41u+y*23u+i*7u));
        }
        next+=(size_t)w*h*4u;
    }
    for(unsigned i=0;i<95;++i){
        unsigned char *e=d+32+C3_UI_SPRITES*16+i*8;put32(e,6u+i%9u);put32(e+4,(uint32_t)next);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
            if(((x*3u+y*5u+i)%7u)<3u&&x<12u&&y<14u)d[next+y*16u+x]=(unsigned char)(40u+((x*29u+y*31u+i*13u)%216u));
        next+=256;
    }
    assert(next==size);*bytes=size;return d;
}

static uint64_t fnv(const unsigned char *p,size_t n)
{uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;}

static void scene(C3BottomUi *ui,int which)
{
    static const uint8_t ramp[16]={255,0,0,0, 0,255,0,77, 0,0,255,128, 250,250,250,254};
    C3UiPager pager={0};
    switch(which){
    case 0: c3_ui_title(ui,1,0);break;
    case 1: c3_ui_title(ui,0,1);c3_ui_header(ui,"HEADER TITLE","subtitle text");break;
    case 2: c3_ui_dialogue(ui,&pager,"A long dialogue message that wraps across several lines and keeps going until the pager has more to show on the next page of text.",1,1);break;
    case 3: ui->content_inset=12;c3_ui_row_value(ui,60,"Selected row","99",1,0);c3_ui_row_value(ui,84,"Disabled row","7 / 9",0,1);
            c3_ui_row(ui,108,"Plain row",0,0);c3_ui_footer(ui,"A: confirm   B: back");break;
    case 4: c3_ui_hp(ui,7,60,150,"UMBREON",86,86);c3_ui_hp(ui,163,60,150,"ESPEON",30,75);
            c3_ui_hp(ui,7,110,150,"ZIGZAGOON A",9,52);c3_ui_hp(ui,163,110,150,"ZIGZAGOON B",0,52);
            c3_ui_experience(ui,10,160,200,37,100);c3_ui_experience(ui,10,170,301,5,0);break;
    case 5: c3_ui_rgba(ui,ramp,2,2,-7,-5,333,251);c3_ui_rgba(ui,ramp,2,2,100,100,3,1);
            c3_ui_sprite(ui,3,-10,200,55,61);c3_ui_sprite(ui,C3_UI_HAND,300,5,40,30);c3_ui_sprite(ui,7,12,12,5,9);break;
    case 6: c3_ui_panel(ui,-20,-30,200,120);c3_ui_panel(ui,250,180,90,70);c3_ui_panel(ui,40,40,13,12);
            c3_ui_rect(ui,-5,100,400,3,0x123456);c3_ui_text_right(ui,310,200,200,"RIGHT 12/34",0xabcdef);break;
    case 7: c3_ui_choices(ui,0);c3_ui_message(ui,&pager,"short",20,2);c3_ui_text(ui,300,230,16,2,"clipped text",0xffffff);break;
    }
}

int main(int argc,char **argv)
{
    (void)argv;
    size_t size;unsigned char *data=fixture(&size);C3UiAssets assets={0};
    assert(c3_ui_assets_open(&assets,data,size));
    static const uint64_t expected[3][8]={
        {0xc8cf0e41cea44dbaull,0x4633e9d6897b0b22ull,0x6b09407930bb89ceull,0x796eca83f867b5c1ull,
         0xf22ec3f5392215bcull,0xfb8b8fa6499d8e99ull,0xb0320a5e867a703cull,0x4e704d9245d77e25ull},
        {0x80e32f421452f518ull,0x66a91e8684ed1e9eull,0xd47a36d490e1122cull,0xa6b409bbb6c7f41dull,
         0x261c1b3311fcb6e4ull,0xfe573c092a240a35ull,0x62cdd5ae2488caaaull,0x854d94d201928e93ull},
        {0xdd65b6d0181a90e6ull,0xdb0b8a6fe5880bdbull,0x59b4f2ff98ee88b8ull,0x2227a3562f81995dull,
         0x33ae10cced60a531ull,0xb5e3a6a22a2b2b9cull,0x9f73455fd42bd9f9ull,0xebc6d220e9ce3f93ull}};
    static const int layouts[3]={0,1,C3_UI_ROTATED_RGB565};
    unsigned char *frame=malloc(320u*240u*3u+64u);assert(frame);
    int print=argc>1,failures=0;
    /* Every scene twice: the second pass copies cached panels and the backdrop
     * template, and must still reproduce the recorded bytes. */
    c3_ui_perf_take(NULL);
    for(unsigned l=0;l<3;++l)for(int s=0;s<8;++s)for(int pass=0;pass<2;++pass){
        size_t bytes=320u*240u*(layouts[l]==C3_UI_ROTATED_RGB565?2u:3u);
        memset(frame,0xa5,320u*240u*3u+64u);
        C3BottomUi ui;c3_ui_begin(&ui,frame+32,layouts[l],&assets,NULL);
        if(s==7){C3BottomUi fallback;c3_ui_begin(&fallback,frame+32,layouts[l],NULL,NULL);ui=fallback;}
        scene(&ui,s);
        for(unsigned k=0;k<32;++k)assert(frame[k]==0xa5&&frame[32+bytes+k]==0xa5);
        uint64_t h=fnv(frame+32,bytes);
        if(print){if(!pass)printf("layout %u scene %d 0x%016llxull\n",l,s,(unsigned long long)h);}
        else if(h!=expected[l][s]){printf("layout %u scene %d pass %d: 0x%016llx != 0x%016llx\n",l,s,pass,(unsigned long long)h,(unsigned long long)expected[l][s]);++failures;}
    }
    C3UiPerf perf;c3_ui_perf_take(&perf);
    /* Scenes 1-4 and 6 draw panels over untouched backdrop (6 also over a panel). */
    if(!print&&perf.panel_cache_hits<10u){printf("panel cache hits: %u\n",perf.panel_cache_hits);++failures;}
    /* The fallback 8x8 font path. */
    {C3BottomUi ui;const unsigned char *font=data+TABLE;c3_ui_begin(&ui,frame+32,1,NULL,font);
     c3_ui_text(&ui,10,10,300,3,"FALLBACK FONT 0123",0x88ff00);c3_ui_title(&ui,0,0);
     uint64_t h=fnv(frame+32,320u*240u*3u);if(print)printf("fallback 0x%016llxull\n",(unsigned long long)h);
     else if(h!=0x5dafc071c536681full){printf("fallback: 0x%016llx\n",(unsigned long long)h);++failures;}}
    free(frame);free(data);
    if(!print&&failures)return 1;
    if(!print)printf("bottom UI golden: 49 framebuffers byte-identical, %u panels from cache\n",perf.panel_cache_hits);
    return 0;
}
