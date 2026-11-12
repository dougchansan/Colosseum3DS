#ifndef COLOSSEUM3DS_TITLE_H
#define COLOSSEUM3DS_TITLE_H

#include <stddef.h>
#include <stdint.h>

#define C3_TITLE_MAX_BONES 64u
#define C3_TITLE_MAX_VERTICES 32768u
#define C3_TITLE_MAX_BATCHES 64u
#define C3_TITLE_MAX_TEXTURES 32u
#define C3_TITLE_MAX_CLIPS 32u
#define C3_TITLE_MAX_FRAMES 4096u
#define C3_TITLE_MAX_LIGHTS 8u
#define C3_TITLE_MAX_FILE_BYTES (24u * 1024u * 1024u)

enum {
    C3_TITLE_CULL_BACK = 1u,
    C3_TITLE_CULL_FRONT = 2u,
    C3_TITLE_TRANSLUCENT = 4u,
    C3_TITLE_ADDITIVE = 8u,       /* SRC_ALPHA, ONE instead of ONE_MINUS_SRC_ALPHA */
    C3_TITLE_LIT = 16u,
    C3_TITLE_VERTEX_COLOR = 32u,
    C3_TITLE_VERTEX_ALPHA = 64u,
    C3_TITLE_SPECULAR = 128u,
    C3_TITLE_TEV_LERP = 256u,    /* texture RGB becomes lerp(tev0, konst, texture RGB) */
    C3_TITLE_NO_DEPTH_WRITE = 512u,
    C3_TITLE_DUAL_MODULATE = 1024u, /* C3T2: a second affine UV/texture stage */
    C3_TITLE_SUBTRACT = 2048u,     /* C3T2: GX subtract, destination - source */
    C3_TITLE_ADDITIVE_ONE = 4096u, /* C3T2: ONE, ONE framebuffer addition */
    C3_TITLE_REFLECTION_BLEND = 8192u, /* C3T3: lerp final RGB with normal-generated texture2 */
    C3_TITLE_SECOND_UV_BLEND = 16384u, /* C3T3: texture2 blend uses authored UV1 */
    C3_TITLE_SOURCE_ALPHA_MASK = 32768u,
    C3_TITLE_PAIRED_NORMAL_EXT = 65536u
};

typedef struct {
    float pos[3], normal[3], uv[2];
    uint8_t rgba[4];
    uint32_t bone;
    float uv2[2];
} C3TitleVertex;

typedef struct {
    float pos[3], normal[3], uv[2];
    uint8_t rgba[4];
    float uv2[2];
} C3TitleDrawVertex;

typedef struct {
    uint32_t first, count, texture, flags;
    /* Raw HSD texture-map selectors. Title color uses 0=pass, 3=blend,
     * 4=modulate; alpha uses 0=pass, 2=blend, 3=modulate. Blend uses
     * texture_blend as lerp(input, texture, factor). For lit materials the
     * diffuse lighting factor multiplies the result after the texture stage. */
    uint32_t color_mode, alpha_mode, render_mode, texture_flags;
    float diffuse[3], ambient[3], specular[3], shininess, alpha, texture_blend;
    uint32_t alpha_ref0, alpha_ref1, alpha_compare0, alpha_compare1, alpha_op, depth_compare;
    uint32_t texture2; /* C3T2 dual mode; zero for C3T1/single-stage batches */
    float texture2_blend; /* C3T3 reflection EXT blend; zero otherwise */
} C3TitleBatch;

typedef struct {
    uint32_t width, height, wrap_s, wrap_t; /* 0=clamp, 1=repeat, 2=mirror */
    const uint8_t *pixels;                 /* top row first, RGBA8 (unless gpu_native) */
    size_t size;
    /* tools/pack_textures.py output: pixels are the finished PICA image
     * (8x8 Morton tiles, rows bottom-up, levels contiguous) in gpu_format
     * (C3_TITLE_GPU_RGBA8 or C3_TITLE_GPU_RGB565) with `levels` mip levels. */
    uint32_t gpu_native, gpu_format, levels;
} C3TitleTexture;

#define C3_TITLE_GPU_RGBA8 0u
#define C3_TITLE_GPU_RGB565 3u
/* Bytes of a gpu_native image: each level is a quarter of the one before. */
size_t c3_title_texture_bytes(uint32_t width, uint32_t height, uint32_t gpu_format, uint32_t levels);

typedef struct { uint32_t first_frame, frame_count, loop; } C3TitleClip;

typedef struct {
    /* Raw LOBJ flags: low two bits ambient=0/infinite=1/point=2/spot=3;
     * bit2 diffuse, bit3 specular, bit4 alpha, bit5 hidden. */
    uint32_t flags, attenuation_flags;
    float rgba[4], pos[3], interest[3];
    float reference_brightness, reference_distance, distance_function;
    uint32_t spot_function; /* C3T4 GXSpotFn 0..6; zero for other light types */
    float spot_cutoff;      /* C3T4 source cutoff degrees */
} C3TitleLight;

typedef struct {
    float uv_scale[2], uv_offset[2];
    float alpha, visible;
    float tev0[3], konst[3];
    float uv2_scale[2], uv2_offset[2];
    float uv_cross[2], uv2_cross[2]; /* C3T3 affine off-diagonal M01, M10 */
    float specular_origin[3]; /* C3T3: original owning joint world position */
} C3TitleMaterialState;

typedef struct {
    float eye[3], target[3], roll, fov_y_degrees, near_plane, far_plane, aspect;
} C3TitleCamera;

typedef struct {
    float matrices[C3_TITLE_MAX_BONES][12]; /* row-major affine 3x4 */
    C3TitleMaterialState materials[C3_TITLE_MAX_BATCHES];
    C3TitleCamera camera;
} C3TitlePose;

/* World placement folded into the bone matrices by c3_title_animate, so an
 * actor needs no second pass over its sampled vertices. Positions use
 * matrix * bone; normals use normal * (bone normal matrix) and are then
 * renormalised. pose.matrices stay the unplaced sampled bones. */
typedef struct {
    uint8_t enabled; /* 0: bones are used as sampled */
    uint8_t origin;  /* also map every batch's specular_origin through matrix */
    float matrix[12]; /* row-major affine 3x4 */
    float normal[9];  /* row-major 3x3, a rotation for rigid placement */
} C3TitlePlacement;

/* Source normals within this squared-length tolerance of one, skinned by an
 * orthonormal normal matrix within the same tolerance, skip the per-vertex
 * renormalisation. The staged colour then differs by far less than one LSB. */
#define C3_TITLE_UNIT_TOLERANCE 1e-5f

typedef struct {
    uint8_t *file_data;
    size_t file_bytes, resident_bytes;
    C3TitleVertex *vertices;
    C3TitleDrawVertex *draw_vertices;
    C3TitleBatch *batches;
    C3TitleTexture *textures;
    C3TitleClip clips[C3_TITLE_MAX_CLIPS];
    C3TitleLight lights[C3_TITLE_MAX_LIGHTS];
    C3TitlePose pose;
    uint32_t bone_count, vertex_count, batch_count, texture_count;
    uint32_t clip_count, frame_count, light_count, frame_offset, frame_bytes;
    uint32_t format_version, material_frame_bytes;
    float fps;
    uint32_t active_clip;
    float active_frame;
    uint8_t borrowed_storage;
    /* Caller-set before c3_title_animate. retained promises that nothing but
     * c3_title_animate writes draw_vertices (use placement, not a second
     * pass); animate then rewrites only batches whose bones, UV state or
     * placement changed, and skips invisible ones. hidden additionally
     * reports every batch invisible. Both default to the legacy behaviour:
     * every vertex of every batch is rewritten by every call. */
    uint8_t retained, hidden;
    C3TitlePlacement placement;
    /* Maintained by the loader and c3_title_animate; read-only for callers.
     * batch_revision advances whenever a batch's draw_vertices are rewritten. */
    C3TitlePlacement skinned_placement;
    uint64_t batch_bones[C3_TITLE_MAX_BATCHES];
    uint32_t batch_revision[C3_TITLE_MAX_BATCHES];
    uint8_t batch_current[C3_TITLE_MAX_BATCHES];
    uint8_t batch_unit_normals[C3_TITLE_MAX_BATCHES];
} C3Title;

/* Interval counters for c3_title_animate. Time is measured only while a
 * microsecond clock is installed; counts are always kept. take copies the
 * counters to out (may be NULL) and clears them. Not thread safe. */
typedef struct {
    uint64_t animate_us;
    uint32_t calls;
    uint32_t vertices_animated; /* skinned */
    uint32_t vertices_retained; /* left untouched: unchanged or invisible */
} C3TitlePerf;
void c3_title_perf_clock(uint64_t (*now_us)(void));
void c3_title_perf_take(C3TitlePerf *out);
/* Running animate_us of the current interval, for callers that time a pass
 * which also samples animation and want the remainder. */
uint64_t c3_title_perf_animate_us(void);

/* Rigid actor placement: uniform scale, then a rotation about +Y given by its
 * cosine and sine, then a translation. origin selects whether the specular
 * origins follow (field actors do; battle actors historically do not). */
static inline void c3_title_place_yaw(C3Title *title, const float position[3],
                                      float cosine, float sine, float scale, int origin)
{
    C3TitlePlacement *p = &title->placement;
    p->enabled = 1; p->origin = (uint8_t)(origin != 0);
    p->matrix[0] = cosine * scale; p->matrix[1] = 0.0f; p->matrix[2] = sine * scale; p->matrix[3] = position[0];
    p->matrix[4] = 0.0f; p->matrix[5] = scale; p->matrix[6] = 0.0f; p->matrix[7] = position[1];
    p->matrix[8] = -sine * scale; p->matrix[9] = 0.0f; p->matrix[10] = cosine * scale; p->matrix[11] = position[2];
    p->normal[0] = cosine; p->normal[1] = 0.0f; p->normal[2] = sine;
    p->normal[3] = 0.0f; p->normal[4] = 1.0f; p->normal[5] = 0.0f;
    p->normal[6] = -sine; p->normal[7] = 0.0f; p->normal[8] = cosine;
}

/* Zero-initialize before first use. Owns its file and decoded geometry after
 * success. Parse copies the caller's bytes; load reads directly into owned
 * storage. Returns 1 on success, 0 on failure. A failed replacement preserves
 * the previously loaded title. Source coordinates are right-handed world
 * space scaled by the pinned importer's factor 0.10. The renderer reads
 * draw_vertices, batches, textures, pose and lights. */
int c3_title_load(C3Title *title, const char *path, char *error, size_t capacity);
int c3_title_parse(C3Title *title, const void *data, size_t bytes, char *error, size_t capacity);
void c3_title_close(C3Title *title);
/* Shares immutable source/geometry with owner; owns pose and sampled vertices.
 * Owner must outlive all instances, including GPU teardown. Zero initialize
 * instance; nested borrowing and self replacement are rejected. */
int c3_title_instance(C3Title *instance, const C3Title *owner, char *error, size_t capacity);

/* Select and sample one authored clip. Each clip's loop flag determines
 * wrapping; nonlooping clips clamp. Finite negative seconds clamp to zero; NaN/Inf or invalid
 * clip fails without changing the pose. Matrix/UV/alpha/camera samples lerp;
 * visibility and quantized TEV colors hold the earlier sample. Returns 1 on
 * success, 0 on failure. It transforms
 * rigid vertices/normals and top-origin UVs; RGBA stays the original vertex
 * channel so the renderer can apply the batch's HSD material source rules.
 * An enabled placement with a nonfinite or excessive matrix also fails
 * without changing the pose. Uses static scratch storage: not reentrant. */
int c3_title_animate(C3Title *title, uint32_t clip, float seconds);

#endif
