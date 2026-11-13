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
- Event kind 1 is a crossed surface (`fn_801101B4`), kind 2 a touched one
  (`GScolsys2ThruGetEventID`, sphere of the person's radius), kind 3 the
  A-button query; `hero_move.c` dispatches 3, 2, 1 in that order. Phenac has
  kinds 1 and 2 on the same surfaces: the touch opens a door (common script
  `05960005`), the crossing changes floor (`05960004`).

## Shortcuts in this state

| Area | Shortcut | Where recorded |
| --- | --- | --- |
| Battle 14 | No battle scene exists for it. The field scene offers "skip as a win": result ID 2 is reported and the script continues. Nothing is written to the party | trace `field_battle_skipped ... temporary=1` |
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
| Back in town | Phenac main (function 7) runs function 4 once `0x3E6` is set: grunts confront Wes, a battle, then `0x3E7`; Rui suggests buying Poke Balls | plays on hardware; the battle is skipped as a win (shortcut above) |

The stadium is reached through Phenac surface 5 (no door) at the top of the
north ramp; surface 4 (door 5) leads to floor 7, a house. Next stop in the
original: the Poke Mart (floor 9, `M1_shop_1F.fsys`).

Memory: with six door object packs each carrying the 1024x1024 town atlas,
Phenac needed 90.6 MB of the 94.8 MB heap and a second load in one session
failed on fragmentation. Object packs now crop each mesh's texture to the
region it samples (`crop_textures_to_use`); two loads in one session then
succeed on hardware.

## Not done

- No audio, no partner follower, no door sounds.
- Battle 14 is still skipped as a win (above).
- "Rui became a partner!" is followed by question marks: a message control
  code the field text does not interpret yet.
- The saved partner name defaults to "N/A" on the naming keyboard.
- Most Phenac interiors have not been entered on hardware yet; the mayor's
  house and the stadium have. A doorway into an area that is not prepared
  leaves only the main menu as a way out.
- Phenac door 3's object pack is not cropped (its coordinates leave the
  image); the Pre Gym (floor 0x0B) stops on unported native 61.

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
