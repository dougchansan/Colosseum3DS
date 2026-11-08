# Reflect source preparation and native boundary

This prepares the original Reflect model, particles, owner attachment tracks and
scalar timeline. Native stage integration and rendered acceptance are separate.
No actor mesh/world bundle is regenerated.

```powershell
python tools/prepare_wzx_reflect.py --disc G:/gamecube-decomp/disc-extract-dump --output private/reflect-sequence-v5
```

The output directory must be new. Original DAT and all prepared resources stay
private. The pinned source contract is
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

## Source schedule

Move115 selects resource kind3, variant0, group`0x3DE`, member`0x07632000` in
`wzx_reflector_special.fsys`. Archive SHA-256:
`9dee2c130b67525fe0074f1f1cb3c97325a040d45b57e2f7e862b7fc9cabc753`.
Member SHA-256:
`28b205cba86a2ce4b5e81f1a9224f0593ce033220773284f3efabcf885a570ac`.

| Entry offset | Kind | Binding | Local start |
| --- | --- | --- | ---: |
| `0xA0` | Particle | GPT1 root0, flags`0x18A9`, format4 | 0 |
| `0x16FE0` | Sound | Game sound`0x220` hexadecimal(544 decimal) | 0 |
| `0x1705C` | Effect | Original floor-color program | 0 |
| `0x17118` | Model | HSD DAT at`0x171C0`, 26647bytes | 0 |

Every link selects point0, the implicit zero in the raw owner table. Espeon
PKX entry0 at metadata`0x380E0` starts`[2,1,0,0,180,...]`; runtime timing is
`[0,180]`. Umbreon's equivalent idle timing is`[0,118]`. Normalization shift is0.
The preparer checks DOL`0x801DB718=0x801E000C` and
`0x801DB720=0x801E0010`: resource-kind3/point0 time queries read runtime+0x0C
and+0x10, including the implicit zero.

Source floor RGB interpolates128 to45 over20ticks, holds45 for110ticks, then
returns to128 over60ticks:190source60Hz ticks total. Source float interpolation
then byte truncation is retained.

## Texture-only model

The model selects joint animation-1 and texture animation0. Its HSD contains
no joint animation trees, one material timeline, three joints and one mesh.
Texture animation0 ends at source frame95. Existing geometry/title conversion
exports static rest matrices plus that material timeline as native clip0:
191samples at120Hz, sampled with model rate0.5 for190source ticks. No skeletal
animation was invented. First/last matrices are identical while material bytes
change.

The C3T contains1536vertices, one batch and one128x128RGBA texture
(65536decoded bytes); total176344bytes. Model SHA-256:
`58dac5ec1dff0f46699a2f3ceb32eb15be3f5657935f76605dd37f3451332bdc`;
SHA-1:`b89d99a306a3a3df35317b0ea3fa36d68d574162`.
GPT1 closure0..9 prepared pack SHA-1:
`d9baa99fe36aa3e7942ddda41b8b447c79e68405`.

## Live source-part attachment

`fn_801D97F0` maps attachment selector2 to owner body-map index2 at table+0x54.
This selects original part62 for Espeon and42 for Umbreon. These are not compact
native bone indices. Both model and particle nodes request position type1.

Particle start calls`fn_80118FB0` state1, one-shot flag0 and initial reset1.
`fn_801183EC` therefore updates the selected source-part position each step.
Flags`0x18A9` bit1 is0: child generators are not directly linked to the JObj.
The model also continuously follows that source-part position, retaining the
owner rotation/scale established at start.

C3WR v2 is288bytes and rejects the prior256byte descriptor. It binds the separate
`reflect_parts.c3r` C3RP v1 sidecar by SHA-1
`75857a091091625e88484d7e30a3e8ea276f91d8`. This3728byte sidecar contains a
checked128byte header plus original idle JObj position samples: Espeon part62,
181samples, and Umbreon part42,119samples, at120Hz in native0.10units. Original
archive/member hashes and the pinned importer are checked. Actor geometry is
not rebuilt.

C3WR v2 descriptor SHA-256:
`d7540d79983505e3b0d342352d5d9b19fe8d620f25ab1f2a4256e5b8ff2fc1c8`.
Model and particle hashes remain unchanged from the earlier descriptor.

Owner rate0.5 is DOL-verified: `0x801D9D34=0xC022ACA8` loads
`lbl_8047E348=0.5`, then`0x801D9D3C=0x4BF12CA1` calls
`GSmodelSetAnimRate` at`0x800EC9DC`. The idle branch also calls`fn_801DF070`:
`0x801DF124=0xC022AD28` loads`lbl_8047E3C8=0.5`, followed by
`0x801DF12C=0x4BF0D8B1` calling the same rate setter. The preparer checks both
paths. Sample the actor idle and attachment with the same authored clock.

## Ally order and completion

`fightWazaWzxTypeFuncAllAlly`(0x80210888) loads caster and ally kind3 sequences.
With a valid ally it invokes the caster with wait1;`fightWazaDoEffect` waits
until that sequence finishes and frees it. Only then does it start the ally
with wait0. This encounter therefore presents Espeon actor1 followed by Umbreon
actor0, mask3 when both are alive. An absent ally leaves only the caster.
Two sequential instances need one simultaneous particle system. Existing
Reflect root0 regression peak1369 fits the current1536particle bound.

Owner minimum180ticks(Espeon) or118ticks(Umbreon) is insufficient. Texture and
floor run190ticks; surviving particles must finish too. Sound entry update
waits while`fn_801666BC` reports status2/3. The selected dry offline sound lasts
3.6214375seconds, longer than the floor/model. Completion must include actual
audio done. Source camera execution remains unsupported.

Each instance needs a distinct increasing audio cue identity. Reusing the same
(sequence,soundID) pair would intentionally suppress the ally's second cue.

## API and ownership

Zero-initialize`C3WzxReflectProgram` before descriptor decode/load, then call
`c3_wzx_reflect_parts_load(program,path,error,capacity)`. The program owns its
immutable sidecar copy. Clock states borrow it: cancel clocks before calling
`c3_wzx_reflect_program_close`. Failed replacement preserves existing storage;
successful replacement releases it. Repeated close is safe.

`c3_wzx_reflect_position(program,species,source_idle_seconds,rootXYZ,yaw,scale,
outXYZ,error,capacity)` interpolates original local part position, then applies
current actor translation, yaw and uniform scale. Its clock matches actor idle
sampling: elapsed seconds times0.5, not wall time. Looping uses frame_count-1
exactly like`c3_title_animate`; negative time clamps0. Unknown species, missing
tracks, nonfinite transforms, zero scale, source-pin/checksum/bounds violations
fail explicitly and preserve output.

`begin(state,program,species)` supports internal species196/197, starts all
entries immediately at tick0, and exposes correct idle clip/source part/minimum.
`step(ms)` preserves fractional60Hz units and exactly-once counters; paused
callers omit it. Beyond60seconds fails without consuming elapsed time.
`finish` requires model, particles, floor and audio done plus internal minimums.
`c3_wzx_reflect_actor(caster,alive_mask,index)` supplies sequential target order.
The scalar clock makes no combat RNG calls.

Five synthetic preparer tests cover implicit-zero scheduling, exact sound ID,
forbidden profiles, material-only animation and isolated source-part export.
Strict and ASan/UBSan actual-resource runs each pass185767checks: clocks,
partitions, independent actors, interpolation, loop/root/yaw/scale math,
alias-safe replacement, every-byte corruption, malformed bounds and cleanup.
These qualify preparation/core behavior, not native rendered Reflect acceptance.
