#ifndef COLOSSEUM3DS_STAGE_ORDER_H
#define COLOSSEUM3DS_STAGE_ORDER_H

#include <stddef.h>

/* Draw groups of the scene renderer. The original submits each GSmodel in
 * list order with its opaque, texture-edge and translucent passes before the
 * next model (gs_model_main.c fn_800E3604: fn_801A13CC masks 1, 4, 2; HSD's
 * z-sort listing is never enabled in retail, so translucent DObjs draw at
 * once). A translucent map layer that does not write depth is therefore
 * covered by a character drawn after the map, instead of washing over it.
 *
 * leading > 0 selects that order: models [0, leading) are one group (one
 * original map split into several packs at the batch bound), every later model
 * is its own group, and each group draws its opaque then its translucent
 * batches. leading == 0 keeps one group of every model: all opaque, then all
 * translucent. Returns the end of the group that starts at start. */
static inline size_t c3_stage_draw_group_end(size_t start, size_t count, size_t leading)
{
    if (start >= count) return count;
    if (!leading) return count;
    if (start < leading) return leading < count ? leading : count;
    return start + 1;
}

#endif
