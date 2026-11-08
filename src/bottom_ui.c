#include "colosseum3ds/bottom_ui.h"
#include <stdio.h>
#include <string.h>

#define HEADER 32u
#define SPRITE_BYTES 16u
#define GLYPHS 95u
#define GLYPH_TABLE (HEADER + C3_UI_SPRITES * SPRITE_BYTES)
#define PAYLOAD (GLYPH_TABLE + GLYPHS * 8u)
#define PACK_MAX (1024u * 1024u)

static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }

int c3_ui_assets_open(C3UiAssets *a,const void *memory,size_t size)
{
    const unsigned char *d=memory;
    C3UiAssets checked={0};
    if(!a||!d||size<PAYLOAD||size>PACK_MAX||memcmp(d,"C3UI",4)||u32(d+4)!=1||
       u32(d+8)!=size||u32(d+12)!=C3_UI_SPRITES||u32(d+16)!=GLYPHS||
       u32(d+20)||u32(d+24)||u32(d+28))return 0;
    size_t next=PAYLOAD;
    for(unsigned i=0;i<C3_UI_SPRITES;++i){
        const unsigned char *e=d+HEADER+i*SPRITE_BYTES;
        uint32_t w=u32(e),h=u32(e+4),offset=u32(e+8),bytes=u32(e+12);
        if(w<8||w>512||h<8||h>512||bytes!=w*h*4u||offset!=next||
           offset>size||bytes>size-offset)return 0;
        next+=bytes;
    }
    for(unsigned i=0;i<GLYPHS;++i){
        const unsigned char *e=d+GLYPH_TABLE+i*8u;
        uint32_t advance=u32(e),offset=u32(e+4);
        if(!advance||advance>16||offset!=next||offset>size||256u>size-offset)return 0;
        next+=256u;
    }
    if(next!=size)return 0;
    checked.data=d;checked.size=size;*a=checked;return 1;
}

static void pixel(C3BottomUi *ui,int x,int y,uint32_t color,unsigned alpha)
{
    if(!ui||!ui->pixels||x<0||x>=320||y<0||y>=240||!alpha)return;
    size_t index=ui->rotated_bgr?(size_t)x*240u+239u-(unsigned)y:(size_t)y*320u+(unsigned)x;
    if(ui->rotated_bgr==C3_UI_ROTATED_RGB565){
        unsigned char *p=ui->pixels+index*2u;
        unsigned old=(unsigned)p[0]|(unsigned)p[1]<<8;
        unsigned r5=old>>11,g6=(old>>5)&63u,b5=old&31u;
        unsigned r=((((color>>16)&255u)*alpha)+((r5<<3)|(r5>>2))*(255u-alpha)+127u)/255u;
        unsigned g=((((color>>8)&255u)*alpha)+((g6<<2)|(g6>>4))*(255u-alpha)+127u)/255u;
        unsigned b=(((color&255u)*alpha)+((b5<<3)|(b5>>2))*(255u-alpha)+127u)/255u;
        unsigned next=(r>>3)<<11|(g>>2)<<5|(b>>3);
        p[0]=(unsigned char)next;p[1]=(unsigned char)(next>>8);return;
    }
    size_t at=index*3u;
    unsigned char *p=ui->pixels+at;
    unsigned r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
    if(ui->rotated_bgr){unsigned swap=r;r=b;b=swap;}
    p[0]=(unsigned char)((r*alpha+p[0]*(255u-alpha)+127u)/255u);
    p[1]=(unsigned char)((g*alpha+p[1]*(255u-alpha)+127u)/255u);
    p[2]=(unsigned char)((b*alpha+p[2]*(255u-alpha)+127u)/255u);
}

void c3_ui_rect(C3BottomUi *ui,int x,int y,int w,int h,uint32_t color)
{
    /* Clip before adding extents, including adversarial host-test inputs. */
    if(w<=0||h<=0||x>=320||y>=240)return;
    int left=x<0?0:x,top=y<0?0:y;
    int right=(int64_t)x+w>320?320:x+w,bottom=(int64_t)y+h>240?240:y+h;
    for(int yy=top;yy<bottom;++yy)for(int xx=left;xx<right;++xx)pixel(ui,xx,yy,color,255);
}

void c3_ui_begin(C3BottomUi *ui,unsigned char *pixels,int rotated,
                 const C3UiAssets *assets,const unsigned char *fallback)
{
    if(!ui)return;
    ui->pixels=pixels;ui->rotated_bgr=rotated==C3_UI_ROTATED_RGB565?rotated:!!rotated;ui->assets=assets;ui->fallback_font=fallback;ui->content_inset=0;
    /* The original title palette and quiet field backdrop. No frame ownership. */
    for(int y=0;y<240;++y){
        unsigned c=(unsigned)(240-y)/18u;
        c3_ui_rect(ui,0,y,320,1,((5u+c)<<16)|((12u+c*2u)<<8)|(16u+c*2u));
    }
}

void c3_ui_sprite(C3BottomUi *ui,unsigned id,int x,int y,int w,int h)
{
    if(!ui||!ui->assets||!ui->assets->data||id>=C3_UI_SPRITES||w<=0||h<=0||w>640||h>480||
       x>=320||y>=240||x < -w||y < -h)return;
    const unsigned char *d=ui->assets->data,*e=d+HEADER+id*SPRITE_BYTES;
    unsigned sw=u32(e),sh=u32(e+4);const unsigned char *src=d+u32(e+8);
    c3_ui_rgba(ui,src,sw,sh,x,y,w,h);
}

void c3_ui_rgba(C3BottomUi *ui,const uint8_t *src,unsigned sw,unsigned sh,int x,int y,int w,int h)
{
    if(!ui||!src||!sw||sw>1024||!sh||sh>1024||w<=0||h<=0||w>640||h>480||x>=320||y>=240||x < -w||y < -h)return;
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        const unsigned char *p=src+((size_t)((unsigned)yy*sh/(unsigned)h)*sw+(unsigned)xx*sw/(unsigned)w)*4u;
        pixel(ui,x+xx,y+yy,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
    }
}

static void panel(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h)
{
    if(w<12||h<12||w>320||h>240||x>=320||y>=240||x < -w||y < -h)return;
    if(!ui||!ui->assets||!ui->assets->data){
        c3_ui_rect(ui,x,y,w,h,0x87968au);c3_ui_rect(ui,x+1,y+1,w-2,h-2,0x12251bu);return;
    }
    const unsigned char *d=ui->assets->data,*e=d+HEADER+sprite*SPRITE_BYTES;
    unsigned sw=u32(e),sh=u32(e+4);const unsigned char *src=d+u32(e+8);
    /* Nine-slice the source window. Keep its corners/border, stretch only the
     * middle texels; the original 640x480 window is reflowed, not letterboxed. */
    unsigned border=sprite==C3_UI_HP_FRAME&&sw>=16&&sh>=16?8u:4u;
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
        unsigned sx=xx<4?(unsigned)xx*border/4u:xx>=w-4?sw-border+(unsigned)(xx-w+4)*border/4u:
            border+(unsigned)(xx-4)*(sw-2u*border)/(unsigned)(w-8);
        unsigned sy=yy<4?(unsigned)yy*border/4u:yy>=h-4?sh-border+(unsigned)(yy-h+4)*border/4u:
            border+(unsigned)(yy-4)*(sh-2u*border)/(unsigned)(h-8);
        const unsigned char *p=src+((size_t)sy*sw+sx)*4u;
        pixel(ui,x+xx,y+yy,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
    }
}

void c3_ui_panel(C3BottomUi *ui,int x,int y,int w,int h)
{panel(ui,C3_UI_WINDOW,x,y,w,h);}

static unsigned advance(const C3BottomUi *ui,unsigned c)
{
    if(c<32||c>126)c='?';
    if(ui->assets&&ui->assets->data)return u32(ui->assets->data+GLYPH_TABLE+(c-32u)*8u);
    return 8;
}

static void glyph(C3BottomUi *ui,int x,int y,unsigned c,uint32_t color)
{
    if(c<32||c>126)c='?';
    if(ui->assets&&ui->assets->data){
        const unsigned char *d=ui->assets->data,*p=d+u32(d+GLYPH_TABLE+(c-32u)*8u+4);
        for(int yy=0;yy<16;++yy)for(int xx=0;xx<16;++xx){
            unsigned a=p[yy*16+xx];
            if(a){pixel(ui,x+xx+1,y+yy+1,0, a*3u/4u);pixel(ui,x+xx,y+yy,color,a);}
        }
    }else if(ui->fallback_font){
        for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx)
            if(ui->fallback_font[c*8u+(unsigned)yy]&(0x80u>>(unsigned)xx))pixel(ui,x+xx,y+yy+3,color,255);
    }
}

static int text_width(const C3BottomUi *ui,const char *text)
{
    unsigned cursor=0,width=0;
    if(!ui||!text)return 0;
    for(unsigned i=0;i<128u&&text[i];++i){
        unsigned c=(unsigned char)text[i],extent=0;
        if(c=='\n')return 0;
        if(c<32||c>126)c='?';
        if(ui->assets&&ui->assets->data){
            const unsigned char *d=ui->assets->data,*p=d+u32(d+GLYPH_TABLE+(c-32u)*8u+4);
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
                if(p[y*16+x]&&x+2u>extent)extent=x+2u;
        }else if(ui->fallback_font){
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
                if((ui->fallback_font[c*8u+y]&(0x80u>>x))&&x+1u>extent)extent=x+1u;
        }
        if(extent&&cursor+extent>width)width=cursor+extent;
        cursor+=advance(ui,c);
        if(cursor>320u||width>320u)return 0;
        if(!text[i+1])return (int)width;
    }
    return 0;
}

int c3_ui_text_right(C3BottomUi *ui,int right,int y,int width,const char *text,uint32_t color)
{
    if(right<0||right>320||y < -240||y>240||width<1||width>320)return 0;
    int measured=text_width(ui,text);
    if(!measured||measured>width)return 0;
    int x=right-measured;
    for(unsigned i=0;text[i];++i){glyph(ui,x,y,(unsigned char)text[i],color);x+=(int)advance(ui,(unsigned char)text[i]);}
    return measured;
}

size_t c3_ui_text(C3BottomUi *ui,int x,int y,int width,int lines,const char *text,uint32_t color)
{
    size_t pos=0;int cursor=0,line=0;
    if(!ui||!text||width<16||width>320||lines<1||lines>15||x < -320||x>320||y < -240||y>240)return 0;
    /* All callers own NUL-terminated strings. Bound scanning even for large
     * unbroken words; never draw outside the requested text rectangle. */
    while(pos<4096u&&text[pos]&&line<lines){
        unsigned c=(unsigned char)text[pos];
        if(c=='\n'){++pos;++line;cursor=0;continue;}
        if(c==' '){if(cursor)cursor+=(int)advance(ui,c);++pos;continue;}
        size_t end=pos;unsigned word=0;
        while(end<4096u&&text[end]&&text[end]!=' '&&text[end]!='\n')word+=advance(ui,(unsigned char)text[end++]);
        if(cursor&&cursor+(int)word>width){++line;cursor=0;if(line>=lines)break;}
        while(pos<end){
            unsigned step=advance(ui,(unsigned char)text[pos]);
            /* Reserve a glyph cell, even when its advance is narrower. */
            if(cursor+16>width){++line;cursor=0;if(line>=lines)return pos;}
            glyph(ui,x+cursor,y+line*17,(unsigned char)text[pos],color);
            cursor+=(int)step;++pos;
        }
    }
    return pos;
}

void c3_ui_header(C3BottomUi *ui,const char *title,const char *subtitle)
{
    c3_ui_panel(ui,5,5,310,45);
    c3_ui_text(ui,15,7,289,1,title,C3_UI_WHITE);
    c3_ui_text(ui,15,26,289,1,subtitle?subtitle:"",C3_UI_MUTED);
}

void c3_ui_row_value(C3BottomUi *ui,int y,const char *label,const char *value,int selected,int disabled)
{
    int inset=ui->content_inset;
    if(selected){
        c3_ui_rect(ui,15+inset,y,290-2*inset,19,0x30494bu);
        c3_ui_rect(ui,15+inset,y,2,19,C3_UI_GOLD);
        c3_ui_sprite(ui,C3_UI_HAND,19+inset,y+1,22,16);
    }
    int left=(selected?45:26)+inset,right=299-inset,value_width=text_width(ui,value);
    uint32_t color=disabled?C3_UI_MUTED:C3_UI_WHITE;
    c3_ui_text(ui,left,y,right-left-(value_width?value_width+10:0),1,label,color);
    if(value_width)c3_ui_text_right(ui,right,y,120,value,color);
}

void c3_ui_row(C3BottomUi *ui,int y,const char *label,int selected,int disabled)
{c3_ui_row_value(ui,y,label,NULL,selected,disabled);}

void c3_ui_footer(C3BottomUi *ui,const char *text)
{
    c3_ui_rect(ui,0,220,320,20,0x070f14u);
    c3_ui_text(ui,10+ui->content_inset,222,300-2*ui->content_inset,1,text,C3_UI_MUTED);
}

void c3_ui_choices(C3BottomUi *ui,int yes)
{
    c3_ui_panel(ui,180,165,130,50);
    c3_ui_text(ui,yes?205:191,167,100,1,"YES",yes?C3_UI_GOLD:C3_UI_WHITE);
    c3_ui_text(ui,yes?191:205,189,100,1,"NO",yes?C3_UI_WHITE:C3_UI_GOLD);
    c3_ui_sprite(ui,C3_UI_HAND,182,yes?170:192,20,15);
}

void c3_ui_hp(C3BottomUi *ui,int x,int y,int width,const char *name,unsigned hp,unsigned max)
{
    char value[48];
    if(width<80||width>310)return;
    panel(ui,C3_UI_HP_FRAME,x,y,width,43);
    c3_ui_text(ui,x+8,y+4,width-16,1,name,C3_UI_WHITE);
    c3_ui_rect(ui,x+9,y+21,width-18,5,0x111811u);
    unsigned filled=max?(unsigned)((uint64_t)(hp>max?max:hp)*(unsigned)(width-20)/max):0;
    uint32_t color=!max||(uint64_t)hp*5u<=max?0xe25e42u:(uint64_t)hp*2u<=max?0xe8cb42u:0x50c65eu;
    c3_ui_rect(ui,x+10,y+22,(int)filled,3,color);
    c3_ui_text(ui,x+9,y+26,32,1,"HP",C3_UI_WHITE);
    snprintf(value,sizeof value,"%u / %u",hp,max);
    c3_ui_text_right(ui,x+width-10,y+26,width-44,value,C3_UI_WHITE);
}

void c3_ui_experience(C3BottomUi *ui,int x,int y,int width,uint32_t progress,uint32_t range)
{
    if(width<3||width>310||x < -310||x>320||y < -3||y>240)return;
    c3_ui_rect(ui,x,y,width,3,0x8994b9u);
    c3_ui_rect(ui,x+1,y+1,width-2,1,0x15192bu);
    unsigned filled=range?(unsigned)((uint64_t)(progress>range?range:progress)*(unsigned)(width-2)/range):0;
    c3_ui_rect(ui,x+1,y+1,(int)filled,1,0x8883efu);
}

void c3_ui_pager_sync(C3UiPager *pager,uint32_t identity)
{
    if(pager&&pager->identity!=identity){*pager=(C3UiPager){0};pager->identity=identity;}
}

int c3_ui_pager_advance(C3UiPager *pager,uint16_t *pressed)
{
    if(!pager||!pressed||!pager->more)return 0;
    *pressed&=(uint16_t)~0x000fu;
    if(!(*pressed&0x0300u))return 0;
    pager->offset=pager->next;pager->more=0;
    /* Choices must remain inaccessible until the final page is visible. */
    *pressed&=(uint16_t)~(0x0300u|0x000fu);
    return 1;
}

void c3_ui_message(C3BottomUi *ui,C3UiPager *pager,const char *text,int y,int lines)
{
    if(!ui||!pager||!text||lines<1||lines>8||y<0||y+lines*17+20>220)return;
    size_t length=strlen(text);
    if(pager->offset>length)pager->offset=0;
    int inset=ui->content_inset,left=inset?15+inset:7,text_left=inset?26+inset:18;
    c3_ui_panel(ui,left,y,320-2*left,lines*17+20);
    pager->next=pager->offset+c3_ui_text(ui,text_left,y+7,inset?267-2*inset:283,lines,text+pager->offset,C3_UI_WHITE);
    pager->more=pager->next<length;
    c3_ui_sprite(ui,C3_UI_ARROW,288-inset,y+lines*17+3,11,11);
}

void c3_ui_dialogue(C3BottomUi *ui,C3UiPager *pager,const char *text,int choices,int yes)
{
    c3_ui_message(ui,pager,text,55,5);
    if(!pager)return;
    if(pager->more){
        c3_ui_footer(ui,"A / B: more");
    }else if(choices){c3_ui_choices(ui,yes);c3_ui_footer(ui,"D-pad: choose   A: confirm   B: no");}
    else c3_ui_footer(ui,"A / B: continue");
}

static void title_heading(C3BottomUi *ui)
{
    const char *text="POKEMON COLOSSEUM";
    int measured=text_width(ui,text);
    if(ui->assets&&ui->assets->data&&measured)--measured;
    int width=(measured*5+3)/4,left=(320-width)/2;
    unsigned cursor=0;
    for(unsigned i=0;text[i];++i){
        unsigned c=(unsigned char)text[i];
        const unsigned char *p=NULL;
        if(ui->assets&&ui->assets->data){const unsigned char *d=ui->assets->data;p=d+u32(d+GLYPH_TABLE+(c-32u)*8u+4);}
        for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x){
            unsigned sx=x*4u/5u,sy=y*4u/5u,a=0;
            if(p)a=p[sy*16u+sx];
            else if(ui->fallback_font&&sx<8&&sy>=3&&sy<11)
                a=(ui->fallback_font[c*8u+sy-3u]&(0x80u>>sx))?255u:0;
            if(a){int xx=left+(int)(cursor*5u/4u+x),yy=62+(int)y;
                if(p)pixel(ui,xx+1,yy+1,0,a*3u/4u);
                pixel(ui,xx,yy,C3_UI_WHITE,a);
            }
        }
        cursor+=advance(ui,c);
    }
}

void c3_ui_title(C3BottomUi *ui,int waiting,int accepted)
{
    if(!ui)return;
    title_heading(ui);
    if(waiting&&ui->assets&&ui->assets->data){
        const unsigned char *d=ui->assets->data,*e=d+HEADER+C3_UI_QUIT*SPRITE_BYTES;
        unsigned sw=u32(e),sh=u32(e+4);const unsigned char *src=d+u32(e+8);
        if(sw>=96){
            /* Reuse the main menu's rounded blue button: preserve its end
             * caps and stretch a text-free vertical strip behind PRESS START. */
            for(unsigned y=0;y<58;++y)for(unsigned x=0;x<264;++x){
                unsigned sx=x<32?x:x>=232?sw-32u+x-232u:24u;
                const unsigned char *p=src+((size_t)(y*sh/58u)*sw+sx)*4u;
                pixel(ui,28+(int)x,94+(int)y,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
            }
        }else c3_ui_panel(ui,28,94,264,58);
        c3_ui_sprite(ui,C3_UI_START,50,109,220,26);
        c3_ui_sprite(ui,C3_UI_HAND,14,114,29,21);
    }
    else c3_ui_text(ui,65,106,220,2,accepted?"Please wait...":waiting?"PRESS START":"A / B / START: advance",C3_UI_WHITE);
    c3_ui_footer(ui,"START + SELECT: exit");
}
