#include "bottom_screen.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *pack;
static C3UiAssets assets;
static C3BottomUi screen;
/* Drawing can span several scanouts. Only publish the completed CPU image. */
static unsigned char frame[320u*240u*3u];
static size_t pending_bytes;
static int pending_format;


void c3_bottom_screen_init(void)
{
    FILE *f=fopen("sdmc:/3ds/Colosseum3DS/boot/ui.c3ui","rb");
    if(!f||pack){if(f)fclose(f);return;}
    if(fseek(f,0,SEEK_END)){fclose(f);return;}
    long size=ftell(f);
    if(size<32||size>1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return;}
    unsigned char *data=malloc((size_t)size);
    if(data&&fread(data,1,(size_t)size,f)==(size_t)size&&!ferror(f)&&
       c3_ui_assets_open(&assets,data,(size_t)size))pack=data;
    else free(data);
    fclose(f);
}

void c3_bottom_screen_close(void)
{
    free(pack);pack=NULL;assets=(C3UiAssets){0};pending_bytes=0;
}

int c3_bottom_screen_has_original_assets(void){return pack!=NULL;}

C3BottomUi *c3_bottom_screen_begin(void)
{
    const ConsoleFont *font=&consoleGetDefault()->font;
    int format=gfxGetScreenFormat(GFX_BOTTOM)==GSP_RGB565_OES?C3_UI_ROTATED_RGB565:C3_UI_ROTATED_BGR8;
    pending_format=format;pending_bytes=320u*240u*(format==C3_UI_ROTATED_RGB565?2u:3u);
    c3_ui_begin(&screen,frame,format,&assets,
        font->gfx&&font->asciiOffset==0&&font->numChars>=128?font->gfx:NULL);
    return &screen;
}

void c3_bottom_screen_present(void)
{
    if(!pending_bytes)return;
    int format=gfxGetScreenFormat(GFX_BOTTOM)==GSP_RGB565_OES?C3_UI_ROTATED_RGB565:C3_UI_ROTATED_BGR8;
    unsigned char *pixels=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,NULL,NULL);
    if(pixels&&format==pending_format){memcpy(pixels,frame,pending_bytes);gfxFlushBuffers();}
    pending_bytes=0;
}

void c3_bottom_screen_title(int waiting,int accepted)
{
    C3BottomUi *ui=c3_bottom_screen_begin();
    c3_ui_title(ui,waiting,accepted);
    c3_bottom_screen_present();
}

void c3_bottom_screen_notice(const char *title,const char *text,const char *footer)
{
    C3BottomUi *ui=c3_bottom_screen_begin();
    c3_ui_header(ui,title,"");c3_ui_panel(ui,7,55,306,157);
    c3_ui_text(ui,18,63,283,8,text,C3_UI_WHITE);
    c3_ui_footer(ui,footer);
    c3_bottom_screen_present();
}
