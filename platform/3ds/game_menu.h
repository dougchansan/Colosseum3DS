#ifndef COLOSSEUM3DS_GAME_MENU_H
#define COLOSSEUM3DS_GAME_MENU_H

#include "n3ds_platform.h"
#include <stddef.h>
#include <stdint.h>

/* Original mode-0 name entry uses seven GSchar characters. This first native
 * keyboard supports the source English ASCII subset; it does not reinterpret
 * UTF-8 bytes as GameCube characters. The state service performs encoding. */
#define C3_GAME_MENU_NAME_MAX 7u

typedef struct {
    int present;       /* Any primary/backup card file: require overwrite consent. */
    int valid;         /* Codec and game-state validation both succeeded. */
    int can_continue;  /* Valid state and a floor supported by the native flow. */
    char name[32], location[48], reason[128];
    uint32_t money, play_seconds;
} C3GameMenuSave;

typedef struct {
    unsigned sound_mono;      /* Source GSsnd output: 0 stereo, 1 mono. */
    unsigned rumble_disabled; /* Source game-data selector 9: 0 on, 1 off. */
} C3GameMenuOptions;

typedef enum {
    C3_MENU_MAIN, C3_MENU_NEW_CONFIRM, C3_MENU_NAME_CHOICES,
    C3_MENU_NAME_KEYBOARD, C3_MENU_NAME_CONFIRM, C3_MENU_CONTINUE_CONFIRM,
    C3_MENU_OPTIONS, C3_MENU_NOTICE, C3_MENU_PENDING, C3_MENU_ERROR
} C3GameMenuPage;

typedef enum {
    C3_MENU_EVENT_NONE, C3_MENU_EVENT_NEW_GAME, C3_MENU_EVENT_CONTINUE,
    C3_MENU_EVENT_RETURN_TITLE, C3_MENU_EVENT_OPTIONS_CHANGED
} C3GameMenuEventKind;

typedef struct {
    C3GameMenuEventKind kind;
    char name[C3_GAME_MENU_NAME_MAX + 1];
    C3GameMenuOptions options;
    int replace_authorized; /* NEW_GAME only; explicit yes to existing-card warning. */
} C3GameMenuEvent;

typedef struct {
    C3GameMenuPage page, return_page;
    C3GameMenuSave save;
    C3GameMenuOptions options, initial_options;
    unsigned selection, name_selection, keyboard_selection, lower_case;
    unsigned confirm_yes, replace_authorized, dirty;
    uint16_t previous_directions;
    float direction_seconds, repeat_seconds;
    char name[C3_GAME_MENU_NAME_MAX + 1];
    char notice[160];
    /* Optional privately extracted C3U1 text pack; owned until close. */
    unsigned char *text_data;
    size_t text_size;
    uint32_t text_count;
} C3GameMenu;

/* No GPU/frame ownership. Init/close own only the optional text pack. Inputs
 * are already polled by the caller. Draw updates the selected bottom console;
 * the caller draws the title/top screen and submits the frame. */
void c3_game_menu_init(C3GameMenu *menu, const C3GameMenuSave *save,
                       const C3GameMenuOptions *options);
int c3_game_menu_load_text(C3GameMenu *menu, const char *path,
                          char *error, size_t capacity); /* 1 success, 0 failure */
void c3_game_menu_close(C3GameMenu *menu);
C3GameMenuEvent c3_game_menu_update(C3GameMenu *menu,
                                   const C3N3dsInput *input, float delta_seconds);
void c3_game_menu_draw(C3GameMenu *menu);
void c3_game_menu_set_save(C3GameMenu *menu, const C3GameMenuSave *save);
void c3_game_menu_set_pending(C3GameMenu *menu, const char *message);
void c3_game_menu_set_error(C3GameMenu *menu, const char *message);
const char *c3_game_menu_page_name(C3GameMenuPage page);

#endif
