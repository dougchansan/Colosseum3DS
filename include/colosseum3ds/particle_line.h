#ifndef C3_PARTICLE_LINE_H
#define C3_PARTICLE_LINE_H
#include "colosseum3ds/particle.h"
/* Source POINT+TRAIL line adapter.
 * Native attachment transforms source-world positions (normally scale .1).
 * GX width is a SOURCE RASTER pixel measure, never attachment scaled.
 * Camera clip is the actual native world->clip matrix. z_min/max define its
 * NDC depth interval: e.g. PICA [-1,0], conventional OpenGL [-1,1]. */
typedef struct {
 float destination_per_source_pixel[2];
 float z_min,z_max;
} C3ParticleLineRaster;
typedef struct {
 float position[3],clip[4],uv[2],rgba[4];
} C3ParticleLineVertex;
typedef struct {
 C3ParticleLineVertex source[2],visible[2]; /* previous, current */
 float source_interval[2],source_width_pixels,destination_width_pixels[2];
 float destination_per_source_pixel[2];
 uint8_t gx_width_sixths,prim[4],env[4];
 uint8_t textured,prim_env,blend,depth_test,depth_write,filter_nearest;
 uint8_t mirror_s,mirror_t,texture_group,texture_frame,alpha_reference;
 /* Default source alpha compare: mode 0x33 (GEQUAL ref AND LEQUAL 255).
  * C3Particle has no custom aCmp fields; source programs changing them are
  * outside this candidate's admitted profile. Depth comparison is LEQUAL. */
 /* GX_TO_ONE is 5: line-side texture offset 1. Endpoint tex indices 0,1.
  * Exact caps, zero-length coverage, edge inclusion, and subpixel snapping
  * remain GPU adapter responsibilities; no billboard substitution implied. */
 uint8_t gx_texture_offset;
} C3ParticleLine;
/* 1 source line retained (even zero length/zero GX width), 0 hidden or entirely
 * depth clipped, -1 unsupported/invalid. No side-plane centerline culling:
 * finite raster width can cover pixels outside the centerline clip rectangle.
 * Untextured source setup clears PrimEnv before getClrTrail. */
int c3_particle_line(const C3Particle *,const float attachment[12],
 const C3ParticleCamera *,const C3ParticleLineRaster *,C3ParticleLine *,char *,size_t);
/* 1 all active particles are POINT, 0 no active POINT, -1 mixed geometry.
 * Includes hidden and size-zero active particles to keep dispatch consistent
 * across source interpolation; unsupported mixtures must fail explicitly. */
int c3_particle_line_system_mode(const C3ParticleSystem *);
typedef struct {float clip[4],uv[2],rgba[4];} C3ParticleLineTriangleVertex;
typedef struct {
 C3ParticleLineTriangleVertex vertices[6]; /* two CCW triangles, NDC y up */
 uint8_t source_major_axis,degenerate; /* 0 horizontal, 1 vertical */
} C3ParticleLineMesh;
/* Source-pixel major-axis expansion into six homogeneous clip vertices.
 * No perspective billboard or longitudinal cap extension. viewport is the
 * UNROTATED destination raster, e.g. {400,240}; apply 3DS framebuffer rotation
 * only afterward. All clip xy offsets multiply the individual endpoint w.
 * Submit with an identity clip-space pass (4D position), source material state
 * from line, culling disabled or matching the CCW convention, and destination
 * viewport clipping. GPU interpolator must preserve screen-linear color and
 * perspective texture interpolation to match the GX profile.
 * 45-degree tie follows Dolphin software (vertical); hardware tie unresolved.
 * Zero-length/width yields six retained degenerate vertices and returns 1. */
int c3_particle_line_triangles(const C3ParticleLine *,const float viewport[2],
 C3ParticleLineMesh *,char *,size_t);
#endif
