#include "colosseum3ds/stage_order.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Draw order of a pack per pass: returns the index of (model, translucent)
 * in the submitted sequence, walking groups exactly as the renderer does. */
static unsigned slot(size_t count, size_t leading, size_t model, unsigned translucent)
{
    unsigned at = 0;
    for (size_t group = 0, end; group < count; group = end) {
        end = c3_stage_draw_group_end(group, count, leading);
        for (unsigned pass = 0; pass < 2; ++pass)
            for (size_t m = group; m < end; ++m) {
                if (m == model && pass == translucent) return at;
                ++at;
            }
    }
    return ~0u;
}

int main(void)
{
    /* Default: one group, all opaque before all translucent. */
    CHECK(c3_stage_draw_group_end(0, 5, 0) == 5);
    CHECK(slot(5, 0, 0, 1) > slot(5, 0, 4, 0));
    /* Shop: map0, map1 (one original map), Wes, people. The map's
     * translucent light shaft precedes Wes's opaque pass, so Wes covers it. */
    CHECK(c3_stage_draw_group_end(0, 9, 2) == 2);
    CHECK(c3_stage_draw_group_end(2, 9, 2) == 3);
    CHECK(c3_stage_draw_group_end(8, 9, 2) == 9);
    CHECK(c3_stage_draw_group_end(9, 9, 2) == 9);
    CHECK(slot(9, 2, 0, 1) < slot(9, 2, 2, 0));
    CHECK(slot(9, 2, 1, 0) < slot(9, 2, 0, 1)); /* the map's packs stay opaque-first */
    CHECK(slot(9, 2, 2, 1) < slot(9, 2, 3, 0)); /* each person: opaque, translucent */
    /* A leading group larger than the scene clamps. */
    CHECK(c3_stage_draw_group_end(0, 3, 7) == 3);
    printf("stage order: %u checks passed\n", checks);
    return 0;
}
