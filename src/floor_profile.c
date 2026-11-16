#include "colosseum3ds/floor_profile.h"

#include <stddef.h>
#include <stdio.h>

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
    /* Pinned M4 story floors; pack identities match tools/under_profiles.py. */
    {0x2Fu, "f02f", "f02f.c3s", "THE UNDER", 0u},
    {0x30u, "f030", "f030.c3s", "THE UNDER - JUNK SHOP", 0u},
    {0x31u, "f031", "f031.c3s", "THE UNDER - JUNK SHOP", 0u},
    {0x32u, "f032", "f032.c3s", "THE UNDER - HOTEL", 0u},
    {0x33u, "f033", "f033.c3s", "THE UNDER - HOTEL", 0u},
    {0x34u, "f034", "f034.c3s", "THE UNDER - SHOP", 0u},
    {0x35u, "f035", "f035.c3s", "THE UNDER - LAB", 0u},
    {0x36u, "f036", "f036.c3s", "THE UNDER - LAB", 0u},
    {0x37u, "f037", "f037.c3s", "THE UNDER - HOUSE", 0u},
    {0x39u, "f039", "f039.c3s", "THE UNDER - SUBWAY", 0u},
    {0x3Au, "f03a", "f03a.c3s", "THE UNDER - SUBWAY", 0u},
    {0x3Bu, "f03b", "f03b.c3s", "THE UNDER - SUBWAY", 0u},
    {0x3Cu, "f03c", "f03c.c3s", "THE UNDER - SUBWAY", 0u},
    {0x3Du, "f03d", "f03d.c3s", "THE UNDER - TRAIN", 0u},
    {0x3Eu, "f03e", "f03e.c3s", "THE DEEP", 0u},
    {0x79u, "f079", "f079.c3s", "THE UNDER - ENTRANCE", 0u},
    {0x7Au, "f07a", "f07a.c3s", "THE UNDER", 0u},
    {0x7Du, "f07d", "f07d.c3s", "THE UNDER - ENTRANCE", 0u},
    {0x7Fu, "f07f", "f07f.c3s", "THE UNDER - HERB SHOP", 0u},
    /* Lifecycle row 121: world_map.fsys, group 0x13. */
    {C3_FLOOR_WORLD_MAP_ID, "worldmap", NULL, "WORLD MAP", C3_FLOOR_WORLD_MAP},
};

/* Surveyed floors without a reviewed row (tools/floor_survey.py, prepared by
 * tools/prepare_tour.py) use the generic names f%03x.c3f/.c3a/.c3s. Whether
 * such a floor loads is still decided by its packs being on the SD card. */
#define C3_FLOOR_AUTO_LIMIT 0x380u
static C3FloorProfile auto_rows[C3_FLOOR_AUTO_LIMIT];
static char auto_text[C3_FLOOR_AUTO_LIMIT][3][16];

const C3FloorProfile *c3_floor_profile(uint32_t floor)
{
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i)
        if (profiles[i].floor == floor) return profiles + i;
    if (!floor || floor >= C3_FLOOR_AUTO_LIMIT) return NULL;
    C3FloorProfile *row = auto_rows + floor;
    if (!row->floor) {
        char (*text)[16] = auto_text[floor];
        snprintf(text[0], sizeof text[0], "f%03lx", (unsigned long)floor);
        snprintf(text[1], sizeof text[1], "f%03lx.c3s", (unsigned long)floor);
        snprintf(text[2], sizeof text[2], "AREA %03lX", (unsigned long)floor);
        *row = (C3FloorProfile){floor, text[0], text[1], text[2], 0u};
    }
    return row;
}
