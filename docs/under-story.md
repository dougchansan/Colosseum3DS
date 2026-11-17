# The Under and Deep Colosseum

The lane is **not complete**. Nineteen walkable floors have pinned profiles,
original private packs and door bindings, and the four subway event scenes
(0x8A-0x8D) run their original scripts. On the host the chapter now plays from
the Rob chapter save through Venus, the subway trainers, the Subway Key, the
train to the Shadow Pokemon Lab and back, and the Under elevator to Pyrite
(round 2, below). Colosseum/battle scene sets remain assigned to battles.
No progress flag is granted to bypass an unported dependency.

## Round 2: Subway Key, train and elevator

`tests/routes/under-subway.route`, played by `tests/test_story_route.c` from
`pokemon-colosseum.3641.gci` with one persistent game state, checks only flags
that original scripts set and items in the real bag (`C3_ROUTE_PDA`):

| Step | Original source | Checked |
| --- | --- | --- |
| Venus's house 0x37, person 3 | battle 0x148 (result 2) | 0x44A, 0x44C |
| Subway B2 0x3A, persons 1-4 | battles 0x14D/0x150/0x153/0x156 | 0x44E |
| Subway B3 0x3C arrival | Venus sequence | 0x44F |
| Train 0x3D arrival from 0x3C, door 0 | fn2 sets 0x450; fn10 -> 0x3C | 0x450, 0xAFB |
| Subway Key, 0x3A | REL125 treasure row 45 (shown after 0xAFB) | 0xC4, item 0x1FC |
| Train key slot, inspect 4 | fn11 opens the pause menu (native D0), kind-4 surface 4 runs fn8: native 11 registers 0x1FC -> fn6 | |
| fn6 with 0x450 set | message 0x55DA, floorLink 0x8A | |
| Event scene 0x8A -> 0x8D -> 0x3D | train clip, Wes on part 12, camera 0x10851800 / 0x10831800 | 0x450 cleared (fn2: previous 0x8D) |
| Train door to the Lab garage 0x49 | fn10 with 0x450 clear -> 0x49 | |
| Elevator key, garage 0x49 | treasure row 27 | 0xB2, item 0x1FB |
| Garage door 0, key slot inspect 5 | fn9 registers 0x1FC -> fn7: floorLink 0x8C | |
| Event scene 0x8C -> 0x8B -> 0x3D | as above | 0x450 set (fn2: previous 0x8B) |
| Train door 0 | fn10 -> 0x3C | |
| Under 0x2F elevator panel, inspect 0 | fn32 opens the pause menu; fn33 registers keys 0x1F7-0x1FB to fn31; 0x1FB sets floor variable 5 = 5 | |
| Elevator floor, touch 11 | fn26, variable 5 = 5: elevator ride, floorLink 0x0F; Pyrite's scripts continue to 0x18 | 0xB5C, 0x43A |

Battles are reported as result 2 (win), as before; the battles lanes own them.
The walk between subway floors and up to 0x2F uses `floor` steps (the route
player has no walking); every floor change inside a step is the scripts' own.

### What changed

- **Treasure** (`src/field_story.c`): rows were compared in source units with
  the field-unit hero, so no treasure on any floor could be picked up. Rows are
  now scaled and grounded from their row position, as the original grounds a
  treasure person (`fn_8018E9B4`). The Subway Key lies on the lower level of
  0x3A (y = 0) beside the 12-unit platform.
- **Event scenes** (`tools/prepare_event_scene.py`): each archive has a script
  bank and one map scene (tunnel model 0, train model 1, four lights, one
  camera) and no REL, collision, entrance or people. The packs are the generic
  `f08a`..`f08d` set: map packs, the train as a C3SM scene-model row (every
  clip kept, none looping, part 12 retained as a sampled bone), Wes, the scene
  camera with its original resource ID, the lifecycle scripts and the script
  bank. Animated lights keep their first values (recorded approximation).
- **Field** (`src/field.c`): a C3F with no entrance and no walk or wall
  triangle is an event scene; any entrance gives the scene origin and the
  scripts place the hero. Walkable floors still need both.
- **Scene models** (`src/field_story.c`): natives 72/73/75 address a C3SM row
  by its resource; it rests on frame 0 until native 73 plays it; native 46
  attaches a person to the row's retained part with its full rotation and
  refuses any other part. `platform/3ds/field_scene.c` attaches streamed packs
  before the scripts start on a floor with C3SM rows.
- **Route player** (`tests/test_story_route.c`): loads treasure packs, event
  cameras and the real pack of a C3SM row; `floor <floor> <entrance>
  [previous]`; `press` stands on the ground (optional source height);
  `expect_item`; items go to the real bag and `use` refuses an item the bag
  lacks.

### Azahar evidence

Probe build (`-DC3_ROUNDTRIP_PROBE`, source 11aabf4 plus the probe film
change), my lane's portable Azahar copy, no keypresses, probe route
`start 8a` / `frames 8a 60 8` (event scenes are filmed without hero control,
every 8th frame). The trace shows the original sequence on the console
runtime: 0x8A attaches Wes to the train's part 12 (sampled bone 19), the
train clip and camera play, `story_floor_link 138 -> 141`; 0x8D attaches
(bone 17) and links `141 -> 61`; every floor ends with result 2. The 28
filmed frames show Wes in the train cab, then the camera following the train
out through the tunnel. Frames and trace stay private (the lane's notes
folder has a contact sheet); binary SHA256 `af802421...8ca1`, save SHA256
`f7c778d9...0c31`, unchanged.

### Still missing

- **Deep Colosseum**: the Under elevator reaches 0x3E only with key 0x1FA
  (floor variable 5 = 4, sets 0xDC3 and 0x43A). 0x1FA is treasure row 54 on
  0x85 (Snagem Hideout 2F, shown after 0xA2A), outside this chapter, and the
  colosseum sets 0x3F/0x40 belong to the battles lane. Not routed.
- Keys 0x1F9 (0x2F fn15) and the other elevator destinations (variable 5 =
  1-3) are not routed.
- Presentation: natives BE/BF/C1 (light animation) and 68 (train sound
  0x4C0/0x4C1) are recorded as unrendered; the tunnel model's material
  animation is frozen.
- The lab chapter on 0x49 is the cipher lane's; the route only takes the key.

## Floors and private SD packs

`tools/under_profiles.py` registers nineteen profiles independently of the private
survey: archive size/SHA256, lifecycle row/group, REL tables, script/message
members, geometry clocks, cameras and door-model mappings. Runtime labels are
in `src/floor_profile.c`; existing `fNNN` pack prefixes are preserved.

| Floor | Original archive | Packs | Bytes | Runtime |
| --- | --- | ---: | ---: | --- |
| `0x2F` | `M4_out.fsys` | 35 | 6,999,941 | field |
| `0x30` | `M4_junk_1F.fsys` | 8 | 1,750,662 | field |
| `0x31` | `M4_junk_B1.fsys` | 7 | 1,480,643 | field |
| `0x32` | `M4_hotel_1F.fsys` | 14 | 3,043,542 | field |
| `0x33` | `M4_hotel_B1.fsys` | 12 | 2,094,294 | field |
| `0x34` | `M4_shop_1F.fsys` | 12 | 2,578,728 | field |
| `0x35` | `M4_labo_1F.fsys` | 7 | 1,586,360 | field |
| `0x36` | `M4_labo_B1.fsys` | 16 | 5,390,855 | field |
| `0x37` | `M4_houseA_1F.fsys` | 19 | 4,758,958 | field |
| `0x39` | `M4_subway_B1.fsys` | 16 | 2,872,201 | field |
| `0x3A` | `M4_subway_B2_1.fsys` | 19 | 5,017,902 | field |
| `0x3B` | `M4_subway_B2_2.fsys` | 7 | 1,341,420 | field |
| `0x3C` | `M4_subway_B3.fsys` | 18 | 2,705,794 | field |
| `0x3D` | `M4_train_1F.fsys` | 15 | 2,095,746 | field |
| `0x3E` | `M4_bottom_1F.fsys` | 11 | 1,756,281 | field |
| `0x3F` | `M4_cylinder_colo.fsys` | 0 | 0 | battle; unqualified |
| `0x40` | `M4_bottom_colo.fsys` | 0 | 0 | battle; unqualified |
| `0x79` | `M4_enter_1F.fsys` | 19 | 4,417,581 | field |
| `0x7A` | `M4_out_2.fsys` | 10 | 1,905,768 | field |
| `0x7D` | `M4_enter_B1.fsys` | 13 | 2,089,248 | field |
| `0x7F` | `M4_kanpo_1F.fsys` | 11 | 2,582,096 | field |
| `0x8A` | `M4_subway_B3_ev_1.fsys` | 0 | 0 | event_scene; unqualified |
| `0x8B` | `M4_subway_B3_ev_2.fsys` | 0 | 0 | event_scene; unqualified |
| `0xD3` | `M4_out_bf.fsys` | 0 | 0 | battle; unqualified |
| `0xD4` | `M4_houseA_1F_bf.fsys` | 0 | 0 | battle; unqualified |
| `0xD5` | `M4_subway_B2_1_bf.fsys` | 0 | 0 | battle; unqualified |

Fresh preparation: **269 packs, 56,468,020 bytes**, including thirteen door packs
with 42 rows and three cameras. Every actor preparation succeeded with original
models. C3T packs use zstd and a 512-pixel texture cap. The private tour manifest
records material/second-texture approximations on 0x2F/0x32/0x3A and animated or
chained-light approximations on 0x3E. Retail visual parity is not claimed.

Compared with the initial lane SD, 176 files match, thirteen door packs are new,
and eighty NPC/partner packs change to the profile-selected C3T3 format. Original
event cameras `f02f_camera.c3c` and `f032_camera.c3c` use member `10581800`;
`f037_camera.c3c` uses `10321800`. Each is 108 bytes: one authored static sample.

The systems C3UI3 pack was regenerated as `boot/ui.c3ui`, **877,144 bytes**, with
315 original non-ASCII glyphs. Exact filenames, sizes and hashes are in the
[SD manifest](evidence/under-story-2026-11-16/sd-files.json). Only metadata is
committed; assets and saves remain private.

## Natives and doors

| Native/service | Original behavior and port |
| --- | --- |
| `DB` scriptAddItem | Silent signed bag mutation; Under consumes key items 0x212/0x213/0x222 with -1. A signed callback uses real inventory transactions, including important items. Undefined zero-count result fails explicitly. Optional original pocket sorting remains absent. |
| `D8` mode 1 | `fn_801C852C` clears poison/toxic conditions 3/4 only, guarded by 0x8AE. HP, PP, unrelated statuses and padding are preserved. Separate from full party healing. |
| `6F` scriptIsMoveButtonPush | Held D-pad or either analog axis outside [-2,2], independent of dialogue button edges. Current field input is bound. |
| `BB` scriptGetPremium | Original flag 0xA9E, paired with existing BC setter; checked using original flag metadata. |
| Door binding | Negative selectors mean no animation. Subway elevator rows 142/143 rest at clip 0; row 142 retains travel clips 0/1. Every nonnegative selector is validated against its model. |

Door flags, collision and regular clips execute. Natives 61/C5 still record
elevator riding/part-animation presentation as unrendered. Battles resume with
original result ID 2 through the existing continuation, explicitly logged as
synthetic wins; this lane does not qualify party battle writeback.

## Dialogue

Five systems commits provide the original control decoder, field value bindings,
glyph/color rendering and PDA item/species namer. `test_message_sweep` executes
**41,533 messages in 162 groups**: zero unknown-control messages, zero missing
field-source control IDs, 274 checks. This establishes decoding with supplied
values, not every branch's exact live value or visual fidelity. Move/trainer
class names and menu-owned strings can still need their owner's live bindings.

## Correct save and original chapter

`pokemon-colosseum.3641.gci`: Rob, 14:54:04, six party members; saved in Agate
Pokemon Center (0x2A, previous 0x24). SHA256:
`f7c778d920fa2dc9d359c3b8e2108d681ca8f886a0370763d8a980ecc4a80c31`.
Under opening flags 0x43E-0x446 are set; Venus/subway flags 0x44A onward are clear.
The shared private saves/INDEX.md records it. Save 35188 is an eight-minute
new-game save and must not be called an Under chapter checkpoint.

`tests/test_under_chapter.c` preserves one game across floors and one live VM
per floor. The [recipe](../tests/routes/under-venus.txt) declares probe placements,
automatic dialogue/default choices and exact flag/item assertions. It verifies
the intended talk actor/script and never writes a progress flag. This is a host
probe, not continuous walking or a complete Azahar chapter playthrough. Source
and private copied saves are hash-checked unchanged.

| Step | Original result verified |
| --- | --- |
| House 0x37 talk actor 3 | Venus battle 0x148; flags 0x44A/0x44C |
| Subway 0x3A four trainers | Battles 0x14D/0x150/0x153/0x156; flag 0x44E |
| Subway 0x3C arrival | Venus sequence sets 0x44F and removes roadblock |
| Train 0x3D door 0 | Returns to 0x3C while 0x450 is set |
| Return to 0x3C from train | Original escape sequence sets 0xAFB |
| Return to 0x3A | Subway Key assertion fails honestly: pickup is missing |

The next dependency is common REL125 treasure row 45 (`lbl_125_data_11BFAC`,
74 rows, stride 28): floor 0x3A, item 0x1FC, position (-76.57,0,-52.17), visible
after 0xAFB, pickup flag 0xC4. Field treasure interaction is unported. Native 11's
key-item callbacks must then invoke train functions 6/7 through the original menu.

Train journeys require `03D fn6 -> 08A -> 08D -> 03D` and
`03D fn7 -> 08C -> 08B -> 03D`. Cipher owns 08C/08D. Under 08A/08B contain models,
cameras and lights, without collision/people REL; field entrances must not be
invented. BE/BF/C1 control light visibility/animation/waits. Non-person model
animation, animated camera/lighting and train-joint attachment remain required.
Train function 10 then selects 0x3C or lab 0x49 using 0x450.

## Exits, world map and menus

Original scripts choose all destinations/entrances. Under 0x7A source 0 is the
elevator touch to 0x79; crossing source 1 goes to world map 0x38E. The world-map
destination is 0x7A gated by 0xA0E, written by its original init. Lab destination
0x41 uses 0xDCF. No unlock is patched. Host reports retain connector results.

Hotel 0x32 and entrances 0x79/0x7D use native 62 to push PC floor 0x391. Modal
suspend/return belongs to systems PC work; it is not replaced by a successful
no-op or an ordinary doorway. Remaining native/menu dependencies are inventoried
for all 26 archives and handed off through the claims ledger.

## Verification and evidence

- Azahar: **19/19 walkable floors**, current Rob chapter save, both frames and
  original prepared/control traces. Eighteen use build `f3b5e9e6949e`; subway
  0x3A uses `4304584a1bee`, whose only runtime change adds a probe capture marker.
  The periodic control log missed its brief handoff; its actual capture marker
  reports `prepared=1 control=1`. Seven special sets remain unqualified.
- Host completed checkpoint: **142 events, zero failures**, fifteen skipped.
  Pre-Venus Rob checkpoint: **151 events, zero failures**, six skipped, nine
  explicit synthetic wins (including prerequisites in independent probes).
- Persistent chapter: five synthetic wins, flags 044A/044C/044E/044F/0AFB set
  by original scripts; fails at original item 01FC acquisition, as intended.
- Story VM: 1,062 checks; field story: 12,094; inventory: 99; premium original
  flag roundtrip passed; message sweep: 274 checks with no unknown controls.
- ASan/UBSan field-story checks and GCC field static analysis pass. Profile,
  floor-export, audit and font-pack targeted Python tests pass.
- Full host build succeeds. Full host test target retains the documented bottom
  UI pixel assertion. Python: 554 tests, 90 skipped, the documented four failures
  and two errors; no additional Under or font-schema failure remains.
- ARM probe build succeeds via Podman/WSL. The existing wrapper fails on the
  Windows worktree .git path before building; a private copy runs its global
  Git config from /tmp and takes an explicit build ID. No shared wrapper edit.

Evidence is under this worktree's ignored `private/under/`:

- `prepared/story/`, `prepared/tour-manifest.json`, `prepared/sd-manifest.json`:
  fresh packs, approximation records and hashes.
- `accepted-azahar/`: chapter-save traces, both frames and binary/save hashes.
  Emulator: `G:/Colosseum3DS-emu/lane-under`; no keypresses, only own PIDs stopped.
- `verified-chapter/`: persistent flags and explicit key-pickup blocker.
- `verified-checkpoints/` and `host-route-fix/`: independent event probes and
  explicitly logged earlier trainer interactions needed to reach a target talk.
- `current-inventory.json`: all-function original dependency inventory, not an
  execution or presentation claim.
- `focused-messages.log`, `sanitizer-*`, `verified-analyzer.log`,
  `python-verified.log`: decoder, memory/static checks and suite limitations.

The first fresh capture attempt could not read a live Windows-shared trace.
The runner now uses read/write sharing; failed evidence remains in `final-azahar/`.

## Reproduction

```text
python tools/prepare_story.py --disc <private-disc> --output <private-boot> --floor under_out
python tools/prepare_tour.py --disc <private-disc> --boot <private-boot> --floor under_out --approximate
python tools/prepare_story.py --disc <private-disc> --output <private-boot> --floor under_out --doors-only
python tools/compress_packs.py <private-boot> --codec zstd --max-texture 512
python tools/run_under_story.py --boot <private-boot> --saves <copied-saves> --walkable
python tools/run_under_chapter.py --boot <private-boot> --saves <copied-3641.gci> --recipe tests/routes/under-venus.txt
```

Repeat preparation for every `under_*` profile. The Azahar runner defaults to all
26 requested floors and fails closed; explicitly select the nineteen walkable
floors for current field-only qualification. The chapter runner returns nonzero
at the missing key. No console connection, asset publication or save write
occurred. PR #15 remains the base; rebase on origin/main after it merges.

## Commits

- `6bd8fa4` Let original Under scripts consume their quest items
- `7b54cc9` Make every walkable Under floor reproducible without a private survey
- `1861895` Let Under ambushes wait for actual movement input
- `cebbfd6` Preserve the original poison-only cleanup after Under dialogue
- `c1409b7` Read the original Colosseum premium state at Under receptions
- `691671b` Keep geometry format checks applicable as Under profiles grow
- `7e55cf1` Interpret every original message control from the msgctrlcode table
- `45fbd06` Answer every value control a field message uses
- `ead3142` Sweep every original message for unknown control codes
- `d60ebec` Draw the original font's non-ASCII glyphs and message colours
- `3a032fa` Name items and species in field messages from the PDA tables
- `cb4e8ae` Keep Under coverage failures reproducible at each checkpoint
- `454674a` Expose the first genuine Under chapter progression blocker
- `f3b5e9e` Allow original elevator rows to rest without an open animation
- `6f7bb22` Keep font-pack validation aligned with the integrated extended glyphs
- `279fe10` Reach later subway talks through their original trainer prerequisites
- `4304584` Make chapter capture evidence reliable at a brief control handoff
