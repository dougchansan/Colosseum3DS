# Pyrite Town (M2) as a story chapter

Pyrite Town's floors (archive prefix `M2_`, floors 0x0F-0x23, 0x78, 0x8E and
the battle fields 0xCD-0xCF) and its main-story chapter, from the world-map
arrival to the flag that unlocks Agate Village (`0x9DF`). This page records
what the original scripts do, how the port runs them and every shortcut.
[phenac-story.md](phenac-story.md) is the worked example this follows.

## Floors

All identities come from the private floor survey (`tools/floor_survey.py`)
and the REL125 lifecycle table; the packs are the surveyed `f%03x` packs
(`tools/prepare_story.py`, `tools/prepare_tour.py`, model-free
`tools/prepare_world.py` for the host).

| Floor | Archive | Bank | Role in the chapter |
| ---: | --- | --- | --- |
| 0x0F | `M2_out.fsys` | `01400E00` | town: arrival, the 0x403 event, windmill cutscene, Colosseum gate, bridge grunt, Agate unlock |
| 0x10 | `M2_police_1F.fsys` | `01940E00` | police: first visit (`0x9D8`) |
| 0x11 | `M2_hotel_1F.fsys` | `018C0E00` | hotel (no chapter event) |
| 0x12 | `M2_uranai_1F.fsys` | `01990E00` | fortune teller (no chapter event) |
| 0x13 | `M2_houseA_1F.fsys` | `018F0E00` | house A (no chapter event) |
| 0x14, 0x15 | `M2_guild_1F_1/2.fsys` | `01860E00`, `01890E00` | Duking's house: introduction (`0x3FD`, `0x3FF`, `0x400`), Plusle (`0x417`) |
| 0x16 | `M2_shop_1F.fsys` | `020C0E00` | shop 5 |
| 0x17, 0x18 | `M2_windmill_1F/2F.fsys` | `02020E00`, `020D0E00` | windmill keeper (`0x404`, `0x405`, `0x8D1`), gear (`0xA17`, `0x408`) |
| 0x19-0x1C | `M2_building_1F..4F.fsys` | `02260E00`, `02280E00`, `02290E00`, `02040E00` | Pyrite Building: grunt (`0x40B`), rooftop (`0xA0A`, `0x9DA`), Silva (`0x40C`-`0x411`) |
| 0x1D-0x22 | `M2_cave_*.fsys` | `01720E00` .. `01810E00` | Pyrite Cave: Miror B. (`0x413`), Plusle's room (`0x414`-`0x416`) |
| 0x23 | `M2_earth_colo.fsys` | none | Pyrite Colosseum stadium |
| 0x78, 0x8E | `M2_enter_1F(_2).fsys` | `020B0E00`, `11160E00` | Colosseum entrance: registration (`0xAF5`), four battles, `0x409` |
| 0xCD-0xCF | `M2_*_bf.fsys` | none | battle fields (battles lane) |

Floor links (door args are decimal floor numbers in the event table): town
surfaces 12/11/13/14/15/16/4/5/7/6/8 lead to 0x10, 0x12, 0x13, 0x14, 0x16,
0x11, 0x17, 0x19, 0x1B (entrance 2), 0x1C and 0x21; surface 9 runs town
function 31, which opens the Colosseum entrance (0x78) only once `0x408` is
set; surface 0 is the world map.

## The chapter (host run)

The chapter is a route for the agate lane's route player
(`tests/test_story_route.c`): one game state is carried through every
floor, the scripts' own floor changes are followed, battles are reported as
wins. Start: GameFAQs save 35188 (Phenac, WES/RUI; see
`private/lanes/saves/INDEX.md`) plus Pyrite's world-map unlock `0x3F8`,
which Realgam's exterior (D4_out bank `02CC0E00` function 2) writes before
this chapter. 83 steps run; the original scripts write 43 flags and show
about 130 messages.

| Step | Original behaviour | Flags written by the scripts |
| --- | --- | --- |
| Arrival (entrance 0) | town pre (fn 2) and main (fn 6) call fn 5, the arrival talk | `0x3F9`, `0x9AB` |
| Walk in | main: box x -132..-33, z -136..-106 runs fn 28 (camera) | `0x3FB` |
| Police | police main fn 6: first visit, fn 4 | `0x9D8` |
| Bookshelf | 0x14: the bookshelf (door row 28, model 2, CCD object 5) covers the doorway (surface 4); A on its surface 6 runs fn 12: a yes/no question, then native 60 opens row 28 and native 76 enables CCD object 6. Init (fn 2) keeps object 6 off until then. Person 1, the lookout, only talks (fn 7) | `0x20` (row 28's open flag) |
| Duking | 0x15 pre on entry; back in 0x14, main fn 5 calls fn 4 once `0x3FD` is set | `0x3FD`, `0x3FF`, `0x400` |
| Rooftop | town door 6 to 0x1C entrance 0; 4F main calls fn 4 while `0xA0A` is clear (it sets `0x9DA` because `0x3FF` is set) and links to the town with spawn 180 (scripted placement) | `0xA0A`, `0x9DA`, then `0x9D9` (town, arrival from 0x1C) |
| Door 0xE | town main: with `0x9DA`, the box x 18..30, z -264..-250 | `0x403` |
| Windmill keeper | 0x17 person 1 (fn 6), then main fn 7 | `0x404`, `0x405`, `0x8D1` |
| Construction site | Realgam exterior (0x66): with `0x8D1`, init enables surface 0; A there runs fn 8 (gear, item 532, native DB) | `0xA58`, `0x407` |
| Gear | 0x17 main fn 8 (box); A on surface 0 opens the pause menu at Items (native D0, route step `use 532`); the gear's registered script, fn 12, runs when that event ends | `0xA17`, `0xAB7` |
| Windmill turns | fn 12 leads to town main fn 30: camera, then link to 0x17 spawn 200; windmill fn 15 takes the gear | `0xAB8`, `0x408` |
| Colosseum | town fn 31 to 0x78; the receptionist (fn 5) accepts from the counter box x 4.6..19.2, z 0.2..1.8; main fn 4 plays four battles and gives item 294 | `0xB09`, `0xAF5`, `0xDD1`, `0x409`, `0xAF7` |
| Bridge | town main fn 24 (box by the gate) leads into the building | `0x40A` |
| Building 1F | 1F main fn 5: Cipher Peon, battle 101 | `0x9C8`, `0x40B` |
| Silva | up 2F, 3F to 4F entrance 1; 4F main fn 5 once Wes leaves the stair box: battles 126 and 129; Silva (person 3, fn 10); fn 11 | `0x40C`-`0x411` |
| Cave | rooftop (town entrance 11), town door 8, cave 3F_1, 2F_1, 1F_1, 1F_2 | |
| Miror B. | 1F_2 main fn 21 (box x -2..6, z -44..-25): battle 164; main fn 11 | `0x9E9`, `0x413`, `0x414` |
| Plusle | person 7 (fn 9); main fn 12 brings Duking and Silva and links to Duking's house; 0x14 main fn 8 | `0x415`, `0x416`, `0x417` |
| Agate | town person 17 (fn 26) | `0x418`, `0x9DF` |

The route writes four checkpoint saves (private): arrival, after the
Colosseum, after Silva, and after `0x9DF`.

## Azahar (New 3DS profile, 2026-11-16)

The warp-tour build (`-DC3_WARP_TOUR`, build `2eb5f4150c47-tour`) loaded
every Pyrite floor from the chapter's own checkpoint saves, ran its scripts
for 12 seconds and captured the top screen (private evidence:
`private/pyrite/azahar/`, tour logs, boot traces and frames).

| Run | Save | Floors | Result |
| --- | --- | --- | --- |
| 1 | arrival (`0x3F9`) | 0x0F-0x18 | all ten loaded, about 28 fps, control returned, frame captured |
| 1 | arrival | 0x19 | the building's arrival event ran to battle 101; the battle engine took over and waited for input, so the tour stopped there (the arrival save is before the Colosseum, which the original requires first) |
| 2 | after Silva (`0x411`) | 0x19-0x22, 0x78 | all eleven loaded, about 28 fps, frame captured |
| 2 | after Silva | 0x8E | loaded; its arrival script links straight on to 0x78, as on the host |

Every floor draws its original map, objects, people and Wes.

### SD files

332 packs, 80 MB after `compress_packs.py --codec zstd --max-texture 512`,
all named `f00f*` .. `f022*`, `f078*`, `f08e*` in `boot/`: per floor the
`.c3f`, `.c3a`, `.c3s`, map groups (`_group*`), objects (`_obj*`), `_wes`,
`_partner` and `_npc*` packs, plus `.c3d` door tables for 0x0F, 0x10, 0x11,
0x14, 0x19, 0x1A, 0x1B, 0x1E, 0x78 and 0x8E. They replace the older warp-tour
packs of the same floors. Commands: `tools/prepare_story.py` (and
`--doors-only`) then `tools/prepare_tour.py` with `C3_FLOOR_SURVEY` for
`auto_00f` .. `auto_022`, `auto_078`, `auto_08e`.

## Engine fixes found here

- Native 97 is `fn_8018CA20` (people.c `peopleSetShadowVisible`): it shows
  or hides a person's shadow only. The port hid the whole person, so the
  windmill keeper (whose shadow the windmill init hides) disappeared.
- GSVM opcode 23 is `fn_800F16C0`, a developer-log printf; it opens town
  function 30. It now runs as a logged no-op.
- Native B5 (head aim) is called with three words by the windmill bank; the
  fourth is the zero of the register image.
- Natives 20 and 21 (`heroMoveInitEvent`, `heroMoveTermEvent`) set and clear
  the party's event flags and do not nest. Miror B.'s function starts an
  event, battles and returns without 21; the port counted the calls and the
  hero stayed frozen.
- Surveyed floors have no reviewed `door_objects`, so their door tables named
  no model: every Pyrite door was invisible while its CCD object still
  blocked. In Duking's house the closed bookshelf was an invisible wall in
  front of the dark doorway to 0x15, next to the lookout (playtest
  2026-11-20, defect 32). `prepare_story.py` now names each door row's map
  model (resourceId low 12 bits, map group `| 0x1000`) and
  `prepare_floor.py` makes those models object packs.
- Door-table close flags lock doors (`floorEventCtrlDoor` mode 0): the
  building's front door (town row 15) stays locked until `0x40A`, so the
  rooftop (town door 6) is how the chapter first reaches 4F.

Natives from other lanes this chapter needs: DB, BB, 3C pitch/roll and D8
mode 1 (under), the message control table and item names (systems), D0 and
native 11 key-item scripts (cipher, 59312a1; its own DB case is dropped in
favour of under's, which was claimed first).

## Shortcuts

| Area | Shortcut | Where recorded |
| --- | --- | --- |
| Gear on the console | The host route answers D0 with the gear; the console field scene does not enable the item menu yet (`c3_field_story_enable_item_menu`), so there D0 closes at once and the generator cannot be repaired | cipher lane's pause-menu work |
| Battles | encounters 169, 170, 171, 172 (Colosseum), 101, 126, 129, 164 play on the battle engine with `C3_ROUTE_BATTLES` (the route's `expect_battle` lines); without it they are reported as skipped | `route_battle ... played=1` |
| Optional trainers | sight-triggered trainers (town fn 18/25, building 2F/3F, cave) are not fought: the port has no trainer sight | not in the route |
| Start | the chapter starts from a Phenac save with `0x3F8` set; the Realgam visit that writes it belongs to the realgam lane | `route_shortcut flag=3f8` |
