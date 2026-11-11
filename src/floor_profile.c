#include "colosseum3ds/floor_profile.h"

#include <stddef.h>

static const C3FloorProfile profiles[] = {
    /* Lifecycle row 0: S1_out.fsys, group 0x1D. */
    {0x01u, "outskirt", "outskirt.c3s", "OUTSKIRT STAND", C3_FLOOR_ARRIVAL},
    /* Lifecycle row 1: S1_shop_1F.fsys, group 0x1E. */
    {0x02u, "shop", "outskirt_shop.c3s", "OUTSKIRT STAND",
     C3_FLOOR_TV | C3_FLOOR_ROOM_CAMERA | C3_FLOOR_LIGHTS},
    /* Lifecycle row 2: M1_out.fsys, group 0x1F. */
    {0x03u, "phenac", "phenac.c3s", "PHENAC CITY", C3_FLOOR_ROOM_CAMERA},
    /* Lifecycle row 121: world_map.fsys, group 0x13. */
    {C3_FLOOR_WORLD_MAP_ID, "worldmap", NULL, "WORLD MAP", C3_FLOOR_WORLD_MAP},
};

const C3FloorProfile *c3_floor_profile(uint32_t floor)
{
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i)
        if (profiles[i].floor == floor) return profiles + i;
    return NULL;
}
