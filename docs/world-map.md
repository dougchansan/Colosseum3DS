# World map (floor 0x38E): native destination choice

After the first battle the original Outskirt Stand script sets flag `0x3DF`.
Leaving through the original exit surface (event kind 1, source 0) runs common
script `05960004` with floor `0x38E`, the world map. This page records what is
lowered from the pinned decomp and what is still the port's own presentation.

## From the pinned source

| Behaviour | Source | Native code |
| --- | --- | --- |
| Destination table (9 rows: unlock flag, floor, menu item, image), cursor links (66 rows), journey rows (`lbl_3_data_270`), info slots | `src/rel/worldmap_menu_rel/worldmap_menu_rel.c`, compiled unchanged and hash-pinned in `config/upstream-files.json` | `src/world_map.c` |
| An entry is selectable when its flag is zero or set | `gs_title.c`, destination menu before `menuOpenCustom(0xBD, ...)` | `c3_world_map_open` |
| The cursor starts on the entry of the previous floor; floor `0x7B` counts as `0x66` | `fn_800259B0`, same menu | `c3_world_map_open` |
| Cancel returns to the previous floor | same menu, `result < 0` | `c3_world_map_choose(-1)` |
| Entry `0x66` links to `0x7B` when flag `0x45D` is set | same menu | `c3_world_map_choose` |
| The journey plays unless the floor is unchanged or the pair is `0x7A`/`0x0F` | same menu, `change` | `travel` output |
| The destination is entered with `floorLink(floor, 0)`: entrance 0 | same menu, last statement | scene writes spawn 0 |
| Journey row: west/east half by the last cursor move's direction, a session-long index per half | `fn_80023E60`, `fn_80025F84` | `c3_world_map_journey_west`, `c3_world_map_journey_pick` |
| D-pad follows the link table: LEFT/RIGHT/DOWN/UP are link bits 1/4/8/2, and a link matches when its mask contains every pressed direction | `fn_80024160` | `c3_world_map_move` |

The third word of each destination row is the menu item ID used by the link
table (`0x907` Outskirt Stand ... `0x919`), not a message ID as
[phenac-floors.md](phenac-floors.md) first assumed.

`tests/test_world_map.c` checks these rules against the compiled table (55
checks, including the journey rows): only Outskirt Stand is selectable before `0x3DF`; with it set Phenac
City (floor 3) is the second entry and is reached with DOWN or RIGHT.
`tests/test_field_story.c` runs the original Outskirt exit and checks that it
requests floor `0x38E` with entrance 0 once the scene accepts that floor.

## Port presentation (not original)

`platform/3ds/world_map_scene.c` draws the original map image, markers and
cursor on the bottom screen (`boot/worldmap.c3w`, `tools/prepare_world_map.py`);
the D-pad follows the original link table and a tap selects a marker (tap
again to go); B is the menu's cancel. Without that pack it falls back to a
list in table order. The top screen orbits the highlighted destination.
- Destination names are the port's own English labels, chosen from the
  archive identity of each floor. The original menu strings and the twelve
  location images are not extracted yet.
- A travel choice plays the original journey on the top screen (below).
- The original 3D Orre map menu and the info text slots are not drawn.
- The world-map script bank `01E00E00` (init/pre/main/post, native `B4`) is
  not executed. Its function 6 only reads story flags `0x84E`-`0x890` and
  sets mail/event flags; this is a known gap for later story events.
- A destination without prepared packs is listed as `not prepared` and cannot
  be entered.

Nothing is written to the card on the world map; the chosen floor, previous
floor `0x38E` and entrance 0 are recorded in the live game state only.

## The journey (menu 0xC3)

When the choice changes floor (`travel=1`), `fn_80025730` opens menu `0xC3`
and `fn_80025F84` plays the journey before `floorLink`. The port plays the
same scene on the top screen (`platform/3ds/world_map_scene.c`
`journey_play`), from packs that `tools/prepare_world_journey.py` converts
from the user's own `world_map.fsys`:

| Original | Port |
| --- | --- |
| `move_demo` (`0C6A0200`): model `0xC6A1000` a one-joint route path with fifteen 149-frame animations, `0xC6A1001` the desert scenery, `0xC6A1002` the ground; registered when the map opens (`fn_800259B0`) | `journey_map0-2.c3t` (scenery + ground), streamed on the second core when the world map opens |
| Bike `0xC6B1000` (`bike_hizuki`, Wes with Rui) when flag `0x3E5` is set, else `0xC6C1000` (`bike_pokemon`); attached to joint 0 of `0xC6A1000` (`fn_801CB61C`), animation 0 looping (`fn_801CB834(bike, 0, 0, 1)`) | `journey_<hizuki|pokemon>_aNN_0-1.c3t`: the bike sampled on route animation NN with the path joint as its attachment, as the Outskirt arrival is prepared |
| Route animation `frame_a` on `0xC6A1000` and camera `frame_b` (`cam_move_demo_01-15`, `0x0C6D1800-0x0C7B1800`) from REL table `lbl_3_data_270` | the camera curve is sampled into each bike pack; `c3_world_map_journey_pick` picks the row |
| Row: `lbl_8047A380` (set by `fn_80023E60` on each cursor move: destination left of the origin on the map) picks rows 0-9 (west) or 10-19 (east); `lbl_8047A394`/`398` advance per journey and wrap after 9, for the whole session | `c3_world_map_journey_west`, `c3_world_map_journey_pick` (`tests/test_world_map.c`) |
| `0xAE` frames after the camera starts, then the map's exit (`fn_800255A4`) fades out over two seconds (`fadeSet(2.0f, 3)`) | same; the fade waits until the destination's map packs have loaded (below) |

The trace records `world_map_journey row= route= camera= bike= west=`,
`world_map_journey ready=1 load_us= route_seconds= prefetch=done/total`,
`world_map_journey_end frames= seconds= fade_start= prefetch=` and
`world_map_link ... journey_presented=1`.

### The ride is the destination's loading time

The highlighted destination's map packs (`<prefix>_map%u.c3t`) stream on the
second core while the map is open (`platform/3ds/area_prefetch.c`); the
orbit preview draws them. When the player travels there, the journey plays
while they finish, and the field load takes them instead of reading them
again (`field_prefetch taken=N of=M`). The fade starts at `0xAE` frames or
when the prefetch is done, whichever is later; the last route frame holds if
the load takes longer than the route animation. The field then reads only
Wes, doors and the arrival packs, with no loading screen.

### Bottom screen

The bottom screen keeps the world map (the original map, cursor and
destination name) through the journey and the following load; the old
"Travelling..." notice is gone. After a journey the top screen stays black
(faded out) until the field draws.

### Shadow and opening dissolve

- **Shadow.** `fn_80025F84` casts the bike's shadow from light `0xC6A1603`
  onto the ground `0xC6A1002`. That light is directional and the ground and
  every route lie at y = 0, so the port draws both bike parts flattened onto
  y = 0 along the light (stage-renderer instance mode 3: stencilled so the
  layers darken once, pulled 2% towards the camera along the view rays for
  depth). Darkness 0.5, matched to the Dolphin reference.
- **Dissolve.** `fn_80025A80` dissolves the captured world-map screen into
  the ride. The port builds that screen from the map pack (map, markers,
  cursor on the destination, 1.25x to fill the top screen) and draws it over
  the ride with `c3_stage_renderer_set_overlay`: opacity falls linearly to
  0 in 0.67 s while it zooms to 2x, as the reference shows.

### Bike particles

`fn_80025F84` links `bike_effect_poke` (`0C831400`) to the bike
(`GSmodelLinkToGSparticleBank`, attach mode 4). The bike's animation 0
fires a callback at frame 0 of every loop on several joints (key value
`generator << 6 | 12`: generators 6, 7, 8 on `bike_hizuki`, 0, 2, 4, 12 on
`bike_pokemon`); `fn_80119BD0` starts that generator at the joint and attach
mode 4 carries the particles with it. The port converts the bank
(`journey_dust.c3p`, C3PT v2), bakes each callback joint per sample into
`journey_<bike>_aNN.c3j` (C3J2: the first callback joint's frame and every
emitter in it), starts each loop's callbacks as one particle system in that
joint's frame and draws it through the joint's current transform (renderer
slots 1-4). Debris and exhaust come off the rear wheel and pipes as in the
reference; the large soft dust cloud behind the rear wheel is much fainter
than the original's.

### Not drawn (recorded in `journey-manifest.json`)

- The engine sound `0x449` (no sound pack carries it yet).
- The second, smaller capture quad of `fn_80025A80` (the menu at the cursor).
- The scenery's 2999-frame loop is sampled sparsely (600 samples).
- The fourth word of each `lbl_3_data_270` row (1.0 or 0.5) is not read by
  `fn_80025F84` and is not used.

### Preparing the packs

```text
python tools/prepare_world_journey.py --disc <extracted GC6E01 disc> --output private/journey
python tools/compress_packs.py private/journey --codec zlib
```

Copy the 63 `journey_*.c3t` files (about 50 MB compressed), `journey_dust.c3p`
and the 30 `journey_*_aNN.c3j` files to `sdmc:/3ds/Colosseum3DS/boot/`
(`--emitters-only` writes just the last two kinds). Without them the world map links straight
to the floor (`journey_presented=0`).

## Memory across world-map round trips

A console trace (Phenac -> world map -> Outskirt -> world map -> Phenac)
ended with `Not enough memory for title file` on the second Phenac load at
84.6 MB of heap in use. The field exit closed only the room's packs
(`opened`), not the people packs that stream in later, so every area exit
leaked them (about 42 MB after Phenac). `run_area` now closes every slot.

`C3_ROUNDTRIP_PROBE` (an ARM build flag) loads the campaign save read-only
and alternates area and world map with no input, logging
`platform_allocator` after each area and each world-map visit. Azahar, New
3DS mode, `used_bytes` after each area exit:

| Build | exits 1-3 | then |
| --- | --- | --- |
| before the fix | 44.96, 54.02, 55.92 MB | the fourth load fails: `Not enough memory for title file` |
| with the fix | 2.12, 2.12, 2.12 MB | 2.12 MB through the seventh area |
| with the fix and the journey | 2.02, 2.02, 2.02 MB | 2.02 MB through the seventh area; each journey played and the field took the prefetched packs |

## Original-game reference (Dolphin A/B, 2026-11-15)

Same save on both sides: the console's `saves/pokemon_colosseum.gci` (read
over FTP, SHA256 `eb88bb99…`; Phenac City entrance 4, flag `0x3E5` set).
The original ran in an isolated Dolphin session (`docs/dolphin-reference-capture.md`):
continue, walk out of Phenac to the world map, cursor RIGHT to Outskirt
Stand, A. The port ran the round-trip probe on Azahar with the same save
(`probe-save.gci`), exiting Phenac to the world map and choosing Outskirt.
Both played the session's first eastward ride: row 10, route 0, camera
`0C7B1800`, `bike_hizuki`.

Dolphin dumps one frame per rendered frame: 60 per second in the menus but
30 per second in the field and the ride (618 s of dumped audio held 22,599
frames), so the reference was aligned at 30 frames per second.

- Camera, route and bike match at every half second from 1.0 s to 3.5 s:
  same shots, same cuts, same framing.
- The original fades out over about 1.9 s after the ride (`fadeSet(2.0f, 3)`);
  the port's fade was 1.0 s and is now 2.0 s.
- The shadow and the opening dissolve were then added and compared again
  at the same times: shadow shape, direction and darkness and the map
  dissolve match. The wheel particles were added last: placement and
  debris match; the soft dust cloud is fainter than the original's.

## Completed-save ride survey (2026-11-16)

Port (Azahar, round-trip probe, the 99:59 completed save `e16f2c42…`,
read-only copy): starting in Agate Village (the save sits in its Pokemon
Center), the world map visited every destination with packs in turn; every
ride played and loaded the destination before its fade, and `used_bytes`
returned to 2.02 MB after every area.

| Ride | Row | Route | Camera | Prefetch |
| --- | --- | --- | --- | --- |
| Agate -> Outskirt Stand | 10 (east) | 0 | `0C7B1800` | 4/4 |
| Outskirt -> Shadow Pokemon Lab (0x41) | 0 (west) | 5 | `0C761800` | 5/5 |
| Lab -> Mt. Battle (0x4A) | 11 | 1 | `0C7A1800` | 2/2 |
| Mt. Battle -> Realgam Tower (0x7B, flag `0x45D`) | 12 | 2 | `0C791800` | 12/12 |
| Realgam -> Phenac City | 13 | 3 | `0C781800` | 7/7 |
| Phenac -> Pyrite Town | 1 (west) | 6 | `0C751800` | 4/4 |
| Pyrite -> Agate Village | 2 (west) | 7 | `0C741800` | 3/3 |
| Agate -> The Under (0x7A) | 14 | 4 | `0C771800` | 2/2 |
| The Under -> next | 15 | 10 | `0C711800` | done |
| next | 3 (west) | 8 | `0C731800` | done |

Snagem Hideout (0x88) has no prepared field packs and was not ridden to.

Original (Dolphin): Agate's terraced village blocked a quick walk to its
exit, so the original rides came from the console save instead (Phenac and
Outskirt only, one Dolphin session): Phenac -> Outskirt (row 10),
Outskirt -> Phenac (row 0, route 5, `0C761800`), Phenac -> Outskirt (row 11,
route 1, `0C7A1800`). Each matched the port's ride of the same row at every
half second from 0.5 s to 3.0 s (camera, cuts, route, bike with Rui, shadow,
debris), and the original reached black at 4.87-4.90 s against the port's
4.91 s.

Observed, not changed: from Outskirt the original opened the map with the
cursor on Phenac City; the port starts on the floor the player came from
(`fn_80025730` as decompiled). The ride row did not depend on it.
