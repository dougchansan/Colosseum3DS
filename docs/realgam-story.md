# Realgam Tower, ending, credits and clear save

Status, 2026-11-16: **qualified subsets; chronological chapter acceptance remains open**.
All 29 owned floor identities have original-asset Azahar evidence through the
appropriate field, battle-stage or special-scene path. The final-floor original
script reaches the ending and full credits, writes a copied clear save, and a
fresh process loads that written checkpoint and returns control. This final
run explicitly warps the latest supplied uncleared save onto floor `0x76` and skips
six battles as wins. It does not prove travel from Agate through the final chapter.

Round 2 (branch `claude/lane-realgam-run-652261`): the chapter now plays as one
host story route, `tests/routes/realgam.route`, from a Realgam chapter checkpoint
to the clear save and Continue, following only the original scripts, doors and
flags (no floor warps, no `set` shortcuts). See "Continuous chapter route" below.

## Continuous chapter route

### Checkpoint

No supplied save sits between the Under lab (`M4_labo_B1` function 8 sets
`0x45D`, the world map's final-Realgam switch) and Realgam. Save `24219`
(WES/RUI, 26:03, six-member natural playthrough) is past both, so the route's
`clear` lines reset every flag the final-chapter D4 banks write (`0x7B`,
`0x67`-`0x76`, `0x7C`; `D4_out` `0x66` is the earlier Realgam visit and keeps its
flags), the staff roll's `0x478`, and the eight Realgam Shadow snag flags the
dome and tower banks branch on (`0x860 0x862 0x864 0x86F 0x870-0x873`, the
first-time encounter variants). The party, bag and other chapters' flags are the
save's own. The route writes chapter-point saves (private) at the `0x7B`
arrival and on tower 2F.

### Order the scripts impose

| Step | Floor | Original trigger | Writes |
| --- | --- | --- | --- |
| World map | 0x38E | `gs_title.c`: Realgam (0x66) opens as 0x7B once `0x45D` | |
| Arrival | 0x7B | function 4 | `0x45E`, then door 0 sets `0xAF1`, `0xAF9` |
| Dome lobby | 0x67 | dome doors run common function 12 | |
| Dome transfer | 0x7B | common fn 12 queues 0x7B fn 7 (native 22) and links to 0x7B; fn 7 links on | |
| Rooms | 0x68, 0x6A | talk: battles 165, 214; keys 533, 534 | `0x45F 0xB3F`, `0x461 0x463` |
| Rooms | 0x6B, 0x6D, 0x6E | talk: 410; 411 and 414; 329 (key 535) | `0x465`, `0xAC9 0x466`, `0x467 0x9E2` |
| Key gate | 0x6F | inspect 0/1/3 + item menu (D0): keys 533-535 | `0xACB 0x9E4 0x9E5` |
| Room | 0x71 (via 0x70) | talk: battle 406, key 536 | `0xA5C 0xA5E` |
| Key gate | 0x6F | inspect 2 + key 536, then fn 7; floor box: battle 417 | `0x9E6 0xACC`, `0xA5B` |
| Tower 1F | 0x72 | box: 423; line: (event); line: 421; box: 422; talk: 420 | `0x9E7 0xA5F 0xA4B 0xA47 0xA48 0x468` |
| 1F rooms | 0x73, 0x74 | talk 424; line 425; talk 426 | `0xA49 0xA4A`, `0x46A` |
| Tower 2F | 0x75 (touch from 0x72) | arrival; box; box: cutscene and battle 427 | `0x46B`, `0x46C`, `0x46D 0x9EA-0x9EC` |
| 2F_2 | 0x7C | talk: battle 430 | `0xDC4` |
| Final | 0x76 (touch from 0x75) | arrival function 4: battles 432-442, scenes, `5F(0x39F)` | `0x46F 0x471 0x473-0x477 0x9C1 0x9C6 0x9FA` |
| Staff roll | 0x39F | `movie.c` fn_80035F64 via `src/ending.c`: 0x444C Yes | `0x478`; clear save (2, prev 1, entrance 1) |
| Return | 2 | floor link (2, 1), fade 05960008 | |
| Continue | 2 | the written card, reloaded fresh | |

Twenty-three story battles are reported by the route (skipped as wins, as in
every host route; combat and writeback are the battles lanes'). 93 messages
decode; three contain non-ASCII glyphs.

### What the route needed

- **Native 22 across floor links.** `hero_move.c` stores
  `heroMoveAddAutoEvent`'s script in hero-move work, and the main step
  (`fn_8013024C`) starts it with its four arguments once the hero is free. The
  dome doors queue 0x7B function 7 with the destination and then link to 0x7B,
  so the queue must survive the floor change. `src/field_story.c` now starts a
  pending script at control time and exposes `c3_field_story_pending_auto_event`
  / `c3_field_story_set_auto_event`; the field scene and route player carry it
  to the next floor's story. Without it every dome door returned to 0x7B.
- **Route player steps** (`tests/test_story_route.c`): `map` (the original
  world-map choice), `clearsave`, `continue`, and the 0x39F staff roll run
  through `src/ending.c` (prepared stream header and last frame, decoded
  prompts). Floor-profile pack names reach floor 2 (`shop`). The 0x76 arrival
  is the whole final event, so an arrival may take up to an hour of frames.

### Azahar run of the final segment

`private/evidence/final-run/` (roundtrip probe, `build-3ds-rt`): the route-written
`realgam-2-tower-2f.gci` loaded unchanged (`continue`: floor 0x75, previous 0x7B,
entrance 0; no warp), probe route `75 1; 7c p0; 7c 2; 75 t2; stop 2`. Every battle
was **played** by the 3D battle autopilot and won: 430 Rugen (2F_2), then on 0x76
432 Jomas, 434 Delan, 436 Nella, 438 Ston, 440 Nascour, 442 Evice. The 0x76 scripts
then linked to 0x39F: `movie_end name=staffroll result=0 displayed=4891 skipped=358
elapsed_us=175175060` (5,250-frame stream; the THP has no audio track and DSP
firmware is absent on this emulator), prompt 0x444C answered Yes, `saved=1`, return
to floor 2 entrance 1 with fade 05960008, and the Outskirt shop rendered with control.
47 dialogue pages on 0x76 were captured. Run time 21.6 min.

`private/evidence/clear-continue/` (`build-3ds-cc`): a fresh Azahar process loads the
clear card that run wrote: floor 2, previous 1, entrance 1, `0x476=1`, `0x478=1`,
control and a frame at 90.

Run (private inputs; boot directory with the `f%03x` and Outskirt packs,
`staffroll.c3s`, `pda.c3pd`, `newgame.c3g`):

```bash
C3_ROUTE_PDA=<boot>/pda.c3pd C3_ROUTE_STAFFROLL=<boot>/staffroll.c3v build/test_story_route <boot> <boot> <copy of 24219> tests/routes/realgam.route report.txt trace.log
```

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
