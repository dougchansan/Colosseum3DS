# Original Outskirt Stand scripts on the native runtime

The story adapter executes the original Outskirt Stand and shop GSVM banks.
It also loads the original common script bank and the original English
message groups. It does not replace those programs with a sequence of invented
dialogue, flags, or battle results. Native engine calls remain a separate porting
boundary. Reaching the original first battle request is **not** a completed
battle or evidence that later areas are playable.

All source references below are pinned to Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`. Prepared packs come from the admitted
GC6E01 revision 0 disc and stay outside the repository. The repository contains
the converter, execution code, tests, and source references, not original
dialogue or model/script assets.

## Execution and ownership

`src/story.c` is a portable executor. It reads big-endian script words without
unaligned host pointer casts and retains the original function offsets, operand
types, global variables, call frames, and 32-bit value representation. Its
reference is [gs_vm.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_vm.c)
and [gs_vm.h](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/include/game/gs_vm.h).

Every native opcode yields a `C3StoryCall` containing its original script ID,
bank-relative PC, native index, and argument words. The scene must complete
that operation before calling `c3_story_resume`. Calling `c3_story_step` again
while a task is waiting returns the same pending call. There is no default
"success" handler. Frame waits and print requests also yield, with distinct
call kinds. A VM error identifies the script and instruction PC.

The native binding in `src/field_story.c` owns actor movement, animation,
camera/wait state, message pagination and choices, source-defined game-state
flags, and source event dispatch. Native battle `98` remains pending until a
real combat backend is available. Presentation features still awaiting a
backend must be reported honestly by that binding. A successful VM test cannot
establish that audio, procedural head movement, TV rendering, or device output
is complete.

The VM supplies independent task contexts. Scheduling order, native child-task
semantics, and actor-script suspension belong to the native binding. Original
`scriptExecTask` creates a task with four script arguments; the script's requested
affinity offset is capped at seven and added to the calling thread's affinity.
Its thread group comes from the active floor context, not the floor's resource
group. Native `D1` runs a four-argument child synchronously. See
[field_range_801CB180.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/field_range_801CB180.c)
and `fn_800F7318`/`fn_800F7434` in `gs_vm.c`.

### Portable VM limits

| Resource | Bound |
|---|---:|
| Whole C3S1 pack | 1 MiB |
| One original bank | 256 KiB |
| Banks / message groups | 8 / 16 |
| Concurrent VM tasks | 16 |
| Original operand stack per task | 64 words |
| Function entries per bank | 2,048 |
| Global words per bank | 1,024 |
| Native arguments | 8 words |
| Decoded console message | Caller bounded; scene uses 4,096 bytes |

The executor covers the defined original opcodes `00..25`; the original null
table slots `09..0C` are rejected. Integer results preserve 32-bit wrapping.
Mixed float/int assignment and arithmetic use the original operand rules,
including the unusual float remainder behavior in `fn_800F4818`. Bounds,
invalid numeric conversions, truncated operands, and unsupported operations
fail explicitly instead of reproducing unsafe memory access.

VM addresses use checked handles for stack/global slots. Native code must use
`c3_story_read_value` and `c3_story_write_value`; a handle is never an ARM or host
pointer. Raw string relocations are validated but are not converted to host
addresses. General native string operands and the print opcode still need a
checked string adapter. Replacing an entire pack replaces its global state;
the current area binding is not a general persistence mechanism for every
common-bank global used by later content.

Constructors are transactional: a failed replacement leaves the old object
intact. Public objects must be zero-initialized before first use. Non-null
error buffers require `C3_STORY_ERROR` bytes.

## Private extraction and C3S1 format

From the repository root:

```sh
python3 tools/prepare_story.py --disc /private/extracted-disc --output /private/new-story-output
```

The output directory must be new. The converter checks the disc identity and
exact admitted archive sizes/hashes before extracting members. It copies
original data bytes, rather than recompiling or rewriting scripts.

| Output | Original field bank / message member | Common data |
|---|---|---|
| `outskirt.c3s` | `S1_out.fsys`: `01430E00` / `01420A00` | GSVM bank `0596`; common English and names groups |
| `outskirt_shop.c3s` | `S1_shop_1F.fsys`: `01460E00` / `01450A00` | Same common banks/groups |

The same converter emits three metadata sidecars for `prepare_area_runtime.py`.
`area-events.json` retains the original kind, source ID, script ID, and all four
event arguments. `area-lifecycle.json` retains the original floor/resource
group and load, init, pre, main, and post hook words. `actor-names.json` joins
each original actor's name index and talk policy to the original heard-flag
and message-ID table. None requires a manually constructed audit file.

```sh
python3 tools/prepare_area_runtime.py \
  --metadata /private/area-output/area-actors.json \
  --links /private/new-story-output/area-events.json \
  --lifecycle /private/new-story-output/area-lifecycle.json \
  --names /private/new-story-output/actor-names.json \
  --output /private/new-area-runtime-output
```

In common REL125 data, the event table begins at `146364` (594 rows, 28 bytes),
the floor table at `8BCF4` (196 rows, 76 bytes), and the name table at `1A34`
(607 rows, 8 bytes). Their counts are read from `14A704`, `14A670`, and
`14A660`. Only floors 1 and 2 are exported. Actor rows come from the original
FSYS REL members `02571C00` and `02581C00`. The manifest records these offsets,
source hashes, sidecar counts, and output hashes for reproduction.

Common REL data starts at file offset `184C` for the admitted revision. The
common script extent is data `89CFC..8BCB4`. The common names/system group is
data `58044..647B4` with 2,018 entries. The English common group starts at data
`647B4`, contains 684 entries, and ends after its last terminated GSchar string
at relative `124DD`.

C3S1 has a 64-byte little-endian header, followed by 16-byte directory rows and
contiguous payloads. The original payloads remain big-endian.

| Header offset | Meaning |
|---:|---|
| `00` | Magic `C3S1` |
| `04` / `08` / `0C` | Version 1 / total bytes / source floor ID |
| `10` / `14` | Bank count / message-group count |
| `18` / `1C` | Bank directory offset / message directory offset |
| `20..3F` | Reserved zero bytes |

Each directory row contains ID, payload offset, payload size, and a reserved
zero word. Banks precede message groups. The decoder checks directory ordering,
payload extents, function/global/relocation bounds, and sorted message IDs.
Message lookup searches the field group followed by common groups.

The text adapter preserves original newlines and page breaks, substitutes the
provided hero/partner/speaker names, and handles the first-area font/ruby and
sound-suppression controls. Unhandled controls fail with the message ID and
control byte. Console presentation maps the original unknown-name glyph to
`???` and accented e to the console's plain e; it is not the original message
font or a complete original message renderer.

### Structural coverage inventory

The converter walks the control-flow graph from every function entry, including
later branches within these area banks. It checks instruction extents and
branch/call destinations. Native IDs are recovered from stack effects within
basic blocks; unresolved IDs would be reported rather than guessed.

| Bank | Original bytes | Native call sites | Distinct required native IDs | Unresolved sites |
|---|---:|---:|---:|---:|
| Exterior `0100` | 14,816 | 321 | 60 | 0 |
| Shop `0100` | 6,496 | 133 | 44 | 0 |
| Common `0596` | 8,120 | 176 | 41 | 0 |

`story-manifest.json` includes every function offset, the reachable opcode set,
every native call's PC/ID/argument count, and source/output hashes. These are
**required-call inventories**, not counts of completed native systems.

## Source-verified first visit

The original floor table uses 76-byte records in common REL data `8BCF4`.
Load/init/pre/main/post script fields are at record offsets
`28/2C/30/34/38`. Exterior floor 1 uses resource group 29 and scripts
`01000000/01/02/0B/03`; shop floor 2 uses group 30 and
`01000000/01/02/04/03`. The corresponding native callback fields are zero.
The [floor initialization code](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/floor.c)
provides their lifecycle, rather than an inferred order from model names.

| Step | Original behavior |
|---|---|
| First approach outside | Main `0100000B` detects Wes in the authored approach rectangle while `03DA` is clear, then calls local function 8 for Folly and Trudly's vehicle departure |
| Enter shop | Source door event starts common `05960004` with `[2,0,1,0]` |
| Shop init `01000001` | TV setup native `D3`; Willie shown while flag `03DE` is clear |
| Shop pre `01000002` | Audio ID 4; five NPCs look at `(-22,8,-12)`; room camera default applied |
| First shop main `01000004` | Sets `0ACE` and `0A05`; plays camera `00E51800`; waits 60 frames; switches to audio 28; runs TV native `B6` |
| Fresh TV native `B6` | Mode 0 render-to-texture; three-second wait; original common message `426E` with voice 5; closes the window |
| Return from news | Restores audio ID 4, waits 60 frames, waits for camera completion, targets hero `(0,100)`, sets `03DB`, enters normal movement loop |
| Interior Willie | Actor 4, talk script `0100000E`; message `514A`, native `0B` choice, message `514C`, sets `03DC` |
| Leave shop | Source door event starts common `05960004` with `[1,1,1,0]`; shop post `01000003` also sets `03DC` |
| First exterior challenge | Main `0100000B` sees `03DC && !03DE`, calls local function 9, moves Willie and Wes using original commands and clips |
| Combat boundary | Original script sets `03DD`, then requests native `98(10,1,0)` at bank PC `2698`; no fabricated result is supplied |

### Mandatory exterior approach before the shop door

The first visit is not a direct walk from the arrival spawn into the shop.
Exterior main `0100000B` tests flag `03DA` and native
`3A(0,100,-45,-45,0,40)` at bank PC `2E4D`. The authored X/Z rectangle is
`x=-45..0, z=-45..40` in original units, or `x=-4.5..0, z=-4.5..4`
in prepared coordinates. It covers the approach to the entrance steps.
When entered with `03DA` clear, the script selects original clip 0 on Wes
and vehicle actor 2, then calls **local function 8**, starting at PC `1544`.
This is separate from local functions 6 and 7, which are its boarding children.
These PCs and argument words are extracted from the admitted exterior GSVM
member `01430E00`; `story-manifest.json` preserves their provenance.

| Exterior actor | Source identity | Original resource | Motions used by this event |
|---|---|---|---|
| 0 | Folly, PeopleInfo `4E`, name message `10A8` | `06B10400` | Walking clip 2; gesture 16; idle 1; boarding 6; seated loop 7 |
| 1 | Trudly, PeopleInfo `4D`, name message `107D` | `06B00400` | Walking clip 2; gesture 16; idle 1; boarding 8; seated loop 9 |
| 2 | Vehicle | `010B0400` | Waiting loop 0; boarding sequence 1; departure sequence 2 |
| Hero `(0,100)` | Wes | Prepared original hero model | Original clip 0 at the approach trigger, distinct from the normal idle/walk/run mapping |

Actors 0 and 1 initially have their visibility bits clear. Their absence from
the initial frame does not make their models optional: function 8 reveals
them through child functions 4 and 5. Folly emerges using a two-point walk
list; Trudly uses a point movement command. The parent waits for both child
tasks before the original messages `515D` and `5146`, including the authored
gesture and idle animation waits. These messages must be loaded privately
from the original message member, not copied into source code.

After those messages, function 8 starts child functions 6 and 7. Each NPC walks
to the vehicle, turns, plays a boarding animation, and attaches to vehicle
actor 2's **original model part 1**. Native `46` is
`fn_801845E4(group,index,parentGroup,parentIndex,partIndex)`; the calls here
are `(29,0,29,2,1)` and `(29,1,29,2,1)`. The following `3B` positions
`(-12,0,16)` and `(15,0,14)` are local offsets in original units, rather than
new world positions. Original child rotations are `(0,90,0)` and `(0,270,0)`
degrees. Dropping this attachment would leave the riders behind when the
vehicle's departure animation moves its part.

The walk-list operations are native `48` (allocate an empty list with the
requested capacity), `49` (append a point), and `4A` (start the list, with
looping selected by its third argument). Original `fn_8018524C` starts facing
the first target at turn speed 1, uses turn speed 2 for subsequent targets,
and consumes movement remaining after passing an intermediate point. A
non-looping list places the actor on its last point and stops. See
[people.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/people/people.c),
particularly `fn_80184190`, `peopleAddWalkList`, `fn_80183E5C`,
`fn_8018524C`, and `fn_801845E4`.

Attachment type 7 has specific source semantics. `GSpartGetTransform` returns
the part's world position, the **sum of ancestor Euler angles**, and the
componentwise product of ancestor scales. The child position becomes the
parent part position plus the child local offset rotated by those parent
Euler angles; the parent scale does not multiply that offset. The child's
Euler angles are added componentwise, and its scales are multiplied
componentwise. The Euler convention is `Rz * Ry * Rx`. This is generally
different from multiplying a parent matrix by a child matrix. The admitted
vehicle part has an identity ancestor and unit scale, and the converter
requires its sampled Euler components to remain within `±0.25` radians so
matrix decomposition recovers the authored branch. That admission does not
establish support for arbitrary later attachments. See
[GSmodelAttachToGSpart](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_main_suffix_candidate_800E4AC0.c),
[GSpartGetTransform](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_part.c),
[modelUpdateAttachments](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_model_anim_suffix_800ED4D4.c),
and [EulerToQuat](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/quatlib.c).

The scene uses authored free-camera mode 3 with native `28` eye positions and
`29` Euler rotations. Child 4 also issues native `30(0,29,0,2.0)`; its target
interpolation state must be preserved, while mode 3's eye/rotation determine
the rendered view. After both boarding children finish, sound ID 90 starts
and vehicle clip 2 plays to completion. Only then does the script hide actors
0, 1, and 2, set `03DA`, restore the actors' movement flags, end the hero event,
disable collision object 10, and call `B3(1.0,1)` for a blocking one-second
camera return. Main subsequently restores mode 0, targets `(0,100)`, and
calls `heroMoveMain` again. The adapter must not treat the first call to
`heroMoveMain` as permanent permission to move during later blocking scenes.
See [camera.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/camera.c)
and [hero_move.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/hero_move.c).

Native `76(10,0)` calls `scriptSetCol`, which passes the **zero-based CCD
object ordinal** directly to `GScolsys2SetObjEnable`. It is not a resource ID,
triangle number, or exit ID. The source records an object's disabled state
in its runtime placement flags and honors it in collision queries. A negative
index is returned unchanged; otherwise the return is 0 for success, 1 for a
missing scene, and 2 for an out-of-range object. See
[field_range_801CA7EC.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/field_range_801CA7EC.c)
and [GScolsys2Get/SetObjEnable](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_colsys_obj_enable_exact_8010C7BC.c).

The shop's exit callback sets `03DC` even if Willie has not been spoken to.
Talking to him is the requested route, but it is not an invented prerequisite
added by this port. The interior yes/no answer is stored by the original script;
both answers follow the same subsequent message/flag path there. Native `0B`
opens original submenu `12`, defaults to selection zero, waits for a choice,
reads the signed byte result, and closes synchronously. See
[menuSub.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/menuSub.c).

Native `D3`/`B6` and the real battle entry are defined in
[gs_range_801C766C.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_801C766C.c).
The first TV report renders animated model `0D561000`, animation 0, using camera
`0D571800` into a 256-square texture replacing resource `00E61002`; it is not a
static photograph. The source implementation is
[field_camera_exact_80117AE4.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/field_camera_exact_80117AE4.c).

### Names, NPC behavior, and collision event IDs

Willie is interior actor 4 and exterior actor 3, using PeopleInfo index `58`.
The actor row stores a name index at byte 8. Common REL's eight-byte character
name records contain a hear-flag at byte 0 and a message ID at byte 4. Name index
1 uses flag `0B5E` and message `1014`; until the hear-flag is set, the message
service selects unknown-name message `0FA3`. Name index zero produces no named
speaker. See [char_name_bios.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/char_name_bios.c)
and [people.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/people/people.c).

Talking through a wall and colliding with a person are different source
policies. Original talk selection `fn_8018CD08` calls the player-wall query
only when `floorCharacterBiosGetTalkWallThrough` is zero. That getter reads
floor-character byte 0 bit 4, retained as `C3AreaActor.flags & 0x10000000`.
Every original actor in these two areas has this bit set, including interior
Willie (`B0400000`). His counter remains a walking barrier, while he can be
spoken to from the aisle. Talk selection uses a 12-source-unit base range
plus both actors' radii and a 40-degree view on either side. The candidate's
head part supplies X/Z, but its Y is explicitly reset to the actor's foot
height. `fn_8010F188` is a different NPC-wall query used by gaze checks;
it is not the talk-selection path. See `fn_8018CD08` and
`peopleGetHeadPosition` in `people.c`, and
[floorCharacterBiosGetTalkWallThrough](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/floor_character.c).

Human-cylinder registration instead requires PeopleInfo collision type 1,
decoded by `fn_8018F5FC` as `(info->flags >> 2) & 3`. The visible vehicle has
type 0: its CCD object 10 supplies collision, so adding a human cylinder would
introduce an extra obstacle. Other admitted actors here have type 1. The
mover's runtime flag `0x400` chooses whether it requests a human-collision
query; it is not a target-cylinder enable flag. Target enable is stored in the
registered human slot and controlled by `fn_8018FB2C`/`GScolsys2HumanEnable`,
including the normal show/hide path. See
[PeopleInfo getters](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/people/people_exact_8018F5B4.c)
and [human enable binding](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/people/people_exact_8018FB2C.c).

Movement ordering also matters in the narrow shop aisle. Original
`fn_8018E9B4` applies human-cylinder correction, then player-wall correction,
then the walking surface height. Applying an NPC push after the final wall
query can move Wes behind the counter or aisle wall. Human cylinders do not
inherit the talk-through policy and are not disabled merely because a wall
stands between the actors. See `fn_8018E9B4` in `people.c` and
[GScolsys2HumanCollision](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/GScolsys2Human_range_8010FAF4.c).

The common event table starts at data `146364`, has 594 records of 28 bytes,
and matches floor, kind, and collision event ID. Its script receives four
original argument words. Crucially, the two CCD triangle lists use different
trailing halfwords:

| CCD object list | Triangle `+30` | Triangle `+32` | Event kind |
|---|---|---|---:|
| `+2C`: movement contact | Flags | Event ID | 1 |
| `+30`: A-button interaction | Event ID | Padding | 3 |

This gives exterior door ID 4, interior door ID 2, exterior interaction ID 3,
and interior TV interaction ID 1. The table's destinations are decoded from the
original records, not inferred from triangle numbers. See
[floor_character.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/floor_character.c)
and [the original interaction query](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/GScolsys2Thru_r56_80111864_o4s.c).

## Verification and practical limits

`tests/test_story.c` uses synthetic banks and text, independent of disc assets.
It checks signed and mixed arithmetic, function return frames, overlapping
struct assignment, bounded handles, native suspension/resumption, wait
preservation, task limits, malformed inputs, transactional replacement, and
message substitutions/rejections. It passes with warnings as errors and under
AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer is disabled in the
managed sandbox because it cannot inspect `/proc` tasks there.

`tests/test_field_story.c` always runs its asset-independent synthetic
regressions; supplying privately prepared packs also runs the actual
field-story adapter against the original area scripts. Every private run
creates a new WES game and explicitly establishes the completed opening
checkpoint. The fixture does not play either opening movie and never reads a
completed-game save.

The maintained integration sequence covers the mandatory exterior approach,
the first shop visit and news dialogue, Willie's conversation, a GCI round trip,
Continue with a rebuilt shop scene, and the original exit to the first battle
boundary. After saving, it verifies the loaded name, party levels, floor tuple,
save-counter increment, and flags. It then destroys the old VM, models, map,
and area, frees the old game state, and reconstructs the shop from the newly
loaded state's floor/previous-floor/spawn fields. This exercises actual scene
reconstruction rather than continuing the VM that performed the save.

The continued scene must recover control, preserve party data and checkpoint
fields, and retain `03DB=1`, `03DC=1`, `03DD=0`, and `03DE=0` without replaying
news message `426E`. Original init still configures the television texture on
every visit, so the assertion concerns the introductory report, not a disabled
TV. The test then dispatches the original shop exit and runs the exterior
challenge to encounter 10. It verifies that the party is unchanged and flag
`03DE` remains clear while native `98` is pending, including after further
A/B input; no battle result is supplied.

```sh
./build/test_story
./build/test_field_story /private/prepared-boot /private/field-story-integration.log
```

On 2026-10-22, the maintained test passed against the current privately
prepared two-area fixture in both builds below. The total includes the
synthetic regressions; it is not an additional count to add to that column.

| Build | Synthetic checks | Total checks, including private integration | Result |
|---|---:|---:|---|
| Normal C11 build, warnings as errors | 7,488 | 15,595 | Passed |
| AddressSanitizer + UndefinedBehaviorSanitizer | 7,488 | 15,595 | Passed; no sanitizer error |

The sanitizer run used `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`; leak inspection is excluded for the sandbox
reason above. Both runs rebuilt the continued shop from saved floor 2,
previous floor 1, spawn 0; returned control without repeating the news
message; and reached pending native `98` with `03DE=0`. These are headless host
results, not emulator or physical-device acceptance.

The initial integration fixture omitted the mandatory exterior approach
function 8 by dispatching the door directly. Its earlier passing result did
not establish that approach. The maintained regression now places the hero
inside the real main-script rectangle with `03DA` clear, admits actors 0/1/2
and Wes's original clip 0, executes the emergence and boarding child pairs,
advances messages `515D` and `5146`, and checks that the original script sets
`03DA`, hides the departing passengers/vehicle, and disables collision object
10 before returning control. The fixture still positions the hero for the
approach and NPC interaction and directly dispatches decoded door events;
it does not walk a controller path through the geometry.

Even a passing host regression does not prove physical navigation, camera
framing, visible animation or TV output, sound, hardware stability, or battle
playability. Actual controller input must complete the approach, enter the
door, traverse the shop aisle, talk to Willie across the counter, and leave by
the original exit. Screenshots and recordings must come from the running
renderer/emulator/device and be assessed separately. Later areas, general
menus, combat, and full-game completion remain distinct porting work; no
first-area test establishes their completion.
