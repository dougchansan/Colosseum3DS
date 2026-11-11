#ifndef C3_WORLD_MAP_H
#define C3_WORLD_MAP_H

#include <stdint.h>

/* World-map destination logic lowered from the pinned decomp:
 * tables from src/rel/worldmap_menu_rel/worldmap_menu_rel.c (compiled
 * unchanged), behaviour from src/game/gs_title.c (fn_800259B0 entry state,
 * the destination menu ending in floorLink(floor, 0), and the cursor-link
 * search fn_80024160). The 3D map, bike ride and menu art are presentation
 * and are not part of this module. */

/* lbl_3_data_0 row: unlock flag (0 = always), destination floor, menu item
 * ID used by the cursor-link table, location image resource. */
typedef struct C3WorldMapEntry {
    uint32_t flag, floor, item, image;
} C3WorldMapEntry;

/* Reads one story flag; returns 0 on failure. */
typedef int (*C3WorldMapFlag)(void *context, uint32_t flag, uint32_t *value);

typedef struct C3WorldMap {
    uint32_t previous_floor; /* floorGetPrevFloorID() */
    uint32_t origin_floor;   /* lbl_8047A3AC: previous floor, 0x7B shown as 0x66 */
    uint32_t enabled;        /* one bit per table entry */
    uint32_t visible_count;
    uint32_t cursor;         /* lbl_8047A368: index among enabled entries */
} C3WorldMap;

uint32_t c3_world_map_entry_count(void);
int c3_world_map_entry(uint32_t index, C3WorldMapEntry *out);

/* Enables every entry whose flag is zero or set and puts the cursor on the
 * entry of the floor the player came from. Returns 0 if a flag read fails. */
int c3_world_map_open(C3WorldMap *map, uint32_t previous_floor,
                      C3WorldMapFlag flag, void *context);
/* Table index of the n-th enabled entry, or -1. */
int c3_world_map_visible(const C3WorldMap *map, uint32_t visible_index);
/* pressed uses GameCube PAD bits (LEFT 1, RIGHT 2, DOWN 4, UP 8). Follows the
 * original link table in order; returns 1 when the cursor moved. */
int c3_world_map_move(C3WorldMap *map, uint16_t pressed);
/* visible_index < 0 is the menu's cancel: the destination is the previous
 * floor. travel is 1 when the original plays the journey before the link.
 * The destination entrance is always 0 (floorLink(floor, 0)). */
int c3_world_map_choose(const C3WorldMap *map, int visible_index,
                        C3WorldMapFlag flag, void *context,
                        uint32_t *floor, int *travel);

#endif
