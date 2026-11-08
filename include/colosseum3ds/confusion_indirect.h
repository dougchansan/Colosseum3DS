#ifndef C3_CONFUSION_INDIRECT_H
#define C3_CONFUSION_INDIRECT_H
#include "colosseum3ds/wzx_confusion.h"
/* Source-derived CPU sampler. RGBA8 rows are top origin and tightly packed.
 * UV is the interpolated original quad TEX1 coordinate (0..1), NOT screen UV.
 * Caller rasterizes/clips the original world quad, including depth/coverage.
 * Uncovered pixels have covered=0 and are written transparent black. */
typedef struct { float u,v; unsigned char covered; } C3ConfusionPixel;
typedef struct {
    unsigned indirect_stage, format, bias, matrix, wrap_s, wrap_t;
    unsigned add_previous, utc_lod, alpha_selection, scale_s, scale_t;
    unsigned mask_nearest, capture_linear, texture_clamp, tev_replace_rgba;
} C3ConfusionSamplerProfile;
/* Explicit source state; no guessed/inherited GPU state. */
C3ConfusionSamplerProfile c3_confusion_sampler_source_profile(void);
/* Program must have passed c3_wzx_confusion_decode/load. Frame must come from
 * c3_confusion_type12_frame for these capture dimensions. All invalid inputs
 * fail before touching destination. No input/output overlap is admitted. */
int c3_confusion_sample_rgba(const C3WzxConfusionProgram *,
    const C3ConfusionType12Frame *,const C3ConfusionSamplerProfile *,
    const unsigned char *capture,size_t capture_bytes,unsigned capture_width,unsigned capture_height,
    const C3ConfusionPixel *,size_t pixel_count,unsigned viewport_width,unsigned viewport_height,
    unsigned char *output,size_t output_bytes,char *error,size_t error_bytes);
typedef struct { float world[3],uv[2]; } C3ConfusionGridVertex;
/* 128 cells per row, 6 unshared vertices per cell (two triangles). Generate
 * stripes without allocating a whole grid. UV incorporates the exact integer
 * indirect displacement. Sample capture with linear/clamp at these UVs.
 * GPU interpolation differs from GX's separately truncated base UV by less
 * than1/128 capture texel; this is NOT a bit-exact PICA/GX filtering claim. */
int c3_confusion_grid_stripe(const C3WzxConfusionProgram *,
    const C3ConfusionType12Frame *,const C3ConfusionSamplerProfile *,
    unsigned capture_width,unsigned capture_height,unsigned first_row,unsigned rows,
    C3ConfusionGridVertex *,size_t vertex_count,char *error,size_t error_bytes);
#endif
