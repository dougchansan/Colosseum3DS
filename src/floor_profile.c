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
    /* Phenac City interiors, lifecycle rows 62-71 and 115 (docs/phenac-floors.md). */
    {0x04u, "phenac_pc1f", "phenac_pc1f.c3s", "PHENAC POKEMON CENTER", 0u},
    {0x05u, "phenac_mayor1f", "phenac_mayor1f.c3s", "PHENAC MAYOR'S HOUSE", 0u},
    {0x06u, "phenac_mayor2f", "phenac_mayor2f.c3s", "PHENAC MAYOR'S HOUSE", 0u},
    {0x07u, "phenac_houseb1f", "phenac_houseb1f.c3s", "PHENAC CITY - HOUSE", 0u},
    {0x08u, "phenac_housec1f", "phenac_housec1f.c3s", "PHENAC CITY - HOUSE", 0u},
    {0x09u, "phenac_shop1f", "phenac_shop1f.c3s", "PHENAC POKE MART", 0u},
    {0x0Au, "phenac_shop2f", "phenac_shop2f.c3s", "PHENAC POKE MART", 0u},
    {0x0Bu, "phenac_pregym1f", "phenac_pregym1f.c3s", "PHENAC PRE GYM", 0u},
    {0x0Cu, "phenac_pregymb1", "phenac_pregymb1.c3s", "PHENAC PRE GYM", 0u},
    {0x0Du, "phenac_stadium1f", "phenac_stadium1f.c3s", "PHENAC STADIUM", 0u},
    {0x77u, "phenac_pcb1", "phenac_pcb1.c3s", "PHENAC POKEMON CENTER", 0u},
    /* Lifecycle row 121: world_map.fsys, group 0x13. */
    {C3_FLOOR_WORLD_MAP_ID, "worldmap", NULL, "WORLD MAP", C3_FLOOR_WORLD_MAP},
};

const C3FloorProfile *c3_floor_profile(uint32_t floor)
{
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i)
        if (profiles[i].floor == floor) return profiles + i;
    return NULL;
}
