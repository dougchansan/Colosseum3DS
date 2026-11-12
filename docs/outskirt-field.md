# Outskirt Stand native scene

The first-area route uses the original `S1_out` map, CCD collision triangles,
entrances, Wes model/motions, and bike-arrival assets. It is a bounded native
scene adapter. It does not execute PPC REL code or silently stub a general
script VM. After the arrival, the available interactions are movement, viewing
the party, saving an entrance checkpoint, and returning to the menu.

## Reviewed source and entry sequence

The decompilation pin is
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).
The source paths below are canonical at that pin:

| Source | Semantics used |
| --- | --- |
| `src/game/movie.c`, `fn_80035E04` | Opening completion sets `0x08D0`, calls `floorLink(1,0)`, schedules fade script `0x05960008` |
| `src/game/floor.c`, `floorLink` | Runtime floor/previous-floor/spawn transition; does not itself rewrite saved checkpoint fields |
| `src/game/gs_vm.c` and `include/game/gs_vm.h` | GSVM instruction/operand semantics used to audit the original entry bytecode |
| `src/game/camera.c` | Camera animation at rate0.5, wait-to-end, follow-mode target, floor defaults |
| `src/game/gs_range_800D1070.c`, `src/game/gs_render_util_exact_800D2B44.c` | Camera completion at authored end-0.1; end150 takes300 half-frame updates, then clamps to149.9 |
| `src/game/people/people.c` | Model attachment, visibility, motion choices, movement speed curve, source collision/ground-selection behavior |
| `src/game/hero_move.c` | Stick scaling/quadrants and movement relative to camera yaw |
| `src/game/gs_model_anim_exact_800EC4D0.c` | Motion frame-count query returns authored end+1 for movement denominators |
| `src/game/gs_model_anim_suffix_800ED4D4.c` | Animation loop/stop uses authored end, separately from the count query |
| `src/game/effect/fade.c` | Source fade types3/2 and one-second timing |

The original map's GSVM member is `0x01430E00`, bank `0x0100`. Its function11
checks `0x08D0 && !0x0AF8` and calls function16. The entry sequence hides normal
Wes, opens actor `0x0C6C0400` and route object `0x105D1000`, plays camera
`0x105C1800`, requests BGM `0x0488`, plays actor clip1 in a loop, and attaches
that actor to route part0. It waits for the camera, closes both temporary
actors, fades to black for one second, restores camera mode0/target(0,100),
shows Wes, starts a one-second fade from black, and sets `0x0AF8`.

`field_scene.c` lowers this reviewed sequence into explicit native phases.
The completion flag is set only after the prepared original arrival and its
black fade finish. It is not used to pretend later NPC/battle scripts ran.
The opening and arrival state changes stay in memory until the user saves.

The reviewed camera ends after five seconds. The source evaluates a pose before
advancing its cursor, so its last running update samples149.5, then marks the
animation ended and clamps the next pose to149.9. The native scene ends at the
same elapsed time and retains the149.9 pose through the black fade. The pack's
301 samples at60Hz retain the complete source curve for interpolation.

The cold-new-game `init`/`pre` paths were also traced. They enable collision
object11, adjust NPC visibility/animation, show resource`0x00E21000`, and schedule
audio; they make no further persistent flag changes or Wes spawn/yaw override
for the new-game flags. The native static collision set includes object11.
NPC visibility/animation dispatch and the delayed audio are still outside this
adapter. These source observations do not imply that arbitrary loaded story
states have had their event scripts executed.

## Movement and field boundaries

Entrance0 has original position(50,0,70), facing180 degrees. Prepared renderer
coordinates use the importer scale0.1, giving(5,0,7) and pi radians. All five
source entrances are retained; a loaded save uses its recorded entrance.
The source camera defaults follow Wes with eye offset(0,11,9.1836), target
height1.4, vertical FOV30 degrees, aspect1.18 and clip planes0.01/3276.8.

The player uses the original PeopleInfo collision radius4 source units,
walk distance9 and run distance13. Movement divides those distances by source
frame counts31/18, uses the original piecewise speed curve, and advances by
active NTSC frame time. Animation loops are independently sampled from the
original curves. The movement heading and actor Y rotation share the source
sin/cos convention. The current native actor turns immediately; retail has an
additional gradual-facing step.

The map has136 original walk triangles and244 original player-wall triangles
across14 static objects. Player walls come from CCD object+0x28; the separate
NPC group at+0x34 is not mixed into them. The runtime preserves face/flagged-edge/
corner tests and original object order, scans the small triangle set linearly,
sweeps movement in radius-sized steps, and uses the source ground-height rule.
It does not implement the original spatial index, moving objects or NPC bodies.
Movement beyond loaded walk surfaces is blocked; source exits into other areas
are not implemented. A stalled movement update is capped at0.25 seconds.

The native pause Save is an explicit convenience checkpoint. A new game arriving
from floor0x03A0 records floor1, previous0x03A0, entrance0; Continue restores the
entrance, original party and progress body. This is not a claim that the original
movie updates save fields or that the original PC save interface is ported.
There is no invented coordinate field in the original body.

## C3F1 metadata format

All integer/float fields are little-endian. The loader validates every section,
finite numeric bound, count and reserved byte before replacing an existing map.
Maximum file size is512KiB; limits are64 entrances and4096 triangles per table.

| Header offset | Type | Meaning |
| ---: | --- | --- |
| 0,4,8,12 | magic,u32,u32,u32 | C3F1, version1, exact bytes, header256 |
| 16,20 | 2u32 | Floor ID and map resource ID |
| 24,28,32 | 3u32 | Entrance, walk-triangle and wall-triangle counts |
| 36,40,44 | 3u32 | Contiguous entrance/walk/wall offsets |
| 48,52 | 2u32 | Triangle stride56 and entrance stride24 |
| 56,60 | 2u32 | Zero |
| 64..88 | 7f32 | Coordinate scale0.1, radius, walk/run distance, walk/run/idle frame counts |
| 92 | u32 | Camera mode0 fixed or1 follow |
| 96..136 | 11f32 | Eye3, target3, roll, FOV, near, far, aspect |
| 140..152 | 4f32 | Follow height/distance/yaw/target-height |
| 156,160,164 | 3u32 | Actor first vertex/count/bone; separate actor uses first0,boneFFFFFFFF |
| 168,172,176 | 3u32 | Idle/walk/run clip indices |
| 180,184 | 2u32 | Required map/arrival C3T pack counts; map 1..47, arrival 0..6, sum at most 47 (`C3_FIELD_MAX_MODEL_PACKS`; the converters still write at most 15 map packs) |
| 188..255 | bytes | Zero |

An entrance is position3f32, yawf32, original indexu32, reserved0u32.
A triangle is9 vertex floats,3 unit-normal floats, raw flagsu16, source IDu16,
object indexu16 and reserved0u16. No host structs or relocated GC pointers are
serialized. File lengths and table boundaries must match exactly.

The fixed asset naming contract is `outskirt_map0.c3t` through the recorded map
count, `outskirt_wes.c3t`, and `outskirt_arrival0.c3t` through its recorded count.
Every required file is checked. Multiple C3T packs retain independent clocks
but share one renderer frame/arena; the scene applies Wes's world transform
after pose sampling. The generated private manifest records all resource,
geometry, texture, source and output identities. Pack counts are metadata, not
a mechanism for accepting missing portions of the scene.

## Remaining scene work

NPC visibility/events/dialogue, the train-interior transition and later story
graphs are not implemented by this adapter. Neither are battles,
particle emitters, the original field UI, dynamic shadows, external BGM/SFX,
or all GX/HSD material combinations. The prepared assets and native runtime
must be compared against original output and profiled on physical New 3DS
hardware before any visual-fidelity or performance claim.
