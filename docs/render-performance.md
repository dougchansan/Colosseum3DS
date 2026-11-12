# Renderer performance: retained geometry, frame pacing and counters

The shared stage renderer (`platform/3ds/title_renderer.c`) lights and stages
vertices on the CPU. On hardware the battle ran at 7 to 8 fps with two
per-vertex loops over about 53,000 vertices taking almost all of the frame:
`c3_title_animate` and the staging loop of `c3_stage_renderer_draw`. Most of
those vertices belong to geometry that does not move. This document describes
what changed, how to read the new counters, and what is still open.

Nothing here has been run on a console yet. Host tests show equivalence and
relative speed only; the trace counters below are the way to confirm each
claim on hardware.

## Retained models

A model whose sampled vertices are written only by `c3_title_animate` sets
`C3Title.retained = 1` (`include/colosseum3ds/title.h`). For such a model:

- `c3_title_animate` samples every bone and material as before, then rewrites
  a batch's `draw_vertices` only when a bone the batch uses, its UV transform
  or the placement differs from what the batch was last built from, and only
  when the batch is visible. The comparison is against the sampled values, so
  a clip or time change, a paused clip and a single-sample pack need no
  special case. Each rewrite advances `batch_revision[batch]`.
- `C3Title.hidden = 1` skips skinning altogether and reports every batch
  invisible; batches that changed while hidden are rewritten when shown.
- `C3Title.placement` (see `c3_title_place_yaw`) is folded into the bone
  matrices, replacing the second pass that rotated and translated every
  sampled vertex. `pose.matrices` stay the unplaced sampled bones. With
  `placement.origin` the specular origins move too (field actors); without
  it they stay (battle actors, as before).

A model that is not retained keeps the old contract exactly: every call
rewrites every vertex, and the caller may modify `draw_vertices` afterwards.
`src/field_story.c` still does that for Wes and the people, so they remain on
this path until its `transform` pass is replaced by the placement fields.

UV scroll is baked into the sampled vertices, so a UV-animated batch is
dynamic; its neighbours on the same static bones are not.

## Staging only what changed

The vertex arena in linear memory persists across frames. For a tracked model
(retained, or opted in with `c3_stage_renderer_set_static`) the renderer keeps
one key per batch (`platform/3ds/stage_cache.h`): the batch revision, a serial
of the model's lights (lit batches), a serial of the view matrix (specular and
normal-reflection batches), the material fields baked into vertices (unlit
alpha, specular origin, reflection UV transform) and the television V flip. A
visible batch is lit, staged and flushed again only when its key differs.
Hidden batches are neither staged nor drawn. Only the rewritten span of a
model is cache-flushed.

Everything is staged again after `c3_stage_renderer_init`, after a television
enable change, after `c3_stage_renderer_set_static`, after any frame in which
the model was untracked, and after a busy wait or recovery
(`c3_stage_renderer_is_idle` acquiring the queue itself). Shop light changes
(`c3_area_lights_apply`) are detected by comparing the model's lights with the
copy taken when it was last staged; unlit batches ignore them.

Who uses it: the battle stage (every model; placement and hidden actors), the
developer floor (every pack; Wes through placement), the PDA preview, and the
story field scene for its map packs.

## The two per-vertex loops

- `c3_title_animate`: positions and UVs use the original expressions and are
  bit-identical. Normals are normalised in single precision, or not at all
  when the batch's source normals and the bone's normal matrix are unit within
  `C3_TITLE_UNIT_TOLERANCE` (1e-5 in squared length). Scratch storage is
  static: the function is not reentrant.
- Staging (`stage_batch`): per-light constants are computed once per model
  (`light_setup`: active lights in order, infinite directions normalised, the
  spot cone's axis and cosine), clamps are inline, byte colours come from a
  256-entry table of the same quotients, and the normal is renormalised only
  when its squared length is off by more than 1e-4. Unlit output is
  bit-identical. Lit output is bit-identical when the renormalisation runs and
  otherwise differs by a relative 1e-4 at most. Specular and normal-generated
  UVs are separate passes over only the batches that need them, unchanged.

`tests/test_render_perf.py` compiles `tests/render_perf_native.c`, which holds
the previous loops verbatim as references and compares them with production on
synthetic packs: positions, UVs and unlit colours bit for bit; normals,
placed positions and lit colours within stated bounds (lit colour at most
1e-4, at most one 8-bit step); retained sampling against a full reference
sample over a sequence with hidden, placement and clip-time changes; and the
persistent arena, staged through the real cache keys, against a full restage
every frame.

    python3 tests/test_render_perf.py bench
    python3 tests/test_render_perf.py packs /boot/outskirt_map*.c3t

Host numbers (Docker gcc:13, `-O2`, 20,160 vertices, 28 bones, 3 lights; host
time shows relative change only, not ARM11 time):

| Loop | Before | After |
| --- | ---: | ---: |
| animate | 14.4 ns/vertex | 4.2 |
| animate + actor placement | 16.7 | 4.2 |
| stage, half lit half unlit | 24.1 | 7.1 |
| stage, lit batches only | 43.1 | 11.0 |

On the prepared packs (`packs` mode, clip 0, 30 fps, counts not times) a
retained model rewrites per frame: Outskirt maps 1,824 of 59,832 vertices;
shop maps 4,116 of 24,417; Phenac City maps 8,946 of 72,366. Worst
old-versus-new difference on those packs: colour 5.4e-6, one 8-bit step.

## Frame ownership and pacing

`platform/3ds/frame_pacing.inc`. A draw now:

1. waits until two VBlanks after the previous frame was acquired (the 30 fps
   cap; acquisitions are VBlank-aligned);
2. acquires the frame with nonblocking `C3D_FrameBegin`, polling every
   0.5 ms for at most 100 ms;
3. stages, builds commands and calls `C3D_FrameEnd` at once.

The scene's input, script and skinning run before the draw, on the CPU, while
the GPU is still drawing the previous frame. With staging plus GPU work under
two VBlanks every frame is swapped two VBlanks after the last. Slower frames
are not delayed further. Simulation still uses real elapsed time.

History: the first version acquired the next frame right after submitting
and held submission until one VBlank after acquisition. On hardware
(3ef1b3f, Outskirt Stand) that serialised about 20 ms of CPU work with an
11-13 ms GPU frame. Standing still it held 29-30 fps, but near Willie and
while running it fell to 26-27 fps, with `perf_pace_us` *rising* (13-14 ms
per frame) and `perf_gpu_busy` on about half the frames: frames were missing
the second VBlank, not running out of CPU.

The VBlank clock is `C3D_FrameCounter(0)` (confirmed in the pinned
libcitro3d: `onVBlank0` counts once per VBlank at the default 60 Hz rate).
Waits poll it with short sleeps and also end on the system clock, because
Citro3D stops counting while the application is suspended. Nothing blocks on
a GSP event. Build with `-DC3_RENDER_VBLANKS_PER_FRAME=1` (60 fps cap) or `=0`
(no waits) through `C3_EXTRA_CFLAGS` to compare.

If the GPU does not release within 100 ms the draw still returns OK; the next
draw makes its one nonblocking attempt, returns `C3_RENDER_BUSY`, and the
scene's `c3_render_recover` path applies as before
([render-recovery.md](render-recovery.md)). An owned frame holds no commands,
so `c3_stage_renderer_is_idle` and shutdown close it empty: the same
quiescence proof the recovery contract already relied on. `c3_render_recover`
now retries a busy queue every 2 ms instead of every 16 ms, and while its
watchdog view is shown waits for the next counted VBlank, at most 16 ms.

The draw no longer calls `gfxFlushBuffers()`: the top framebuffer is written
by the GPU and the bottom screen is flushed by `c3_bottom_screen_present`.

## Counters

`C3TitleRenderStats` gained, per frame: `prepare_us`, `upload_us`,
`upload_bytes`, `submit_us`, `wait_us` (idle time inside the draw: cadence
and GPU acquisition), `cached_batches`, `vertices_static`.

Interval totals over submitted frames are kept by the renderer and appended
to the existing once-per-second trace lines by
`c3_stage_renderer_perf_format`, which also clears them. Existing fields are
unchanged and come first. Divide sums by `perf_frames` for per-frame values.

| Field | Meaning |
| --- | --- |
| `perf_frames` | frames submitted in the interval |
| `perf_animate_us`, `perf_animate_max_us` | time in `c3_title_animate`; largest single frame |
| `perf_animate_calls` | calls to `c3_title_animate` |
| `perf_vertices_animated`, `perf_vertices_retained` | vertices skinned; vertices left untouched (unchanged or invisible) |
| `perf_place_us` | scene-reported placement outside `c3_title_animate` (field: story sample minus its animation) |
| `perf_prepare_us`, `perf_prepare_max_us` | lighting and staging, cache flushes excluded |
| `perf_vertices_prepared`, `perf_vertices_static` | vertices staged; visible vertices of tracked batches kept in the arena |
| `perf_cached_models`, `perf_cached_batches` | tracked models with nothing staged; tracked batches kept |
| `perf_upload_us`, `perf_upload_bytes` | `GSPGPU_FlushDataCache` of models, HUD, particles and Confusion |
| `perf_submit_us`, `perf_submit_max_us` | command building |
| `perf_end_us` | `C3D_FrameEnd` |
| `perf_pace_us` | idle time holding the cadence |
| `perf_gpu_wait_us`, `perf_gpu_busy` | time waiting for a busy GPU; frames whose acquisition found it busy |
| `perf_gpu_max_ms` | largest completed GPU drawing time |

Where they are printed:

- `battle_render`: existing fields, then the `perf_*` fields. `draw_cpu_us`
  excludes `wait_us`, so it is still CPU time.
- `battle_frame`: existing fields (`full_cpu_frame_us` also excludes
  `wait_us`), then this frame's `draw_prepare_us prepare_us upload_us
  upload_bytes submit_us wait_us cached_batches vertices_static`. Timeline
  time is `sample_clock_cpu_us` minus the frame's share of `perf_animate_us`.
- `field_tick`: existing fields, then `vertices vertices_prepared
  cached_models gpu_previous_ms` and the `perf_*` fields.
- `dev_floor_tick`: existing fields, then `vertices vertices_prepared
  cached_models command_words gpu_previous_ms` and the `perf_*` fields.

What to expect on hardware, to accept or reject each change:

- Retained geometry: `perf_vertices_animated / perf_frames` and
  `perf_vertices_prepared / perf_frames` fall to the dynamic set (battle: the
  six actors and animated arena batches; Outskirt: actors plus about 1,800 map
  vertices), with `perf_vertices_static` holding the rest.
- Loops: `perf_animate_us / perf_vertices_animated` and
  `perf_prepare_us / perf_vertices_prepared` fall well below the measured 0.9
  and 1.45 microseconds; `perf_place_us` is zero in the battle and on the
  developer floor.
- Pacing: `perf_frames` settles at 30 per second with `perf_pace_us` above
  zero and `perf_gpu_busy` near zero. `perf_gpu_busy` close to `perf_frames`
  means the scene is GPU-bound or over budget.

## Scene bounds and the Phenac City budget

`C3_STAGE_MAX_MODELS` is 48 (was 16), `C3_STAGE_MAX_VERTICES` 196,608 (was
131,072), `C3_FIELD_MAX_MODEL_PACKS` 47; `C3_STAGE_MAX_TEXTURES` stays 128.
A model is a range of the shared arena plus a texture remap, so the model
bound costs static bookkeeping only (about 6 KiB per model for the staging
keys). The PDA preview keeps its own 16-part bound.

Computed from the prepared Phenac City packs (8 map packs, Wes, 26 people):

| | |
| --- | ---: |
| models | 35 |
| vertices | 128,358 |
| batches | 426 |
| texture references / unique GPU textures | 94 / 75 |
| vertex arena (56 B per vertex) | 7,188,048 B |
| unique RGBA8 texture data | 8,794,624 B |
| arena + textures | 15,982,672 B of the 33,554,432 B linear heap |

The linear heap also holds the framebuffers, the 1 MiB command buffer and
audio buffers; the hardware traces showed about 14 to 16 MB free at Outskirt
Stand. The renderer's own 32 MiB storage check equals the whole linear heap,
so allocation failure is the effective limit. Each `C3Title` still keeps its
whole file, including texture pixels already uploaded: about 79 MB of CPU
memory for these 35 packs (file sizes plus 92 bytes per vertex of source and
sampled arrays), which is close to the application heap and is the constraint
to watch. That figure is computed, not measured on hardware.

Identical textures are shared across packs by content; models that share one
owner through `c3_title_instance` are matched by pointer first.

## GPU-native field textures

The field's large images are real 1024x1024 GameCube textures (CMPR, half a
byte per texel there). The prepare tools emit RGBA8 rows, which the renderer
used to swizzle into a single-level RGBA8 texture: 4 MiB each, always sampled
from full size on a 400x240 screen. `tools/pack_textures.py` rewrites a
finished C3T pack's texture section into the exact PICA image (header byte
112 = 1; the descriptor's wrap_s word carries format and level count):

- opaque images: RGB565 with a box-filtered mip chain down to 8 texels;
- images with any alpha below 255: single-level RGBA8, so alpha-tested
  cutouts do not erode;
- `--max-dimension` drops leading mip levels (not used yet).

The renderer copies these bytes and enables trilinear filtering
(`C3D_TexSetFilterMipmap`). Legacy packs load unchanged. Run it on the field
packs after preparation:

    python tools/pack_textures.py <boot>/outskirt_map0.c3t <out>/outskirt_map0.c3t --report <out>/outskirt_map0.json

Measured on a New 3DS (build 0d61814, same binary, same route; standing at
the stand, then a D-pad square past the wagon; `field_tick` intervals,
loading and exit excluded):

| | original packs | GPU-native packs |
| --- | ---: | ---: |
| Outskirt texture bytes (11 packs) | 22.35 MB | 15.05 MB |
| GPU storage at Outskirt (`gpu_bytes`) | 15.66 MB | 11.81 MB |
| standing: GPU ms per frame, median / p90 | 13.03 / 14.07 | 11.56 / 13.53 |
| walking: GPU ms per frame, median / p90 | 15.79 / 19.14 | 13.13 / 15.37 |
| walking: worst interval `gpu_max_ms` | 20.05 | 16.70 |
| standing fps | 30 | 30 |

Native-size console captures and the Azahar A/B show no visible difference
(Azahar mean difference about 2/255 per channel). Walking still reports 29
frames in most one-second intervals in both runs while the GPU has about 17
ms of slack, so that shortfall is on the CPU side, not textures.

## Not done

- Confusion still rebuilds and uploads its 98,304-vertex grid every frame,
  and particles still issue one draw per quad. Both remain the worst spikes.
- Wes and the people in the story field are still animated, placed in a
  second pass and staged in full every frame (`src/field_story.c`).
- No culling, no state deduplication, no index buffers, no smaller vertex.
- GPU skinning. The pieces it needs are in place or isolated: per-batch bone
  sets (`batch_bones`), placement as a matrix composed with the bones, a
  persistent arena with per-batch dirty keys, and lighting separated from the
  position/UV copy (`stage_batch`). It still needs bind-space positions and a
  bone index in the GPU vertex (so a second vertex layout or stream), a bone
  palette of at most about 28 matrices per draw with batches split by bone
  set, a normal palette or a uniform-scale restriction, and either a colour
  stream that stays CPU-lit or lighting in the shader.
