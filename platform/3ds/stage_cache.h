#ifndef C3_STAGE_CACHE_H
#define C3_STAGE_CACHE_H
#include <stdbool.h>
#include <string.h>
/* Explicitly opted-in immutable geometry. Dynamic scenes remain uncached.
 * Shader-side modulation does not affect staged vertices. Specular/reflection
 * use the view key; changing a view invalidates those entries automatically. */
typedef struct {
    bool enabled, valid, view_dependent;
    unsigned char view[64];
} C3StageVertexCache;
static inline bool c3_stage_cache_hit(const C3StageVertexCache *c,const void *view)
{return c->enabled&&c->valid&&(!c->view_dependent||!memcmp(c->view,view,sizeof c->view));}
static inline void c3_stage_cache_record(C3StageVertexCache *c,const void *view)
{memcpy(c->view,view,sizeof c->view);c->valid=true;}
#endif
