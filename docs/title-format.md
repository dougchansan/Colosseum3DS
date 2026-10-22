# Native title preparation and C3T1

The same portable loader also reads C3T2, C3T3 and C3T4 field packs. Their extra UV,
reflection and source-joint material fields are specified in
[outskirt-assets.md](outskirt-assets.md); C3T4 spotlights and independent light/camera
tracks are specified in [outskirt-interior-assets.md](outskirt-interior-assets.md).
The title exporter continues to write
the original C3T1 layout described here.

`tools/prepare_title.py` converts the **original animated title scene** from a
verified private GC6E01 revision 0 extraction into native geometry, textures,
lights, camera samples, joint transforms and material samples. This is a 3D
scene rendered by Citro3D. No title screenshot or title movie is used.

The exporter is deliberately specific to the reviewed title. It is not a general
HSD renderer, nor does the resulting pack contain the title menu logic, save
menu, attract movie, new-game opening, gameplay or music. Those are independent
runtime tasks. Original assets and generated packs stay private.

## Reproduce the private pack

Requirements are Python 3, NumPy, the pinned HSD parser and a verified extracted
disc. Blender is not required for this path. Initialize the repository's
`vendor/hsd-importer` submodule at its recorded pin before running:

```sh
python3 tools/prepare_title.py \
  --disc /path/to/private-disc \
  --output /path/to/private-output/title.c3t
```

The disc must have `sys/boot.bin`, `sys/main.dol` and `files/title.fsys` in their
original extracted locations. The tool checks the disc ID/revision, original
DOL size and SHA1, then admits the exact reviewed title archive **before** its
decompression or HSD parse. It verifies the parser's 150-file Python dependency
closure against Git blob identities in `config/title-parser-files.json`.

The parser pin is
[`dc71de837978a6a2fe8c5082f2840cfafb135235`](https://github.com/StarsMmd/Blender-Addon-Gamecube-Models/tree/dc71de837978a6a2fe8c5082f2840cfafb135235).
The decompilation semantics were read at
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).
The expected title archive is 1,374,336 bytes with SHA256
`8ae138e5f73cd024a60fe42da2a08220f42a8ce90424c3649cc67e7f27b4797a`.

The output must be a new path. Repository-local outputs are accepted only under
ignored `private/`. The adjacent `title.c3t.json` records source/member hashes,
parser/source pins, geometry counts, texture resampling, timing and limitations.
The tool stages both completed outputs before publishing them; existing files
are never intentionally replaced.

### Reviewed asset inventory

| Source | Meaning | Exported state |
|---|---|---|
| `title.fsys` / `logo_demo`, member `0x0B540200` | The first model registered as `0x0B541000` | 31 rigid bones, 43 batches, 5,759 triangles, 17,277 expanded triangle vertices |
| `cam_logo_demo_start`, member `0x0B551800` | Intro camera | Original eye/FOV/clip-plane/roll curves, continuous across model clips 0 and 1 |
| `cam_logo_demo_stop`, member `0x0B561800` | Title waiting camera | Original camera state; all animated channels are constant in this asset |
| Model scene lights | Ambient, infinite and point lights | Original color, flags, position and point attenuation parameters |

The produced pack has 20 distinct texture descriptors, including the generated
white texture for the untextured batch. Deduplication includes pixel content and
wrap state. Both meshes hidden in the rest pose remain in the pack: HSD NODE and
BRANCH visibility channels determine when they appear.

The reviewed result is 18,353,048 file bytes and 19,678,020 resident bytes in the
64-bit host loader. Actual 3DS resident accounting uses its own `sizeof` values;
GPU textures and the renderer's linear vertex arena are additional allocations.
The file includes 3,439 sampled states at 60 Hz.

## Source semantics retained

The original title helper selects model animations 0, 1 and then looping 2 and
sets animation rate 0.5. The graphics loop advances model animation by elapsed
retraces, so the NTSC title advances 30 authored frames per second even if
rendering skips a retrace. Camera playback also sets rate 0.5. Relevant source:
[`gs_title_8001EF78.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title_8001EF78.c),
[`camera.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/camera.c),
[`gs_model_anim_exact_800EC4D0.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_anim_exact_800EC4D0.c)
and [`gs_model_anim_suffix_800ED4D4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_anim_suffix_800ED4D4.c).

| Model clip | Authored interval | C3T samples | Duration | Camera |
|---|---:|---:|---:|---|
| 0 | 0–199 | 399 | 199/30 seconds | Start camera, time 0–199 |
| 1 | 0–20 | 41 | 20/30 seconds | Start camera, time 199–219 |
| 2 | 0–1499, looping | 2,999 | 1499/30 seconds per loop | Stop camera, starting at time 0 |

The source stops/loops at `anim_end_frame`, not at the `end_frame + 1` returned
by its separate frame-count query. Thus each C3T interval includes its endpoint
and its duration is `(frame_count - 1) / fps`. The final sample of a looping clip
contains its reset state, providing the interpolation neighbor at the boundary.
The source stop-camera animation has a different end frame (1520), but every
channel holds a constant value; sharing the model-loop clock therefore does not
alter the exported camera for this specific scene.

The exporter reads raw HSD channels in addition to the parser's intermediate
scene. This preserves channels not exposed by that intermediate representation:

- Joint channel 11 changes one node's visibility; channel 12 changes its entire
  descendant branch. Visibility uses the source threshold `value > 0.5`.
- Material alpha channel 10 stores transparency. Native opacity is clamped
  `1 - value`, as in `MObjUpdateFunc`.
- Texture channels 12–14 and 16–18 update konst and TEV0 RGB. Native conversion
  clamps the value, multiplies by 255 and truncates to an unsigned byte.
- Title texture coordinates use HSD's matrix order. For the used zero-rotation
  subset, `UVout = UVin * (repeat / scale) - translation * (repeat / scale)`.
  The floor's 50×50 and 25×25 repetition and scrolling cloud layers survive.
- Only three title batches have active custom TEV state. Inactive descriptors
  on cloud textures do not enable the custom stage. The active title expression
  is `lerp(tev0.rgb, konst.rgb, texture.rgb)` before the ordinary texture stage.
- Raw pixel-engine data is used for the two additive batches. Their actual
  alpha compares are ALWAYS/ALWAYS with AND, depth compare is LEQUAL, blending
  is SRC_ALPHA/ONE, and depth writes are disabled.

These rules come from canonical
[`mobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/mobj.c),
[`tobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/tobj.c),
[`hsd_jobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/hsd_jobj.c)
and [`fobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/fobj.c).

Joint transforms preserve the parser's aligned scale-inheritance convention.
Vertices are in the original right-handed, Y-up coordinate system, uniformly
scaled by the pinned parser's `GC_TO_METERS = 0.10`; the name is a parser
convention, not a claim about physical measurements. Camera/light positions,
translation animation and point reference distance use the same factor.
Rigid geometry is stored in rest world space, with per-frame skin matrices
`animated_world * inverse(rest_world)`. Normals use inverse-transpose matrices.

HSD CONSTANT, LINEAR and cubic Hermite curves are evaluated at half-authored-frame
steps. The right key holds the two slopes for the preceding interval; this also
preserves asymmetric SLP tangents. Translation scaling applies to both values
and slopes. Duplicate-frame keys select the last value at that time.

## C3T1 disk layout

All integers and binary32 floats are little-endian. No structure casts,
host-layout assumptions, pointer relocations or native ABI padding are used by
the loader. Every section is contiguous in the following order; padding and
unreferenced trailing bytes are rejected.

### Header: 128 bytes

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 4 bytes | `C3T1` |
| 4 | u32 | Version 1 |
| 8 | u32 | Exact total file bytes |
| 12 | u32 | Bone count |
| 16 | u32 | Expanded vertex count, divisible by 3 |
| 20 | u32 | Batch count |
| 24 | u32 | Texture count |
| 28 | u32 | Clip count |
| 32 | u32 | Total sample count across all clips |
| 36 | u32 | Light count |
| 40 | f32 | Samples per second, 60 in the reviewed title |
| 44 | u32 | Vertex offset, exactly 128 |
| 48 | u32 | Batch offset |
| 52 | u32 | Texture descriptor offset |
| 56 | u32 | Clip descriptor offset |
| 60 | u32 | Light descriptor offset |
| 64 | u32 | Frame offset |
| 68 | u32 | Pixel-data offset |
| 72 | u32 | Frame stride, exactly `(bones + batches + 1) * 48` |
| 76 | u32 | Total pixel bytes |
| 80 | 32 bytes | Original title archive SHA256, provenance only |
| 112 | 16 bytes | Reserved, all zero |

The provenance hash is not a signature or an integrity check for the generated
pack. The adjacent manifest supplies the full generated-file hash. The loader
validates structure and numeric values independently of provenance.

### Geometry, material and resource sections

Each **vertex** is 40 bytes: position XYZ (3 f32), normal XYZ (3 f32), UV (2 f32),
RGBA (4 u8), and bone index (u32). Normals and positions are in rest world space.
UV and pixels both use a top-origin convention. Original face-loop UV and color
seams are retained; triangles are expanded and quads are triangulated in the
parser's existing winding.

Each **batch** is 112 bytes, in this order:

| Fields | Storage |
|---|---|
| First vertex, count, texture index, runtime flags | 4 u32 |
| Color mode, alpha mode, original render mode, original texture flags | 4 u32 |
| Diffuse RGB, ambient RGB, specular RGB | 9 f32 |
| Shininess, base alpha, texture blend factor | 3 f32 |
| Alpha reference 0/1, alpha compare 0/1, alpha operation, depth compare | 6 u32 |
| Reserved zeros | 2 u32 |

Batches partition all vertices once, in source order. Runtime flag values are
defined in `title.h`: back/front cull (1/2), translucent (4), additive (8), lit
(16), vertex color/alpha (32/64), specular (128), custom TEV lerp (256), and no
depth write (512). The current title does not set specular. The raw material
source selectors remain available in the original render-mode word.

Texture color modes used here are 0=pass, 3=blend and 4=modulate. Texture alpha
modes are 0=pass, 2=blend and 3=modulate. Blend means interpolation from input to
texture using the material texture-blend factor. Lit title batches multiply
the textured diffuse result by the GX lighting channel.

Each **texture descriptor** is six u32 values (24 bytes): width, height,
absolute pixel offset, pixel bytes, wrap S and wrap T. Dimensions are powers of
two in 8–1024. Pixel data is RGBA8, top row first, `width * height * 4` bytes.
Wrap values are 0=clamp, 1=repeat and 2=mirror. Pixel regions are contiguous and
cover the pixel section exactly. The renderer's Morton tiling and row reversal
preserve these source UVs; a native four-corner probe verified the convention.

Each **clip descriptor** is four u32 values (16 bytes): first sample, sample
count, loop flag (0/1), reserved zero. Clip intervals cover the frame section
exactly. Clips 0/1 clamp; clip 2 loops over `sample_count - 1` intervals.

Each **light descriptor** is 64 bytes: flags and attenuation flags (2 u32),
RGBA (4 f32), position (3 f32), interest (3 f32), reference brightness, reference
distance, distance function (3 f32), and one reserved zero f32. Original light
flag low bits are ambient=0, infinite=1, point=2; bits 2/3/4 enable diffuse,
specular and alpha respectively; bit 5 hides a light. Only attenuation flags
zero are supported. Point distance function is stored as an integral float in
0–3 because the remaining light record is numeric float data. The actual title
uses function 3 with reference brightness 0.99 and normalized distance 1000.

### Frame section

Every frame stores:

1. One row-major affine 3×4 matrix (12 f32) per bone.
2. One material state (12 f32) per batch: UV scale S/T, UV offset S/T, alpha,
   visibility (exactly 0 or 1), TEV0 RGB and konst RGB.
3. One camera state (12 f32): eye XYZ, target XYZ, roll in radians, vertical FOV
   in degrees, near plane, far plane, aspect ratio and reserved zero.

The runtime interpolates matrices, UVs, alpha and camera values. Visibility and
quantized TEV colors hold the earlier state. The renderer receives transformed
vertices and unchanged original vertex RGBA, so material/vertex color-source
selection remains separate from animation sampling.

## Runtime API and bounds

Zero-initialize `C3Title` before first use. `c3_title_load`, `c3_title_parse` and
`c3_title_animate` return **1 on success and 0 on failure**. `c3_title_close` is
idempotent and clears the structure.

`parse` copies caller bytes; `load` reads directly into its owned file buffer.
A successful replacement closes the previous asset; a failed replacement
preserves it. Parsing from the currently owned file buffer is supported because
the replacement copy is made before the old asset is freed. The renderer must
be shut down before replacing an asset to which it holds a pointer.

| Resource | Maximum |
|---|---:|
| File bytes | 24 MiB |
| Bones | 64 |
| Expanded vertices | 32,768 |
| Batches | 64 |
| Texture descriptors | 32 |
| Clips | 32 (the title itself uses 3) |
| Samples across clips | 4,096 |
| Lights | 8 |

Counts, canonical offsets, overflow-safe extents, numeric bounds, reserved
words, references, texture dimensions and batch/clip coverage are checked
before use. Every stored matrix is finite and nonsingular. Camera projection
parameters, light parameters, opacity and visibility have explicit bounds.
Finite negative sample times clamp to zero. Invalid clips or NaN/Inf times
leave the current pose untouched. If interpolation between otherwise valid
matrices becomes singular, that sample also fails without changing the pose or
draw arena. Runtime sampling allocates no memory.

## Fidelity limits and validation

This path preserves the reviewed scene's source content and animation subset.
It does not claim bit-identical GameCube graphics. In particular:

- Curves are sampled at the original NTSC title tick, then stored as float32.
  Between ticks the runtime linearly interpolates affine matrices and camera
  parameters; it does not reevaluate the original cubic curves on ARM.
- The title includes non-power-of-two images. They are resampled with nearest
  pixels to the next power of two, preserving the complete image and UV repeat
  domain. Original pixel sampling, mipmaps, anisotropy and filtering are not
  reproduced exactly.
- Citro3D implements the title's fixed material subset. Lighting, depth,
  blending and TEV operation results need comparison against the original game
  on hardware before claiming visual equivalence. No specular-enabled title
  material, spot light, raw attenuation block, skin envelope, morph, constraint,
  texture swap, multiple texture layer, animated UV rotation/scale, fog or
  arbitrary custom TEV program is accepted by this exporter.
- Early skip, menus, input routing, save handling and sound are controlled by
  other native modules. The pack itself carries no progress or save state.

Synthetic Python tests cover raw curve tangent semantics, duplicate/end keys,
alpha inversion, TEV truncation, visibility scope, repeat/translation matrices,
pin admission and layout. The portable C test covers truncation and malicious
extents, ownership and failed replacement, negative/nonfinite/extreme times,
loop boundaries, inverse-transpose normals and singular-interpolation failure.
Both the synthetic test and the actual private pack were exercised under host
AddressSanitizer/UndefinedBehaviorSanitizer. Leak scanning is disabled in the
sandbox because its ptrace environment prevents LeakSanitizer process scanning.

The actual pack also renders in the native 3DS executable under the supplied
Azahar core: the original title logo, animated geometry, desert ruins and sky
are visible. This establishes working native scene delivery; it is not a claim
that the complete game or title-menu flow has been ported.
