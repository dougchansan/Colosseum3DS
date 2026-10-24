# Encounter 10 private asset preparation

`tools/prepare_battle_scene.py` extends the existing pinned HSD conversion
path (`prepare_field_geometry.Model`, `build_mesh`, `prepare_pack_set`).
It emits private C3T3 geometry, authored joint/material samples, and a JSON
provenance report. It does not establish a complete battle presentation.

```powershell
python tools/prepare_battle_scene.py --disc <verified-extraction> `
  --common-rel <canonical-common_rel.rel> --parser <pinned-hsd-importer> `
  --output private/battle-scene
```

The separate common REL argument supports the established private extraction
overlay: the original disc's common REL is inside `common.fsys`. Both inputs
are hash admitted. Existing output paths are refused. Original assets and
prepared packs must remain private.

## Source bindings

The source pin is `51bfe4314a218dfaf9592fc80723c50498d0ee55`.
Encounter record 10 at common REL data `0x109FBC + 10*56` selects fight-floor
record 5. Its floor ID 200 resolves common floor row 148, at
`0x8BCF4 + 148*0x4C`. The floor's resource group `0x569` resolves through
`GSfsys.toc` to `S1_out_bf.fsys`. Its reviewed scene member is `0x0DA80200`.
The arena includes three models, one camera, five lights, and no fog.
The battle-floor record's introduction WZX, BGM, and environmental sound
IDs are all zero. Encounter record byte12 supplies the nonzero music override
`0x447`, selected first by `fightEncountGetBgmSndDataId` (0x8020DD44).
The encounter wipe ID is9. This scene exporter does not decode audio. Separate
[battle audio preparation](native-battle-audio.md) now supplies selected Bite,
Reflect and Confusion effect cues; encounter music and a full MusyX runtime
remain separate work.

Species records use internal IDs. Zigzagoon is 288 here; its National Pokédex
number is not a valid substitute. `PokemonData.pkxDataId` at byte `0x2C`
selects a twelve-byte DOL resource record at `0x36DBD0` (RAM `0x80370BD0`,
symbol `lbl_80370BD0`). Each record contains archive group, member, and name ID.
The archive name comes from the original TOC, not a romanized-name heuristic.

| Actor | Internal species/PKX ID | Group | Member | Output |
| --- | --- | --- | --- | --- |
| Umbreon | 197 / 197 | `0xA3` | `0x03381E00` | `pokemon197_0.c3t` |
| Espeon | 196 / 196 | `0x110` | `0x03B71E00` | `pokemon196_0.c3t` |
| Zigzagoon, shared twice | 288 / 288 | `0x22E` | `0x04C51E00` | `pokemon288_0.c3t` |

The report preserves all 17 original PKX animation entries, sub-animation
indices, timing fields, body-part maps, and particle callbacks. Joint clip
indices remain unchanged in the packs. PKX entry zero selects idle: Umbreon
clip 0, Espeon clip 1, Zigzagoon clip 0. Other entry semantics and move-effect
dispatch must be qualified against the source before runtime role binding.
Partial blinking and arbitrary PKX particle callbacks are not implied by
preserving their records. Selected WZX GPT1 playback now comes through the
separate [bounded particle path](native-battle-particles.md), not through this
model export alone. The two Zigzagoon runtime instances must own independent poses and
clocks while sharing the one prepared asset.

Trainer record byte0x10 selects sequence IDs1 (Wes) and38 (Willie).
`fightTrainerCreateSequence` calls `fn_801DE418` with this field. Its resource
table is at DOL file0x36D840 (RAM `lbl_80370840`), stride12. Wes resolves to
group0x9D/member0x030C1E00 (`pkx_ken_a1.fsys`); Willie resolves to
group0x2DB/member0x058A1E00 (`pkx_rider_m_a1.fsys`). Trainer byte6 is AI data
and must not be used as a model index. Both trainer idle bindings select clip1.

All arena meshes are retained, partitioned by existing native bounds. The
long sky animation uses 1001 samples across its full authored duration.
Other clips use half-frame samples at 120 Hz. The report's camera is the
arena's authored camera, not the original battle camera sequence.

The shared envelope sampler now admits non-root owners. The parser's constant
right-side envelope coordinate matrix cancels in
`D(t) * inverse(D(rest))`; multi-weight inverse binds remain in the blend.
Tiny uniform scales use a relative invertibility check. Exactly all-axis-zero
rigid samples make their batch invisible and carry an unused invertible
normal matrix. Collapsed individual axes remain explicit errors.

## Verified preparation and remaining gates

Local canonical preparation generated all eight packs, with full geometry:

| Pack | Vertices | Batches | Sampled matrices | Bytes |
| --- | ---: | ---: | ---: | ---: |
| arena0_0 | 25,296 | 46 | 2 | 12,294,400 |
| arena1_0 | 9,600 | 1 | 1 | 1,510,168 |
| arena2_0 | 7,344 | 31 | 2 | 4,135,024 |
| pokemon197_0 | 2,346 | 15 | 28 | 3,932,104 |
| pokemon196_0 | 2,220 | 19 | 48 | 3,997,504 |
| pokemon288_0 | 2,679 | 21 | 28 | 5,247,600 |
| trainer1_0 | 5,466 | 24 | 59 | 5,675,168 |
| trainer10_0 | 4,245 | 9 | 27 | 1,944,184 |

These are disk pack sizes, not measured 3DS memory usage. Hashes of archives,
decompressed members, DATs, and output packs are in private `scene.json`.
Ten synthetic regressions cover TOC bounds/names, clip index validation,
non-root envelope cancellation, and tiny versus collapsed transforms.

`scene.c3v` uses the bounded C3BV version1 little-endian layout, with
three arena records and six actor records. Zigzagoon actor3 shares actor2
geometry and retains independent runtime animation state. `battleGridUpdate`
and `fightSideGetFightTrainerGridParam` derive the original positions and
yaw from floor5 side IDs2/1. In converted units: Wes(-5.6875,0,0),
Willie(5.6875,0,0), Umbreon(-3.5,0,-2.625), Espeon(-3.5,0,2.625),
Zigzagoon0(3.5,0,2.625), Zigzagoon1(3.5,0,-2.625); player yaw+pi/2,
opponent yaw-pi/2, scale1. Sequence kind-1 gives the Pokémon distance factor
0.875. The descriptor now resolves the source PKX entries below to each actor's
authored joint clip. Entry numbers and clip numbers are distinct.

The generated scene packs alone do not qualify original attacks, send-outs,
support effects or hardware rendering. Separate bounded preparation/runtime
paths now execute Bite, Reflect and Confusion model/particle/effect/audio
dependencies and selected Bite/Confusion procedural cameras. Their resource
contracts and acceptance limits remain distinct from this base scene export.
Original HUD/font resources, battle shadows, complete ball/light send-outs,
trainer commands, encounter music and general WZX dispatch remain unsupported
or incomplete. The remaining nine admitted move presentations are not covered
by the three implemented slices; see [the coverage table](battle-presentation-coverage.md).

## Optional matched presentation packs

Install optional effects alongside `scene.c3v` in the private `boot/battle`
directory, preserving their generated names and preparation manifests. Retain
existing field/title/scene assets. Present invalid dependencies fail admission;
an absent optional effect means explicitly incomplete actor-only presentation,
not successful original effect execution.

| Slice | Separate dependencies and contract |
| --- | --- |
| Bite | Attack/damage model and timeline packs, damage GPT1, selected camera and cues `0x85/0x86`; [Bite contract](native-wzx-bite.md) |
| Reflect | Model/timeline, source-part tracks, GPT1 and cue `0x220`; [Reflect contract](native-wzx-reflect.md) |
| Confusion | `confusion_sequence.c3s` (`C3CS`, all 15 nodes/source-part tracks), `confusion_type12.c3f` (`C3CF`, type12 profiles/I8 mask), two GPT1 packs, `confusion_camera.c3c` (`C3CC`, move-93 camera/FOV/track delay), and four additional cues; [Confusion contract](native-wzx-confusion.md) |

For Confusion, prepare into new private directories:

```powershell
python tools/prepare_wzx_confusion.py --disc $disc --parser $parser --output $confusion
python tools/prepare_battle_camera.py --disc $disc --parser $parser --confusion --output $confusionCamera
python tools/prepare_battle_audio.py --disc $disc --confusion --output $audio
```

The complete audio pack contains seven selected game requests across all three
slices. Its dry offline rendering and SRC/vibrato/sweep/mixer/reverb limitations
remain explicit. C3CS/C3CF checksums bind their matched particle/mask contracts;
C3CC retains independent source camera tables rather than treating the authored
arena camera as the battle sequence. The native capture adapts source 640×480 to
512×256, so prepared descriptors and successful draws do not establish original
GX/PICA raster/filtering parity or performance acceptance.

Build `749708b3bd33` has a [bounded physical Bite/Reflect checkpoint](evidence/reflect-bite-hardware-2026-10-23/README.md).
Its Confusion presentation was the incomplete fallback. That checkpoint does
not qualify the current Confusion implementation or later corrections. Current
source/host/emulator coverage must not be relabeled as fresh physical acceptance.


## Qualified pose dispatch and integrity manifest

`battle-scene-manifest.json` records source archive/member/output SHA-256
identities, bounded runtime records and the twelve per-move pose selections.
The adjacent `.sha256` file is an integrity digest, not a digital signature.
No game asset bytes are embedded in either file.

For version2 WZX, `fn_801DC46C` selects member+0x70; `fn_801DC5F0`
reads its u32 motion kind. `wazaSequenceStart`/`fn_801DD100` stores the
kind in the owner, then `PokemonMotionStart` uses it as a PKX entry index.
The selected original roots verify the following attacker entries:

| PKX entry | Moves |
| --- | --- |
| 2 | Bite44, Return216, Tackle33, Struggle165 |
| 1 | Secret Power290, Snatch289, Confusion93, Helping Hand270, Growl45 |
| 0 | Taunt269, Tail Whip39, Reflect115 |

Damage roots for Bite, Secret Power, Confusion, Return, Tackle and Struggle
select entry8. All twelve ball types' down-in sequences select entry10 for
fainting; open/out sequences select entry0 for the send-out pose. This is the
source pose binding and does not implement the ball/light/particle sequence.
Ball records are at DOL0x364AF0, stride48; in/open/out/down-in/throw resource
references are at +4/+8/+12/+16/+20. Trainer outcomes use special WZX IDs
0x59(win) and0x5A(loss), selected through DOL0x36F7C8, stride12; their kinds
select trainer PKX entries5/10. Both trainer packs resolve these to clips9/6.
Trainer command remains unsupported.

No default-variant override matches exist for encounter PKX IDs196/197/288
among the twelve admitted moves in the original `lbl_80373750` ranges for
kinds1/2/3. Nonzero variants remain outside this qualification. The per-move
archive/member hashes and parsed root kinds appear in the integrity manifest.
These pose mappings alone do not qualify original WZX effects, particles, camera
sequences, support visuals or audio playback. The separate bounded Bite,
Reflect and Confusion implementations qualify only their documented resource
and execution slices; the other moves and full retail presentation remain open.
