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
    C3_TITLE_REFLECTION_BLEND = 8192u /* C3T3: lerp final RGB with normal-generated texture2 */
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
    const uint8_t *pixels;                 /* top row first, RGBA8 */
    size_t size;
} C3TitleTexture;

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
} C3Title;

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
 * channel so the renderer can apply the batch's HSD material source rules. */
int c3_title_animate(C3Title *title, uint32_t clip, float seconds);

#endif
