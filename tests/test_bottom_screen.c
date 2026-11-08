/* Compile the production publisher with a bounded libctru surface stub. */
#include "bottom_screen.h"
#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_BYTES (320u*240u*3u)
static unsigned char guarded[MAX_BYTES+32u];
static unsigned char expected[MAX_BYTES];
static PrintConsole console;
static int screen_format;
static unsigned flushes;
PrintConsole *consoleGetDefault(void){return &console;}
int gfxGetScreenFormat(int screen){assert(screen==GFX_BOTTOM);return screen_format;}
unsigned char *gfxGetFramebuffer(int screen,int side,uint16_t *width,uint16_t *height)
{assert(screen==GFX_BOTTOM&&side==GFX_LEFT&&!width&&!height);return guarded+16;}
void gfxFlushBuffers(void){++flushes;}
static void unchanged(size_t begin,size_t end)
{for(size_t i=begin;i<end;++i)assert(guarded[i]==0xa5);}
static void exercise(int format)
{
    screen_format=format;size_t bytes=320u*240u*(format==GSP_RGB565_OES?2u:3u);
    memset(guarded,0xa5,sizeof guarded);unsigned count=flushes;
    C3BottomUi *ui=c3_bottom_screen_begin();assert(ui->pixels!=guarded+16);
    assert(ui->rotated_bgr==(format==GSP_RGB565_OES?C3_UI_ROTATED_RGB565:C3_UI_ROTATED_BGR8));
    unchanged(0,sizeof guarded);assert(flushes==count);
    c3_ui_rect(ui,0,0,320,240,0x123456);c3_ui_rect(ui,319,239,1,1,0xff0000);
    unchanged(0,sizeof guarded);assert(flushes==count);memcpy(expected,ui->pixels,bytes);
    c3_bottom_screen_present();assert(flushes==count+1);assert(!memcmp(guarded+16,expected,bytes));
    unchanged(0,16);unchanged(16+bytes,sizeof guarded);
    c3_ui_rect(ui,0,0,320,240,0xabcdef);c3_bottom_screen_present();
    assert(flushes==count+1);assert(!memcmp(guarded+16,expected,bytes));
    c3_bottom_screen_begin();c3_bottom_screen_close();c3_bottom_screen_present();assert(flushes==count+1);
    assert(!memcmp(guarded+16,expected,bytes));unchanged(0,16);unchanged(16+bytes,sizeof guarded);
    c3_bottom_screen_title(1,0);assert(flushes==count+2);unchanged(0,16);unchanged(16+bytes,sizeof guarded);
    c3_bottom_screen_notice("NOTICE","Complete image","B: return");assert(flushes==count+3);
    unchanged(0,16);unchanged(16+bytes,sizeof guarded);
}
int main(void)
{
    c3_bottom_screen_present();assert(!flushes);
    exercise(GSP_RGB565_OES);exercise(GSP_BGR8_OES);
    /* A console-format transition cannot publish stale bytes into its new layout. */
    memset(guarded,0xa5,sizeof guarded);screen_format=GSP_BGR8_OES;c3_bottom_screen_begin();
    unsigned count=flushes;screen_format=GSP_RGB565_OES;c3_bottom_screen_present();
    unchanged(0,sizeof guarded);assert(flushes==count);
    puts("Bottom screen: deferred publication, RGB565/BGR8 copy bounds and wrapper completion passed.");return 0;
}
