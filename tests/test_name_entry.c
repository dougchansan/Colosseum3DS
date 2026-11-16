#include "colosseum3ds/name_entry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

enum { LEFT = 1, RIGHT = 2, DOWN = 4, UP = 8, R = 0x20, A = 0x100, B = 0x200, START = 0x1000 };

/* A synthetic group with the name-entry messages' IDs and layout (the
 * original texts are private): UTF-8 here, stored as GSchars. */
static const struct { uint32_t id; const char *text; } texts[] = {
    {0x2EED, "Your name?"}, {0x2EEE, "Nickname?"}, {0x2EEF, "Box?"},
    {0x2EF0, "UPPER"}, {0x2EF1, "lower"}, {0x2EF3, "DONE"}, {0x2EF4, "BACK"}, {0x2EF5, "R BUTTON"},
    {0x2EF6, "Is \x01 OK?"}, {0x2EFD, "NEW NAME"},
    {0x2EFE, "WES"}, {0x2EFF, "SETH"}, {0x2F00, "THOMAS"},
    {0x2F01, "RUI"}, {0x2F02, "ANCA"}, {0x2F03, "YUKI"},
    {0x2F04, "ABCDE FGHIJ 789"}, {0x2F05, "KLMNO PQRST 456"}, {0x2F06, "UVWXY Z     123"},
    {0x2F07, "!?\xe2\x99\x82\xe2\x99\x80/-x\xe2\x80\x9c\xe2\x80\x9d'' 0.,"},
    {0x2F08, "abcde fghij 789"}, {0x2F09, "klmno pqrst 456"}, {0x2F0A, "uvwxy z     123"},
    {0x2F0B, "!?\xe2\x99\x82\xe2\x99\x80/-x\xe2\x80\x9c\xe2\x80\x9d'' 0.,"},
};

static unsigned build(unsigned char *g)
{
    unsigned count = sizeof texts / sizeof texts[0], at = 16 + count * 8;
    memset(g, 0, 4096);
    g[5] = (unsigned char)count;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char *e = g + 16 + i * 8;
        e[2] = (unsigned char)(texts[i].id >> 8); e[3] = (unsigned char)texts[i].id;
        e[6] = (unsigned char)(at >> 8); e[7] = (unsigned char)at;
        for (const unsigned char *p = (const unsigned char *)texts[i].text; *p;) {
            unsigned c;
            if (*p == 1) { g[at++] = 0xff; g[at++] = 0xff; g[at++] = 0x4d; ++p; continue; }
            if (*p < 0x80) c = *p++;
            else if ((*p & 0xe0) == 0xc0) { c = (unsigned)(p[0] & 0x1f) << 6 | (p[1] & 0x3f); p += 2; }
            else { c = (unsigned)(p[0] & 0x0f) << 12 | (unsigned)(p[1] & 0x3f) << 6 | (p[2] & 0x3f); p += 3; }
            g[at++] = (unsigned char)(c >> 8); g[at++] = (unsigned char)c;
        }
        g[at++] = 0; g[at++] = 0;
    }
    return at;
}

static C3NameEvent pad(C3NameEntry *e, uint16_t pressed) { return c3_name_entry_update(e, pressed, 0, 0, 0); }

int main(void)
{
    static unsigned char group[4096];
    char error[C3_STORY_ERROR];
    C3Story messages = {0};
    unsigned size = build(group);
    CHECK(c3_story_add_messages(&messages, group, size, error));

    /* Partner (mode 1): presets RUI/ANCA/YUKI, seven characters. */
    C3NameEntryText text;
    CHECK(c3_name_entry_text_load(&text, &messages, 1));
    CHECK(!strcmp(text.title, "Your name?") && text.preset_count == 3 && text.max_length == 7);
    CHECK(!strcmp(text.presets[0], "RUI") && !strcmp(text.presets[2], "YUKI") && !strcmp(text.new_name, "NEW NAME"));
    CHECK(text.keys[0][0][0] == 'A' && text.keys[0][0][5] == 0 && text.keys[1][0][0] == 'a');
    CHECK(text.keys[0][3][2] == 0x2642 && text.keys[0][3][3] == 0x2640 && text.keys[0][3][7] == 0x201c);
    CHECK(text.keys[0][2][6] == 'Z' && text.keys[0][2][7] == 0 && text.keys[0][2][14] == '3');

    /* A preset: A, then "Is RUI OK?" yes. */
    C3NameEntry e;
    c3_name_entry_init(&e, &text, &messages);
    CHECK(e.phase == C3_NAME_PRESETS && e.length == 3);
    CHECK(pad(&e, A) == C3_NAME_CHANGED && e.phase == C3_NAME_CONFIRM && !strcmp(e.prompt, "Is RUI OK?"));
    CHECK(pad(&e, A) == C3_NAME_DONE && !strcmp(e.result, "RUI"));
    /* "No" returns to the list; the second preset can be taken. */
    c3_name_entry_init(&e, &text, &messages);
    CHECK(pad(&e, DOWN) == C3_NAME_CHANGED && pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.prompt, "Is ANCA OK?"));
    CHECK(pad(&e, DOWN) == C3_NAME_CHANGED && !e.yes && pad(&e, A) == C3_NAME_CHANGED && e.phase == C3_NAME_PRESETS);
    CHECK(e.choice == 1 && pad(&e, A) == C3_NAME_CHANGED && e.yes && pad(&e, A) == C3_NAME_DONE && !strcmp(e.result, "ANCA"));

    /* NEW NAME: the keyboard, prefilled with the first preset. */
    c3_name_entry_init(&e, &text, &messages);
    for (int i = 0; i < 3; ++i) CHECK(pad(&e, DOWN) == C3_NAME_CHANGED);
    CHECK(e.choice == 3 && pad(&e, A) == C3_NAME_CHANGED && e.phase == C3_NAME_KEYBOARD && e.length == 3);
    CHECK(pad(&e, A) == C3_NAME_CHANGED && e.length == 4 && e.name[3] == 'A');
    /* Right skips the gap after E; R switches to lower case. */
    for (int i = 0; i < 5; ++i) CHECK(pad(&e, RIGHT) == C3_NAME_CHANGED);
    CHECK(e.column == 6 && pad(&e, R) == C3_NAME_CHANGED && e.page == 1 && pad(&e, A) == C3_NAME_CHANGED && e.name[4] == 'f');
    /* Seven characters at most. */
    CHECK(pad(&e, A) == C3_NAME_CHANGED && pad(&e, A) == C3_NAME_CHANGED && e.length == 7);
    CHECK(pad(&e, A) == C3_NAME_NONE && e.length == 7);
    /* B deletes; the gender sign is a key; START finishes in UTF-8. */
    CHECK(pad(&e, B) == C3_NAME_CHANGED && e.length == 6);
    e.row = 3; e.column = 2;
    CHECK(pad(&e, A) == C3_NAME_CHANGED && e.name[6] == 0x2642);
    CHECK(pad(&e, START) == C3_NAME_DONE && !strcmp(e.result, "RUIAff\xe2\x99\x82"));
    /* Down from the keys reaches the buttons; DONE / BACK / page buttons. */
    c3_name_entry_init(&e, &text, &messages);
    CHECK(pad(&e, B) == C3_NAME_CHANGED && e.phase == C3_NAME_KEYBOARD);
    for (int i = 0; i < 4; ++i) CHECK(pad(&e, DOWN) == C3_NAME_CHANGED);
    CHECK(e.row == C3_NAME_ENTRY_ROWS && e.column == 0);
    CHECK(pad(&e, RIGHT) == C3_NAME_CHANGED && pad(&e, A) == C3_NAME_CHANGED && e.page == 1);
    CHECK(pad(&e, RIGHT) == C3_NAME_CHANGED && e.column == 2);
    for (int i = 0; i < 3; ++i) CHECK(pad(&e, A) == C3_NAME_CHANGED);
    CHECK(e.length == 0 && pad(&e, A) == C3_NAME_CHANGED && e.phase == C3_NAME_PRESETS); /* BACK on empty */
    /* An empty name cannot be finished. */
    c3_name_entry_init(&e, &text, &messages);
    CHECK(pad(&e, B) == C3_NAME_CHANGED);
    while (e.length) CHECK(pad(&e, B) == C3_NAME_CHANGED);
    CHECK(pad(&e, START) == C3_NAME_NONE);
    /* Stylus: a list row, then "yes". */
    c3_name_entry_init(&e, &text, &messages);
    CHECK(c3_name_entry_update(&e, 0, 1, 100, 62 + 28 * 2 + 5) == C3_NAME_CHANGED && !strcmp(e.prompt, "Is YUKI OK?"));
    CHECK(c3_name_entry_update(&e, 0, 1, 200, 170) == C3_NAME_DONE && !strcmp(e.result, "YUKI"));

    /* Nickname (mode 2): no presets, ten characters, keyboard first. */
    CHECK(c3_name_entry_text_load(&text, &messages, 2) && text.preset_count == 0 && text.max_length == 10);
    c3_name_entry_init(&e, &text, &messages);
    CHECK(e.phase == C3_NAME_KEYBOARD && e.length == 0 && pad(&e, B) == C3_NAME_NONE);
    /* Box (mode 3): eight characters. */
    CHECK(c3_name_entry_text_load(&text, &messages, 3) && text.max_length == 8 && !c3_name_entry_text_load(&text, &messages, 4));

    /* Drawing touches only the bottom-screen buffer. */
    unsigned char *pixels = calloc(1, 320u * 240u * 3u);
    C3BottomUi ui;
    CHECK(c3_name_entry_text_load(&text, &messages, 1));
    c3_name_entry_init(&e, &text, &messages);
    c3_ui_begin(&ui, pixels, 0, NULL, NULL);
    c3_name_entry_draw(&e, &ui);
    CHECK(pad(&e, A) == C3_NAME_CHANGED);
    c3_name_entry_draw(&e, &ui);
    CHECK(pad(&e, B) == C3_NAME_CHANGED && pad(&e, B) == C3_NAME_CHANGED);
    c3_name_entry_draw(&e, &ui);
    free(pixels);
    c3_story_free(&messages);
    printf("name entry: %u checks passed\n", checks);
    return 0;
}
