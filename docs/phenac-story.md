# Phenac City (floor 0x03) as a story floor

Phenac City's original field bank `013E0E00` now runs on the native story
adapter from the world-map arrival, on the host and on a New 3DS. This page
records what the original script does there, what the port executes, and
every shortcut. The console run is under "Hardware run, 2026-11-12".

## What runs (host, original script and packs)

`tests/test_phenac_story.c` creates a fresh game, presets the eleven flags
the original Outskirt scripts have written by then (each is observed being
written by those scripts in `tests/test_field_story.c`), links to floor 3
entrance 0 as the world map does, and ticks the production adapter with the
private packs. 7,230 checks pass.

| Step | Original behaviour | Evidence in the run |
| --- | --- | --- |
| Floor scripts start | init `01000001`, pre `01000002`, main `01000007`, each with the group ID as first argument | `story_prepared floor=3` |
| Arrival camera | main calls function 28: native `2A` plays camera member `0C101800` | pack `phenac_camera.c3c`, 661 samples |
| Arrival event | function 5: actors 2 and 3 (the Outskirt pair) with the sack, dialogue, then native `98(14,1,0)` | battle request 14 with flag `0x3E2` set |
| After the battle | the same function continues with the result: dialogue with actors 5 and 7, ends with flag `0x3E1` | flags `0x3E3`, `0x3E1`, control returns |
| Partner event | talking to the sack (actor 0, talk script `01000021`) inside x -8..10, z 314..340 sets bank global 7; main calls function 6 | `story_talk actor=0`, the reported name read back from the state, partner byte set, flags `0x3E4`, `0x3E5`, `0xACF` |

## Engine facts found while porting

- `gs_floor.c` starts every registered floor script (init, pre, main, post)
  with four arguments `(group, 0, 0, 0)`; the load script gets none.
  Outskirt's bank keeps the group in a bank global, Phenac's reads the
  argument.
- `people.c` `peopleFindSelf` falls back to the index in any group when the
  group does not match, so `(0, index)` reaches a floor actor.
- Script calls put their arguments below the return frame: the second count
  of opcode 4 is the number of values the caller pushed for the callee.
- Rui is a human cylinder like any collision-type-1 person: `hero_move.c`
  registers (0,101) with `fn_8018CB5C`, PeopleInfo row 2 has collision type
  1 and radius 4, and her visibility enables it. `GScolsys2HumanCollision`
  sweeps the hero's step in hero-radius slices. The port tested only the
  end of each step and skipped the partner, so after the sack event (Wes
  at (0, 323.75) facing Rui at (0, 313), both placed by native 3B) Wes ran
  straight through her (`test_phenac_events` partner row).
- Event kind 1 is a crossed surface (`fn_801101B4`), kind 2 a touched one
  (`GScolsys2ThruGetEventID`, sphere of the person's radius), kind 3 the
  A-button query; `hero_move.c` dispatches 3, 2, 1 in that order. Phenac has
  kinds 1 and 2 on the same surfaces: the touch opens a door (common script
  `05960005`), the crossing changes floor (`05960004`).

## Shortcuts in this state

| Area | Shortcut | Where recorded |
| --- | --- | --- |
| Battle 14 | Plays on the general battle engine like every story battle ([story-battles.md](story-battles.md)); no battle is skipped as a win | trace `field_battle_engine encounter=14 ... mode=played` |
| Partner name | Native `16` mode 1 waits while the field scene shows the port's own bottom-screen keyboard; the name goes to hero `+0xAC2`. The original name-entry floor `0x390` is not ported, and other modes keep the saved name | trace `name_entry_open`, `story_name_entry status=named` |
| Partner on the field | Person `(0,101)` has no model yet; natives addressed to it are the original no-op for a missing person. The save's partner byte (hero `+0xA98`) is written | `c3_game_state_partner` |
| Jingle wait | Native `82` blocks for the jingle length, which lives in the sound data; the port waits two seconds | trace `jingle_backend_pending_wait_2s` |
| Presentation natives | `7E` head tracking, `83` "!" emote, `89`/`DA` audio are recorded as unrendered | trace `story_presentation` |
| Gaze test | Native `A9` omits the original wall query between the person and the hero | comment at the native |
| Door sounds | Native `60` plays the door's clip and changes its flag and collision; the door sound is recorded as unrendered | trace `door_sound_backend_pending` |

## Doors and fences

- Each floor's door-table rows (`floor_event.c` FloorDoorEntry) are written
  by `prepare_story.py --doors-only` as `<prefix>.c3d` (C3D1) with the object
  pack of each door. Phenac's six doors are map models 0-5 (open clip 0,
  close clip 1), now object packs that rest until native `60` plays a clip;
  as looping map geometry they swung open and shut.
- The arrival is boxed in by CCD object 40 until the partner event sets flag
  `0x3E4` (function 2 and the partner event disable it with native `76`).
  The mayor's door is boxed by object 41, whose own touch surfaces (source
  11) run function 27; it sets flag `0x9A2` and removes the box.
- Touches use the step before wall correction, as `people.c` passes it to
  `GScolsys2ThruGetEventID`; with the corrected position the fence's touch
  surface was unreachable and the story stopped at the mayor's door.

## After the mayor (hardware, 2026-11-12)

| Step | Original behaviour | Port |
| --- | --- | --- |
| Mayor's welcome | mayor's-house bank `014E0E00` sets flag `0x9A4` | runs to the end on a New 3DS |
| Leaving the mayor's house | exit surfaces sit on CCD object 3 of a three-object walk/wall set | `c3_field_cover_objects` enables event-only objects; fixed |
| Stadium receptionist | stadium function 7 reads `0x9A0` with native `52` (GSflagGet), then sets `0x3E6` once `0x9A4` is set | native `52` lowered; `0x3E6` set on hardware |
| Back in town | Phenac main (function 7) runs function 4 once `0x3E6` is set: grunts confront Wes, a battle, then `0x3E7`; Rui suggests buying Poke Balls | plays on hardware; encounter 26 plays on the battle engine |

| Poke Mart | shop bank `01610E00` function 8 (a shopper at (-1.6, -5.0)) checks `0x413` and `0x3E7`, then sets `0x99E` and says Poke Balls are sold at the Outskirt Stand | plays on hardware (save 8) |

The stadium is reached through Phenac surface 5 (no door) at the top of the
north ramp; surface 4 (door 5) leads to floor 7, a house; surface 1 (door 2)
leads to the Poke Mart. The Mart clerk (function 6) opens shop 3 with native `19`.

## Shops (native 19)

`menuShopOpen` stores the shop number and links to menu floor `0x38F`
(`pocket_menu.fsys`). That archive's REL module 2 publishes, from its
_prolog, the 4-byte shop entries (`lbl_80478E54`: u8, u8 kind, u16 offset)
and the 0-terminated u16 item lists (`lbl_80478E44`) the shop menu reads.
`tools/prepare_shop.py` writes them to a private `shops.c3h`; prices are the
item records' bytes 6-7 (Poke Ball 200, Potion 300). Shop 3, the Phenac
Mart, sells Potion, Super Potion and the status cures, and no Poke Balls,
which is what its shopper says. Shops 1 and 2 include Poke Balls.

The field story waits on native 19 with `view.shop`; the field scene shows
the port's own bottom-screen menu (the original floor `0x38F` is not
ported): the stock with prices and money, a quantity up to what the money
covers, and a result. A purchase checks money, then bag space, and a
refusal changes nothing. On a New 3DS (2026-11-12) the clerk opened the
menu with the original stock; three Potions cost 900 (10000 to 9100), the
bag showed Potion x8 (5 from the new game plus 3) and the clerk's script
resumed when the shop was left.

Selling follows the original sell path (`fn_800181C4`): an item whose price
is 0 cannot be sold; otherwise each brings price / 2, credited with
`heroAddPokedoru`. The menu opens on BUY / SELL / QUIT; SELL lists the bag
(not the key-item pocket) with what each item brings, and key or important
items are refused by the bag's toss rules. The port stops the quantity where
the money would pass 9,999,999. On the console two Potions sold for 300
(10000 to 10300) and the stack went from 5 to 3. The prize-coupon counters
(kinds 1-4) are not ported.

Next stop in the original: back to the Outskirt Stand through the world
map, to buy Poke Balls.

Memory: with six door object packs each carrying the 1024x1024 town atlas,
Phenac needed 90.6 MB of the 94.8 MB heap and a second load in one session
failed on fragmentation. Object packs now crop each mesh's texture to the
region it samples (`crop_textures_to_use`); two loads in one session then
succeed on hardware.

## Every Phenac floor (2026-11-16)

| Floor | Area | Packs | Reached from | Its exits |
| --- | --- | --- | --- | --- |
| 0x03 | Phenac City | `phenac*` | world map, every interior | sources 0 → 0x04, 2 → 0x05, 4 → 0x07, 9 → 0x08, 1 → 0x09, 3 → 0x0B, 5 → 0x0D; 6, 7, 8 → world map |
| 0x04 | Pokemon Center 1F | `phenac_pc1f*` | town source 0 | 4 → town entrance 1; 5 → 0x77 |
| 0x77 | Pokemon Center B1 | `phenac_pcb1*` | PC 1F source 5 | 3 → PC 1F entrance 1 |
| 0x05 | Mayor's house 1F | `phenac_mayor1f*` | town source 2 | 3 → town entrance 6; 4 (stairs) → 0x06 |
| 0x06 | Mayor's house 2F | `phenac_mayor2f*` | 1F stairs | 1 → 1F entrance 1 |
| 0x07 | House B | `phenac_houseb1f*` | town source 4 | 2 → town entrance 5 |
| 0x08 | House C | `phenac_housec1f*` | town source 9 | 2 → town entrance 2 |
| 0x09 | Poke Mart 1F | `phenac_shop1f*` | town source 1 | 0 → town entrance 4; 1 → 0x0A |
| 0x0A | Poke Mart 2F | `phenac_shop2f*` | Mart 1F source 1 | 0 → 1F entrance 1 |
| 0x0B | Pre Gym 1F | `phenac_pregym1f*` | town source 3 | 5 → town entrance 3; elevator touch 4 → 0x0C |
| 0x0C | Pre Gym B1 | `phenac_pregymb1*` | elevator | elevator touch 0 → 0x0B |
| 0x0D | Stadium | `phenac_stadium1f*` | town source 5 | 4 → town entrance 7 |
| 0x80 | Card room (`M1_carde_1`) | `f080*` (survey packs) | no door or script on this disc | 1 → stadium entrance 2 |

0x0E (`M1_water_colo`) and 0xCA-0xCC (`M1_out_bf`, `M1_gym_1F_bf`,
`M1_houseA_1F_bf`) have no collision and no field scripts: they are battle
stages (the Phenac Colosseum and battle fields), owned by the battles lane.

On Azahar (New 3DS mode, probe build `C3_ROUNDTRIP_PROBE`), one route walks
all 22 links above from three saves: `35188` (just after Rui joined; the
mayor's welcome plays on the way), `15310` (after Pyrite) and `27046`
(every Phenac flag). Every link lands on the entrance in the table and every
floor reaches control. The card room was loaded directly (`start 80`) and
its exit reached the stadium. Route: `probe-route.txt` in the commit that
added touch steps; frames and traces are private (`private/run-*`).

The Pre Gym elevator is common script `05960006` (native C5 animations and
61, both recorded as unrendered presentation), then a floor link; it no
longer stops.

## Doorways into floors that are not on the SD card

A floor link (native 5F/7D) to a floor whose packs (`.c3f`, `.c3a`, `.c3s`,
`_wes.c3t`, `_map0.c3t`) are missing shows "Area N is not prepared in this
build. Press A to stay here." A resumes the door script without linking: the
common door script `05960004` is native 7D followed by a return, so the event
ends and the hero keeps control on the same floor (trace
`story_floor_unprepared`, then `story_floor_refused ... stayed=1`). START or X
still return to the main menu. The console's "Area 6" stop at the mayor's
stairs matches this notice, which appears only when one of floor 6's core
packs is missing from the SD card; with the packs present the stairs link
on Azahar.

## Story events across the game (host)

`tests/test_phenac_events.c` loads a chapter save, sets or clears the story
flags for one point of the story, enters a floor and runs the original
scripts with the prepared packs. A row passes only when the scripts write
the expected flags and request exactly the expected encounters.

| Point | Floor, trigger | Original result |
| --- | --- | --- |
| First visit | 0x05 arrival | mayor's welcome, `0x9A4` |
| | 0x0D, receptionist (talk `01000007`) | `0x3E6` |
| | 0x03 arrival from the stadium | grunts confront Wes: encounter 26, `0x3E7`, `0xB10` |
| | 0x09 / 0x0A shoppers | `0x99E` (Poke Balls are sold at the Outskirt Stand) |
| Back with Poke Balls (`0x3E8`, Outskirt shop) | 0x03 arrival | function 20, `0x3E9` |
| | 0x05 arrival | the mayor's-house ambush: encounters 15 and 17, `0x3EA`, `0x9B1`-`0x9B4` |
| | 0x03, trainer (talk `0100000C`) | encounter 20, `0x3EE` |
| After Pyrite (`0x3F9`, `0x413`) | 0x06, person (talk `01000005`) | dialogue only |
| | 0x0B, challenge (talk `0100000C`) | `0x3F1`, `0x995` |
| | 0x0B, standing in the ring | four trainers: encounters 22-25, `0x3F2`-`0x3F7`, `0x48B` |
| | 0x0B, ring with `0x41D` | Justy: encounter 36, `0x41E`, `0x41F`, `0x48C`, `0xDDD` |
| Later (`0x478`) | 0x05, talks `01000014` / `01000015` | encounters 16 and 19, `0x47C`-`0x47F` |
| | 0x03 arrival with `0x3EC` | function 26, `0xA59` |
| | 0x03, talks `01000016`-`01000018` (`0x84F`-`0x851`) | encounters 532-534, `0x3EC`, `0x9D2`-`0x9D4` |
| | 0x0D, receptionist | `0x9B7` |

Arrival, the partner event and trainer 15 (encounter 21, approached by
gaze) are in `tests/test_phenac_story.c`. With `C3_ROUTE_BATTLES` set,
`test_phenac_events` plays every encounter above on the battle engine with
the original trainers and teams and checks each event's list
([story-battles.md](story-battles.md#chapter-routes-with-real-battles)).
The Phenac Colosseum tournament (stadium function 4, flags `0xB12`-`0xB16`)
plays each round the stadium script requests.

The same test talks to every visible person on every Phenac floor from the
saves `35188`, `15310` and `27046`: 155 conversations end and give control
back, and no message uses an unknown control. One gap remains: the card
room's dealer (talk `01000008`) loops, because select menu C7 takes its
first row ("read a card") and the card menu floor 0x393 returns at once.
The Pre Gym's rank messages use U+2605 (a star), drawn by the original
font's extended glyphs.

## Not done

- No audio, no partner follower, no door sounds.
- Every Phenac battle plays on the general battle engine (story-battles.md).
- Native C7 (select menu) takes the first row and native EB (save menu)
  reports "not saved": the card dealer loops and the PC B1 attendant's save
  is not offered (systems lane).
- The saved partner name defaults to "N/A" on the naming keyboard (the
  original name entry is the systems lane's).
- Phenac door 3's object pack is not cropped (its coordinates leave the
  image).

"Rui became a partner!" no longer ends in question marks: the three
full-width spaces after it print as spaces (`src/story.c`).

## Hardware run, 2026-11-12 (build 57d11bd, New 3DS)

From a save moved to floor 3: the arrival event and the skipped battle 14,
talking to the sack, naming the partner on the keyboard ("Rui"), the
partner joining, then a planned walk (stick legs searched on the host with
`c3_field_move` and the touch query against the prepared collision) to the
fence at the mayor's door. Its event played, the door opened and the
mayor's house (floor 5) loaded with the mayor's welcome. Phenac ran at about
28 fps with GPU-skinned people and GPU-native textures.

## Regenerating the private packs

See the commands in the commit that added `tools/floor_profiles.py`:
`prepare_floor.py`, `prepare_area.py`, `prepare_story.py` with
`--floor phenac`, then `prepare_area_runtime.py`. The flat SD set is 39
files (the 38 from those tools plus `phenac_camera.c3c`).

## Hardware playtest, 2026-11-11 (build 01051e8, New 3DS)

The owner played Continue → shop → Willie → world map → cancel → Outskirt.
The trace (`boot-trace.log`, private) shows `preview_exit outcome=2`,
`story_battle_result encounter=10 result=2 native98_resumed=1`, native 67,
the script's own writes of flags `0x3DE`, `0xA5A`, `0x3DF`, the exit door,
`world_map_begin visible=2`, cancel and a clean return to Outskirt Stand.
Battle taps, cameras, animations and move sounds worked.

Fixed in 729dbda: PRESS START ignored taps; main-menu D-pad did not follow
the drawn grid; menus ignored the Circle Pad.

Open items from the playtest:

| Item | Note |
| --- | --- |
| Field movement is jumpy | frame rate; renderer work in progress |
| Battle load shows a debug console for a while | battle load time and loading screen |
| No background music in field or battle | BGM backend not ported |
| World map is a text list | needs the original 3D map, markers, journey and location art from `world_map.fsys` |
| World-map cancel reloads the area | the original also relinks the floor; avoid the reload by keeping the loaded area across the world map |
