#include "colosseum3ds/name_entry.h"

#include <stdio.h>
#include <string.h>

#define KEY_X 20
#define KEY_Y 60
#define KEY_W 28
#define KEY_H 20
#define BUTTON_Y (KEY_Y + (int)C3_NAME_ENTRY_ROWS * KEY_H + 2)
#define BUTTON_H 16
#define KEYS (C3_NAME_ENTRY_ROWS * C3_NAME_ENTRY_COLUMNS)
#define KEY_DELETE ((int)KEYS)
#define KEY_DONE ((int)KEYS + 1)

enum { PAD_LEFT = 1, PAD_RIGHT = 2, PAD_DOWN = 4, PAD_UP = 8, PAD_A = 0x100, PAD_B = 0x200, PAD_START = 0x1000 };

static const char keys[C3_NAME_ENTRY_ROWS][C3_NAME_ENTRY_COLUMNS + 1u] = {
    "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ .-'", "abcdefghij",
    "klmnopqrst", "uvwxyz0123", "456789!?&+",
};

void c3_name_entry_init(C3NameEntry *entry, const char *initial)
{
    if (!entry) return;
    memset(entry, 0, sizeof *entry);
    entry->armed = -1;
    if (initial) {
        size_t length = strlen(initial);
        if (length > C3_NAME_ENTRY_MAX) length = C3_NAME_ENTRY_MAX;
        for (size_t i = 0; i < length; ++i)
            entry->fallback[i] = initial[i] >= 32 && initial[i] <= 126 ? initial[i] : '?';
        memcpy(entry->text, entry->fallback, length);
        entry->length = (unsigned)length;
    }
}

static int key_at(unsigned x, unsigned y)
{
    if (x >= KEY_X && x < KEY_X + C3_NAME_ENTRY_COLUMNS * KEY_W && y >= KEY_Y &&
        y < KEY_Y + C3_NAME_ENTRY_ROWS * KEY_H)
        return (int)((y - KEY_Y) / KEY_H * C3_NAME_ENTRY_COLUMNS + (x - KEY_X) / KEY_W);
    if (y >= (unsigned)BUTTON_Y && y < (unsigned)(BUTTON_Y + BUTTON_H)) {
        if (x >= 20 && x < 150) return KEY_DELETE;
        if (x >= 170 && x < 300) return KEY_DONE;
    }
    return -1;
}

static C3NameEvent press(C3NameEntry *entry, int key)
{
    if (key == KEY_DONE) {
        if (!entry->length) {
            if (!entry->fallback[0]) return C3_NAME_NONE;
            memcpy(entry->text, entry->fallback, sizeof entry->text);
            entry->length = (unsigned)strlen(entry->text);
        }
        /* A name may not end in a space or consist of spaces only. */
        while (entry->length && entry->text[entry->length - 1u] == ' ') entry->text[--entry->length] = 0;
        return entry->length ? C3_NAME_DONE : C3_NAME_CHANGED;
    }
    if (key == KEY_DELETE) {
        if (!entry->length) return C3_NAME_NONE;
        entry->text[--entry->length] = 0;
        return C3_NAME_CHANGED;
    }
    if (key < 0 || key >= (int)KEYS || entry->length >= C3_NAME_ENTRY_MAX) return C3_NAME_NONE;
    {
        char c = keys[key / (int)C3_NAME_ENTRY_COLUMNS][key % (int)C3_NAME_ENTRY_COLUMNS];
        if (c == ' ' && !entry->length) return C3_NAME_NONE;
        entry->text[entry->length++] = c;
        entry->text[entry->length] = 0;
    }
    return C3_NAME_CHANGED;
}

C3NameEvent c3_name_entry_update(C3NameEntry *entry, uint16_t pressed,
                                 int touch_held, int touch_pressed, int touch_released,
                                 unsigned touch_x, unsigned touch_y)
{
    C3NameEvent event = C3_NAME_NONE, next;
    if (!entry) return C3_NAME_NONE;
    if (touch_pressed) { entry->armed = key_at(touch_x, touch_y); event = C3_NAME_CHANGED; }
    if (touch_released) {
        int key = key_at(touch_x, touch_y);
        if (key >= 0 && key == entry->armed) {
            if (key < (int)KEYS) { entry->row = (unsigned)key / C3_NAME_ENTRY_COLUMNS; entry->column = (unsigned)key % C3_NAME_ENTRY_COLUMNS; }
            else { entry->row = C3_NAME_ENTRY_ROWS; entry->column = key == KEY_DONE; }
            next = press(entry, key);
            if (next == C3_NAME_DONE) return next;
        }
        entry->armed = -1;
        event = C3_NAME_CHANGED;
    } else if (!touch_held && entry->armed >= 0) entry->armed = -1;

    unsigned columns = entry->row == C3_NAME_ENTRY_ROWS ? 2u : C3_NAME_ENTRY_COLUMNS;
    if (pressed & PAD_LEFT) { entry->column = entry->column ? entry->column - 1u : columns - 1u; event = C3_NAME_CHANGED; }
    if (pressed & PAD_RIGHT) { entry->column = entry->column + 1u < columns ? entry->column + 1u : 0; event = C3_NAME_CHANGED; }
    if (pressed & (PAD_UP | PAD_DOWN)) {
        unsigned rows = C3_NAME_ENTRY_ROWS + 1u, before = entry->row;
        entry->row = pressed & PAD_UP ? (entry->row ? entry->row - 1u : rows - 1u)
                                      : (entry->row + 1u < rows ? entry->row + 1u : 0);
        /* The two buttons sit under columns 0-4 and 5-9. */
        if (entry->row == C3_NAME_ENTRY_ROWS) entry->column = entry->column >= 5u;
        else if (before == C3_NAME_ENTRY_ROWS) entry->column = entry->column ? 7u : 2u;
        event = C3_NAME_CHANGED;
    }
    if (pressed & PAD_B) { if (press(entry, KEY_DELETE) != C3_NAME_NONE) event = C3_NAME_CHANGED; }
    if (pressed & PAD_START) { next = press(entry, KEY_DONE); if (next != C3_NAME_NONE) event = next; }
    else if (pressed & PAD_A) {
        int key = entry->row == C3_NAME_ENTRY_ROWS ? (entry->column ? KEY_DONE : KEY_DELETE)
                                                   : (int)(entry->row * C3_NAME_ENTRY_COLUMNS + entry->column);
        next = press(entry, key);
        if (next != C3_NAME_NONE) event = next;
    }
    return event;
}

void c3_name_entry_draw(const C3NameEntry *entry, C3BottomUi *ui, const char *title)
{
    char shown[C3_NAME_ENTRY_MAX + 2u], key[2] = {0, 0};
    if (!entry || !ui) return;
    snprintf(shown, sizeof shown, "%s%s", entry->text, entry->length < C3_NAME_ENTRY_MAX ? "_" : "");
    c3_ui_header(ui, title ? title : "NAME", shown);
    for (unsigned row = 0; row < C3_NAME_ENTRY_ROWS; ++row) {
        for (unsigned column = 0; column < C3_NAME_ENTRY_COLUMNS; ++column) {
            int index = (int)(row * C3_NAME_ENTRY_COLUMNS + column);
            int selected = entry->row == row && entry->column == column;
            int x = KEY_X + (int)column * KEY_W, y = KEY_Y + (int)row * KEY_H;
            c3_ui_rect(ui, x + 1, y + 1, KEY_W - 2, KEY_H - 2,
                       index == entry->armed ? 0x5a7f82u : selected ? 0x30494bu : 0x16262bu);
            if (selected) c3_ui_rect(ui, x + 1, y + KEY_H - 3, KEY_W - 2, 2, C3_UI_GOLD);
            key[0] = keys[row][column];
            if (key[0] != ' ') c3_ui_text(ui, x + 10, y + 2, KEY_W - 10, 1, key, C3_UI_WHITE);
        }
    }
    for (unsigned button = 0; button < 2; ++button) {
        int selected = entry->row == C3_NAME_ENTRY_ROWS && entry->column == button;
        int x = button ? 170 : 20, armed = entry->armed == (button ? KEY_DONE : KEY_DELETE);
        c3_ui_rect(ui, x, BUTTON_Y, 130, BUTTON_H, armed ? 0x5a7f82u : selected ? 0x30494bu : 0x16262bu);
        if (selected) c3_ui_rect(ui, x, BUTTON_Y + BUTTON_H - 2, 130, 2, C3_UI_GOLD);
        c3_ui_text(ui, x + (button ? 50 : 40), BUTTON_Y, 90, 1, button ? "OK" : "DELETE", C3_UI_WHITE);
    }
    c3_ui_footer(ui, "Touch or A: type   B: delete   START: OK");
}
