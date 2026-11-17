#ifndef C3_TEST_DNA_ROUTE_H
#define C3_TEST_DNA_ROUTE_H
/* Headless player for the DNA disc puzzle (floor 0x387) shared by the story
 * route player and the crawler: when native CC waits with view.menu_floor
 * 0x387, the original puzzle rules (src/dna_key.c) are played with D-pad
 * and A presses, one wrong trio first (the retail buzzer and reset), then
 * the discs' species, and the retail result is reported to the script. */
#include "colosseum3ds/dna_key.h"
#include "colosseum3ds/field_story.h"

#include <stdio.h>
#include <stdlib.h>

static const uint16_t dna_route_grid[9] = {0xb9, 0xc8, 0xf4, 0x11f, 0x9c, 0xf3, 0x99, 0xf5, 0x9f};

static unsigned dna_route_go(C3DnaKey *s, unsigned cell)
{
    unsigned presses = 0;
    while (s->x != cell % 3 || s->y != cell / 3) {
        uint16_t k = 0;
        if (s->y > cell / 3) k = 1; else if (s->y < cell / 3) k = 2;
        else if (s->x > cell % 3) k = 4; else k = 8;
        c3_dna_key_input(s, k, 0); ++presses;
    }
    c3_dna_key_input(s, 0x10, 0);
    return presses + 1;
}

/* Returns the retail result (0 matched) or -1; *presses counts inputs. */
static int dna_route_play(const uint16_t expected[3], int wrong_first, unsigned *presses, unsigned *resets)
{
    C3DnaKey s; *presses = 0; *resets = 0;
    if (!c3_dna_key_init(&s, expected)) return s.result;
    unsigned want[3], wrong = 9;
    for (unsigned i = 0; i < 3; ++i) { unsigned j; for (j = 0; j < 9 && dna_route_grid[j] != expected[i]; ++j) {} want[i] = j; }
    for (unsigned j = 0; j < 9 && wrong == 9; ++j) if (j != want[0] && j != want[1] && j != want[2]) wrong = j;
    if (wrong_first && wrong < 9 && want[0] != want[1]) {
        *presses += dna_route_go(&s, want[0]); *presses += dna_route_go(&s, want[1]); *presses += dna_route_go(&s, wrong);
        if (!s.reset_pending || s.result != -1 || s.sound != 0x26) return -1;
        c3_dna_key_sound_done(&s); ++*resets;
        if (s.count) return -1;
    }
    /* A repeated species is one cell, which the grid cannot pick twice:
     * such a trio cannot be matched and the player backs out. */
    for (unsigned i = 0; i < 3 && s.result == -1; ++i) {
        if (s.occupied[want[i]]) break;
        *presses += dna_route_go(&s, want[i]);
    }
    if (s.result == -1) { c3_dna_key_input(&s, 0, 0x20); while (s.count) c3_dna_key_input(&s, 0, 0x20); c3_dna_key_input(&s, 0, 0x20); }
    return s.result;
}

/* Report the puzzle to a waiting CC. Returns 0 on a story error. */
static int dna_route_answer(C3FieldStory *story, FILE *report, const char *tag, uint32_t floor, char *error)
{
    const C3FieldStoryView *v = c3_field_story_view(story);
    if (v->menu_floor != 0x387) return 1;
    uint16_t expected[3] = {v->dna_species[0], v->dna_species[1], v->dna_species[2]};
    unsigned presses = 0, resets = 0;
    int result = dna_route_play(expected, 1, &presses, &resets);
    /* C3_DNA_CANCEL_FIRST: the first visit backs out with B instead (the
     * failure branch); later visits solve it. */
    static int cancelled;
    if (!cancelled && getenv("C3_DNA_CANCEL_FIRST")) { cancelled = 1; result = 1; }
    if (report) fprintf(report, "%s_dna floor=%03lx species=%x,%x,%x presses=%u wrong_trio_resets=%u result=%d\n", tag,
                        (unsigned long)floor, expected[0], expected[1], expected[2], presses, resets, result);
    return c3_field_story_dna_result(story, result < 0 ? 1 : result, error);
}
#endif
