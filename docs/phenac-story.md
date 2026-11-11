# Phenac City (floor 0x03) as a story floor

Phenac City's original field bank `013E0E00` now runs on the native story
adapter from the world-map arrival. This page records what the original
script does there, what the port executes, and every shortcut. It is host
evidence only: Phenac has not been drawn by the 3DS field scene yet (see
"Not done").

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
| Doors | Kind-2 links to `05960005` are skipped: native `60` and the door table are not ported | trace `door_open_event_unported` |
| Touch position | The touch query uses the position after wall correction; the original samples the uncorrected step | `platform/3ds/field_scene.c` |

## Not done

- The 3DS field scene cannot load Phenac yet: it needs 35 model packs and
  the stage renderer is bounded at 16 models.
- Natives `60` (doors), `77` (item gift), `B1` (walk up to a person), `D8`
  (party heal) and `E8` (follower step-aside) are unported; they belong to
  later Phenac events.
- Phenac's interiors (floors 4-13, `0x77`) have no packs or profiles, so
  every doorway except the three world-map exits leads nowhere yet.
- No audio, no partner follower, no physical New 3DS run of any of this.

## Regenerating the private packs

See the commands in the commit that added `tools/floor_profiles.py`:
`prepare_floor.py`, `prepare_area.py`, `prepare_story.py` with
`--floor phenac`, then `prepare_area_runtime.py`. The flat SD set is 39
files (the 38 from those tools plus `phenac_camera.c3c`).
