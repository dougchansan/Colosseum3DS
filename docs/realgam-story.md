# Realgam Tower, ending, credits and clear save

Status, 2026-11-16: **qualified subsets; chronological chapter acceptance remains open**.
All 29 owned floor identities have original-asset Azahar evidence through the
appropriate field, battle-stage or special-scene path. The final-floor original
script reaches the ending and full credits, writes a copied clear save, and a
fresh process loads that written checkpoint and returns control. This final
run explicitly warps the latest supplied uncleared save onto floor `0x76` and skips
six battles as wins. It does not prove travel from Agate through the final chapter.

## Floors, profiles and doors

`tools/realgam_profiles.py` makes all identities permanent, without requiring a
private survey. The 19 walkable floors use `f%03x` map, actor, collision, story
and camera packs. Each pack is derived from admitted original resources.

| Floor | Original archive | Entrances | People | Local messages |
| --- | --- | ---: | ---: | ---: |
| `0x66` | `D4_out.fsys` | 1 | 3 | 10 |
| `0x67` | `D4_dome_1.fsys` | 4 | 1 | 0 |
| `0x68` | `D4_dome_2.fsys` | 1 | 1 | 5 |
| `0x69` | `D4_dome_3.fsys` | 2 | 1 | 0 |
| `0x6A` | `D4_dome_4.fsys` | 1 | 1 | 5 |
| `0x6B` | `D4_dome_5.fsys` | 3 | 2 | 6 |
| `0x6C` | `D4_dome_6.fsys` | 2 | 1 | 0 |
| `0x6D` | `D4_dome_7.fsys` | 3 | 2 | 6 |
| `0x6E` | `D4_dome_8.fsys` | 1 | 1 | 5 |
| `0x6F` | `D4_dome_9.fsys` | 3 | 1 | 7 |
| `0x70` | `D4_dome_10.fsys` | 2 | 1 | 0 |
| `0x71` | `D4_dome_11.fsys` | 1 | 1 | 10 |
| `0x72` | `D4_tower_1F_1.fsys` | 4 | 5 | 15 |
| `0x73` | `D4_tower_1F_2.fsys` | 1 | 6 | 20 |
| `0x74` | `D4_tower_1F_3.fsys` | 1 | 1 | 4 |
| `0x75` | `D4_tower_2F.fsys` | 4 | 2 | 23 |
| `0x76` | `D4_casino_colo.fsys` | 1 | 15 | 35 |
| `0x7B` | `D4_out_2.fsys` | 2 | 2 | 4 |
| `0x7C` | `D4_tower_2F_2.fsys` | 2 | 1 | 1 |


The 29 common door rows map to original object resources and clips. Thirteen
C3D packs add those bindings to the existing objects. Door163 intentionally has
no opening animation: its source value -1 rests at frame0, and native `60` keeps
the source missing-animation result. Elevator travel animations remain unrendered.

T1 `0x7E` has one entrance/collision but no field lifecycle scripts or people
table; the seven `0xDD`?`0xE3` battle fields have no walk entrances/collision.
Their original battle-stage resources are loaded separately, without inventing
walkable geometry, NPCs or doors.

| Story floor | Archive | Battle-floor index |
| --- | --- | --- |
| DD | D4_dome_2_bf | 27 |
| DE | D4_tower_1F_1_bf | 28 |
| DF | D4_tower_1F_2_bf | 29 |
| E0 | D4_tower_1F_3_bf | 30 |
| E1 | D4_tower_2F_bf | 31 |
| E2 | D4_tower_2F_2_bf | 32 |
| E3 | D4_casino_colo_bf | 38 and47 |
| 7E | T1_ancient_colo | 48 |

`private/evidence-stages/boot-trace.log` records **requested 9, passed 9** on
Azahar. Every stage draws at least 60 frames/two seconds, with original camera,
clips and placement. This tests arena resources, without battle actors or combat.
Battle31/47/48 were absent from the original SD index; only those three index
rows were added, preserving every model/species/trainer/other-floor byte.
Their original index and source reports are retained privately.

## Ending and clear save

Pinned source: `vendor/colosseum/src/game/movie.c`, `fn_800361C0`,
`fn_80035F64` and `fn_80035F34`. `src/ending.c` preserves the previous-floor `0x76`
and flag476==1 guards; it never grants that flag. It prepares478 and checkpoint
(2,1,1), preserves the original444C/default-Yes and3C02/default-No prompts,
and writes a deep snapshot so saving does not advance live counters or RNG.
The live return is floor `2`/previous `0x39F`/entrance1. Its original `05960008` half-second
fade runs in the destination VM; Continue's persisted previous `1` excludes it.

`tools/prepare_staffroll.py` pins the original disc, THP hash and common messages.
The complete5,250-frame stream is806,400,064 bytes. The bounded player reads one
frame at a time; the admitted file bound is1 GiB. The THP is silent and the scene
requests original BGM `0x4C9`; the existing music index carries `w71_s69.c3m`
(6267124 bytes). DSP playback was not independently captured.

The final sequence needs natives `70`/`71` transient people and A6 root attachments.
`tools/prepare_realgam_dynamic.py` reserves five hidden original models in C3A
metadata:115A0400 and115E0400?11610400. The last four are animated joint
carriers with no mesh. Transform-only C3T support retains their authored joints,
with no placeholder triangles or textures. Mixed-zero geometry packs fail.
A6 follows original root0, including rotations beyond the vehicle-part subset.
NativeDD particle attachment is explicitly logged as unrendered.

Production host closure using actual model and camera packs reaches39F:
`private/final-dynamic-report.txt`, `private/final-dynamic-trace.log`.
The runner's36000-frame row is a recorded connector, not a chronological route.

### Azahar final-floor run

`private/evidence-final/boot-trace.log` (`realgam-fin04`):

- Copy of `pokemon-colosseum.3641.gci`, uncleared, placed on76/previous75.
- Encounters431,433,435,437,439,441 explicitly report skip-as-win2, with no
  battle party/EXP writeback. The original script writes476=1 afterwards.
- Original115A and115E dynamic models open; source scripts request39F.
- Full credits finish naturally:175177076 microseconds,4891 displayed frames,
 359 timing-dropped frames. Both original save prompts execute; save succeeds.
- The returned Outskirt shop yields control. Thirty-three reached dialogue
  message/page snapshots are retained on the lane SD under `final-probe/`.

`private/evidence-final-continue/boot-trace.log` is a **fresh Azahar process**
loading that written copy unchanged: floor `2`/previous `1`/entrance1, clear flags1,
save count 50, control and frame 90 capture. This bypasses title-menu input;
it verifies the production save decoder and field reconstruction, not a manual
title-menu interaction. `private/evidence-return/` separately verifies the
original `05960008` override, without writing a save.

The earlier completed-save guard fixture is retained in `private/evidence-ending/`
(count 37), followed by `private/evidence-continue/`. It is not final-story proof.
Only copied saves were written. The supplied15 originals remain unchanged.

## DNA-key special screen, floor `0x387`

Source: typed `fn_80087C64` and CardGrid helpers in
`vendor/colosseum/src/game/menu/cardesavedata.c`; wrapper `fn_80088428`.
Lifecycle row190 runs common `05960014`/native `CE`. There are no NPCs, dialogue,
CCD collision or walk entrances. `DNA_key.fsys` contains model107E0200 and
intro/exit cameras107C1800/107D1800.

The port retains all 12 models and every source tile/cursor/counter clip.
`src/dna_key.c` implements the original grid, unique choices, undo/cancel,
wrong-trio sound/reset and success. The scene directly ports the CE sequence;
it does not claim to execute05960014 in the field VM. Direction controls use
edges rather than held-button repeat. Sound requests use the existing service;
missing sound assets are silent.

`private/evidence-dna-final/boot-trace.log` records original-screen Azahar loading,
wrong 3 reset, undo, correct 3, both cameras and result0 with `flags_written=0`.
The standalone scene is ready for the Cipher-owned native `CC` modal handoff.
Standard field gameplay cannot enter it until that connection is integrated.
That connection remains open; this branch does not duplicate or silently solve
Cipher's claimed native. Its API is `c3_dna_key_scene_run(expected[3],...)`,
returning0 success/1 cancel/2 invalid, with renderer teardown required by caller.

## Host and visual evidence

- `private/evidence-tour/` plus `private/evidence-7b/`:19/19 walkable original
  floors loaded and captured. These are independent warped visits.
- `private/realgam-preending-host-v3.txt` and completed counterpart:75 and67
  successful event rows, zero script errors/blocked rows. Hidden actors and
  unchosen branches are not coverage. The original runner labels a budget-limited
  arrival as ok; floor `0x76`'s1800-frame row alone never qualified its ending.
- `private/realgam-own-messages.txt`: all 156 local messages decode with explicit
  fixture variables. This proves decoding, not every conditional on-screen talk.
  Shared banks retain83 distinct decoder failures owned by the systems lane.
- Field adapter13655 synthetic checks, ending133 checks, DNA32 assertions,
  transform-only parse/sample/instance and mixed-zero rejection, four reflection/
  ADD exporter tests, and eleven profile tests pass.
- `private/host-checks.log`:74-command final host inventory, only declared
  baseline failures (`test_bottom_ui`; Python four failures/two errors).
  Python:556 tests,91 skipped. `private/final-validation.log` records source
  pin verification and GCC analyzer checks for ending/DNA code.
- ARM probes and the normal build use pinned devkitARM through Podman/WSL.
  The only linker warning is the pre-existing missing GNU-stack note.

## Material and presentation limits

The 7B exterior preserves 4,866 vertices, 20 batches and 27 clips; its two authored
zero-blend reflection stages are inert and omitted. Arena31 retains 54,666 expanded
vertices and 153 batches; its 14 normal-generated ADD EXT materials now add and
clamp **after** lighting, rather than incorrectly lighting the added texture.
Existing extra-layer omissions/fog/sample reductions remain explicitly recorded
in `private/realgam-stage-sd-manifest.json`; this is not GX pixel parity.

Other open presentation work: generic particles (DD), raw map-model animation
start/stop, elevator riding, head tracking/emotes/audio hooks and partner-follow
integration from the systems lane. Every reached final dialogue is advanced
through the port's normal UI; all possible story-state branches were not played.

## Saves, publication and remaining acceptance

The latest supplied uncleared save is14:54 at Agate2A/previous24, count 49.
Two other uncleared saves are early Phenac (00:08 and00:12). The other 12,
including `completed.gci`, already have476/478 set. No immediate pre-final
checkpoint was found. Identification is in private local/shared `saves/INDEX.md`.

Still open: continuous final-chapter travel from an authentic checkpoint;
Cipher's CC-to-DNA handoff; all conditional talks/events and their on-screen
text; authoritative combat/writeback in the battles lane. No console connection
or desktop key injection was used. Only owned Azahar PIDs were stopped.

Branch: `claude/lane-realgam-7cc77c`, draft
[PR19](https://github.com/dougchansan/Colosseum3DS/pull/19).
Base PR15 remains open; rebase onto origin/main when it merges.

## SD deliverables

All names below are relative to `3ds/Colosseum3DS/`. Pack/save/frame bytes stay
private. Detailed hashes and probe executable sizes: `private/realgam-sd-files.json`.
Copied save outputs are393280 bytes each. Probe routes and evidence are isolated
to this lane's portable emulator. Source pack files changed:

| File | Bytes |
| --- | ---: |
| `boot/battle3d/floor31.c3v` | 512 |
| `boot/battle3d/floor31_arena0_0.c3t` | 181,864 |
| `boot/battle3d/floor31_arena0_1.c3t` | 532,518 |
| `boot/battle3d/floor31_arena0_2.c3t` | 201,450 |
| `boot/battle3d/floor47.c3v` | 1,152 |
| `boot/battle3d/floor47_arena0_0.c3t` | 194,774 |
| `boot/battle3d/floor47_arena0_1.c3t` | 175,343 |
| `boot/battle3d/floor47_arena0_2.c3t` | 441,267 |
| `boot/battle3d/floor47_arena1_0.c3t` | 1,260 |
| `boot/battle3d/floor47_arena2_0.c3t` | 1,263 |
| `boot/battle3d/floor47_arena3_0.c3t` | 75,752 |
| `boot/battle3d/floor47_arena4_0.c3t` | 8,227 |
| `boot/battle3d/floor47_arena5_0.c3t` | 44,858 |
| `boot/battle3d/floor48.c3v` | 512 |
| `boot/battle3d/floor48_arena0_0.c3t` | 498,577 |
| `boot/battle3d/floor48_arena0_1.c3t` | 606,021 |
| `boot/battle3d/floor48_arena1_0.c3t` | 27,663 |
| `boot/battle3d/index.c3n` | 169,508 |
| `boot/f066.c3d` | 96 |
| `boot/f067.c3d` | 224 |
| `boot/f068.c3d` | 96 |
| `boot/f069.c3d` | 160 |
| `boot/f06b.c3d` | 96 |
| `boot/f06c.c3d` | 160 |
| `boot/f06d.c3d` | 160 |
| `boot/f06f.c3d` | 224 |
| `boot/f070.c3d` | 160 |
| `boot/f072.c3d` | 224 |
| `boot/f075.c3d` | 288 |
| `boot/f076.c3a` | 3,328 |
| `boot/f076.c3d` | 224 |
| `boot/f076_dynamic115a.c3t` | 92,356 |
| `boot/f076_dynamic115e.c3t` | 12,017 |
| `boot/f076_dynamic115f.c3t` | 19,980 |
| `boot/f076_dynamic1160.c3t` | 23,645 |
| `boot/f076_dynamic1161.c3t` | 23,181 |
| `boot/f07b.c3d` | 160 |
| `boot/f07b_obj0.c3t` | 213,985 |
| `boot/f387_107c1800.c3c` | 988 |
| `boot/f387_107d1800.c3c` | 988 |
| `boot/f387_obj0.c3t` | 666 |
| `boot/f387_obj1.c3t` | 665 |
| `boot/f387_obj10.c3t` | 471,924 |
| `boot/f387_obj11.c3t` | 2,147 |
| `boot/f387_obj2.c3t` | 664 |
| `boot/f387_obj3.c3t` | 664 |
| `boot/f387_obj4.c3t` | 656 |
| `boot/f387_obj5.c3t` | 665 |
| `boot/f387_obj6.c3t` | 671 |
| `boot/f387_obj7.c3t` | 673 |
| `boot/f387_obj8.c3t` | 674 |
| `boot/f387_obj9.c3t` | 1,413 |
| `boot/staffroll.c3s` | 134,261 |
| `boot/staffroll.c3v` | 806,400,064 |

Total stored pack files: **810,571,068 bytes** across54 files (includes replaced index/metadata).

## Implementation commits

- `6d7983c Keep Realgam resource identities available without a private survey`
- `e1c8295 Let Realgam initialization and bar scripts resume with source native behavior`
- `157ce37 Bind Realgam doors to their original source model clips`
- `3084791 Preserve live progress while writing the guarded clear-save checkpoint`
- `7ee8431 Admit the complete original staff roll through bounded streaming`
- `e5dfe2b Reach credits and clear-save prompts from the story floor dispatcher`
- `f31da93 Avoid rejecting Realgam geometry for an inert reflection stage`
- `44bbc30 Allow clear-save Continue while people models stream in`
- `8a3fb62 Keep the source ending model alive through transient person allocation`
- `8f3913a Expose final-event blockers with an isolated uncleared-save probe`
- `00f9c84 Retain authored ending motion without inventing carrier geometry`
- `d8ae44a Let the final characters follow their original animated carriers`
- `8a4c9d1 Keep review frames for each reached final-event dialogue page`
- `f83dc01 Preserve the original DNA-key selection and undo rules`
- `6b2d36f Load the original DNA-key screen and selector animations`
- `abde884 Run the original return fade after staff roll`
- `4404555 Keep Realgam reflections additive after the original lighting stage`
- `3ba02d1 Present the DNA selector at full brightness and drain before teardown`
- `8e2211a Qualify the remaining Realgam and ancient arena resources separately from combat`
- `c0c0953 Make the Realgam text and DNA rule checks repeatable`
- `5db932d Capture the completed DNA selection after its GPU frame settles`

Normal ARM build: `5db932d4b3ca`, `private/arm-release.log`; isolated SD executable `RealgamStory.3dsx` (1264828 bytes). Save immutability attestation: `private/immutable-save-attest.json` (15/15 source copies identical).

Probe-only capture refinement: `15c2952` holds the solved DNA grid until the settled screenshot is taken. The rerun uses the original intro/grid/exit and returns0; normal field gameplay is unchanged.
