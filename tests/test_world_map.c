#include "colosseum3ds/world_map.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct { uint32_t set[8]; unsigned count, reads; int fail; } Flags;
static int read_flag(void *context, uint32_t flag, uint32_t *value)
{
    Flags *f = context;
    ++f->reads;
    if (f->fail) return 0;
    *value = 0;
    for (unsigned i = 0; i < f->count; ++i) if (f->set[i] == flag) *value = 1;
    return 1;
}

int main(void)
{
    C3WorldMapEntry entry;
    C3WorldMap map;
    Flags flags = {0};
    uint32_t floor = 0;
    int travel = -1;

    /* Pinned REL 3 table: nine destinations; row 0 is Outskirt Stand and is
     * always available, row 5 is Phenac City behind flag 0x3DF. */
    CHECK(c3_world_map_entry_count() == 9u);
    CHECK(c3_world_map_entry(0, &entry) && entry.flag == 0 && entry.floor == 1 && entry.item == 0x907);
    CHECK(c3_world_map_entry(5, &entry) && entry.flag == 0x3DF && entry.floor == 3 && entry.item == 0x90C);
    CHECK(!c3_world_map_entry(9, &entry) && !c3_world_map_entry(0, NULL));

    /* Before Willie's result only Outskirt Stand is selectable. */
    CHECK(c3_world_map_open(&map, 1, read_flag, &flags));
    CHECK(map.visible_count == 1 && map.enabled == 1u && map.cursor == 0 && map.origin_floor == 1);
    CHECK(flags.reads == 8);
    CHECK(!c3_world_map_move(&map, 4) && map.cursor == 0);
    CHECK(c3_world_map_choose(&map, 0, read_flag, &flags, &floor, &travel) && floor == 1 && travel == 0);
    CHECK(c3_world_map_choose(&map, -1, read_flag, &flags, &floor, &travel) && floor == 1 && travel == 0);
    CHECK(!c3_world_map_choose(&map, 1, read_flag, &flags, &floor, &travel));

    /* With 0x3DF set Phenac is the second visible entry. A link matches when
     * its mask contains every pressed direction, so DOWN, RIGHT and
     * DOWN+RIGHT from Outskirt all reach it (link masks 0x08 and 0x0C); LEFT
     * and UP have no enabled target. */
    flags.set[flags.count++] = 0x3DF;
    CHECK(c3_world_map_open(&map, 1, read_flag, &flags));
    CHECK(map.visible_count == 2 && map.enabled == (1u | 1u << 5) && map.cursor == 0);
    CHECK(c3_world_map_visible(&map, 0) == 0 && c3_world_map_visible(&map, 1) == 5 && c3_world_map_visible(&map, 2) == -1);
    CHECK(!c3_world_map_move(&map, 1) && !c3_world_map_move(&map, 8) && !c3_world_map_move(&map, 0));
    CHECK(c3_world_map_move(&map, 4) && map.cursor == 1);
    CHECK(c3_world_map_choose(&map, (int)map.cursor, read_flag, &flags, &floor, &travel) && floor == 3 && travel == 1);
    /* Phenac links back to Outskirt with UP (link mask 0x02). */
    CHECK(!c3_world_map_move(&map, 4) && c3_world_map_move(&map, 8) && map.cursor == 0);
    CHECK(c3_world_map_move(&map, 2) && map.cursor == 1);
    map.cursor = 0;
    CHECK(c3_world_map_move(&map, 4 | 2) && map.cursor == 1);
    /* DOWN+LEFT is in no link from Outskirt (masks 0x09 lead to a disabled entry). */
    map.cursor = 0;
    CHECK(!c3_world_map_move(&map, 4 | 1) && map.cursor == 0);

    /* Arriving from Phenac puts the cursor on Phenac; cancel returns there. */
    CHECK(c3_world_map_open(&map, 3, read_flag, &flags) && map.cursor == 1);
    CHECK(c3_world_map_choose(&map, -1, read_flag, &flags, &floor, &travel) && floor == 3 && travel == 0);
    CHECK(c3_world_map_choose(&map, 0, read_flag, &flags, &floor, &travel) && floor == 1 && travel == 1);

    /* Floor 0x7B is shown as entry 0x66; choosing 0x66 with flag 0x45D set
     * links to 0x7B, and 0x7A <-> 0x0F skips the journey. */
    flags.set[flags.count++] = 0xA59; flags.set[flags.count++] = 0x45D;
    flags.set[flags.count++] = 0x3F8; flags.set[flags.count++] = 0xA0E;
    CHECK(c3_world_map_open(&map, 0x7B, read_flag, &flags) && map.origin_floor == 0x66);
    {int index = c3_world_map_visible(&map, map.cursor);
     CHECK(index == 4 && c3_world_map_entry((uint32_t)index, &entry) && entry.floor == 0x66);}
    CHECK(c3_world_map_choose(&map, (int)map.cursor, read_flag, &flags, &floor, &travel) && floor == 0x7B && travel == 0);
    CHECK(c3_world_map_open(&map, 0x0F, read_flag, &flags));
    for (uint32_t i = 0; i < map.visible_count; ++i) {
        int index = c3_world_map_visible(&map, i);
        CHECK(index >= 0 && c3_world_map_entry((uint32_t)index, &entry));
        CHECK(c3_world_map_choose(&map, (int)i, read_flag, &flags, &floor, &travel));
        if (entry.floor == 0x7A) CHECK(floor == 0x7A && travel == 0);
        else if (entry.floor == 0x0F) CHECK(floor == 0x0F && travel == 0);
        else CHECK(travel == 1);
    }

    /* A failed flag read leaves the previous state untouched. */
    {C3WorldMap before = map;
     flags.fail = 1;
     CHECK(!c3_world_map_open(&map, 1, read_flag, &flags));
     CHECK(map.cursor == before.cursor && map.enabled == before.enabled && map.previous_floor == before.previous_floor);}
    CHECK(!c3_world_map_open(NULL, 1, read_flag, &flags) && !c3_world_map_open(&map, 1, NULL, NULL));

    /* Info text (fn_800246FC over lbl_3_data_90, columns lbl_802E4F58).
     * Just after the Phenac troop battle (0x3EC, 0xA59) the first passing
     * row is row 8 (flag 0): Outskirt 3CDA, Phenac 3CDC, entry 0x66
     * 3CDE "CONSTRUCTION LOT", Pyrite 3CE9; Snagem has no text yet. */
    {   Flags early = {{0x3DF, 0x3EC, 0xA59}, 3, 0, 0};
        CHECK(c3_world_map_info_message(0, read_flag, &early) == 0x3CDAu);
        CHECK(c3_world_map_info_message(5, read_flag, &early) == 0x3CDCu);
        CHECK(c3_world_map_info_message(4, read_flag, &early) == 0x3CDEu);
        CHECK(c3_world_map_info_message(6, read_flag, &early) == 0x3CE9u);
        CHECK(c3_world_map_info_message(1, read_flag, &early) == 1u);
        CHECK(c3_world_map_info_message(9, read_flag, &early) == 1u);
        /* The finished tower (0x45D, row 1): entry 0x66 is 3CF6 "REALGAM
         * TOWER"; 0xA0E (row 3) names entry 0x41 3CDD "MYSTERY LAB";
         * 0x477 (row 0) gives every place, Snagem Hideout 3D0E. */
        early.set[early.count++] = 0xA0E;
        CHECK(c3_world_map_info_message(2, read_flag, &early) == 0x3CDDu);
        CHECK(c3_world_map_info_message(4, read_flag, &early) == 0x3D16u);
        early.set[early.count++] = 0x45D;
        CHECK(c3_world_map_info_message(4, read_flag, &early) == 0x3CF6u);
        early.set[early.count++] = 0x477;
        CHECK(c3_world_map_info_message(1, read_flag, &early) == 0x3D0Eu);
        CHECK(c3_world_map_info_message(4, read_flag, &early) == 0x3D08u);
        early.fail = 1;
        CHECK(c3_world_map_info_message(4, read_flag, &early) == 0u);
        CHECK(c3_world_map_info_message(4, NULL, NULL) == 0u); }

    /* Journey rows (fn_80025F84 over lbl_3_data_270). */
    {   C3WorldMapJourney journey = {0}; uint32_t anim = 99, camera = 0;
        CHECK(c3_world_map_journey_west(0, 100, 40) == 1 && c3_world_map_journey_west(1, 100, 160) == 0);
        CHECK(c3_world_map_journey_west(1, 100, 100) == 1 && c3_world_map_journey_west(0, 100, 100) == 0);
        CHECK(c3_world_map_journey_pick(&journey, 0, &anim, &camera) == 10u && anim == 0u && camera == 0x0C7B1800u);
        CHECK(journey.east_index == 1u && journey.west_index == 0u);
        CHECK(c3_world_map_journey_pick(&journey, 1, &anim, &camera) == 0u && anim == 5u && camera == 0x0C761800u);
        for (int i = 0; i < 8; ++i) (void)c3_world_map_journey_pick(&journey, 0, NULL, NULL);
        CHECK(journey.east_index == 9u);
        CHECK(c3_world_map_journey_pick(&journey, 0, &anim, &camera) == 19u && anim == 14u && camera == 0x0C6D1800u);
        CHECK(journey.east_index == 0u);
        CHECK(c3_world_map_journey_pick(&journey, 0, &anim, &camera) == 10u && anim == 0u); }

    printf("world-map: %u checks passed\n", checks);
    return 0;
}
