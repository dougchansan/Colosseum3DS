# Outskirt Stand interior, NPC and television assets

`tools/prepare_area.py` extends the admitted first-area converter to the original
shop, all five initial interior NPCs, and exterior Folly, Trudly, the parked
vehicle and Willie. It
also prepares the real animated TV news model and the original camera that
looks at the screen. Generated files contain original game data and belong only
in a private SD preparation directory. No extracted images, model packs, script
bytes, dialogue, saves or disc files belong in the repository.

This document describes asset conversion and renderer verification. The native
story adapter owns interactions, source flags, dialogue and map changes. Rendering
the shop does not by itself establish a completed story event or playable battle.

## Reproduce

Use the same verified GC6E01 revision 0 extraction and pinned HSD parser as the
[exterior preparation](outskirt-assets.md):

```sh
python3 tools/prepare_outskirt.py \
  --disc /absolute/private-disc \
  --output /absolute/private-output/outskirt

python3 tools/prepare_area.py \
  --disc /absolute/private-disc \
  --output /absolute/private-output/first-area
```

The output must be a new directory. Preparation verifies the disc, exact archive
sizes and SHA256 values, and the existing parser dependency closure before
conversion. It stages the complete result before publishing the directory.
`area-manifest.json` records every source member, generated pack hash, original
clip index, image variant and resource-to-file mapping. `area-actors.json` is an
offline interchange file; the native application reads its bounded C3A1
counterpart through `tools/prepare_area_runtime.py`, not JSON.
The first command provides seven exterior map/Wes/arrival model packs plus
`outskirt.c3f`; the second provides thirteen interior/exterior-NPC model packs,
`shop.c3f`, `shop.c3l`, `shop_camera.c3c` and actor metadata. Combine those
outputs with the separately prepared story/runtime metadata. The original Wes
arrival motorcycle is distinct from NPC2's vehicle used by Folly and Trudly.

For incremental preparation, `--wes-only` writes both Wes packs and a provenance
manifest. `--exterior-actors-only` writes the four initial exterior actors and
source actor/CCD metadata; optional indices, such as `--exterior-actors-only 2`,
prepare only the selected model files. Every mode requires a new output
directory and stages its files before publishing. Use the full preparation for
a self-contained set of all models described below.

The additional admitted archive is `S1_shop_1F.fsys`, 2,947,200 bytes, SHA256
`7d74dcb75c7f8aebc7479fee695e87a02ab58112d110dada15e88353d7215329`.
Semantics are pinned to Count-Sheep/pkmn-colosseum commit
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55);
source paths and Git blob identities are recorded in
[`config/area-source-files.json`](../config/area-source-files.json).

## Pack mapping and original motions

The interior is source floor 2, resource group `0x1E`. Its map member is
`0x00E60200`, collision member `0x00E70600`, and REL module is 5. The map has three
original model roots, partitioned into two C3T files at the unchanged 64-batch
bound. Its third root is the screen receiving the rendered TV texture.

| Prepared file | Expanded vertices | Batches | Clips | File bytes |
|---|---:|---:|---:|---:|
| `shop_map0.c3t` | 21,729 | 64 | 1 | 10,710,032 |
| `shop_map1.c3t` | 2,688 | 7 | 1 | 4,786,368 |
| `shop_wes.c3t` | 2,445 | 7 | 4 | 661,072 |
| `shop_npc0.c3t` | 1,635 | 13 | 12 | 2,151,584 |
| `shop_npc1.c3t` | 1,950 | 6 | 5 | 595,648 |
| `shop_npc2.c3t` | 1,803 | 7 | 11 | 846,208 |
| `shop_npc3.c3t` | 1,632 | 5 | 11 | 808,752 |
| `shop_npc4.c3t` | 1,953 | 6 | 18 | 1,120,816 |
| `shop_tv.c3t` | 5,109 | 21 | 1 | 9,891,096 |
| `outskirt_npc0.c3t` | 1,929 | 5 | 22 | 1,548,720 |
| `outskirt_npc1.c3t` | 2,028 | 5 | 20 | 1,867,448 |
| `outskirt_npc2.c3t` | 2,538 | 6 | 15 | 4,042,296 |
| `outskirt_npc3.c3t` | 1,953 | 6 | 18 | 1,120,896 |

Interior NPC rows retain source PeopleInfo indices `0x18`, `0x1A`, `0x0B`, `0x0D`
and `0x58`; NPC4 is Willie. The exterior metadata retains all seven original
rows. NPC0 through NPC3 have prepared exterior geometry for the initial story.
NPC0 is Folly (PeopleInfo `0x4E`, model `0x06B10400`) and NPC1 is Trudly
(`0x4D`, model `0x06B00400`). Their models are necessary for the original first
approach sequence before entering the shop.
The original cold exterior script shows NPC2 and keeps NPC3 hidden until his
challenge event. Interior rows initially show all five characters. These bits
come from
[`floor_character.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/floor_character.c),
with visibility in raw row byte0 bit7 and load-initial in bit6.

NPC pack indices preserve every original model clip index. They are stored as
one-shot clips; the source `peoplePlayMotion` call determines looping at runtime.
Set the selected `C3TitleClip.loop` before sampling. Idle defaults and all nine
PeopleInfo motion mappings are retained in metadata. Willie therefore retains
clips 1, 13, 14, 15 and 7 needed by his original approach/challenge sequence.
Wes keeps pack clips 0/1/2 mapped to source clips 1/2/3 for idle/walk/run. Pack
clip3 adds genuine source clip0, which has an authored end of 10 frames and
21 prepared samples. It differs from idle and is requested by the original
first-approach script. Both the exterior and interior Wes exporters share this
explicit mapping. The portable clip bound rises from 3 to 32;
the 4,096-total-sample and 24-MiB-per-file limits remain unchanged.

The vehicle retains original part1 as sampled bone9, appended after its nine
render tracks. The source joint is preorder index1, address `0x625C` in model
member `0x010B0400`. Its full model-space world matrix is sampled for all 15
clips; the original boarding sequence attaches Folly and Trudly to this part.
These matrices retain the original model origin, rather than inverse-rest
matrices used by mesh skinning. The offline record's `source_part_bones` maps
source part `1` to sampled bone `9`, and the pack manifest also records the
original joint address and transform convention. The C3T layout is unchanged.
Source lookup handles an engine-added null root explicitly, preserving original
part indices; see
[`gs_part_exact_800EE150.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_part_exact_800EE150.c).
The runtime attachment behavior must follow `GSmodelAttachToGSpart` and
`GSpartGetTransform`, including the source's sum of ancestor Euler rotations.
For this reviewed vehicle, the exporter verifies that part1's only source
ancestor is static identity and that no scale curves alter its unit scale.
Every sampled Euler component must also remain within 0.25 radians of zero.
This admits recovery of the part's Euler rotation from its matrix; that
assumption must be revisited before another attachment hierarchy is enabled.
The C3A actor's word at offset156 stores the part1 sampled-bone index plus1
(zero means absent). Other source part IDs remain unsupported by this adapter.

`shop.c3f` contains two original entrances, two walk triangles and 94 wall
triangles. Arrival geometry is zero for this interior. Event surfaces preserve
their original source IDs rather than inventing destination floors. The CCD
layout differs between event tables: transition triangles at object+44 store
`flags,id` in their last two halfwords, while A-button triangles at object+48
store `id,padding`. The latter has no transition flags. This distinction follows
[`GScolsys2Thru_r56_80111864_o4s.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/GScolsys2Thru_r56_80111864_o4s.c)
and its query-triangle types. The separately audited event table resolves
exterior event4 to floor2/entrance0 and interior event2 to floor1/entrance1.

## C3T4 and original interior lighting

C3T4 keeps C3T3's 128-byte header, 48-byte vertices, 112-byte batches and
96-byte per-batch material samples. Only the light record changes, from 64 to
80 bytes. Versions 1–3 keep their existing byte layouts and remain accepted.

| Light offset | Storage | Meaning |
|---:|---|---|
| 0–63 | Existing C3T light record | Flags, RGBA, position, interest and distance attenuation |
| 64 | u32 | Original GXSpotFn, 0–6 |
| 68 | f32 | Original cutoff in degrees |
| 72, 76 | u32 | Reserved zeros |

Light type 3 is admitted only by C3T4. Other light types must set both spotlight
fields to zero. Cutoff must be finite with absolute value at most 360; raw
attenuation descriptors remain unsupported. The renderer currently implements
spot functions 0 (off) and 3 (GX_SP_COS2), rejecting other functions before GPU
submission. For COS2, with `c=cos(cutoff)`, its coefficients are
`(0,-c/(1-c),1/(1-c))`; the light-to-vertex cone cosine is clamped to zero before
the polynomial is evaluated. Invalid source cutoffs disable the cone according
to the original SDK behavior. Source:
[`sdk_range_800BA1B4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/dolphin/sdk_range_800BA1B4.c).

The shop has three map lights: ambient, infinite, and a spot light with original
cutoff 87.52290344238281 degrees, COS2 function, reference brightness 0.25 and
scaled reference distance 10. The animated RGB values follow the original
300-frame light tracks. Actor animation clocks remain independent. Apply the
area light track to the map, Wes and room NPCs; the TV news model has its own
four original lights and must retain them.

The TV model includes a specular material lit by point lights. The renderer now
uses the original HSD joint-origin half vector for point and spot lights as
well as infinite lights, including the coincident-light fallback. The specular
shininess setup replaces distance coefficients; spot-cone attenuation belongs
to the diffuse channel. The existing separate specular draw retains its known
fractional-alpha blending limitation. Source:
[`lobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/lobj.c).

Raw pixel-engine descriptors override both blending and depth writes. The
interior's source flags `0x31` retain depth writing despite translucent blend
mode. Equal GREATER/AND alpha comparisons also retain their original 8-bit
reference; NPC hair uses 127. These are material-state conversions, not
unconditional renderer overrides.

## Independent C3L1 light and C3C1 camera tracks

Both formats use a 64-byte little-endian header and exact contiguous payload.
Offsets 0/4/8/12 are magic, version1, exact total bytes and header size64.
Offsets32–63 contain the admitted archive SHA256 as provenance, not a signature.

| Header offset | C3L1 | C3C1 |
|---:|---|---|
| 16 | u32 light count | u32 frame count |
| 20 | u32 frame count | f32 sample FPS |
| 24 | f32 sample FPS | u32 loop flag |
| 28 | u32 loop flag | u32 reserved zero |
| Payload | Frame-major RGBA8 for each light | Frame-major 11 f32 camera values |

C3C camera values are eyeXYZ, targetXYZ, roll, vertical FOV, near, far and aspect,
matching `C3TitleCamera`. There are at most 4,096 frames, eight lights, and
1–120 samples per second. Parsers reject bad extents, unknown reserved fields,
nonfinite camera values, invalid clip planes and coincident eye/target. Failed
replacement retains the previous track. Sample calls reject NaN/Inf times;
negative time clamps to the first sample. Loop duration is `(frames-1)/fps`.

`c3_area_lights_load/apply/close` uses held quantized RGB samples and changes
only model light colors. `c3_area_camera_load/sample/close` linearly interpolates
the previously evaluated HSD camera curve samples. The original curves are
sampled at 60Hz by the converter; these APIs do not reinterpret source keys.
`shop.c3l` is 7,276 bytes, 601 frames and a 10-second loop.
`shop_camera.c3c` is 26,508 bytes, 601 frames and a 10-second nonlooping track,
from original camera resource `0x00E51800`.

## Television render pass

The original field-camera texture table entry0 specifies a 256×256 target,
screen resource `0x00E61002`, model resource `0x0D561000`, animation0 and camera
`0x0D571800`. The FSYS model member is `0x0D560400`. Its clip loops at original
frame96, or 3.2 seconds at 30 authored frames per second. Two meshes share a
three-image TIMG animation; the converter emits visible per-image variants
with shared sampled transform tracks. It uses the original images and curve
values, without texture mutation or replacement footage.

This is a real animated 3D model drawn into the room's TV, as implemented by
[`field_camera_exact_80117AE4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/field_camera_exact_80117AE4.c).
The native renderer includes the TV model in its bounded scene arena and calls:

```c
/* With map0, map1, Wes, five NPCs, TV in that order: */
c3_stage_renderer_set_tv(8, 1, 1, true, error, capacity);
```

The arguments are TV model index, screen model index, screen texture index and
enabled state. The destination is exactly `shop_map1.c3t`, batch6, texture1.
Configuration returns 1 on success and 0 on failure. Call after scene init and
before the first draw. Identities stay fixed until shutdown; later calls may
toggle enabled. The TV model is excluded from the main room pass even while
disabled. Its own sampled camera drives the offscreen pass.

The shared Citro3D frame first renders the TV to a VRAM texture-backed color and
depth target, then renders the room while sampling that texture. This uses
[`C3D_RenderTargetCreateFromTex`](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/renderqueue.c),
which supplies native GPU tiling. The framebuffer switch flushes the prior
render buffer and the texture binding invalidates texture caches through the
SDK. No recursive scene initialization or framebuffer feedback is used. The
screen's V coordinate is reversed because this framebuffer's rows differ from
the converter's top-origin image upload convention. A native colored-corner
probe verifies both axes.

The target adds 512KiB for RGBA8 color plus depth. It counts toward the existing
32MiB aggregate storage limit. Target and texture ownership remain alive until
the GPU completion barrier permits scene teardown. The scene capacity is 16
models; aggregate vertex, texture and memory bounds remain enforced.

## Verification and remaining scope

The host asset probe sampled every authored state and every half-step in the
initial eleven generated model packs, checked finite positions/normals, applied the
light track, loads both shop entrances, and samples the camera track. It ran
with AddressSanitizer and UndefinedBehaviorSanitizer. Portable format tests
cover all clip slots, C3T4 light strides, camera/light replacement, malformed
extents and numeric limits. Renderer tests execute production paths against
SDK ownership contracts, including failed TV allocation, toggling, feedback
avoidance and disabled-model exclusion.

The later Folly/Trudly additions passed another 2,200/2,120 authored and half-step
pose checks under the same sanitizers. The vehicle attachment extension passed
6,150 pose checks; all 3,075 stored render/material/camera samples remain
bitwise identical to the prior pack, and every appended part matrix matches the
source joint. `wes_asset_probe.c` checks each old/new Wes pair: all 356 poses
are finite, the existing three clips remain bitwise identical, and the appended
source clip0 differs from idle. `area_asset_probe.c` accepts optional model
basenames for incremental validation without rechecking unrelated maps.

The standalone native `shop_scene_probe.c` loads the actual nine-model interior
and original camera, places actors from private C3A1 metadata, and renders the
real TV. It intentionally runs no GSVM and shows that distinction on screen.
With camera time4.0s and model time0–2.5s, the verified software-PICA run used
132 draws and 40,284 submitted vertices per frame, 26 GPU textures, 17,694,848
owned GPU bytes, and 35,394,032 CPU pack bytes on ARM32. The observed command
peak was 16,656 words, or 66,624 bytes. This is emulator integration evidence,
not a New 3DS hardware frame-rate measurement.
That captured run used the earlier three-clip Wes pack; the fourth clip adds
50,416 CPU pack bytes and leaves the GPU vertex/texture allocation unchanged.

Build either the synthetic orientation probe or the original shop probe with:

```sh
cd tools/sandbox
make -f scene_probe.mk PROBE=tv_texture_probe BUILD=tv-probe-build \
  DEVKITPRO=/absolute/devkitpro
make -f scene_probe.mk PROBE=shop_scene_probe BUILD=shop-probe-build \
  DEVKITPRO=/absolute/devkitpro
```

Stage the private `shop*.c3t`, `shop.c3a`, `shop.c3f`, `shop.c3l` and
`shop_camera.c3c` under the emulator's usual `3ds/Colosseum3DS/boot` directory.
The shop probe optionally reads `shop-probe.txt` containing camera time0–10
seconds, and writes `shop-probe.log` with resource and submission counters.
Allow enough frontend frames for initial pack reads; 180 frames can end during
loading. A bounded 3,000-frame/55-second software-renderer run reached the
animated TV and then stopped at its wall-time limit.

Source neck targeting, per-part animation blends, shadow rendering, particle
callbacks, additional TV programs, event audio and later-area geometry still
need their corresponding runtime systems. NPC scripts and battle logic remain
separate from these assets. The full application must be tested through real
door transitions, TV dialogue, Willie interaction and battle entry; this probe
does not substitute for those checks.
