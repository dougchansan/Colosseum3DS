# Original opening and Outskirt Stand asset preparation

`tools/prepare_intro_media.py` and `tools/prepare_outskirt.py` prepare private
GC6E01 revision 0 assets for the bounded native opening/field path. The latter
uses `prepare_field_geometry.py` for original HSD geometry, animation and
materials. These tools contain no original game data. Their generated files
must remain outside the repository, or under its ignored `private/` directory.

The field is live geometry. The opening explosion is the original THP movie;
the subsequent bike arrival is an independently animated 3D scene with its
original parent-motion and camera resources. The map, bike and Wes are not
replaced by screenshots or prerendered gameplay footage.

## Reproduce

Use an extracted disc with `sys/boot.bin`, `sys/main.dol` and the original
`files/` hierarchy. Initialize `vendor/hsd-importer` at its recorded pin.
Python 3 and NumPy are required for field conversion; FFmpeg/FFprobe are
required for movie conversion. Blender is not required.

```sh
python3 tools/prepare_intro_media.py \
  --disc /path/to/private-disc \
  --output /path/to/new-private-opening

python3 tools/prepare_outskirt.py \
  --disc /path/to/private-disc \
  --output /path/to/new-private-outskirt
```

Both destinations must be new directories. Preparation completes in a staging
directory and publishes the directory only after every required output and
manifest succeeds. The disc admission checks GC6E01 revision 0, the original
DOL size, and DOL SHA1 `870e8b9693ca780782d80f22a6a4572d8ba9458f`.

Field archives are admitted by exact size and SHA256 before FSYS decompression
or HSD parsing:

| Archive | Bytes | SHA256 |
|---|---:|---|
| `S1_out.fsys` | 1,468,000 | `37900ccab1353ab835c7e4acc40cd5e1349237589a53b441c78ce91a891f8034` |
| `field_common.fsys` | 211,424 | `83bc8cf05026263cd0c1d3dfb7cc448445922b34213ef973ab3c94090c669fb5` |
| `common.fsys` | 743,136 | `f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a` |

The parser's verified dependency closure is the same 150-file closure used by
the title exporter, pinned to
[`dc71de837978a6a2fe8c5082f2840cfafb135235`](https://github.com/StarsMmd/Blender-Addon-Gamecube-Models/tree/dc71de837978a6a2fe8c5082f2840cfafb135235).
All game semantics below refer to Count-Sheep commit
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).
`config/outskirt-source-files.json` records the exact Git blob identities and
sizes of the 21 source files used for this preparation and runtime audit.

## Reviewed original resources

| Resource | Container | Use |
|---|---|---|
| `movie/openingdemo.thp` | Disc movie directory | Original hideout explosion/opening movie |
| `00E20200` | `S1_out.fsys` | Five original map models, camera and lights |
| `00E30600` | `S1_out.fsys` | Fourteen static CCD collision objects |
| `02571C00` | `S1_out.fsys` | REL131, including the five entrance records |
| `01430E00` | `S1_out.fsys` | Original GSVM field bank; entry function16 drives the bike arrival |
| `00F70400` | `field_common.fsys` | Wes (`ken_b1`), including idle/walk/run animation |
| `0C6C0400` | `S1_out.fsys` | Original animated bike and associated actors |
| `105D0400` | `S1_out.fsys` | Geometry-free parent motion, registered as model `105D1000` |
| `105C1800` | `S1_out.fsys` | Original arrival camera |

The generated manifest records each selected member's exact decoded size and
SHA256, every generated pack hash, the source mesh/bone assignment, clocks and
explicit limitations. REL131's entrance records come from its section3 data
at `+0x12C`; common REL125 uses section4 data. These different section indices
are verified independently rather than treated as a generic REL layout.
The canonical registrations are in
[`S1_out.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/S1_out/S1_out.c)
and [`common_rel.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/common_rel/common_rel.c).

The opening source is exactly 64,046,624 bytes with SHA256
`092d180eec49a002fb2a99d4d25e967c616a4ce8d8f06c1f95b160b1897d2526`.
It produces `openingdemo.c3v`: 1,926 frames, 320×240 RGB565LE at nominal
30000/1001 fps, with 2,058,287 stereo PCM sample frames at 32kHz. The output is
304,066,812 bytes. Decoding streams through bounded buffers; the native runtime
also reads one movie frame and bounded PCM blocks at a time. The movie's
embedded THP audio is retained. Its separately triggered BGM and four event
sound cues are recorded in the manifest but are not mixed into that PCM.
See [boot-media-format.md](boot-media-format.md) for the C3V1 layout.

## Field pack set and independent clocks

| File | Expanded vertices | Batches | Sampled matrices | Format | Bytes |
|---|---:|---:|---:|---|---:|
| `outskirt_map0.c3t` | 14,070 | 64 | 3 | C3T2 | 12,053,184 |
| `outskirt_map1.c3t` | 28,212 | 47 | 3 | C3T2 | 7,751,912 |
| `outskirt_map2.c3t` | 9,738 | 3 | 1 | C3T2 | 4,765,944 |
| `outskirt_map3.c3t` | 7,812 | 33 | 3 | C3T2 | 3,406,456 |
| `outskirt_wes.c3t` | 2,445 | 7 | 35 | C3T2 | 575,568 |
| `outskirt_arrival0.c3t` | 13,698 | 39 | 62 | C3T3 | 3,519,720 |
| `outskirt_arrival1.c3t` | 3,552 | 5 | 27 | C3T3 | 1,113,808 |

The complete five-model map retains 19,944 triangles / 59,832 expanded
vertices. Wes retains 815 triangles; the arrival retains 5,750 triangles.
Static geometry shares identity tracks, and equal sampled affine tracks are
deduplicated. Source batch order is retained within each ordered pack.
The pack split does not increase any single C3T allocation limit.

Map packs 0–2 use one 359-sample clip at 60 Hz, looping over 179/30 seconds.
Map3 contains the independent slow sky rotation: 1,001 samples over its
29999/30-second period, using `fps = 30000/29999` in the pack. It is not forced
to reset with the short map animation. The last sky sample retains its authored
endpoint; the loop resets at the period boundary.

Wes native clips 0/1/2 select original clips 1/2/3 (idle, walk, run). They contain
61/61/35 samples at 60 Hz, with periods 1, 1 and 17/30 seconds. Those durations
use the source's actual loop boundary, `anim_end_frame`. The separate movement
query returns `end_frame + 1`: its 31/31/18 frame counts remain in C3F metadata,
together with original per-clip distances 0.9/1.3 and collision radius 0.4 in
pack coordinates. The distinction follows
[`gs_model_anim_exact_800EC4D0.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_anim_exact_800EC4D0.c)
and [`gs_model_anim_suffix_800ED4D4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_anim_suffix_800ED4D4.c).

Both arrival packs contain the same 301-sample, nonlooping 5-second timeline.
The actor's original clip1 loops every 30 authored frames while its attached
parent and camera continue through frame150. Parent attachment and source
joint origins are baked into the sampled world transforms. The camera's
runtime end calculation subtracts 1 from its inclusive stored end before the
`end - 0.1` check; 300 updates at rate 0.5 complete the original 5-second wait.
See [`gs_range_800D1070.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_800D1070.c).

## Geometry, IK and material fidelity

Coordinates keep the source Y-up axes and the established uniform 0.1 scale.
All map meshes are rigid. Wes has 38 original joints and 35 distinct used skin
envelopes; the bike has 170 joints and multiple rigid/weighted submodels.
Each used envelope becomes one sampled affine matrix, retaining the original
weights rather than selecting a dominant bone. Weighted normal transforms use
the inverse transpose. Per-corner GX matrix indices are checked against the
parser's IR assignments; ambiguous shared-position envelopes are rejected.

The evaluator implements the original aligned-scale SRT composition, eight
reviewed two-bone IK chains (two Wes legs and six arrival chains), and the
arrival's position-copy constraint. The no-pole-target IK path uses the owning
parent's world origin, the original Z-axis/cross-product bend plane, the
original length hints and the source flip bit. Unreachable targets extend the
original limb lengths. These equations follow the matched
[`jobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/jobj.c),
not the older approximation in another HSD source file.

The field extends the supported material subset with the map's two UV layers,
ONE/ONE additive and subtractive framebuffer operations, nine specular arrival
meshes and four environment-reflection blends. The latter generate UVs from
the camera-space normal and blend the second texture after the existing RGB
result. Full UV affine transforms retain the arrival texture animation's X/Y/Z
rotation, including GX's constant third UV component. Original opacity,
visibility, TEV colors, culling, depth, alpha comparisons and texture wrapping
remain explicit. These expressions come from
[`tobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/tobj.c)
and [`mobj.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/mobj.c).

## C3T2 and C3T3 extensions

The canonical little-endian header, section order and native bounds remain as
documented for [C3T1](title-format.md). Magic and numeric version must match.
All variants use the `.c3t` filename extension.

| Variant | Vertex bytes | Material bytes per sample | Runtime flag mask |
|---|---:|---:|---:|
| C3T1 | 40 | 48 | 1023 |
| C3T2 | 48 | 64 | 8191 |
| C3T3 | 48 | 96 | 16383 |

The frame stride is `(bone_count + 1) * 48 + batch_count * material_bytes`.
C3T2/3 append second UV (two f32) to the C3T1 vertex. Batch offset104 becomes
the second texture index; singles retain zero. Offset108 is zero in C3T1/2;
C3T3 uses it as a finite f32 reflection blend factor in0–1, zero when unused.

Additional flag values are dual modulate1024, subtract2048, ONE/ONE additive4096
and reflection blend8192. Dual modulate preserves the reviewed expression
`texture0 * texture1 * konst² * raster`, with zero TEV0 and matching source
constants. Reflection is the distinct source expression
`lerp(existing_rgb, texture1_rgb, texture2_blend)`. Conflicting blend/dual flags,
invalid texture indices and values outside the version's mask are rejected.

C3T2 appends UV2 scale/offset (four f32) to each old material state. C3T3 adds:

| Material-relative offset | Storage | Meaning |
|---:|---|---|
| 64 | 2 f32 | UV0 cross terms M01, M10 |
| 72 | 2 f32 | UV1 cross terms M01, M10 |
| 80 | 3 f32 | Original owning joint's world origin for GX specular lighting |
| 92 | u32 | Reserved zero |

Each UV is transformed as `u' = M00*u + M01*v + offset_u`,
`v' = M10*u + M11*v + offset_v`; the existing scale fields store M00/M11.
Native animation applies UV0 to ordinary mesh coordinates. The renderer uses
the UV1 matrix after generating reflection coordinates from the transformed
normal. Earlier versions default all cross terms and source origins to zero.

`outskirt.c3f` is a 21,656-byte C3F1 file with five original entrances,
136 walk triangles and 244 player-wall triangles. Original normals, raw flags,
source IDs and object indices are retained; the separate NPC-wall table is not
merged into player walls. Header offsets180/184 declare four map and two
arrival packs; all required files must be present. See
[outskirt-field.md](outskirt-field.md) for the native movement/state interface.

## Validation and remaining limits

Synthetic tests cover overlapping LZSS copies and zero-filled history, malformed
CCD extents/normals, IK signs/origins/lengths, weighted versus single-weight
envelope branches, GX UV affine coordinates, actual loop periods, and C3T2/3
parsing/interpolation failures. Tests require no original assets. Numeric
exporter tests explicitly skip when NumPy is unavailable; the C loader and
FSYS/CCD tests still run. The reviewed private seven-pack set also passed
ASan/UBSan loading and sampling halfway between every pair of stored states,
with no singular transforms or invalid material/camera values.

This exporter does not implement the general game VM, NPC system, dialogue,
battles or interactions. Only the map, Wes and the original arrival actors are
included. Source particle generators and type40 callbacks remain listed in
the manifest but are not drawn. Field music and separately triggered movie
BGM/SFX require their own audio path. The three Wes motion clips do not by
themselves reproduce the original general animation transition/blend system.

The source curves are sampled and rendered through the native PICA material
implementation, so this is not a pixel-identical GX execution. In particular,
the separate specular pass can differ in clamp order at filtered fractional
alpha edges of the bike's otherwise binary-alpha texture. Host format checks
do not establish real-console performance, sustained SD bandwidth or complete
gameplay behavior. Those require native execution and further engine work.
