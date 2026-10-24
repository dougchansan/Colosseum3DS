# Common stat-down preparation contracts

By default this tool prepares the original GREEN common Defense-down special
`0x29`. Explicit `--attack-down` selects original RED Attack-down special `0x27`.
These special IDs are independent of move IDs and caster WZX roots.
Native clock, particle, camera, audio and stage verification are separate.
Original and converted resources remain private; no actor/arena is regenerated.

The source contract is `51bfe4314a218dfaf9592fc80723c50498d0ee55` with exact
files in `config/wzx-stat-down-source-files.json`. DOL special table
`0x803727C8 + 0x29*12` resolves group `0x49C`, member `0x0B6F2000` from
`wzx_greendown_status.fsys`:

| Resource | Bytes | SHA-256 |
| --- | ---: | --- |
| Archive | 1,376 | `8947e10d57f4d17f426debe1ddb713c8856cf889f3d7cf6c5a8ced0e1b602dd8` |
| Decompressed WZX | 2,264 | `5e08695ee116a4a5aaded4deef4fd1af71e5f4178992e187610a7e92a63e7c1e` |
| GPT1 at `0x120` | 1,644 | `7f42a1c5a723ed7563e290ede2feb2b1828f90498553feaea96ec1e650bdefeb` |

The root header is `[0,4,0,2,5,0]`. The bank ends at `0x78C`; twenty zero
bytes align the next node. Source order is particle / sound / actor color:

| Key / offset | Source payload | Binding |
| --- | --- | --- |
| 1 / `0xA0` | `[0,0xA1,1644,4]` | Root 0, closure 0/1/2; root/body-map 0 position-only, one-shot (node flags 2). |
| 2 / `0x7A0` | `[0x19B,3,0]` | SE `0x19B`; selector 3 reads runtime flags from word 2, so flags are zero and playback is direct SE. |
| 3 / `0x81C` | Type 0, duration 50, target 2 | Actor-model color, not arena-floor color. |

All starts and normalization are zero; original common timing words are zero.
The color program has 10/10/30-tick transitions, neutral `0x80808080` to
`0x80437A3D`, hold, return. Its byte order yields RGB `(61,122,67)`; it is not
high-byte red 67/blue 61. Source `fn_8013B268` combines each program component
with the previous actor color as clamped `previous - 128 + program`.
Capture and restore prior actor modulation on stop/cancel.

## Target and completion semantics

Only actual successful Defense stage decreases trigger this profile. Direct
stat handling calls `fn_8021B910`; at minimum stored stage zero it returns
handled and the caller never enters the common effect script. Mist, ability,
reflection and failure paths can also suppress or redirect it. The successful
path sets the current defender as focus field `0x47`; common script
`0x803763CE` selects that focus via target selector `0x16` and starts special
`0x29`. Preserve committed STATUS order and negative stage deltas; do not
parse English messages, rerun combat or advance combat RNG.

The general outer target enumeration/sort and multiple-current global waiter
overlap are not fully qualified by this preparer. The source global wait
helper's first-eligible-current behavior must not become an invented wait-all
rule. Only the two admitted player target species are supported. RED requires its
explicit profile described below; other specials and variants fail closed.

The two admitted targets are Umbreon entry 0/source clip 0/end 59 and Espeon
entry 0/source clip 1/end 90. Their timing gates are 118/180 source ticks,
root parts 0 and camera parts 42/62. Both original first subanimation kinds
are zero. The loader's reserved prefix and normalization zero make motion
start take `fn_801DF070`, explicitly setting source frame zero/rate 0.5.
This does not preserve an arbitrary prior idle phase. Original root-part
frame-zero matrices are checked identity, with native translation `(0,0,0)`.
Original HSD/PKX SHA-1s are resource provenance, not C3T file hashes.

`fn_801DA9E8` clears owner bytes `0x16/0x17` before sequence start, disabling
the early owner-enabled return for this admitted uninterrupted path. Normal
completion waits for the owner minimum, color program, particle family and
direct sound to finish. There is no fixed 50-tick total, and 118/180 is not
an unconditional terminal timeout when resources remain pending. Source SE
running states are 2/3; these payloads do not start a GSthread. Natural finish
and explicit STOPPED/cancel status must remain distinct.

Full teardown restores the prior actor color/camera, kills the owned particle
family and sends SE keyoff. Original DSP release tail and scheduler phase are
not qualified as instantaneous PCM silence. Selected audio is prepared
separately. Root flags zero/body-map 2/no external camera payload require a
separate special-effect camera profile, not a move-ID alias.

## Particle conversion boundary

Generators 1/2 use source shape 7, not the existing shape-8 tornado. Opcode
`0x8F` adds position XYZ `(0,22,0)` through source offset-local rotation; it
does not set position or velocity. Mode `0xA1` has offset-transform bit `0x8`
clear, so the source helper returns without rotating or scaling this offset. The new preparer checks the exact original bank,
recognizes 8F at its two admitted source offsets, and reuses checked decoding
for other instructions. Only the inspection view skips this already decoded
instruction; the exported script and raw bank are byte-identical.

Three textures are converted: one 32x32 I8 and two 8x8 I4. Existing intensity
RGBA conversion includes intensity alpha. No AppSRT, sparse large bank,
new HSD model or red-bank admission is introduced by this tool.

## Private output and format

```powershell
python tools/prepare_wzx_stat_down.py `
  --disc G:/gamecube-decomp/disc-extract-dump `
  --parser vendor/hsd-importer `
  --output C:/Colosseum3DS-pr7-captures/stat-down-preparation-v2
```

Outputs are `stat_down.c3w`, `stat_down.c3p`, unchanged `stat_down.gpt` and
`stat_down.json`. C3PT uses the established format and source bindings.

`C3SG` version 1 is 1,024 little-endian bytes. Its 256-byte header identifies
special 0x29/kind 4/stat 2, two owners and three nodes. It binds commit at 32,
WZX SHA-256 at 52, bank SHA-256 at 84 and C3PT SHA-1 at 116. SE/root/raw flags/
tick rate/color duration/target are at 136..159; class scale/model rate at
160/164. Effect type/common script/node table/owner table/bank length/selector/
position/node flags/particle mode occupy 168..203. Bytes 204..235 are zero.
Whole-file SHA-1 at 236..255 is computed with that field zero; this is an
integrity check, not a signature.

Two 96-byte owner records at 256 hold species, actor slot, entry, source clip,
authored end, gate, root part and camera part (eight uint32 fields), extracted
HSD SHA-1 at +32, whole PKX SHA-1 at +52, three native root translation floats
at +72 and signed selector -1 at +84. Remaining bytes are zero.

Three 192-byte node records start at 448. Each contains twelve uint32 fields
(key, kind, start, attachment, position, link, source, target, timing index,
flags, part, state), sixteen raw timing words, nineteen zero-padded payload
words and one reserved zero. Original PS/SE/color order is retained. Synthetic
fixtures reject red headers, changed links/payloads, bad owner bindings, hash
lengths, unknown/truncated ADDXYZ and extent drift. Optional private tests
verify complete touched texture groups, checksum bindings and unchanged raw
bank bytes. Native execution and retail visual acceptance remain separate.

## Native stage admission

The optional descriptor connects only successful typed Tail Whip Defense
changes on Umbreon and Espeon. It compares committed stage snapshots and
preserves their order; unchanged/minimum-stage outcomes do not start a cue or
emitter. The existing particle pool is reused, with a sixth registered bank
and three small textures. No extra actor or effect model is loaded.

Source root part0 is checked in the original HSD by the preparer. Compact
native bone0 is a different namespace and is never used as this attachment.
Original checked root translation is transformed into world space once;
particle source coordinates convert back to native units once at rendering.
The actor-color path snapshots both RGB and enabled state, restores them after
50 ticks, and preserves them on cancellation. Source idle loops use periods
118/180 rather than retaining the duplicated C3T endpoint sample.

Completion requires the source owner gate, natural particle completion and
actual selected sound FINISHED. Whole-family release, color/camera restoration
and exact-cue keyoff occur once. A pause-hidden cleanup acknowledgment can wait
up to ten seconds of active time; background idle continues, while paused time
does not advance either clock. STOPPED before natural completion is an error.

Private actual-resource stage fixtures cover both owners, reversed committed
target order, suppressed outcomes, color restoration, source idle continuation,
partitioned time, audio/error/pause cleanup and GPU-busy cancellation. These
checks do not establish physical rendering, original target sorting, shared
RNG, original sound release-tail fidelity or complete Tail Whip/Growl coverage.
This GREEN checkpoint does not qualify RED Attack-down special0x27.

## Physical green Defense-down checkpoint

The [2026-10-23 normal-source 5b checkpoint](evidence/stat-down-hardware-2026-10-23/README.md)
shows Tail Whip B followed by green/cyan streaks and green actor modulation on
Umbreon and Espeon, then visible return to each actor's normal color. It also
records Growl A, WIN, field return, reentry/cancel and HBL/FTPD exit, with renderer
recovery overlays disclosed. The independent trace audit records target 0/1 completion at 118000/180000 units,
each 369 emitted/232 peak particles and 1772 cosmetic draws, audio FINISHED and
color/camera restoration. Both preview guards/save bytes are unchanged.
Modulo-idle continuity is source/host-qualified rather than directly
measured per-actor hardware clocks. Original Defense-down comparison is 84/revise
(category match), with camera/phase/RNG/aspect mismatch; there is no verified
original Growl reference, audible proof or retail-performance acceptance. This GREEN evidence does not qualify RED Attack-down or the seven remaining
move presentations.


## Explicit RED Attack-down preparation

```
python tools/prepare_wzx_stat_down.py --disc G:/gamecube-decomp/disc-extract-dump --parser vendor/hsd-importer --output C:/stat-down-red-preparation-v1 --attack-down
```

This opt-in produces `attack_down.c3w`, `attack_down.c3p`, `attack_down.gpt`
and `attack_down.json`. Default GREEN binary packs and the current corrected
GREEN report remain byte-identical. RED native stage, camera, particle and
audio qualifications belong to their separate implementation lanes; this
preparation does not establish playback or hardware acceptance.

DOL special table `0x803727C8 + 0x27*12` resolves group `0x49B`, member
`0x0B6E2000`, `wzx_reddown_status.fsys`. Exact source proofs are in the
`attack_down` section of `config/wzx-stat-down-source-files.json`.

| Resource | Bytes | SHA-256 |
| --- | ---: | --- |
| Archive | 1,376 | `a05747fe283ce15e197cef32c61a1394de84ae66e5293fcf5bc2201095b1399c` |
| Original WZX | 2,264 | `0e0103bdcf6f2dca317e8ae86cfbc5294de2bcd2d7f1af4bf42c04a01bdf08de` |
| Original GPT1 | 1,644 | `9b54c2d13b9c50949ca13d7fec95ba1d49effb8268490ac1a65d4b04dcd27d97` |

RED root header `[0,4,0x78,2,5,0]` and original node order differ from GREEN:

| Key / offset | Source payload | Binding |
| --- | --- | --- |
| 1 / `0xA0` | `[0,1,1644,4]` | Root0, original part0, POSITION mode1, flags0: live attachment. |
| 2 / `0x7A0` | Color program | Actor-only RGB modulation; duration50, segments10/10/30. |
| 3 / `0x85C` | `[0x19A,3,0]` | Independent direct SE `0x19A`, runtime flags0, link0. |

All starts and normalization are zero. Actor color word `0x80454681` yields
low-byte RGB `(129,70,69)`; preserve the prior color and use the same clamped
`previous - 128 + program` merge and restoration. Closure0/1/2 and all three
textures are preserved in the original bank. Shapes0/7/7 reuse the source
shape7 equations, with RED parameters/colors checked by the exact bank hash.
The `8F` offset remains `(0,22,0)` with source particle kind bit8 clear;
inspection metadata records that it is not locally rotated or height-scaled.
No HSD model or actor geometry is generated.

The preparer checks original part0 matrices over every half-frame idle sample:
119 samples for Umbreon and181 for Espeon, including each endpoint. All are
identity within `1e-7`; the report includes measured maximum errors. This is
original part0, never compact C3T bone0. Current player world roots are also
constant. Moving world roots and the breadth/order of live generator followers
remain unqualified and must be refused until separately proven. The live mode
must not be generalized from GREEN's one-shot flags2/modeA1.

The common script address remains `0x803763CE`: successful source stat-down
cases22..28 and46..52 call it after `fn_8021B910`. `fn_80222110` maps down kinds
`0x16`/`0x2E`, stat1/4 to special27, while stat2/5/7 selects special29.
Minimum-stage/failure suppression and committed target order remain required;
Growl's announcement alone does not authorize a target effect.

C3SG stays version1/1024 bytes. RED has special27 at12, stat1 at20,
sequence flags78 at144, particle attachment flags0 at196 and particle mode1
at200. Node records preserve PS/color/SE order. Member/bank/C3PT hashes and the
whole-descriptor SHA1 cover the selected profile; cross-profile headers,
nodes, payloads, altered source pins and bank bytes fail closed. Owner gates
118/180, source frame0/rate.5, resource completion and explicit cleanup remain
separate from the50-tick color program. No fixed50/118/180 terminal is inferred.
