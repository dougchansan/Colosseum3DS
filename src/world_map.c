#include "colosseum3ds/world_map.h"

#include <stddef.h>

/* Compile the pinned REL 3 tables unchanged, with private symbol names. */
#define lbl_80478DDC c3_wm_entries
#define lbl_80478DD8 c3_wm_entry_count
#define lbl_80478DE4 c3_wm_slots
#define lbl_80478DE0 c3_wm_slot_count
#define lbl_80478DEC c3_wm_cameras
#define lbl_80478DE8 c3_wm_camera_count
#define lbl_80478DF4 c3_wm_links
#define lbl_80478DF0 c3_wm_link_count
#define lbl_3_data_0 c3_wm_data_entries
#define lbl_3_data_90 c3_wm_data_slots
#define lbl_3_data_270 c3_wm_data_cameras
#define lbl_3_data_3B0 c3_wm_data_links
#define lbl_3_data_6C8 c3_wm_data_entry_count
#define lbl_3_data_6CC c3_wm_data_slot_count
#define lbl_3_data_6D0 c3_wm_data_camera_count
#define lbl_3_data_6D4 c3_wm_data_link_count
#define _prolog c3_wm_prolog
#define _epilog c3_wm_epilog
#define _unresolved c3_wm_unresolved
#include "../vendor/colosseum/src/rel/worldmap_menu_rel/worldmap_menu_rel.c"

/* The DOL globals the REL publishes through _prolog. */
void *c3_wm_entries, *c3_wm_slots, *c3_wm_cameras, *c3_wm_links;
u32 *c3_wm_entry_count, *c3_wm_slot_count, *c3_wm_camera_count, *c3_wm_link_count;

static void publish(void)
{
    if (!c3_wm_entries) c3_wm_prolog();
}

uint32_t c3_world_map_entry_count(void)
{
    publish();
    return *c3_wm_entry_count;
}

int c3_world_map_entry(uint32_t index, C3WorldMapEntry *out)
{
    const u32 *row;
    publish();
    if (!out || index >= *c3_wm_entry_count) return 0;
    row = (const u32 *)c3_wm_entries + index * 4u;
    out->flag = row[0]; out->floor = row[1]; out->item = row[2]; out->image = row[3];
    return 1;
}

int c3_world_map_open(C3WorldMap *map, uint32_t previous_floor,
                      C3WorldMapFlag flag, void *context)
{
    C3WorldMap next = {0};
    uint32_t count = c3_world_map_entry_count();
    if (!map || !flag || count > 32u) return 0;
    next.previous_floor = previous_floor;
    /* fn_800259B0: the Realgam alternate floor 0x7B is shown as 0x66. */
    next.origin_floor = previous_floor == 0x7Bu ? 0x66u : previous_floor;
    for (uint32_t i = 0; i < count; ++i) {
        const u32 *row = (const u32 *)c3_wm_entries + i * 4u;
        uint32_t value = 1;
        if (next.origin_floor == row[1]) next.cursor = next.visible_count;
        if (row[0] && !flag(context, row[0], &value)) return 0;
        if (value) { next.enabled |= 1u << i; ++next.visible_count; }
    }
    *map = next;
    return 1;
}

int c3_world_map_visible(const C3WorldMap *map, uint32_t visible_index)
{
    uint32_t count = c3_world_map_entry_count(), seen = 0;
    if (!map) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (!(map->enabled >> i & 1u)) continue;
        if (seen++ == visible_index) return (int)i;
    }
    return -1;
}

static int entry_of_item(uint32_t item)
{
    uint32_t count = c3_world_map_entry_count();
    for (uint32_t i = 0; i < count; ++i)
        if (((const u32 *)c3_wm_entries)[i * 4u + 2u] == item) return (int)i;
    return -1;
}

/* fn_80024160: PAD LEFT/RIGHT/DOWN/UP become link bits 1/4/8/2. The first
 * link, in table order, whose mask contains every pressed direction, whose
 * source is the active entry and whose target is enabled wins. */
int c3_world_map_move(C3WorldMap *map, uint16_t pressed)
{
    uint8_t mask = 0;
    int active;
    if (!map) return 0;
    if (pressed & 1u) mask |= 1u;
    if (pressed & 2u) mask |= 4u;
    if (pressed & 4u) mask |= 8u;
    if (pressed & 8u) mask |= 2u;
    if (!mask || (active = c3_world_map_visible(map, map->cursor)) < 0) return 0;
    for (uint32_t link = 0; link < *c3_wm_link_count; ++link) {
        const u32 *row = (const u32 *)c3_wm_links + link * 3u;
        int from, to;
        if (((row[0] >> 24) & mask) != mask) continue;
        from = entry_of_item(row[1]); to = entry_of_item(row[2]);
        if (from != active || to < 0 || !(map->enabled >> to & 1u)) continue;
        uint32_t visible = 0;
        for (int i = 0; i < to; ++i) visible += map->enabled >> i & 1u;
        if (visible == map->cursor) return 0;
        map->cursor = visible;
        return 1;
    }
    return 0;
}

int c3_world_map_choose(const C3WorldMap *map, int visible_index,
                        C3WorldMapFlag flag, void *context,
                        uint32_t *floor, int *travel)
{
    uint32_t chosen, previous;
    if (!map || !flag || !floor || !travel) return 0;
    previous = map->previous_floor;
    if (visible_index < 0) chosen = previous;
    else {
        int index = c3_world_map_visible(map, (uint32_t)visible_index);
        const u32 *row;
        if (index < 0) return 0;
        row = (const u32 *)c3_wm_entries + (uint32_t)index * 4u;
        chosen = row[1];
        if (chosen == 0x66u) {
            uint32_t alternate = 0;
            if (!flag(context, 0x45Du, &alternate)) return 0;
            if (alternate) chosen = 0x7Bu;
        }
    }
    /* The journey is skipped for the same floor and between 0x7A and 0x0F. */
    *travel = chosen != previous &&
              !((chosen == 0x7Au && previous == 0x0Fu) || (chosen == 0x0Fu && previous == 0x7Au));
    *floor = chosen;
    return 1;
}
