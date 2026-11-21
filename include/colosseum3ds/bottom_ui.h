#ifndef C3_BOTTOM_UI_H
#define C3_BOTTOM_UI_H
#include <stddef.h>
#include <stdint.h>

/* Borrowed, validated private asset pack; no GPU or frame ownership.
 * sprites: entries in the pack's sprite table (version 1: 10, version 2:
 * 11, adding C3_UI_BALL). Older packs keep working without the extras. */
typedef struct { const unsigned char *data; size_t size; unsigned sprites, extra_glyphs; } C3UiAssets;
typedef struct {
    unsigned char *pixels;
    const C3UiAssets *assets;
    const unsigned char *fallback_font; /* libctru 8x8 ASCII, borrowed */
    int rotated_bgr; /* 0: RGB8, 1: rotated BGR8, 2: rotated RGB565. */
    int content_inset; /* Additional horizontal clearance for framed menus. */
    /* Maintained by the drawing functions since c3_ui_begin: areas no longer
     * showing the plain backdrop. A panel drawn over untouched backdrop is
     * cached and later copied instead of re-blended. */
    int16_t touched[64][4];
    unsigned touched_count; /* > 64: everything counts as touched */
} C3BottomUi;
typedef struct {
    uint32_t identity;
    size_t offset,next;
    int more;
} C3UiPager;
enum { C3_UI_STORY, C3_UI_BATTLE, C3_UI_OPTIONS, C3_UI_QUIT,
       C3_UI_HAND, C3_UI_START, C3_UI_WINDOW, C3_UI_HP_FRAME,
       C3_UI_MOVE_FRAME, C3_UI_ARROW, C3_UI_BALL, C3_UI_SPRITES };
#define C3_UI_SPRITES_V1 10u
#define C3_UI_WHITE 0xf5f5eeu
#define C3_UI_MUTED 0xaab7b5u
#define C3_UI_GOLD 0xffdc63u
enum { C3_UI_RGB8, C3_UI_ROTATED_BGR8, C3_UI_ROTATED_RGB565 };
int c3_ui_assets_open(C3UiAssets *assets,const void *data,size_t size);
/* Borrowed RGBA8 pixels of one sprite; 0 when the pack lacks it. */
int c3_ui_sprite_pixels(const C3UiAssets *assets,unsigned sprite,
                        const uint8_t **rgba,unsigned *width,unsigned *height);
/* Blend rgba (sw x sh) rotated by the angle whose cosine and sine are given
 * (clockwise on screen, y down), scaled to size x size and
 * centred on (cx,cy), into a rotated BGR8 framebuffer (libctru layout:
 * screen_w columns of screen_h pixels, bottom to top). Bilinear, clipped. */
void c3_ui_spin_rgba(unsigned char *rotated_bgr8,int screen_w,int screen_h,
                     const uint8_t *rgba,unsigned sw,unsigned sh,
                     int cx,int cy,int size,float cosine,float sine);
/* Interval counters per drawing primitive, timed only while a microsecond
 * clock is installed (counts are always kept). take copies and clears. */
typedef struct {
    uint64_t begin_us, panel_us, text_us, rect_us, sprite_us;
    uint32_t panels, glyphs, rects, sprites;
    uint32_t panel_cache_hits; /* panels copied from the cache */
} C3UiPerf;
void c3_ui_perf_clock(uint64_t (*now_us)(void));
void c3_ui_perf_take(C3UiPerf *out);
void c3_ui_begin(C3BottomUi *ui,unsigned char *pixels,int rotated_bgr,
                 const C3UiAssets *assets,const unsigned char *fallback_font);
void c3_ui_rect(C3BottomUi *ui,int x,int y,int w,int h,uint32_t color);
void c3_ui_sprite(C3BottomUi *ui,unsigned sprite,int x,int y,int w,int h);
void c3_ui_rgba(C3BottomUi *ui,const uint8_t *rgba,unsigned sw,unsigned sh,int x,int y,int w,int h);
void c3_ui_panel(C3BottomUi *ui,int x,int y,int w,int h);
/* Returns the number of source bytes displayed. Explicit pagination must use
 * this offset before advancing the underlying story/battle message. */
size_t c3_ui_text(C3BottomUi *ui,int x,int y,int width,int lines,const char *text,uint32_t color);
/* Rows c3_ui_text would use for text at this width (up to 15), drawing
 * nothing. A hard line ('\n' separated) that fits, or fits with its spacing
 * squeezed by at most one pixel per glyph and two per space, takes one row;
 * a longer one wraps at spaces where the next word's ink would pass the
 * width. *split_words (optional) counts rows that end inside a word, which
 * happens only for a word wider than the row on its own. */
int c3_ui_text_rows(const C3BottomUi *ui,int width,const char *text,int *split_words);
/* Text width of c3_ui_message/c3_ui_dialogue without a content inset. */
#define C3_UI_MESSAGE_WIDTH 294
/* Single-line numeric fields align by visible glyph bounds, including shadow. */
int c3_ui_text_right(C3BottomUi *ui,int right,int y,int width,const char *text,uint32_t color);
void c3_ui_header(C3BottomUi *ui,const char *title,const char *subtitle);
void c3_ui_row(C3BottomUi *ui,int y,const char *label,int selected,int disabled);
void c3_ui_row_value(C3BottomUi *ui,int y,const char *label,const char *value,int selected,int disabled);
void c3_ui_footer(C3BottomUi *ui,const char *text);
void c3_ui_choices(C3BottomUi *ui,int yes);
void c3_ui_hp(C3BottomUi *ui,int x,int y,int width,const char *name,unsigned hp,unsigned maximum);
void c3_ui_experience(C3BottomUi *ui,int x,int y,int width,uint32_t progress,uint32_t range);
/* A/B edges page through the UI before reaching the original state machine.
 * identity distinguishes equal consecutive script/event messages. */
void c3_ui_pager_sync(C3UiPager *pager,uint32_t identity);
int c3_ui_pager_advance(C3UiPager *pager,uint16_t *pressed);
void c3_ui_dialogue(C3BottomUi *ui,C3UiPager *pager,const char *text,int choices,int yes);
void c3_ui_message(C3BottomUi *ui,C3UiPager *pager,const char *text,int y,int lines);
void c3_ui_title(C3BottomUi *ui,int waiting,int accepted);
#endif
