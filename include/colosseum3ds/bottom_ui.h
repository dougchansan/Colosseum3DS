#ifndef C3_BOTTOM_UI_H
#define C3_BOTTOM_UI_H
#include <stddef.h>
#include <stdint.h>

/* Borrowed, validated private asset pack; no GPU or frame ownership. */
typedef struct { const unsigned char *data; size_t size; } C3UiAssets;
typedef struct {
    unsigned char *pixels;
    const C3UiAssets *assets;
    const unsigned char *fallback_font; /* libctru 8x8 ASCII, borrowed */
    int rotated_bgr; /* 0: RGB8, 1: rotated BGR8, 2: rotated RGB565. */
    int content_inset; /* Additional horizontal clearance for framed menus. */
} C3BottomUi;
typedef struct {
    uint32_t identity;
    size_t offset,next;
    int more;
} C3UiPager;
enum { C3_UI_STORY, C3_UI_BATTLE, C3_UI_OPTIONS, C3_UI_QUIT,
       C3_UI_HAND, C3_UI_START, C3_UI_WINDOW, C3_UI_HP_FRAME,
       C3_UI_MOVE_FRAME, C3_UI_ARROW, C3_UI_SPRITES };
#define C3_UI_WHITE 0xf5f5eeu
#define C3_UI_MUTED 0xaab7b5u
#define C3_UI_GOLD 0xffdc63u
enum { C3_UI_RGB8, C3_UI_ROTATED_BGR8, C3_UI_ROTATED_RGB565 };
int c3_ui_assets_open(C3UiAssets *assets,const void *data,size_t size);
/* Interval counters per drawing primitive, timed only while a microsecond
 * clock is installed (counts are always kept). take copies and clears. */
typedef struct {
    uint64_t begin_us, panel_us, text_us, rect_us, sprite_us;
    uint32_t panels, glyphs, rects, sprites;
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
