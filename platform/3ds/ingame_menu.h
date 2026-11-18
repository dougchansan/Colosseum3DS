#ifndef C3_INGAME_MENU_H
#define C3_INGAME_MENU_H
#include "n3ds_platform.h"
#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/pda.h"
#include "colosseum3ds/inventory.h"

typedef enum {
    C3_GAME_HUB,C3_GAME_PARTY,C3_GAME_POKEMON,C3_GAME_ITEMS,C3_GAME_ITEM,
    C3_GAME_TRAINER,C3_GAME_OPTIONS,C3_GAME_PDA,C3_GAME_MAIL,C3_GAME_LETTER,
    C3_GAME_PICTURE,C3_GAME_TONES,C3_GAME_MEMO,C3_GAME_MEMO_ENTRY,
    C3_GAME_SNAG,C3_GAME_SNAG_ENTRY,C3_GAME_MEMO_FILTER,C3_GAME_NOTICE,
    C3_GAME_ITEM_ACTIONS,C3_GAME_PARTY_ACTIONS,C3_GAME_ITEM_TARGET,
    C3_GAME_ITEM_QUANTITY,C3_GAME_ITEM_CONFIRM,C3_GAME_ITEM_CHOICE,C3_GAME_ITEM_RESULT
} C3IngamePage;
typedef enum { C3_INGAME_NONE,C3_INGAME_RESUME,C3_INGAME_SAVE,C3_INGAME_LEAVE,
               C3_INGAME_OPTIONS,C3_INGAME_READ_MAIL,C3_INGAME_PREVIEW,
               C3_INGAME_PLAY_TONE,C3_INGAME_STOP_TONE } C3IngameEventKind;
typedef struct { C3IngameEventKind kind; C3PdaMailAction mail; unsigned tone; } C3IngameEvent;
typedef struct {
    C3IngamePage page,parent;
    unsigned selection,hub_selection,pda_selection,pocket,sort,tab,detail,option_mono,option_rumble;
    unsigned count,order[512],active,dirty,preview;
    unsigned idle; /* hub shown as the field bottom screen: no cursor */
    unsigned mail_alert; /* new-mail envelope visible this frame (mail_alert.h), drawn on the hub */
    unsigned filter_types[2],filter_color,saved_filters[3];
    C3InventoryRequest inventory;
    C3InventoryResult inventory_result;
    unsigned inventory_origin,inventory_selection,inventory_choice,notice_selection;
    uint16_t direction;
    /* Stylus: the target pressed (C3_INGAME_HIT_*), whether it is still
     * armed, whether a row was already selected when pressed, and keys the
     * release adds to the next update (option toggles). */
    int touch_item;
    unsigned touch_active,touch_was_selected;
    uint16_t touch_keys;
    /* Original UI sound request (0x23 cursor, 0x24 decide, 0x25 cancel)
     * left for the caller to play; 0 when none. */
    uint32_t sound;
    float held_seconds,repeat_seconds;
    C3UiPager pager;
    C3PdaAssets assets;
    uint8_t *asset_memory;
    char error[192],text[8192];
} C3IngameMenu;

/* Init first; close before reinitializing an owned/loaded menu. */
void c3_ingame_init(C3IngameMenu *menu,const C3GameState *state);
int c3_ingame_load(C3IngameMenu *menu,const char *path);
void c3_ingame_close(C3IngameMenu *menu);
/* The new-mail envelope over any bottom view while menu->mail_alert is set
 * (needs the loaded P*DA pack). Returns 1 when drawn. */
int c3_ingame_draw_mail_alert(C3IngameMenu *menu,C3BottomUi *ui);
/* Borrowed assets enable source-identical host previews without file ownership. */
void c3_ingame_set_assets(C3IngameMenu *menu,const C3PdaAssets *assets);
C3IngameEvent c3_ingame_update(C3IngameMenu *menu,C3GameState *state,const C3N3dsInput *input,float seconds);
void c3_ingame_draw_to(C3IngameMenu *menu,const C3GameState *state,C3BottomUi *ui);
void c3_ingame_error(C3IngameMenu *menu,const char *message);
/* Resolve the displayed model from the current live record; no save mutation. */
/* Hub item (0..7) under a touch on the bottom screen, or -1. */
int c3_ingame_hub_hit(int x,int y);
/* Touch targets on the 320x240 bottom screen, matching c3_ingame_draw_to. */
enum { C3_INGAME_HIT_NONE=-1, C3_INGAME_HIT_BACK=-2, C3_INGAME_HIT_PANEL=-3 };
#define C3_INGAME_SOUND_CURSOR 0x23u
#define C3_INGAME_SOUND_DECIDE 0x24u
#define C3_INGAME_SOUND_CANCEL 0x25u
/* Back button, bottom right on every page (drawn rectangle). */
#define C3_INGAME_BACK_X 252
#define C3_INGAME_BACK_Y 221
#define C3_INGAME_BACK_W 64
#define C3_INGAME_BACK_H 18
/* What lies under (x,y) on the current page: a row/item index (an absolute
 * list index; the same value m->selection takes), the Back button, the text
 * panel of a detail page, or nothing. */
int c3_ingame_hit(const C3IngameMenu *menu,int x,int y);
/* Stylus pass, run before c3_ingame_update with the same input. A press
 * highlights the row under it; a release on the same target acts. Returns the
 * button edges the release stands for (Back is exactly B, a row is A after
 * selecting it), which the caller ORs into input.pressed so every button
 * path, including closing an open model on B, is shared. */
uint16_t c3_ingame_touch(C3IngameMenu *menu,const C3N3dsInput *input);
int c3_ingame_preview_identity(const C3IngameMenu *menu,const C3GameState *state,
                              uint16_t *species,uint32_t *personality,uint32_t *trainer);
#endif
