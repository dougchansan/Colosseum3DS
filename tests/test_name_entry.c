#include "colosseum3ds/name_entry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

enum { LEFT = 1, RIGHT = 2, DOWN = 4, UP = 8, A = 0x100, B = 0x200, START = 0x1000 };

static C3NameEvent pad(C3NameEntry *e, uint16_t pressed) { return c3_name_entry_update(e, pressed, 0, 0, 0, 0, 0); }
/* One stylus tap: press, then lift at the same place. */
static C3NameEvent tap(C3NameEntry *e, unsigned x, unsigned y)
{
    CHECK(c3_name_entry_update(e, 0, 1, 1, 0, x, y) == C3_NAME_CHANGED);
    return c3_name_entry_update(e, 0, 0, 0, 1, x, y);
}

int main(void)
{
    C3NameEntry e;
    /* The initial name is shown and bounded to ten characters. */
    c3_name_entry_init(&e, "RUI");
    CHECK(!strcmp(e.text, "RUI") && e.length == 3 && e.row == 0 && e.column == 0 && e.armed == -1);
    c3_name_entry_init(&e, "ABCDEFGHIJKLMNOP");
    CHECK(e.length == 10 && !strcmp(e.text, "ABCDEFGHIJ"));
    c3_name_entry_init(&e, NULL);
    CHECK(e.length == 0 && !e.text[0]);

    /* Buttons: A types the key under the cursor, B deletes, START confirms. */
    c3_name_entry_init(&e, "RUI");
    CHECK(pad(&e, B) == C3_NAME_CHANGED && !strcmp(e.text, "RU"));
    CHECK(pad(&e, B) == C3_NAME_CHANGED && pad(&e, B) == C3_NAME_CHANGED && e.length == 0);
    CHECK(pad(&e, B) == C3_NAME_NONE);
    CHECK(pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.text, "A"));
    CHECK(pad(&e, RIGHT) == C3_NAME_CHANGED && pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.text, "AB"));
    CHECK(pad(&e, LEFT) == C3_NAME_CHANGED && pad(&e, LEFT) == C3_NAME_CHANGED && e.column == 9);
    CHECK(pad(&e, DOWN) == C3_NAME_CHANGED && e.row == 1 && pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.text, "ABT"));
    CHECK(pad(&e, START) == C3_NAME_DONE && !strcmp(e.text, "ABT"));

    /* The field holds ten characters; a leading or trailing space is not kept. */
    c3_name_entry_init(&e, NULL);
    e.row = 2; e.column = 6; /* the space key */
    CHECK(pad(&e, A) == C3_NAME_NONE && e.length == 0);
    e.row = 0; e.column = 0;
    for (unsigned i = 0; i < 10; ++i) CHECK(pad(&e, A) == C3_NAME_CHANGED);
    CHECK(pad(&e, A) == C3_NAME_NONE && e.length == 10 && !strcmp(e.text, "AAAAAAAAAA"));
    c3_name_entry_init(&e, NULL);
    CHECK(pad(&e, A) == C3_NAME_CHANGED);
    e.row = 2; e.column = 6;
    CHECK(pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.text, "A "));
    CHECK(pad(&e, START) == C3_NAME_DONE && !strcmp(e.text, "A") && e.length == 1);

    /* An empty confirm restores the initial name; with none it does nothing. */
    c3_name_entry_init(&e, "RUI");
    while (e.length) CHECK(pad(&e, B) == C3_NAME_CHANGED);
    CHECK(pad(&e, START) == C3_NAME_DONE && !strcmp(e.text, "RUI"));
    c3_name_entry_init(&e, NULL);
    CHECK(pad(&e, START) == C3_NAME_NONE && e.length == 0);

    /* The button row: DELETE under the left half, OK under the right. */
    c3_name_entry_init(&e, "RUI");
    CHECK(pad(&e, UP) == C3_NAME_CHANGED && e.row == C3_NAME_ENTRY_ROWS && e.column == 0);
    CHECK(pad(&e, A) == C3_NAME_CHANGED && !strcmp(e.text, "RU"));
    CHECK(pad(&e, RIGHT) == C3_NAME_CHANGED && e.column == 1 && pad(&e, A) == C3_NAME_DONE);
    CHECK(pad(&e, DOWN) == C3_NAME_CHANGED && e.row == 0 && e.column == 7);

    /* Stylus: key (row 1, column 2) is 'M' at x 20+2*28.., y 60+20..; a lift
     * elsewhere types nothing. */
    c3_name_entry_init(&e, NULL);
    CHECK(tap(&e, 20 + 2 * 28 + 5, 60 + 20 + 5) == C3_NAME_CHANGED && !strcmp(e.text, "M") && e.row == 1 && e.column == 2);
    CHECK(c3_name_entry_update(&e, 0, 1, 1, 0, 25, 65) == C3_NAME_CHANGED && e.armed == 0);
    CHECK(c3_name_entry_update(&e, 0, 0, 0, 1, 200, 65) == C3_NAME_CHANGED && !strcmp(e.text, "M") && e.armed == -1);
    CHECK(tap(&e, 5, 5) == C3_NAME_CHANGED && !strcmp(e.text, "M"));      /* outside every key */
    CHECK(tap(&e, 30, 205) == C3_NAME_CHANGED && e.length == 0);          /* DELETE */
    CHECK(tap(&e, 25, 65) == C3_NAME_CHANGED && !strcmp(e.text, "A"));
    CHECK(tap(&e, 200, 205) == C3_NAME_DONE && !strcmp(e.text, "A"));     /* OK */

    /* Drawing covers every state without touching memory outside the frame. */
    {
        unsigned char *pixels = calloc(320u * 240u, 3u);
        C3BottomUi ui;
        CHECK(pixels != NULL);
        c3_ui_begin(&ui, pixels, C3_UI_RGB8, NULL, NULL);
        c3_name_entry_init(&e, "RUI");
        c3_name_entry_draw(&e, &ui, "PARTNER'S NAME");
        e.row = C3_NAME_ENTRY_ROWS; e.column = 1; e.armed = 3;
        c3_name_entry_draw(&e, &ui, NULL);
        c3_name_entry_draw(NULL, &ui, NULL);
        c3_name_entry_draw(&e, NULL, NULL);
        free(pixels);
    }
    CHECK(c3_name_entry_update(NULL, A, 0, 0, 0, 0, 0) == C3_NAME_NONE);
    printf("name-entry: %u checks passed\n", checks);
    return 0;
}
