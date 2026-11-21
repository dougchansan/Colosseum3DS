#define _POSIX_C_SOURCE 200809L
#include "game_menu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static C3GameMenuEvent tap(C3GameMenu *menu, uint16_t button)
{
    C3N3dsInput input = {.held = button, .pressed = button};
    C3GameMenuEvent result = c3_game_menu_update(menu, &input, 1.0f / 60.0f);
    input.held = input.pressed = 0;
    c3_game_menu_update(menu, &input, 1.0f / 60.0f);
    return result;
}

static void down(C3GameMenu *menu, unsigned count)
{
    while (count--) tap(menu, C3_PAD_DOWN);
}

static void write32(unsigned char *p, unsigned value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (i * 8));
}

int main(void)
{
    C3GameMenu menu;
    C3GameMenuSave save = {.present = 1, .valid = 1, .can_continue = 1};
    C3GameMenuOptions options = {0, 0};
    C3GameMenuEvent event;
    c3_game_menu_init(&menu, &save, &options);
    CHECK(menu.selection == 1);
    /* CONTINUE sits above NEW GAME and left of COLOSSEUM BATTLE. */
    tap(&menu, C3_PAD_UP);CHECK(menu.selection == 1);
    tap(&menu, C3_PAD_LEFT);CHECK(menu.selection == 1);
    tap(&menu, C3_PAD_RIGHT);CHECK(menu.selection == 2);
    tap(&menu, C3_PAD_DOWN);CHECK(menu.selection == 3);
    tap(&menu, C3_PAD_DOWN);CHECK(menu.selection == 5);
    tap(&menu, C3_PAD_LEFT);CHECK(menu.selection == 4);
    tap(&menu, C3_PAD_UP);CHECK(menu.selection == 0);
    tap(&menu, C3_PAD_RIGHT);CHECK(menu.selection == 3);
    tap(&menu, C3_PAD_UP);CHECK(menu.selection == 2);
    tap(&menu, C3_PAD_LEFT);CHECK(menu.selection == 1);
    tap(&menu, C3_PAD_DOWN);CHECK(menu.selection == 0);
    CHECK(tap(&menu, C3_PAD_A).kind == C3_MENU_EVENT_NONE);
    CHECK(menu.page == C3_MENU_NEW_CONFIRM && !menu.confirm_yes);
    CHECK(tap(&menu, C3_PAD_A).kind == C3_MENU_EVENT_NONE);
    CHECK(menu.page == C3_MENU_MAIN && !menu.replace_authorized);

    tap(&menu, C3_PAD_A);
    tap(&menu, C3_PAD_UP);
    tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_NAME_CHOICES && menu.replace_authorized);
    down(&menu, 1);
    tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_NAME_CONFIRM && !strcmp(menu.name, "WES"));
    event = tap(&menu, C3_PAD_A);
    CHECK(event.kind == C3_MENU_EVENT_NEW_GAME && event.replace_authorized && !strcmp(event.name, "WES"));
    CHECK(menu.page == C3_MENU_PENDING);
    CHECK(tap(&menu, C3_PAD_A).kind == C3_MENU_EVENT_NONE);
    CHECK(tap(&menu, C3_PAD_B).kind == C3_MENU_EVENT_NONE);
    c3_game_menu_set_error(&menu, "Failed to load the source starter data.");
    CHECK(menu.page == C3_MENU_ERROR);
    tap(&menu, C3_PAD_B);
    CHECK(menu.page == C3_MENU_MAIN);

    save.present = save.valid = save.can_continue = 0;
    c3_game_menu_init(&menu, &save, &options);
    tap(&menu, C3_PAD_A); tap(&menu, C3_PAD_A); tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_NAME_KEYBOARD && menu.name[0] == 0);
    tap(&menu, C3_PAD_START);
    CHECK(menu.page == C3_MENU_NAME_CONFIRM && !strcmp(menu.name, "WES"));
    tap(&menu, C3_PAD_B);
    CHECK(menu.page == C3_MENU_NAME_CHOICES);
    tap(&menu, C3_PAD_A);
    tap(&menu, C3_PAD_R);
    tap(&menu, C3_PAD_A);
    CHECK(!strcmp(menu.name, "a"));
    for (unsigned i = 0; i < 6; ++i) tap(&menu, C3_PAD_A);
    CHECK(strlen(menu.name) == 7 && menu.keyboard_selection == 43);
    tap(&menu, C3_PAD_B);
    CHECK(strlen(menu.name) == 6);
    tap(&menu, C3_PAD_START);
    event = tap(&menu, C3_PAD_A);
    CHECK(event.kind == C3_MENU_EVENT_NEW_GAME && !event.replace_authorized && !strcmp(event.name, "aaaaaa"));

    c3_game_menu_init(&menu, &save, &options);
    tap(&menu, C3_PAD_RIGHT); tap(&menu, C3_PAD_UP);
    CHECK(menu.selection == 2);
    CHECK(tap(&menu, C3_PAD_A).kind == C3_MENU_EVENT_COLOSSEUM_BATTLE && menu.page == C3_MENU_MAIN && menu.selection == 2);
    tap(&menu, C3_PAD_LEFT); down(&menu, 2); tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_OPTIONS);
    tap(&menu, C3_PAD_LEFT); tap(&menu, C3_PAD_DOWN); tap(&menu, C3_PAD_RIGHT);
    CHECK(menu.options.sound_mono && menu.options.rumble_disabled);
    tap(&menu, C3_PAD_B);
    CHECK(!menu.options.sound_mono && !menu.options.rumble_disabled && menu.page == C3_MENU_MAIN && menu.selection == 4);
    tap(&menu, C3_PAD_A); tap(&menu, C3_PAD_LEFT); down(&menu, 2);
    event = tap(&menu, C3_PAD_A);
    CHECK(event.kind == C3_MENU_EVENT_OPTIONS_CHANGED && event.options.sound_mono && !event.options.rumble_disabled);
    CHECK(menu.page == C3_MENU_MAIN);

    save.present = save.valid = 1;
    save.can_continue = 0;
    snprintf(save.reason, sizeof(save.reason), "This saved floor is not implemented.");
    c3_game_menu_init(&menu, &save, &options);
    tap(&menu, C3_PAD_UP); tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_NOTICE && strstr(menu.notice, "floor"));
    tap(&menu, C3_PAD_B);
    save.can_continue = 1;
    c3_game_menu_set_save(&menu, &save);
    tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_CONTINUE_CONFIRM);
    tap(&menu, C3_PAD_RIGHT); tap(&menu, C3_PAD_A);
    CHECK(menu.page == C3_MENU_MAIN);
    tap(&menu, C3_PAD_A);
    CHECK(tap(&menu, C3_PAD_A).kind == C3_MENU_EVENT_CONTINUE && menu.page == C3_MENU_PENDING);

    /* A held direction repeats after a delay: OPTIONS -> NEW GAME -> CONTINUE. */
    c3_game_menu_init(&menu, NULL, NULL);
    menu.selection = 4;
    C3N3dsInput held = {.held = C3_PAD_UP, .pressed = C3_PAD_UP};
    c3_game_menu_update(&menu, &held, 0.01f);
    CHECK(menu.selection == 0);
    held.pressed = 0;
    c3_game_menu_update(&menu, &held, NAN);
    CHECK(menu.selection == 0);
    for (unsigned i = 0; i < 12; ++i) c3_game_menu_update(&menu, &held, 0.02f);
    CHECK(menu.selection == 0);
    for (unsigned i = 0; i < 15; ++i) c3_game_menu_update(&menu, &held, 0.02f);
    CHECK(menu.selection == 1);

    /* Stylus: a tap selects and confirms the entry under it. */
    save.present = save.valid = save.can_continue = 1;
    c3_game_menu_init(&menu, &save, &options);
    {C3N3dsInput touch = {.touch_held = true, .touch_pressed = true, .touch_x = 60, .touch_y = 120};
     c3_game_menu_update(&menu, &touch, 0.01f);
     CHECK(menu.selection == 0 && menu.page == C3_MENU_NEW_CONFIRM);
     tap(&menu, C3_PAD_B);CHECK(menu.page == C3_MENU_MAIN);
     touch.touch_x = 60; touch.touch_y = 60;c3_game_menu_update(&menu, &touch, 0.01f);
     CHECK(menu.selection == 1 && menu.page == C3_MENU_CONTINUE_CONFIRM);
     tap(&menu, C3_PAD_B);CHECK(menu.page == C3_MENU_MAIN);
     touch.touch_x = 300; touch.touch_y = 235;c3_game_menu_update(&menu, &touch, 0.01f);
     CHECK(menu.selection == 1 && menu.page == C3_MENU_MAIN);}

    /* Stylus hit-testing on the New Game naming pages, at drawn cells. */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 9, 56) == 0);      /* A */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 312, 56) == 7);    /* H */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 47, 81) == 9);     /* J */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 160, 180) == 35);  /* row 5 col 4 */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 8, 60) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 313, 60) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 100, 183) == -1);  /* gap above buttons */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 20, 190) == 40);   /* CASE */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 120, 200) == 41);  /* SPACE */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 200, 211) == 42);  /* BACK */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 300, 186) == 43);  /* DONE */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 83, 200) == -1);   /* between buttons */
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_KEYBOARD, 120, 212) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 70) == 0);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 105) == 1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 135) == 2);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 170) == 3);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 40) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 150, 200) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CHOICES, 5, 105) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CONFIRM, 200, 170) == 0);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CONFIRM, 200, 200) == 1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_NAME_CONFIRM, 100, 200) == -1);
    CHECK(c3_game_menu_touch_hit(C3_MENU_OPTIONS, 150, 70) == -1);

    /* The whole New Game naming flow by stylus: a list row is selected by
     * a tap and confirmed by a second tap; keys are entered at once. */
    save.present = save.valid = save.can_continue = 0;
    c3_game_menu_init(&menu, &save, &options);
    {C3N3dsInput touch = {.touch_held = true, .touch_pressed = true};
#define TOUCH(px, py) (touch.touch_x = (px), touch.touch_y = (py), c3_game_menu_update(&menu, &touch, 0.01f))
     TOUCH(60, 120); CHECK(menu.page == C3_MENU_NEW_CONFIRM && menu.confirm_yes);
     TOUCH(200, 170); CHECK(menu.page == C3_MENU_NAME_CHOICES && menu.name_selection == 0);
     TOUCH(150, 135); CHECK(menu.page == C3_MENU_NAME_CHOICES && menu.name_selection == 2);
     TOUCH(150, 70); CHECK(menu.page == C3_MENU_NAME_CHOICES && menu.name_selection == 0);
     TOUCH(150, 70); CHECK(menu.page == C3_MENU_NAME_KEYBOARD && !menu.name[0]);
     TOUCH(47, 81); CHECK(!strcmp(menu.name, "J") && menu.keyboard_selection == 9);
     TOUCH(20, 190); CHECK(menu.lower_case == 1);
     TOUCH(9, 56); CHECK(!strcmp(menu.name, "Ja"));
     TOUCH(120, 200); CHECK(!strcmp(menu.name, "Ja "));
     TOUCH(200, 200); CHECK(!strcmp(menu.name, "Ja"));
     TOUCH(100, 183); CHECK(!strcmp(menu.name, "Ja") && menu.page == C3_MENU_NAME_KEYBOARD);
     TOUCH(300, 200); CHECK(menu.page == C3_MENU_NAME_CONFIRM && !strcmp(menu.name, "Ja"));
     TOUCH(200, 200); CHECK(menu.page == C3_MENU_NAME_CONFIRM && !menu.confirm_yes);
     TOUCH(200, 200); CHECK(menu.page == C3_MENU_NAME_CHOICES);
     TOUCH(150, 105); CHECK(menu.name_selection == 1);
     TOUCH(150, 105); CHECK(menu.page == C3_MENU_NAME_CONFIRM && !strcmp(menu.name, "WES"));
     event = TOUCH(200, 170);
     CHECK(event.kind == C3_MENU_EVENT_NEW_GAME && !strcmp(event.name, "WES"));
#undef TOUCH
    }

    /* A valid private pack is replaced only after complete validation. */
    char path[] = "/tmp/c3-menu-test-XXXXXX", error[128];
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    unsigned char bytes[32] = {'C','3','U','1'};
    write32(bytes + 4, 1); write32(bytes + 8, sizeof(bytes)); write32(bytes + 12, 1);
    write32(bytes + 16, 0x2EFE); write32(bytes + 20, 28); write32(bytes + 24, 4);
    memcpy(bytes + 28, "WES", 4);
    CHECK(write(fd, bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK(close(fd) == 0);
    CHECK(c3_game_menu_load_text(&menu, path, error, sizeof(error)));
    CHECK(menu.text_count == 1);
    FILE *stream = fopen(path, "wb"); CHECK(stream);
    bytes[28] = 27; /* Escapes must never reach the native console. */
    CHECK(fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes));
    CHECK(fclose(stream) == 0);
    CHECK(!c3_game_menu_load_text(&menu, path, error, sizeof(error)));
    CHECK(menu.text_count == 1 && menu.text_data[28] == 'W');
    c3_game_menu_close(&menu);
    CHECK(!menu.text_data && !menu.text_count);
    CHECK(unlink(path) == 0);
    printf("native game menu: %u checks passed\n", checks);
    return 0;
}
