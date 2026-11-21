#include "colosseum3ds/bottom_ui.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 32u
#define SPRITE_BYTES 16u
#define GLYPHS 95u
#define GLYPH_TABLE_AT(count) (HEADER + (count) * SPRITE_BYTES)
#define GLYPH_TABLE GLYPH_TABLE_AT(ui->assets->sprites)
#define PAYLOAD_MIN (GLYPH_TABLE_AT(C3_UI_SPRITES_V1) + GLYPHS * 8u)
#define PACK_MAX (1024u * 1024u)

static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }

static uint64_t (*ui_clock)(void);
static C3UiPerf ui_perf;
void c3_ui_perf_clock(uint64_t (*now_us)(void)) { ui_clock = now_us; }
void c3_ui_perf_take(C3UiPerf *out) { if (out) *out = ui_perf; memset(&ui_perf, 0, sizeof ui_perf); }
#define UI_TIMED(field, body) do { uint64_t ui_started_ = ui_clock ? ui_clock() : 0; body; \
    if (ui_clock) ui_perf.field += ui_clock() - ui_started_; } while (0)

int c3_ui_assets_open(C3UiAssets *a,const void *memory,size_t size)
{
    const unsigned char *d=memory;
    C3UiAssets checked={0};
    if(!a||!d||size<PAYLOAD_MIN||size>PACK_MAX||memcmp(d,"C3UI",4))return 0;
    /* Version 1: the ten original sprites. Version 2 appends C3_UI_BALL.
     * Version 3 adds the font's non-ASCII glyphs (count at +20): a table of
     * {code, advance, offset, 0} sorted by code after the ASCII table. */
    uint32_t version=u32(d+4),count=version==1u?C3_UI_SPRITES_V1:version>=2u&&version<=3u?C3_UI_SPRITES:0u;
    uint32_t extra=version==3u?u32(d+20):0u;
    if(!count||u32(d+8)!=size||u32(d+12)!=count||u32(d+16)!=GLYPHS||
       (version!=3u&&u32(d+20))||extra>1024u||u32(d+24)||u32(d+28))return 0;
    size_t payload=GLYPH_TABLE_AT(count)+GLYPHS*8u+extra*16u;
    if(size<payload)return 0;
    size_t next=payload;
    for(unsigned i=0;i<count;++i){
        const unsigned char *e=d+HEADER+i*SPRITE_BYTES;
        uint32_t w=u32(e),h=u32(e+4),offset=u32(e+8),bytes=u32(e+12);
        if(w<8||w>512||h<8||h>512||bytes!=w*h*4u||offset!=next||
           offset>size||bytes>size-offset)return 0;
        next+=bytes;
    }
    for(unsigned i=0;i<GLYPHS;++i){
        const unsigned char *e=d+GLYPH_TABLE_AT(count)+i*8u;
        uint32_t advance=u32(e),offset=u32(e+4);
        if(!advance||advance>16||offset!=next||offset>size||256u>size-offset)return 0;
        next+=256u;
    }
    for(unsigned i=0,previous=126;i<extra;++i){
        const unsigned char *e=d+GLYPH_TABLE_AT(count)+GLYPHS*8u+i*16u;
        uint32_t code=u32(e),advance=u32(e+4),offset=u32(e+8);
        if(code<=previous||code>0xffffu||!advance||advance>16||offset!=next||u32(e+12)||
           offset>size||256u>size-offset)return 0;
        previous=code;next+=256u;
    }
    if(next!=size)return 0;
    checked.data=d;checked.size=size;checked.sprites=count;checked.extra_glyphs=extra;*a=checked;return 1;
}

int c3_ui_sprite_pixels(const C3UiAssets *a,unsigned sprite,
                        const uint8_t **rgba,unsigned *width,unsigned *height)
{
    if(!a||!a->data||sprite>=a->sprites||sprite>=C3_UI_SPRITES||!rgba||!width||!height)return 0;
    const unsigned char *e=a->data+HEADER+sprite*SPRITE_BYTES;
    *width=u32(e);*height=u32(e+4);*rgba=a->data+u32(e+8);
    return 1;
}

void c3_ui_spin_rgba(unsigned char *fb,int screen_w,int screen_h,
                     const uint8_t *src,unsigned sw,unsigned sh,
                     int cx,int cy,int size,float c,float s)
{
    if(!fb||!src||screen_w<=0||screen_h<=0||screen_w>1024||screen_h>1024||
       !sw||!sh||sw>1024||sh>1024||size<=0||size>512||!(c==c)||!(s==s)||
       c>1.0f||c<-1.0f||s>1.0f||s<-1.0f)return;
    /* Inverse mapping: each destination pixel centre rotates back into the
     * source. Coverage is the bilinear alpha; colour is alpha weighted so
     * transparent texels never bleed their RGB into the edge. */
    float scale_x=(float)sw/(float)size,scale_y=(float)sh/(float)size;
    float half=(float)size*0.5f;
    int reach=(int)(half*1.4142136f)+2;
    for(int y=cy-reach;y<=cy+reach;++y){
        if(y<0||y>=screen_h)continue;
        for(int x=cx-reach;x<=cx+reach;++x){
            if(x<0||x>=screen_w)continue;
            float dx=(float)x+0.5f-(float)cx,dy=(float)y+0.5f-(float)cy;
            float u=(c*dx+s*dy+half)*scale_x-0.5f,v=(-s*dx+c*dy+half)*scale_y-0.5f;
            if(u<=-1.0f||v<=-1.0f||u>=(float)sw||v>=(float)sh)continue;
            int u0=(int)(u+1.0f)-1,v0=(int)(v+1.0f)-1; /* floor: u,v > -1 */
            float fu=u-(float)u0,fv=v-(float)v0,acc[4]={0,0,0,0};
            for(int k=0;k<4;++k){
                int tu=u0+(k&1),tv=v0+(k>>1);
                if(tu<0||tv<0||tu>=(int)sw||tv>=(int)sh)continue;
                float w=((k&1)?fu:1.0f-fu)*((k>>1)?fv:1.0f-fv);
                const uint8_t *p=src+((size_t)tv*sw+(size_t)tu)*4u;
                float a=w*(float)p[3];
                acc[0]+=a*(float)p[0];acc[1]+=a*(float)p[1];acc[2]+=a*(float)p[2];acc[3]+=a;
            }
            if(acc[3]<0.5f)continue;
            float alpha=acc[3]/255.0f;
            unsigned char *d=fb+((size_t)x*(size_t)screen_h+(size_t)(screen_h-1-y))*3u;
            for(int ch=0;ch<3;++ch){
                float colour=acc[ch]/acc[3];
                unsigned char *out=d+(2-ch); /* BGR byte order */
                float mixed=colour*alpha+(float)*out*(1.0f-alpha);
                *out=(unsigned char)(mixed<0.0f?0.0f:mixed>255.0f?255.0f:mixed+0.5f);
            }
        }
    }
}

/* The 3DS ARM11 has no divide instruction: every '/' below used to be a
 * library call, three per blended pixel plus one per scaled coordinate, and a
 * battle redraw took 34 ms. (x+1+(x>>8))>>8 equals x/255 for every
 * 0 <= x <= 255*255+127, the full range a blend produces (checked
 * exhaustively), and alpha 255 returns the source colour exactly, so output is
 * byte-identical (tests/test_bottom_ui_golden.c). */
static inline unsigned div255(unsigned x){return (x+1u+(x>>8))>>8;}

static void pixel(C3BottomUi *ui,int x,int y,uint32_t color,unsigned alpha)
{
    if(!ui||!ui->pixels||x<0||x>=320||y<0||y>=240||!alpha)return;
    size_t index=ui->rotated_bgr?(size_t)x*240u+239u-(unsigned)y:(size_t)y*320u+(unsigned)x;
    unsigned r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
    if(ui->rotated_bgr==C3_UI_ROTATED_RGB565){
        unsigned char *p=ui->pixels+index*2u;
        if(alpha<255u){
            unsigned old=(unsigned)p[0]|(unsigned)p[1]<<8;
            unsigned r5=old>>11,g6=(old>>5)&63u,b5=old&31u;
            r=div255(r*alpha+((r5<<3)|(r5>>2))*(255u-alpha)+127u);
            g=div255(g*alpha+((g6<<2)|(g6>>4))*(255u-alpha)+127u);
            b=div255(b*alpha+((b5<<3)|(b5>>2))*(255u-alpha)+127u);
        }
        unsigned next=(r>>3)<<11|(g>>2)<<5|(b>>3);
        p[0]=(unsigned char)next;p[1]=(unsigned char)(next>>8);return;
    }
    unsigned char *p=ui->pixels+index*3u;
    if(ui->rotated_bgr){unsigned swap=r;r=b;b=swap;}
    if(alpha==255u){p[0]=(unsigned char)r;p[1]=(unsigned char)g;p[2]=(unsigned char)b;return;}
    p[0]=(unsigned char)div255(r*alpha+p[0]*(255u-alpha)+127u);
    p[1]=(unsigned char)div255(g*alpha+p[1]*(255u-alpha)+127u);
    p[2]=(unsigned char)div255(b*alpha+p[2]*(255u-alpha)+127u);
}

/* Areas drawn since c3_ui_begin (clipped, exclusive right/bottom). The panel
 * cache only reuses or captures pixels of a panel drawn over untouched
 * backdrop, so a cached copy is exactly what blending would produce. */
static void touch(C3BottomUi *ui,int x,int y,int w,int h)
{
    if(!ui||w<=0||h<=0)return;
    int64_t l=x<0?0:x,t=y<0?0:y,r=(int64_t)x+w>320?320:(int64_t)x+w,b=(int64_t)y+h>240?240:(int64_t)y+h;
    if(l>=r||t>=b)return;
    if(ui->touched_count>=64u){ui->touched_count=65u;return;}
    int16_t *q=ui->touched[ui->touched_count++];
    q[0]=(int16_t)l;q[1]=(int16_t)t;q[2]=(int16_t)r;q[3]=(int16_t)b;
}

static int untouched(const C3BottomUi *ui,int l,int t,int r,int b)
{
    if(ui->touched_count>64u)return 0;
    for(unsigned i=0;i<ui->touched_count;++i){
        const int16_t *q=ui->touched[i];
        if(l<q[2]&&q[0]<r&&t<q[3]&&q[1]<b)return 0;
    }
    return 1;
}

static unsigned bytes_per_pixel(const C3BottomUi *ui){return ui->rotated_bgr==C3_UI_ROTATED_RGB565?2u:3u;}

/* Copy the clipped rectangle between the framebuffer and a packed buffer.
 * Rotated layouts keep a screen column contiguous; RGB8 keeps a row. */
static void copy_rect(C3BottomUi *ui,int l,int t,int r,int b,unsigned char *buffer,int to_screen)
{
    unsigned bpp=bytes_per_pixel(ui);
    if(ui->rotated_bgr){
        size_t run=(size_t)(b-t)*bpp;
        for(int x=l;x<r;++x,buffer+=run){
            unsigned char *p=ui->pixels+((size_t)x*240u+240u-(unsigned)b)*bpp;
            if(to_screen)memcpy(p,buffer,run);else memcpy(buffer,p,run);
        }
        return;
    }
    size_t run=(size_t)(r-l)*3u;
    for(int y=t;y<b;++y,buffer+=run){
        unsigned char *p=ui->pixels+((size_t)y*320u+(unsigned)l)*3u;
        if(to_screen)memcpy(p,buffer,run);else memcpy(buffer,p,run);
    }
}

static void rect_fill(C3BottomUi *ui,int x,int y,int w,int h,uint32_t color);

void c3_ui_rect(C3BottomUi *ui,int x,int y,int w,int h,uint32_t color)
{
    ++ui_perf.rects;
    UI_TIMED(rect_us, rect_fill(ui,x,y,w,h,color));
    touch(ui,x,y,w,h);
}

static void rect_fill(C3BottomUi *ui,int x,int y,int w,int h,uint32_t color)
{
    /* Clip before adding extents, including adversarial host-test inputs. */
    if(w<=0||h<=0||x>=320||y>=240||!ui||!ui->pixels)return;
    int left=x<0?0:x,top=y<0?0:y;
    int right=(int64_t)x+w>320?320:x+w,bottom=(int64_t)y+h>240?240:y+h;
    if(left>=right||top>=bottom)return;
    /* Opaque fill: the bytes pixel() would write, without per-pixel work.
     * In the rotated layouts one screen column is contiguous in memory. */
    unsigned r=(color>>16)&255u,g=(color>>8)&255u,b=color&255u;
    if(ui->rotated_bgr==C3_UI_ROTATED_RGB565){
        unsigned v=(r>>3)<<11|(g>>2)<<5|(b>>3);unsigned char lo=(unsigned char)v,hi=(unsigned char)(v>>8);
        for(int xx=left;xx<right;++xx){
            unsigned char *p=ui->pixels+((size_t)xx*240u+240u-(unsigned)bottom)*2u;
            for(int n=bottom-top;n>0;--n,p+=2){p[0]=lo;p[1]=hi;}
        }
        return;
    }
    if(ui->rotated_bgr){
        for(int xx=left;xx<right;++xx){
            unsigned char *p=ui->pixels+((size_t)xx*240u+240u-(unsigned)bottom)*3u;
            for(int n=bottom-top;n>0;--n,p+=3){p[0]=(unsigned char)b;p[1]=(unsigned char)g;p[2]=(unsigned char)r;}
        }
        return;
    }
    for(int yy=top;yy<bottom;++yy){
        unsigned char *p=ui->pixels+((size_t)yy*320u+(unsigned)left)*3u;
        for(int n=right-left;n>0;--n,p+=3){p[0]=(unsigned char)r;p[1]=(unsigned char)g;p[2]=(unsigned char)b;}
    }
}

void c3_ui_begin(C3BottomUi *ui,unsigned char *pixels,int rotated,
                 const C3UiAssets *assets,const unsigned char *fallback)
{
    if(!ui)return;
    ui->pixels=pixels;ui->rotated_bgr=rotated==C3_UI_ROTATED_RGB565?rotated:!!rotated;ui->assets=assets;ui->fallback_font=fallback;ui->content_inset=0;
    ui->touched_count=0;
    if(!pixels)return;
    /* The original title palette and quiet field backdrop. No frame ownership.
     * It never changes: render it once per layout (240 one-pixel rows are
     * 76,800 scattered writes in a rotated framebuffer, 3.5 ms on a New 3DS)
     * and copy it afterwards. */
    UI_TIMED(begin_us, {
        static unsigned char *backdrop[3];
        size_t bytes=320u*240u*bytes_per_pixel(ui);
        unsigned char *template=backdrop[ui->rotated_bgr];
        if(!template&&(template=malloc(bytes))){
            C3BottomUi plain=*ui;plain.pixels=template;
            for(int y=0;y<240;++y){
                unsigned c=(unsigned)(240-y)/18u;
                rect_fill(&plain,0,y,320,1,((5u+c)<<16)|((12u+c*2u)<<8)|(16u+c*2u));
            }
            backdrop[ui->rotated_bgr]=template;
        }
        if(template)memcpy(pixels,template,bytes);
        else for(int y=0;y<240;++y){
            unsigned c=(unsigned)(240-y)/18u;
            rect_fill(ui,0,y,320,1,((5u+c)<<16)|((12u+c*2u)<<8)|(16u+c*2u));
        }
    });
}

void c3_ui_sprite(C3BottomUi *ui,unsigned id,int x,int y,int w,int h)
{
    if(!ui||!ui->assets||!ui->assets->data||id>=ui->assets->sprites||id>=C3_UI_SPRITES||w<=0||h<=0||w>640||h>480||
       x>=320||y>=240||x < -w||y < -h)return;
    const unsigned char *d=ui->assets->data,*e=d+HEADER+id*SPRITE_BYTES;
    unsigned sw=u32(e),sh=u32(e+4);const unsigned char *src=d+u32(e+8);
    c3_ui_rgba(ui,src,sw,sh,x,y,w,h);
}

static void rgba_blit(C3BottomUi *ui,const uint8_t *src,unsigned sw,unsigned sh,int x,int y,int w,int h);

void c3_ui_rgba(C3BottomUi *ui,const uint8_t *src,unsigned sw,unsigned sh,int x,int y,int w,int h)
{
    ++ui_perf.sprites;
    UI_TIMED(sprite_us, rgba_blit(ui,src,sw,sh,x,y,w,h));
    if(ui&&src&&w>0&&h>0&&w<=640&&h<=480)touch(ui,x,y,w,h);
}

static void rgba_blit(C3BottomUi *ui,const uint8_t *src,unsigned sw,unsigned sh,int x,int y,int w,int h)
{
    if(!ui||!src||!sw||sw>1024||!sh||sh>1024||w<=0||h<=0||w>640||h>480||x>=320||y>=240||x < -w||y < -h)return;
    /* Same nearest mapping as before, divided once per column and row. */
    unsigned columns[640];
    for(int xx=0;xx<w;++xx)columns[xx]=(unsigned)xx*sw/(unsigned)w;
    for(int yy=0;yy<h;++yy){
        if(y+yy<0||y+yy>=240)continue;
        const unsigned char *row=src+(size_t)((unsigned)yy*sh/(unsigned)h)*sw*4u;
        for(int xx=0;xx<w;++xx){
            const unsigned char *p=row+(size_t)columns[xx]*4u;
            pixel(ui,x+xx,y+yy,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
        }
    }
}

static void panel_draw(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h);

/* Finished pixels of panels drawn over untouched backdrop, keyed by
 * everything that determines them. 8 entries, least recently used replaced. */
typedef struct {
    const unsigned char *assets; size_t asset_bytes;
    unsigned sprite; int x,y,w,h,layout;
    unsigned char *pixels; size_t bytes, capacity; unsigned age;
} PanelCache;
static PanelCache panel_cache[8];
static unsigned panel_clock;

static void panel_cached(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h)
{
    if(!ui||!ui->pixels||!ui->assets||!ui->assets->data||sprite>=ui->assets->sprites||sprite>=C3_UI_SPRITES||
       w<12||h<12||w>320||h>240||x>=320||y>=240||x < -w||y < -h){panel_draw(ui,sprite,x,y,w,h);return;}
    int l=x<0?0:x,t=y<0?0:y,r=x+w>320?320:x+w,b=y+h>240?240:y+h;
    if(!untouched(ui,l,t,r,b)){panel_draw(ui,sprite,x,y,w,h);return;}
    size_t bytes=(size_t)(r-l)*(size_t)(b-t)*bytes_per_pixel(ui);
    PanelCache *slot=NULL;
    for(unsigned i=0;i<8u;++i){
        PanelCache *c=&panel_cache[i];
        if(c->pixels&&c->assets==ui->assets->data&&c->asset_bytes==ui->assets->size&&c->sprite==sprite&&
           c->x==x&&c->y==y&&c->w==w&&c->h==h&&c->layout==ui->rotated_bgr&&c->bytes==bytes){
            copy_rect(ui,l,t,r,b,c->pixels,1);
            c->age=++panel_clock;++ui_perf.panel_cache_hits;touch(ui,x,y,w,h);return;
        }
        if(!slot||c->age<slot->age)slot=c;
    }
    panel_draw(ui,sprite,x,y,w,h);
    if(slot->capacity<bytes){
        unsigned char *grown=realloc(slot->pixels,bytes);
        if(!grown)return;
        slot->pixels=grown;slot->capacity=bytes;
    }
    copy_rect(ui,l,t,r,b,slot->pixels,0);
    slot->assets=ui->assets->data;slot->asset_bytes=ui->assets->size;slot->sprite=sprite;
    slot->x=x;slot->y=y;slot->w=w;slot->h=h;slot->layout=ui->rotated_bgr;slot->bytes=bytes;slot->age=++panel_clock;
}

static void panel(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h)
{
    ++ui_perf.panels;
    UI_TIMED(panel_us, panel_cached(ui,sprite,x,y,w,h));
}

static void panel_draw(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h)
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
    /* Source columns once per call, source row once per row (w <= 320). */
    unsigned columns[320];
    for(int xx=0;xx<w;++xx)
        columns[xx]=xx<4?(unsigned)xx*border/4u:xx>=w-4?sw-border+(unsigned)(xx-w+4)*border/4u:
            border+(unsigned)(xx-4)*(sw-2u*border)/(unsigned)(w-8);
    for(int yy=0;yy<h;++yy){
        if(y+yy<0||y+yy>=240)continue;
        unsigned sy=yy<4?(unsigned)yy*border/4u:yy>=h-4?sh-border+(unsigned)(yy-h+4)*border/4u:
            border+(unsigned)(yy-4)*(sh-2u*border)/(unsigned)(h-8);
        const unsigned char *row=src+(size_t)sy*sw*4u;
        for(int xx=0;xx<w;++xx){
            const unsigned char *p=row+(size_t)columns[xx]*4u;
            pixel(ui,x+xx,y+yy,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
        }
    }
    touch(ui,x,y,w,h);
}

void c3_ui_panel(C3BottomUi *ui,int x,int y,int w,int h)
{panel(ui,C3_UI_WINDOW,x,y,w,h);}

/* Glyph pixels for a code point: the ASCII table, then the pack's sorted
 * non-ASCII table (version 3). NULL when the font lacks the character. */
static const unsigned char *glyph_pixels(const C3BottomUi *ui,unsigned c,unsigned *step)
{
    if(!ui->assets||!ui->assets->data)return NULL;
    const unsigned char *d=ui->assets->data;
    if(c>=32&&c<=126){*step=u32(d+GLYPH_TABLE+(c-32u)*8u);return d+u32(d+GLYPH_TABLE+(c-32u)*8u+4);}
    unsigned lo=0,hi=ui->assets->extra_glyphs;
    const unsigned char *table=d+GLYPH_TABLE+GLYPHS*8u;
    while(lo<hi){
        unsigned mid=(lo+hi)/2,code=u32(table+mid*16u);
        if(code==c){*step=u32(table+mid*16u+4);return d+u32(table+mid*16u+8);}
        if(code<c)lo=mid+1;else hi=mid;
    }
    return NULL;
}

static unsigned advance(const C3BottomUi *ui,unsigned c)
{
    unsigned step=0;
    if(ui->assets&&ui->assets->data){
        if(glyph_pixels(ui,c,&step))return step;
        /* gs_msg.c: a code the font lacks advances by the font's cell
         * width (22 at GameCube resolution) and draws nothing. */
        if(c>126)return 11;
        return u32(ui->assets->data+GLYPH_TABLE+('?'-32u)*8u);
    }
    return 8;
}

static void glyph(C3BottomUi *ui,int x,int y,unsigned c,uint32_t color)
{
    ++ui_perf.glyphs;
    unsigned step;
    if(ui->assets&&ui->assets->data){
        const unsigned char *p=glyph_pixels(ui,c,&step);
        if(!p&&c<=126)p=glyph_pixels(ui,'?',&step);
        if(!p)return;
        for(int yy=0;yy<16;++yy)for(int xx=0;xx<16;++xx){
            unsigned a=p[yy*16+xx];
            if(a){pixel(ui,x+xx+1,y+yy+1,0, a*3u/4u);pixel(ui,x+xx,y+yy,color,a);}
        }
    }else if(ui->fallback_font){
        if(c<32||c>126)c='?';
        for(int yy=0;yy<8;++yy)for(int xx=0;xx<8;++xx)
            if(ui->fallback_font[c*8u+(unsigned)yy]&(0x80u>>(unsigned)xx))pixel(ui,x+xx,y+yy+3,color,255);
    }
}

/* msgctrlPalette's colours: the six-entry RGBA table common_rel registers
 * (lbl_80478E8C = REL125 data+0, count at data+0x14A654). */
static const uint32_t message_palette[6]={0xffffffu,0xdcb900u,0x47ff25u,0x005091u,0xffd200u,0x000000u};
static unsigned hex_value(const char *text,unsigned digits,int *ok)
{
    unsigned value=0;
    for(unsigned i=0;i<digits;++i){
        char c=text[i];unsigned d;
        if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='a'&&c<='f')d=(unsigned)(c-'a'+10);
        else if(c>='A'&&c<='F')d=(unsigned)(c-'A'+10);else{*ok=0;return 0;}
        value=value<<4|d;
    }
    return value;
}
/* One character of field text: a UTF-8 code point, or an ESC markup
 * sequence from the message decoder (C3_STORY_TEXT_MARKUP), which has no
 * width. Colour sequences update *color (the original white entry of the
 * palette resets it to the caller's colour). Returns the byte length. */
static size_t next_char(const char *text,size_t pos,unsigned *code,uint32_t *color,uint32_t base)
{
    const unsigned char *s=(const unsigned char *)text+pos;
    if(s[0]==0x1b){
        static const unsigned char lengths[128]={['C']=8,['P']=2,['W']=2,['F']=2,['A']=2,['S']=1};
        unsigned n=s[1]&&s[1]<128?lengths[s[1]]:0,k;int ok=1;
        for(k=0;k<n&&s[2+k];++k){}
        if(k==n&&color){
            if(s[1]=='C'){unsigned rgba=hex_value((const char *)s+2,8,&ok);if(ok)*color=rgba==0xffffffffu?base:rgba>>8;}
            else if(s[1]=='P'){unsigned i=hex_value((const char *)s+2,2,&ok);if(ok)*color=i==0||i>=6?base:message_palette[i];}
        }
        *code=0x1b;return 2u+k;
    }
    if(s[0]<0x80){*code=s[0];return 1;}
    if((s[0]&0xe0)==0xc0&&(s[1]&0xc0)==0x80){*code=(unsigned)(s[0]&0x1f)<<6|(s[1]&0x3f);return 2;}
    if((s[0]&0xf0)==0xe0&&(s[1]&0xc0)==0x80&&(s[2]&0xc0)==0x80){
        *code=(unsigned)(s[0]&0x0f)<<12|(unsigned)(s[1]&0x3f)<<6|(s[2]&0x3f);return 3;}
    *code='?';return 1;
}

/* Visible right edge of a glyph drawn at x=0, its shadow included; 0 when
 * it draws nothing. */
static unsigned glyph_extent(const C3BottomUi *ui,unsigned c)
{
    unsigned extent=0;
    if(ui->assets&&ui->assets->data){
        unsigned step;const unsigned char *p=glyph_pixels(ui,c,&step);
        if(!p&&c<=126)p=glyph_pixels(ui,'?',&step);
        if(p)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)
            if(p[y*16+x]&&x+2u>extent)extent=x+2u;
    }else if(ui->fallback_font){
        unsigned f=c<32||c>126?'?':c;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            if((ui->fallback_font[f*8u+y]&(0x80u>>x))&&x+1u>extent)extent=x+1u;
    }
    return extent;
}

static int text_width(const C3BottomUi *ui,const char *text)
{
    unsigned cursor=0,width=0;
    if(!ui||!text)return 0;
    for(size_t i=0;i<256u&&text[i];){
        unsigned c,extent;
        i+=next_char(text,i,&c,NULL,0);
        if(c=='\n')return 0;
        if(c==0x1b){if(!text[i])return (int)width;continue;}
        extent=glyph_extent(ui,c);
        if(extent&&cursor+extent>width)width=cursor+extent;
        cursor+=advance(ui,c);
        if(cursor>320u||width>320u)return 0;
        if(!text[i])return (int)width;
    }
    return 0;
}

static int text_right_draw(C3BottomUi *ui,int right,int y,int width,const char *text,uint32_t color);
static size_t text_draw(C3BottomUi *ui,int x,int y,int width,int lines,const char *text,uint32_t color,int *rows);

int c3_ui_text_right(C3BottomUi *ui,int right,int y,int width,const char *text,uint32_t color)
{
    int result;
    UI_TIMED(text_us, result=text_right_draw(ui,right,y,width,text,color));
    if(result)touch(ui,right-result,y,result+2,18);
    return result;
}

size_t c3_ui_text(C3BottomUi *ui,int x,int y,int width,int lines,const char *text,uint32_t color)
{
    size_t result;
    UI_TIMED(text_us, result=text_draw(ui,x,y,width,lines,text,color,NULL));
    if(ui&&text&&width>=16&&width<=320&&lines>=1&&lines<=15)touch(ui,x,y,width+2,lines*17+2);
    return result;
}

static int text_right_draw(C3BottomUi *ui,int right,int y,int width,const char *text,uint32_t color)
{
    if(right<0||right>320||y < -240||y>240||width<1||width>320)return 0;
    int measured=text_width(ui,text);
    if(!measured||measured>width)return 0;
    int x=right-measured;uint32_t current=color;
    for(size_t i=0;text[i];){
        unsigned c;i+=next_char(text,i,&c,&current,color);
        if(c==0x1b)continue;
        glyph(ui,x,y,c,current);x+=(int)advance(ui,c);
    }
    return measured;
}

/* Spacing a squeezed line gives up. Word spaces give first (two pixels
 * each), then glyph advances (one pixel each); within each class the cut is
 * spread evenly (Bresenham). Only spaces and glyphs before the line's last
 * visible glyph give anything. [0]: spaces, [1]: glyphs. */
typedef struct { unsigned cut[2], slack[2], acc[2]; } Squeeze;

static void squeeze_start(Squeeze *q,unsigned cut,const unsigned slack[2])
{
    for(int k=0;k<2;++k){q->slack[k]=slack[k];q->acc[k]=slack[k]/2u;}
    q->cut[0]=cut<slack[0]?cut:slack[0];q->cut[1]=cut-q->cut[0];
}

static unsigned squeeze(Squeeze *q,int kind,unsigned units)
{
    unsigned taken=0;
    if(!q->cut[kind])return 0;
    while(units--){q->acc[kind]+=q->cut[kind];if(q->acc[kind]>=q->slack[kind]){q->acc[kind]-=q->slack[kind];++taken;}}
    return taken;
}

/* Lays out the hard line at text+pos (to '\n' or the end) from a line start
 * as text_draw does, giving up cut pixels of spacing. Returns its visible
 * width, or INT_MAX when it is unbounded; reports the slack of each class
 * and the position of its last visible glyph (squeezing stops there). */
static int line_layout(const C3BottomUi *ui,const char *text,size_t pos,unsigned cut,const unsigned slack[2],
                       unsigned slack_out[2],size_t *last_out)
{
    static const unsigned none[2]={0,0};
    int cursor=0,width=0;unsigned units[2]={0,0},pending[2]={0,0};size_t last=pos;Squeeze q;
    squeeze_start(&q,cut,slack?slack:none);
    for(size_t i=pos;i<4096u&&text[i];){
        unsigned c;size_t n=next_char(text,i,&c,NULL,0);
        if(c=='\n')break;
        if(c==0x1b){i+=n;continue;}
        if(c==' '){
            if(cursor){cursor+=(int)advance(ui,c)-(int)squeeze(&q,0,2u);pending[0]+=2u;}
            i+=n;continue;
        }
        unsigned extent=glyph_extent(ui,c);
        if(extent){
            for(int k=0;k<2;++k){units[k]+=pending[k];pending[k]=0;}
            last=i;
            if(cursor+(int)extent>width)width=cursor+(int)extent;
        }
        cursor+=(int)advance(ui,c)-(int)squeeze(&q,1,1u);pending[1]+=1u;i+=n;
        if(cursor>2048)return INT_MAX;
    }
    if(slack_out){slack_out[0]=units[0];slack_out[1]=units[1];}
    if(last_out)*last_out=last;
    return width;
}

/* Original message lines carry hard breaks sized for the GameCube window,
 * whose half-resolution glyphs are about one pixel narrower than these
 * (advance = half the source width rounded up, plus one pixel of spacing).
 * A hard line that does not fit is squeezed by up to two pixels per space
 * and that pixel per glyph rather than wrapped, which would leave an orphan
 * word below it. 1: draw it on one row, squeezed as *q says. */
static int line_fit(const C3BottomUi *ui,const char *text,size_t pos,int width,Squeeze *q,size_t *last)
{
    unsigned slack[2];
    int natural=line_layout(ui,text,pos,0,NULL,slack,last);
    if(natural==INT_MAX)return 0;
    for(unsigned d=natural>width?(unsigned)(natural-width):0u;d<=slack[0]+slack[1];++d)
        if((d?line_layout(ui,text,pos,d,slack,NULL,NULL):natural)<=width){squeeze_start(q,d,slack);return 1;}
    return 0;
}

static size_t text_draw(C3BottomUi *ui,int x,int y,int width,int lines,const char *text,uint32_t color,int *rows)
{
    size_t pos=0,last=0;int cursor=0,line=0,fit=-1;uint32_t current=color;Squeeze q={{0,0},{0,0},{0,0}};
    if(rows)*rows=0;
    if(!ui||!text||width<16||width>320||lines<1||lines>15||x < -320||x>320||y < -240||y>240)return 0;
    /* All callers own NUL-terminated strings. Bound scanning even for large
     * unbroken words; never draw outside the requested text rectangle. */
    while(pos<4096u&&text[pos]&&line<lines){
        /* Each hard line is measured where it starts: one that fits (or
         * fits squeezed) is drawn on one row, any other wraps by words. */
        if(fit<0)fit=line_fit(ui,text,pos,width,&q,&last);
        if(rows)*rows=line+1;
        unsigned c;size_t n=next_char(text,pos,&c,&current,color);
        if(c=='\n'){pos+=n;++line;cursor=0;fit=-1;continue;}
        if(c==' '){
            if(cursor){cursor+=(int)advance(ui,c);if(fit&&pos<last)cursor-=(int)squeeze(&q,0,2u);}
            pos+=n;continue;
        }
        if(c==0x1b){pos+=n;continue;}
        size_t end=pos;unsigned word=0;
        while(end<4096u&&text[end]){
            unsigned w;size_t m=next_char(text,end,&w,NULL,0);
            if(w==' '||w=='\n')break;
            if(w!=0x1b)word+=advance(ui,w);
            end+=m;
        }
        if(!fit&&cursor&&cursor+(int)word>width){++line;cursor=0;if(line>=lines)break;if(rows)*rows=line+1;}
        while(pos<end){
            unsigned g;size_t m=next_char(text,pos,&g,&current,color);
            if(g==0x1b){pos+=m;continue;}
            unsigned step=advance(ui,g);
            /* Reserve a glyph cell, even when its advance is narrower. */
            if(!fit&&cursor+16>width){++line;cursor=0;if(line>=lines)return pos;if(rows)*rows=line+1;}
            glyph(ui,x+cursor,y+line*17,g,current);
            cursor+=(int)step;
            if(fit&&pos<last)cursor-=(int)squeeze(&q,1,1u);
            pos+=m;
        }
    }
    return pos;
}

int c3_ui_text_rows(const C3BottomUi *ui,int width,const char *text)
{
    int rows;
    if(!ui)return 0;
    C3BottomUi measure=*ui;measure.pixels=NULL;
    text_draw(&measure,0,0,width,15,text,0,&rows);
    return rows;
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
    /* The window spans the header's width; text keeps 4 px inside its
     * border (C3_UI_MESSAGE_WIDTH, measured by the message sweep). */
    int inset=ui->content_inset,left=inset?15+inset:5,text_left=inset?26+inset:13;
    c3_ui_panel(ui,left,y,320-2*left,lines*17+20);
    pager->next=pager->offset+c3_ui_text(ui,text_left,y+7,inset?267-2*inset:C3_UI_MESSAGE_WIDTH,lines,text+pager->offset,C3_UI_WHITE);
    pager->more=pager->next<length;
    c3_ui_sprite(ui,C3_UI_ARROW,(inset?288:290)-inset,y+lines*17+3,11,11);
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
    touch(ui,0,62,320,22); /* the scaled heading and its shadow */
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
            for(unsigned y=0;y<58;++y){
                const unsigned char *row=src+(size_t)(y*sh/58u)*sw*4u;
                for(unsigned x=0;x<264;++x){
                    unsigned sx=x<32?x:x>=232?sw-32u+x-232u:24u;
                    const unsigned char *p=row+(size_t)sx*4u;
                    pixel(ui,28+(int)x,94+(int)y,(uint32_t)p[0]<<16|(uint32_t)p[1]<<8|p[2],p[3]);
                }
            }
            touch(ui,28,94,264,58);
        }else c3_ui_panel(ui,28,94,264,58);
        c3_ui_sprite(ui,C3_UI_START,50,109,220,26);
        c3_ui_sprite(ui,C3_UI_HAND,14,114,29,21);
    }
    else c3_ui_text(ui,65,106,220,2,accepted?"Please wait...":waiting?"PRESS START":"A / B / START: advance",C3_UI_WHITE);
    c3_ui_footer(ui,"START + SELECT: exit");
}
