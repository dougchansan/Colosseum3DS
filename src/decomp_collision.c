#include "colosseum3ds/core.h"
#define GScolsy2UtilGetSidePlanePoint c3_gs_side_plane
#include "../vendor/colosseum/src/game/GScolsys2Util_exact_8010C77C.c"

float c3_side_plane(const float normal[3], const float p1[3], const float p2[3])
{
    Vec3f n = {normal[0], normal[1], normal[2]};
    Vec3f a = {p1[0], p1[1], p1[2]};
    Vec3f b = {p2[0], p2[1], p2[2]};
    return c3_gs_side_plane(&n, &a, &b);
}
