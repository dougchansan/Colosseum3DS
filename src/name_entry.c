#include "colosseum3ds/name_entry.h"

#include <stdio.h>
#include <string.h>

enum { PAD_LEFT = 1, PAD_RIGHT = 2, PAD_DOWN = 4, PAD_UP = 8, PAD_R = 0x20, PAD_A = 0x100, PAD_B = 0x200, PAD_START = 0x1000 };
enum { BUTTON_UPPER, BUTTON_LOWER, BUTTON_BACK, BUTTON_DONE, BUTTONS };
#define KEY_X 13
#define KEY_Y 58
#define KEY_W 19
#define KEY_H 24
#define BUTTON_Y (KEY_Y + (int)C3_NAME_ENTRY_ROWS * KEY_H + 4)
#define BUTTON_W 68
#define BUTTON_H 22
#define LIST_Y 62
#define LIST_H 28

/* lbl_80266DD8 (rodata_80266D78.c): per mode the title message, the length
 * limit and the preset names (lbl_80266DC0 / lbl_80266DCC). */
static const struct { uint32_t title, length, presets[C3_NAME_ENTRY_PRESETS]; unsigned count; } modes[4] = {
    {0x2EED, 7, {0x2EFE, 0x2EFF, 0x2F00}, 3},
    {0x2EED, 7, {0x2F01, 0x2F02, 0x2F03}, 3},
    {0x2EEE, 10, {0}, 0},
    {0x2EEF, 8, {0}, 0},
};

static int render(const C3Story *messages, uint32_t id, char *out, size_t capacity)
{
    char error[C3_STORY_ERROR];
    return c3_story_render(messages, id, NULL, NULL, C3_STORY_TEXT_RAW, out, capacity, error);
}
static size_t utf8_decode(const char *s, uint16_t *code)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *code = u[0]; return 1; }
    if ((u[0] & 0xe0) == 0xc0 && (u[1] & 0xc0) == 0x80) { *code = (uint16_t)((u[0] & 0x1f) << 6 | (u[1] & 0x3f)); return 2; }
    if ((u[0] & 0xf0) == 0xe0 && (u[1] & 0xc0) == 0x80 && (u[2] & 0xc0) == 0x80) {
        *code = (uint16_t)((u[0] & 0x0f) << 12 | (u[1] & 0x3f) << 6 | (u[2] & 0x3f)); return 3; }
    *code = '?'; return 1;
}
static size_t utf8_encode(uint16_t c, char *out)
{
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) { out[0] = (char)(0xc0 | c >> 6); out[1] = (char)(0x80 | (c & 63)); return 2; }
    out[0] = (char)(0xe0 | c >> 12); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3;
}
static void name_utf8(const uint16_t *name, unsigned length, char *out, size_t capacity)
{
    size_t used = 0;
    for (unsigned i = 0; i < length && used + 4 < capacity; ++i) used += utf8_encode(name[i], out + used);
    out[used] = 0;
}

int c3_name_entry_text_load(C3NameEntryText *t, const C3Story *messages, unsigned mode)
{
    char row[96];
    if (!t || !messages || mode > 3) return 0;
    memset(t, 0, sizeof *t);
    t->max_length = modes[mode].length;
    if (!render(messages, modes[mode].title, t->title, sizeof t->title) ||
        !render(messages, 0x2EFD, t->new_name, sizeof t->new_name) ||
        !render(messages, 0x2EF0, t->upper, sizeof t->upper) || !render(messages, 0x2EF1, t->lower, sizeof t->lower) ||
        !render(messages, 0x2EF3, t->done, sizeof t->done) || !render(messages, 0x2EF4, t->back, sizeof t->back) ||
        !render(messages, 0x2EF5, t->toggle, sizeof t->toggle)) return 0;
    for (unsigned i = 0; i < modes[mode].count; ++i)
        if (!render(messages, modes[mode].presets[i], t->presets[i], sizeof t->presets[i])) return 0;
    t->preset_count = modes[mode].count;
    /* Pages: 2F04-2F07 upper case, 2F08-2F0B lower case. A space is a gap. */
    for (unsigned page = 0; page < 2; ++page)
        for (unsigned r = 0; r < C3_NAME_ENTRY_ROWS; ++r) {
            const char *p = row;
            if (!render(messages, 0x2F04 + page * 4 + r, row, sizeof row)) return 0;
            for (unsigned c = 0; c < C3_NAME_ENTRY_COLUMNS && *p; ++c) {
                uint16_t code;
                p += utf8_decode(p, &code);
                t->keys[page][r][c] = code == ' ' ? 0 : code;
            }
        }
    return 1;
}

static void set_name(C3NameEntry *e, const char *utf8)
{
    e->length = 0;
    while (utf8 && *utf8 && e->length < e->text->max_length && e->length < C3_NAME_ENTRY_MAX) {
        uint16_t code;
        utf8 += utf8_decode(utf8, &code);
        e->name[e->length++] = code;
    }
    e->name[e->length] = 0;
}

void c3_name_entry_init(C3NameEntry *e, const C3NameEntryText *text, const C3Story *messages)
{
    if (!e || !text) return;
    memset(e, 0, sizeof *e);
    e->text = text; e->messages = messages;
    /* menuNameEntry's nameBuf: the first preset for the hero and partner. */
    set_name(e, text->preset_count ? text->presets[0] : NULL);
    e->phase = text->preset_count ? C3_NAME_PRESETS : C3_NAME_KEYBOARD;
}

static int prompt_source(void *context, uint8_t control, C3StoryValue *value)
{
    if (control != 0x4d) return 0;
    value->text = context;
    return 1;
}
static C3NameEvent choose_preset(C3NameEntry *e)
{
    if (e->choice >= e->text->preset_count) { e->phase = C3_NAME_KEYBOARD; e->row = e->column = 0; return C3_NAME_CHANGED; }
    /* msgctrlSetValue(0x4D, name); winMsgOpen(2, 0x2EF6, ...); yes/no. */
    char error[C3_STORY_ERROR];
    const char *name = e->text->presets[e->choice];
    if (!e->messages || !c3_story_render(e->messages, 0x2EF6, prompt_source, (void *)name, 0, e->prompt, sizeof e->prompt, error))
        snprintf(e->prompt, sizeof e->prompt, "%s?", name);
    e->phase = C3_NAME_CONFIRM; e->yes = 1;
    return C3_NAME_CHANGED;
}
static C3NameEvent done(C3NameEntry *e)
{
    /* A name may not be empty, end in spaces or consist of spaces only. */
    while (e->length && e->name[e->length - 1] == ' ') e->name[--e->length] = 0;
    if (!e->length) return C3_NAME_NONE;
    name_utf8(e->name, e->length, e->result, sizeof e->result);
    return C3_NAME_DONE;
}
static C3NameEvent back(C3NameEntry *e)
{
    if (e->length) { e->name[--e->length] = 0; return C3_NAME_CHANGED; }
    /* inputName returning to the preset list (modes with presets). */
    if (e->text->preset_count) { e->phase = C3_NAME_PRESETS; return C3_NAME_CHANGED; }
    return C3_NAME_NONE;
}
static C3NameEvent activate(C3NameEntry *e)
{
    if (e->row == C3_NAME_ENTRY_ROWS) {
        switch (e->column) {
        case BUTTON_UPPER: e->page = 0; return C3_NAME_CHANGED;
        case BUTTON_LOWER: e->page = 1; return C3_NAME_CHANGED;
        case BUTTON_BACK: return back(e);
        default: return done(e);
        }
    }
    uint16_t key = e->text->keys[e->page][e->row][e->column];
    if (!key || e->length >= e->text->max_length || e->length >= C3_NAME_ENTRY_MAX) return C3_NAME_NONE;
    e->name[e->length++] = key;
    e->name[e->length] = 0;
    return C3_NAME_CHANGED;
}
/* The nearest key in a row (gaps are skipped), searching outward. */
static unsigned nearest(const C3NameEntry *e, unsigned row, unsigned column)
{
    for (unsigned d = 0; d < C3_NAME_ENTRY_COLUMNS; ++d) {
        if (column >= d && e->text->keys[e->page][row][column - d]) return column - d;
        if (column + d < C3_NAME_ENTRY_COLUMNS && e->text->keys[e->page][row][column + d]) return column + d;
    }
    return column;
}
static void move_keyboard(C3NameEntry *e, uint16_t pressed)
{
    if (pressed & (PAD_LEFT | PAD_RIGHT)) {
        unsigned count = e->row == C3_NAME_ENTRY_ROWS ? BUTTONS : C3_NAME_ENTRY_COLUMNS;
        for (unsigned step = 0; step < count; ++step) {
            e->column = pressed & PAD_LEFT ? (e->column ? e->column - 1 : count - 1) : (e->column + 1 < count ? e->column + 1 : 0);
            if (e->row == C3_NAME_ENTRY_ROWS || e->text->keys[e->page][e->row][e->column]) break;
        }
    }
    if (pressed & (PAD_UP | PAD_DOWN)) {
        unsigned rows = C3_NAME_ENTRY_ROWS + 1, before = e->row;
        e->row = pressed & PAD_UP ? (e->row ? e->row - 1 : rows - 1) : (e->row + 1 < rows ? e->row + 1 : 0);
        if (e->row == C3_NAME_ENTRY_ROWS) e->column = e->column * BUTTONS / C3_NAME_ENTRY_COLUMNS;
        else {
            if (before == C3_NAME_ENTRY_ROWS) e->column = e->column * C3_NAME_ENTRY_COLUMNS / BUTTONS + 1;
            e->column = nearest(e, e->row, e->column);
        }
    }
}

C3NameEvent c3_name_entry_update(C3NameEntry *e, uint16_t pressed, int touch_pressed, unsigned x, unsigned y)
{
    C3NameEvent event = C3_NAME_NONE;
    if (!e || !e->text) return C3_NAME_NONE;
    switch (e->phase) {
    case C3_NAME_PRESETS: {
        unsigned rows = e->text->preset_count + 1;
        if (touch_pressed && y >= LIST_Y && y < LIST_Y + rows * LIST_H) { e->choice = (y - LIST_Y) / LIST_H; return choose_preset(e); }
        if (pressed & PAD_UP) { e->choice = e->choice ? e->choice - 1 : rows - 1; event = C3_NAME_CHANGED; }
        if (pressed & PAD_DOWN) { e->choice = e->choice + 1 < rows ? e->choice + 1 : 0; event = C3_NAME_CHANGED; }
        /* B (menu 0x70 returning 0) goes to the keyboard. */
        if (pressed & PAD_B) { e->phase = C3_NAME_KEYBOARD; e->row = e->column = 0; return C3_NAME_CHANGED; }
        if (pressed & PAD_A) return choose_preset(e);
        return event; }
    case C3_NAME_CONFIRM:
        if (touch_pressed && x >= 180 && y >= 165 && y < 215) { e->yes = y < 187; pressed |= PAD_A; }
        if (pressed & (PAD_UP | PAD_DOWN)) { e->yes = !e->yes; event = C3_NAME_CHANGED; }
        if (pressed & PAD_B) { e->phase = C3_NAME_PRESETS; return C3_NAME_CHANGED; }
        if (pressed & PAD_A) {
            if (!e->yes) { e->phase = C3_NAME_PRESETS; return C3_NAME_CHANGED; }
            set_name(e, e->text->presets[e->choice]);
            return done(e);
        }
        return event;
    case C3_NAME_KEYBOARD:
        if (touch_pressed) {
            if (x >= KEY_X && x < KEY_X + C3_NAME_ENTRY_COLUMNS * KEY_W && y >= KEY_Y && y < KEY_Y + C3_NAME_ENTRY_ROWS * KEY_H) {
                unsigned row = (y - KEY_Y) / KEY_H, column = (x - KEY_X) / KEY_W;
                if (e->text->keys[e->page][row][column]) { e->row = row; e->column = column; return activate(e); }
            } else if (y >= (unsigned)BUTTON_Y && y < (unsigned)(BUTTON_Y + BUTTON_H) && x >= 15 && x < 15 + BUTTONS * 74) {
                e->row = C3_NAME_ENTRY_ROWS; e->column = (x - 15) / 74;
                C3NameEvent r = activate(e);
                return r == C3_NAME_NONE ? C3_NAME_CHANGED : r;
            }
        }
        if (pressed & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) { move_keyboard(e, pressed); event = C3_NAME_CHANGED; }
        if (pressed & PAD_R) { e->page ^= 1u; if (e->row < C3_NAME_ENTRY_ROWS) e->column = nearest(e, e->row, e->column); event = C3_NAME_CHANGED; }
        if (pressed & PAD_B) { C3NameEvent r = back(e); if (r != C3_NAME_NONE) event = r; }
        if (pressed & PAD_START) { C3NameEvent r = done(e); if (r != C3_NAME_NONE) return r; }
        else if (pressed & PAD_A) { C3NameEvent r = activate(e); if (r != C3_NAME_NONE) event = r; }
        return event;
    }
    return C3_NAME_NONE;
}

void c3_name_entry_draw(const C3NameEntry *e, C3BottomUi *ui)
{
    char shown[C3_NAME_ENTRY_MAX * 3u + 2u], key[4];
    if (!e || !e->text || !ui) return;
    name_utf8(e->name, e->length, shown, sizeof shown - 1);
    if (e->phase == C3_NAME_KEYBOARD && e->length < e->text->max_length) strcat(shown, "_");
    c3_ui_header(ui, e->text->title, e->phase == C3_NAME_KEYBOARD ? shown : "");
    if (e->phase != C3_NAME_KEYBOARD) {
        c3_ui_panel(ui, 9, LIST_Y - 6, 302, (int)(e->text->preset_count + 1) * LIST_H + 10);
        for (unsigned i = 0; i <= e->text->preset_count; ++i)
            c3_ui_row(ui, LIST_Y + (int)i * LIST_H, i < e->text->preset_count ? e->text->presets[i] : e->text->new_name,
                      e->choice == i, 0);
        if (e->phase == C3_NAME_CONFIRM) {
            c3_ui_panel(ui, 7, 165, 168, 50);
            c3_ui_text(ui, 18, 172, 150, 2, e->prompt, C3_UI_WHITE);
            c3_ui_choices(ui, (int)e->yes);
            c3_ui_footer(ui, "D-pad: choose   A: confirm   B: back");
        } else c3_ui_footer(ui, "A: choose   B: enter a new name");
        return;
    }
    for (unsigned r = 0; r < C3_NAME_ENTRY_ROWS; ++r)
        for (unsigned c = 0; c < C3_NAME_ENTRY_COLUMNS; ++c) {
            uint16_t code = e->text->keys[e->page][r][c];
            int x = KEY_X + (int)c * KEY_W, y = KEY_Y + (int)r * KEY_H, selected = e->row == r && e->column == c;
            if (!code) continue;
            c3_ui_rect(ui, x + 1, y + 1, KEY_W - 2, KEY_H - 2, selected ? 0x30494bu : 0x16262bu);
            if (selected) c3_ui_rect(ui, x + 1, y + KEY_H - 3, KEY_W - 2, 2, C3_UI_GOLD);
            key[utf8_encode(code, key)] = 0;
            c3_ui_text(ui, x + 4, y + 3, KEY_W - 2, 1, key, C3_UI_WHITE);
        }
    const char *labels[BUTTONS] = {e->text->upper, e->text->lower, e->text->back, e->text->done};
    for (unsigned b = 0; b < BUTTONS; ++b) {
        int x = 15 + (int)b * 74, selected = e->row == C3_NAME_ENTRY_ROWS && e->column == b;
        int current = (b == BUTTON_UPPER && !e->page) || (b == BUTTON_LOWER && e->page);
        c3_ui_rect(ui, x, BUTTON_Y, BUTTON_W, BUTTON_H, selected ? 0x30494bu : current ? 0x24383cu : 0x16262bu);
        if (selected) c3_ui_rect(ui, x, BUTTON_Y + BUTTON_H - 2, BUTTON_W, 2, C3_UI_GOLD);
        c3_ui_text(ui, x + 6, BUTTON_Y + 2, BUTTON_W - 8, 1, labels[b], current ? C3_UI_GOLD : C3_UI_WHITE);
    }
    c3_ui_footer(ui, e->text->toggle);
}
