# World map (floor 0x38E): native destination choice

After the first battle the original Outskirt Stand script sets flag `0x3DF`.
Leaving through the original exit surface (event kind 1, source 0) runs common
script `05960004` with floor `0x38E`, the world map. This page records what is
lowered from the pinned decomp and what is still the port's own presentation.

## From the pinned source

| Behaviour | Source | Native code |
| --- | --- | --- |
| Destination table (9 rows: unlock flag, floor, menu item, image), cursor links (66 rows), route cameras, info slots | `src/rel/worldmap_menu_rel/worldmap_menu_rel.c`, compiled unchanged and hash-pinned in `config/upstream-files.json` | `src/world_map.c` |
| An entry is selectable when its flag is zero or set | `gs_title.c`, destination menu before `menuOpenCustom(0xBD, ...)` | `c3_world_map_open` |
| The cursor starts on the entry of the previous floor; floor `0x7B` counts as `0x66` | `fn_800259B0`, same menu | `c3_world_map_open` |
| Cancel returns to the previous floor | same menu, `result < 0` | `c3_world_map_choose(-1)` |
| Entry `0x66` links to `0x7B` when flag `0x45D` is set | same menu | `c3_world_map_choose` |
| The journey plays unless the floor is unchanged or the pair is `0x7A`/`0x0F` | same menu, `change` | `travel` output |
| The destination is entered with `floorLink(floor, 0)`: entrance 0 | same menu, last statement | scene writes spawn 0 |
| D-pad follows the link table: LEFT/RIGHT/DOWN/UP are link bits 1/4/8/2, and a link matches when its mask contains every pressed direction | `fn_80024160` | `c3_world_map_move` |

The third word of each destination row is the menu item ID used by the link
table (`0x907` Outskirt Stand ... `0x919`), not a message ID as
[phenac-floors.md](phenac-floors.md) first assumed.

`tests/test_world_map.c` checks these rules against the compiled table (46
checks): only Outskirt Stand is selectable before `0x3DF`; with it set Phenac
City (floor 3) is the second entry and is reached with DOWN or RIGHT.
`tests/test_field_story.c` runs the original Outskirt exit and checks that it
requests floor `0x38E` with entrance 0 once the scene accepts that floor.

## Port presentation (not original)

`platform/3ds/world_map_scene.c` shows the selectable destinations as a
bottom-screen list. A row can be chosen with the stylus (touch a row, lift on
the same row) or with UP/DOWN and A; B is the menu's cancel.

- The list order is the table order. The lowered link navigation
  (`c3_world_map_move`) is tested but not used by the list; it is kept for the
  original map view.
- Destination names are the port's own English labels, chosen from the
  archive identity of each floor. The original menu strings and the twelve
  location images are not extracted yet.
- The original 3D Orre map (`0C6A0200`), the bike journey (menu `0xC3`), the
  route cameras and the info text slots are not drawn. The top screen keeps
  the last field frame. The trace records `journey_presented=0`.
- The world-map script bank `01E00E00` (init/pre/main/post, native `B4`) is
  not executed. Its function 6 only reads story flags `0x84E`-`0x890` and
  sets mail/event flags; this is a known gap for later story events.
- A destination without prepared packs is listed as `not prepared` and cannot
  be entered.

Nothing is written to the card on the world map; the chosen floor, previous
floor `0x38E` and entrance 0 are recorded in the live game state only.
