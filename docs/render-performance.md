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
| `perf_interval_us`, `perf_fps` | wall time since the previous take; frames per second over it |

The field also prints a line on every new button press, which ends the
interval early. Judge frame rate by `perf_fps`, not `perf_frames`.

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
- Pacing: `perf_fps` settles at 29.9 (59.83 Hz / 2) with `perf_pace_us` above
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
(Azahar mean difference about 2/255 per channel).

Walking showed `perf_frames` of 28-29 in both runs. That was the counter,
not the game: the field writes a trace line, which takes the interval, on
every new button press, and the walk was D-pad taps a median 0.96 s apart
(input redirection allows at most 0.5 s per press). At 29.92 fps those
windows hold 28.3-30.2 frames, matching what was reported. Intervals now
carry `perf_interval_us` and `perf_fps`.

## Battle on hardware (Willie preview)

Measured on a New 3DS with a probe build that boots straight into the
Willie preview with a fresh game and no save writes
(`make -f Makefile.3ds BUILD=build-3ds-probe C3_EXTRA_CFLAGS=-DC3_BATTLE_PREVIEW_PROBE`,
trace in `preview-probe.log`). Route: Umbreon Bite on Zigzagoon A, Espeon
Confusion on Zigzagoon B; both Zigzagoon Tackle. Phases are split by the
`wzx_*` start/complete lines; `battle_frame` carries interval sums of the
bottom-screen redraw (`ui_us`, `ui_draws`), `c3_battle_stage_step`
(`step_us`) and the draw (`draw_us`).

| phase | fps before | fps after | per frame after (step / draw / UI) |
| --- | ---: | ---: | --- |
| idle | 21.8 | 21.8 | 14.1 / 25.0 / 0 ms |
| Confusion | 12.6 | 17.5 | 16.2 / 29.9 / 10.9 ms |
| Tackle | 11.8 | 16.6 | 18.1 / 25.5 / 12.7 ms |
| damage | 12.3 | 16.8 | 18.7 / 25.8 / 12.7 ms |

The effects themselves were not the main cost. HP events redraw the
bottom screen every frame, and each redraw took 34 ms because the
software UI divided by 255 three times per pixel and per scaled coordinate
on a CPU without a divide instruction. `src/bottom_ui.c` now writes opaque
pixels directly and blends without division: 12.8 ms per redraw, output
byte-identical (`tests/test_bottom_ui_golden.c`). Effect logic adds only
2-5 ms to the step.

What then kept the battle under 30 fps was the idle cost: 13.5 ms of
skinning and 20.9 ms of lighting/staging for about 17,500 actor vertices
every frame (the two loops at about 0.8 and 1.2 microseconds per vertex).
GPU skinning below removed it.

## GPU skinning

Batches a caller opts into (`C3Title.gpu_batches`, retained titles only)
are skinned, lit and UV-mapped by a second vertex program, DVLE 1 of
`title_shader.shbin` (`platform/3ds/title_skin_shader.pica`, built together
with `title_shader.v.pica` so switching programs uploads no shader code).

- Vertices stay in bind space in a static 48-byte stream written once at
  scene init (`platform/3ds/skin_renderer.inc`), with the vertex's palette
  slot. Instances of one pack share it.
- `c3_title_animate` no longer skins those batches; it stores the placed
  bone matrix and normal matrix of their bones in `pose.skin` and
  `pose.skin_normal`.
- Per draw: up to 10 bones (position and normal rows), the model's ambient
  light folded into the batch base colour plus up to four infinite or point
  lights with GX distance attenuation, the batch's colour/alpha source mode
  and its two UV rows (`src/skin_uniforms.c`).
- On the CPU path still: specular batches, normal-generated (reflection)
  UVs, spotlights, batches with more than 10 bones, and any model the caller
  does not request. The battle requests its six actor slots
  (`c3_stage_renderer_request_gpu_skin` before init); each costs 48 bytes of
  linear memory per vertex, and the battle has about 4 MB to spare before
  audio. Requesting every model also streamed the arena packs and battle
  audio then failed to allocate.

`src/skin_uniforms.c` also mirrors the shader in C. Against the five battle
actor packs, 126,162 sampled vertices (three clips, three times, placed as a
battle actor) match the CPU path exactly in position and UV and within
0.001/255 in colour (`C3_SKIN_PACKS=<dir> build/test_skin_uniforms`).

On a New 3DS, Willie preview probe, same route (Bite, Confusion, two
Tackles), CPU path at 5cbb260 against GPU skinning at 926fddc:

| phase | fps CPU | fps GPU | CPU ms per frame CPU / GPU |
| --- | ---: | ---: | --- |
| idle | 21.8 | 29.4 | 39.5 / 9.0 |
| Confusion | 17.5 | 28.7 | 55.8 / 25.1 |
| Tackle | 16.6 | 28.9 | 57.1 / 27.9 |
| damage | 16.8 | 28.9 | 56.9 / 28.4 |

Vertices skinned on the CPU fell from 17,499 to 1,050 per frame (the
specular batches), animate from 13.6 to 2.7 ms, prepare from 21.0 to
2.6 ms and uploads from 982 to 59 KB per frame. The GPU's own frame time
fell too (19.0 to 16.7 ms median). Native-size captures of the battle and
of Confusion's hit show the same models, shading and textures. Effect
phases now spend most of their remaining CPU time in the 13 ms bottom-screen
redraw.

## Bottom-screen redraw

`battle_frame` also breaks the redraw down per primitive (`ui_begin_us`,
`ui_panel_us`, `ui_text_us`, `ui_rect_us`, `ui_sprite_us`, `ui_present_us`
and counts; `C3UiPerf` in `colosseum3ds/bottom_ui.h`). A battle redraw with
the GPU-skinned probe build, per redraw on a New 3DS:

| part | before (154fe2d) | after (b4ee268) |
| --- | ---: | ---: |
| 5 panels (4 HP frames, message box) | 7.77 ms | 1.21 ms |
| backdrop | 3.51 ms | 1.63 ms |
| text (114 glyphs) | 1.39 ms | 1.39 ms |
| present (copy + flush) | 0.86 ms | 1.79 ms |
| total | 13.85 ms | 6.26 ms |

- The backdrop gradient never changes: it is rendered once per framebuffer
  layout and copied (it was 240 one-pixel rows, 76,800 scattered writes in
  the rotated layout).
- Drawing functions record the areas they touched since `c3_ui_begin`. A
  panel drawn over untouched backdrop always yields the same pixels, so it
  is cached (8 entries, keyed by asset pack, sprite, rectangle and layout)
  and copied on later redraws; anything drawn underneath first disables the
  cache for that panel.
- `tests/test_bottom_ui_golden.c` renders every scene twice per layout;
  all 49 framebuffers match the bytes recorded before any UI optimisation.

Effect frames went from about 28 to 20 ms of CPU and Tackle/damage now run
at 28.9 fps, level with idle moments of the same run (29.4). The remaining
copies (backdrop, present) are two 150 KB memcpys that drawing straight
into the framebuffer could merge, not attempted.

## Not done

- Confusion still rebuilds and uploads its 98,304-vertex grid every frame,
  and particles still issue one draw per quad. On hardware these cost a few
  ms (Confusion's draw is about 5 ms above idle), far less than the actor
  skinning below.
- A battle UI redraw is 6.3 ms; HP events still redraw every frame.
- Wes and the people in the story field are still animated, placed in a
  second pass and staged in full every frame (`src/field_story.c`).
- No culling, no state deduplication, no index buffers, no smaller vertex.
- GPU skinning covers battle actors only. Field actors (Wes, people) rewrite
  sampled vertices after animation (passengers, developer floor) and need
  that moved into placement first. The CPU arena still reserves 56 bytes for
  every GPU-skinned vertex, and the battle sits at 29.4 rather than 29.9 fps
  idle; neither is investigated yet.
