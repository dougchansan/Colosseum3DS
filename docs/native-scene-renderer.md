# Native title and field renderer

`platform/3ds/title_renderer.c` implements both the single-title API and the
multipart API declared by `stage_renderer.h`. The same Citro3D vertex shader
and texture uploader render the title, Outskirt Stand, Wes, and the arrival
models. It consumes the checked C3T1/C3T2/C3T3 assets prepared separately from
the user's original data. It does not execute the original GX command stream.

## Ownership and bounds

Initialization accepts up to eight fixed model identities. Each individual
pack retains the limits in `title.h`; the shared renderer allows 131,072
expanded vertices, 128 distinct GPU textures, and at most 32 MiB of owned
vertex/texture storage. The current GPU vertex is 56 bytes. Split packs reuse
a GPU texture only when its dimensions, sampler state, and full pixel contents
match. The caller keeps all packs alive until renderer shutdown.

Each draw submits one `C3D_FrameBegin` / `C3D_FrameEnd` pair. The nonblocking
frame begin proves the previous queue drained before the CPU writes the single
linear vertex arena; a successful draw then paces presentation to two VBlanks
and acquires the next frame before returning. The arena persists across
frames: batches of retained models are lit and staged again only when their
inputs change. [render-performance.md](render-performance.md) describes the
retained path, the pacing and the trace counters.
Different packs have disjoint ranges. Every model/pass reacquires
`C3D_GetBufInfo` to mark its binding dirty; changing a previously acquired
buffer-info structure without doing so can silently retain the previous pack's
GPU vertex binding. The shader byte array is embedded static storage and
outlives `DVLB_Free`.

Shutdown deletes the render target before freeing textures, vertices, or the
shader; the target deletion drains its linked queue. It does not wait on
`C3D_FrameSync`, whose VBlank counters may stop while the application is
suspended; the pacing waits poll those counters with a time bound for the same
reason. These ownership choices follow Citro3D's
[render-queue implementation](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c)
and [context implementation](https://github.com/devkitPro/citro3d/blob/master/source/base.c).

The single-title path preserves authored batch order. The multipart path draws
all opaque batches before translucent/additive/subtractive batches, preserving
pack and batch order within each class. It does not introduce a general
transparent-surface sorter. Invisible batches are neither prepared nor drawn.

`draw_calls` and `vertices` count actual submitted draws and vertices, including
the additional specular passes and the final fade triangle. `draw_prepare_us`
includes CPU vertex preparation and GPU command construction after frame begin;
it excludes the caller's animation sampling and the preceding GPU wait.
`gpu_previous_ms` is the last completed GPU sample. `gpu_buffer_bytes` counts
owned vertex and texture storage, excluding render targets, decoded asset
memory, and command buffers. Emulator timings are not New3DS hardware timings.

## Material subset used by these scenes

The renderer preserves the source material's RGB/alpha selectors, culling,
depth comparison/write policy, texture wrapping, animated alpha/visibility,
and the reviewed texture combiner operations. Unsupported combinations fail
validation rather than silently falling back. Pixel rows and normalized UVs
use the exporter's top-origin convention; the PICA upload reverses the pixel
rows while producing Morton-tiled ABGR storage. The native colored-corner
probe verifies the complete upload, UV, projection, and screen-orientation
pairing.

The diffuse path follows the pinned HSD
[channel setup](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/state.c)
and [material expressions](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/mobj.c):
ambient material times the active ambient light, clamped diffuse lighting,
then the material/texture expression. Infinite lights use the source light
position for diffuse direction; their interest point is used by specular
direction. The title's point-light attenuation uses the original reference
distance, brightness, and distance-function selector. Spot lights and raw
attenuation coefficients are outside this subset.

The reviewed Outskirt Stand dual-texture batch modulates two independently
animated UV channels. Both stages have the same zero TEV0 endpoint and the
same constant color, so the native expression is
`raster * texture0 * texture1 * konst * konst`. The source subtract mode uses
destination minus source with ONE/ONE factors; the other added field mode is
ONE/ONE addition. These operations are tested with separate model packs and
independent UVs.

### Arrival specular channel

The bike introduces a separate specular raster channel. The implementation
follows pinned
[HSD light setup](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/lobj.c),
including `HSD_LObjSetupSpecularInit`, the `GXInitLightShininess` coefficients,
and the original allocation of eight physical light slots plus a logical
ambient slot. All four combined diffuse/specular suns in the arrival scene
fit those slots.

The half vector is derived from the camera-space translation of the original
mesh-owning joint, supplied by C3T3's sampled `specular_origin`. It is not
recomputed from each vertex position. Infinite-light direction comes from
interest minus position; the stored GX half direction has the opposite sign
to the vector passed to `GXInitLightDir`. The facing test uses the light's
far-away position, as in the original setup.

For the applicable GX specular channel, shininess `s` and clamped normal/half
dot product `t` produce the rational attenuation

```
intensity = t*t / (s/2 + (1 - s/2)*t*t)
```

Each light's color contribution is rounded to a byte, the accumulated channel
is clamped, and the material's specular RGB multiplies that result. The
relationship between the GX coefficients and this equation is also visible
in Dolphin's primary
[lighting shader generator](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp).
The supported arrival path uses infinite lights, diffuse-enabled materials,
depth writes, and the reviewed color/alpha texture selectors.

PICA submits that independent channel in a second additive pass with equal
depth testing, the original alpha test, and the same immutable vertex range.
The original binary cutout material uses GREATER-zero alpha testing. A final
fullscreen black overlay applies scene fading after rendering and saturation;
its immediate vertex attributes are copied into the command buffer and never
overwrite model data already submitted to the GPU.

There is one explicit clamp-order approximation: when texture filtering makes
an otherwise binary-alpha edge fractional, and textured diffuse plus specular
would saturate, the two native passes may saturate after alpha blending rather
than before the original single GX blend. Opaque pixels and zero/one-alpha
interior pixels avoid that particular difference. Fixed-point interpolation
and quantization can also differ between GX and PICA; this is not a claim of
pixel-exact console parity.

### Arrival reflection texture

The other added bike material uses an EXT reflection layer and has no
specular channel. From a normalized camera-space normal, the generated UV is

```
u = 0.5 * normal.x + 0.5
v = -0.5 * normal.y + 0.5
```

The sampled C3T3 affine matrix then applies both scale and cross terms:
`u' = u*scale.x + v*cross.x + offset.x`,
`v' = u*cross.y + v*scale.y + offset.y`.
The fragment combiner interpolates the lit primary result with the reflection
texture using the original blend factor and leaves alpha unchanged. These
steps follow pinned
[texture-coordinate generation](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/tobj.c)
and the EXT stage in the material expressions linked above.

## Arrival camera end boundary

The compiled source owner
[`gs_render_util_exact_800D2B44.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_render_util_exact_800D2B44.c)
stores `aobj->end_frame + 1`. The compiled per-frame owner
[`gs_range_800D1070.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_800D1070.c)
subtracts one again, evaluates the current camera pose, advances the cursor,
then marks a mode-zero animation ended when its cursor reaches
`authored_end - 0.1`. The corresponding literal values are in
[`gs_render_util_sdata2.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_render_util_sdata2.c).

For the arrival camera's authored end 150 and rate 0.5, a callback receiving one
source tick per update sets the end flag after 300 updates, or five seconds at
60 Hz. It clamps the retained cursor to 149.9. Because pose evaluation precedes
the increment and end test, the update that first sets the flag evaluates 149.5;
a following update evaluates the retained 149.9. The runtime can retain that
clamped final pose during the following fade without adding a fictitious extra
tick before animation completion.

## Reproducible native checks

The source-only programs and commands are in
[`tools/sandbox/README.md`](../tools/sandbox/README.md). The corner/combiner
probes cover the original title path; the stage probe covers separate arena
bindings, independent UV channels, subtraction and ONE/ONE addition. The
arrival probe independently calculates the rational highlight at two joint
origins, checks zero-alpha rejection, and tests reflection coordinates with
and without cross terms. Its fade variant checks saturation before fading.

The stage and arrival tests passed using the supplied Azahar software renderer.
The arrival checks measured specular RGB values `(81,73,68)` and `(112,88,76)`;
the saturated half-fade case measured `(127,95,63)`. All checked pixels were
within one byte of their independent expectations, and each run completed its
requested 90 frames without interruption. These tests establish this bounded
native rendering path in that emulator. Actual scene captures and hardware
measurements must be reported separately, with the exact executable and
private asset hashes recorded by the run harness.
