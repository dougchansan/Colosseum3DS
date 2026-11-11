#include "world_map_scene.h"
#include "bottom_screen.h"
#include "field_scene.h"
#include "n3ds_platform.h"
#include "colosseum3ds/floor_profile.h"
#include "colosseum3ds/world_map.h"

#include <3ds.h>
#include <stdio.h>

#define ROW_TOP 52
#define ROW_HEIGHT 18

/* Port labels for the nine destination floors of the pinned table. The
 * original menu strings are not extracted yet (docs/world-map.md). */
static const char *label(uint32_t floor)
{
    switch (floor) {
    case 0x01: return "OUTSKIRT STAND";
    case 0x03: return "PHENAC CITY";
    case 0x0F: return "PYRITE TOWN";
    case 0x24: return "AGATE VILLAGE";
    case 0x4A: return "MT. BATTLE";
    case 0x7A: return "THE UNDER";
    case 0x66: return "REALGAM TOWER";
    case 0x41: return "SHADOW PKMN LAB";
    case 0x88: return "SNAGEM HIDEOUT";
    default: return "UNKNOWN AREA";
    }
}

static int read_flag(void *context, uint32_t flag, uint32_t *value)
{
    return c3_game_state_get_flag(context, flag, value);
}

int c3_world_map_scene_available(void) { return 1; }

static void draw(const C3WorldMap *map, const char *note)
{
    C3BottomUi *ui = c3_bottom_screen_begin();
    c3_ui_header(ui, "WORLD MAP", "Choose a destination");
    c3_ui_panel(ui, 7, 46, 306, 170);
    for (uint32_t i = 0; i < map->visible_count; ++i) {
        C3WorldMapEntry entry;
        int index = c3_world_map_visible(map, i);
        if (index < 0 || !c3_world_map_entry((uint32_t)index, &entry)) continue;
        c3_ui_row_value(ui, ROW_TOP + (int)i * ROW_HEIGHT, label(entry.floor),
                        c3_field_scene_supports(entry.floor) ? NULL : "not prepared",
                        i == map->cursor, !c3_field_scene_supports(entry.floor));
    }
    c3_ui_footer(ui, note ? note : "Touch or A: go   B: back");
    c3_bottom_screen_present();
}

int c3_world_map_scene_run(C3GameState *state, FILE *trace, char *error, size_t capacity)
{
    C3GameSummary summary;
    C3WorldMap map;
    C3N3dsInput input = {0};
    const char *note = NULL;
    int dirty = 1, released = 0, armed = -1;
    if (!state || !c3_game_state_summary(state, &summary) || summary.floor != C3_FLOOR_WORLD_MAP_ID ||
        !c3_world_map_open(&map, summary.previous_floor, read_flag, state)) {
        snprintf(error, capacity, "The world map could not be opened from this state.");
        return -1;
    }
    if (trace) {
        fprintf(trace, "world_map_begin previous=%lu enabled=%03lx visible=%lu cursor=%lu save_writes=0\n",
                (unsigned long)summary.previous_floor, (unsigned long)map.enabled,
                (unsigned long)map.visible_count, (unsigned long)map.cursor);
        fflush(trace);
    }
    while (c3_n3ds_poll(&input)) {
        int choice = -2; /* -2 none, -1 cancel, otherwise visible index */
        if (!input.held && !input.touch_held) released = 1;
        if (released) {
            if ((input.pressed & C3_PAD_UP) && map.cursor) { --map.cursor; dirty = 1; note = NULL; }
            if ((input.pressed & C3_PAD_DOWN) && map.cursor + 1u < map.visible_count) { ++map.cursor; dirty = 1; note = NULL; }
            if (input.pressed & C3_PAD_A) choice = (int)map.cursor;
            if (input.pressed & C3_PAD_B) choice = -1;
            /* A tap selects a row; lifting the stylus on the same row goes. */
            if (input.touch_held || input.touch_released) {
                int row = input.touch_y >= ROW_TOP && input.touch_x >= 15 && input.touch_x < 305
                    ? (input.touch_y - ROW_TOP) / ROW_HEIGHT : -1;
                if (row < 0 || (uint32_t)row >= map.visible_count) row = -1;
                if (input.touch_pressed) armed = row;
                if (row >= 0 && input.touch_held && (uint32_t)row != map.cursor && armed == row) {
                    map.cursor = (uint32_t)row; dirty = 1; note = NULL;
                }
                if (input.touch_released) {
                    if (row >= 0 && row == armed) choice = row;
                    armed = -1;
                }
            }
        }
        if (choice != -2) {
            uint32_t floor = 0;
            int travel = 0;
            if (!c3_world_map_choose(&map, choice, read_flag, state, &floor, &travel)) {
                snprintf(error, capacity, "The world-map destination is invalid.");
                return -1;
            }
            if (!c3_field_scene_supports(floor)) {
                note = "That area is not prepared in this build.";
                dirty = 1;
            } else {
                /* floorLink(floor, 0): entrance 0, arriving from floor 0x38E. */
                if (!c3_game_state_set_floor(state, floor, C3_FLOOR_WORLD_MAP_ID, 0)) {
                    snprintf(error, capacity, "The world-map destination could not be recorded.");
                    return -1;
                }
                if (trace) {
                    fprintf(trace, "world_map_link floor=%lu entrance=0 cancel=%d journey=%d journey_presented=0 save_writes=0\n",
                            (unsigned long)floor, choice < 0, travel);
                    fflush(trace);
                }
                c3_bottom_screen_notice(label(floor), "Travelling...", "Please wait...");
                return 2;
            }
        }
        if (dirty) { draw(&map, note); dirty = 0; }
        gspWaitForVBlank();
    }
    return 1;
}
