# Agate Village and Mt. Battle as story floors

The agate lane owns M3, Agate Village (floors 0x24-0x2E and the ceremony
set 0x385), and D2, Mt. Battle (0x4A-0x5F and the present room 0x8F).
Every walkable floor of both areas loads on Azahar, and the main-story
chapters of both areas run on the host with the original scripts, from
the Agate arrival to the flag that opens the Shadow Pokemon Lab. This page
records the route, what the scripts do, the natives ported for it and
every shortcut. The worked example this follows is
[phenac-story.md](phenac-story.md).

## Floors

All 33 walkable floors are surveyed floors (`tools/floor_survey.py`,
`f%03x` packs from `tools/prepare_tour.py`): map, people, doors, script
bank, messages and collision come from the original archives, and the
3DS runtime finds them through `src/floor_profile.c`'s generic rows. No
reviewed row in `tools/floor_profiles.py` was needed for the story.

| Floors | Archive | Doors (surface -> floor) |
| --- | --- | --- |
| 0x24 village | `M3_out` | 0 world map, 1 0x28, 2 0x29, 3 0x2B, 4 0x26, 5 0x2C (Eagun), 6 0x25 (Relic Cave), 7 0x2A (Pokemon Center), 8 0x2E |
| 0x25, 0x26 Relic Cave | `M3_cave_1F_1`, `_2` | 0x25: 0 village, 1 0x27 |
| 0x27 Relic Forest shrine | `M3_shrine_1F` | 0 cave; the Relic Stone ([relic-forest.md](relic-forest.md)) |
| 0x28-0x2E houses, PC, shop | `M3_houseA_1F` ... `M3_houseD_1F` | 0x2C door 1 leads to 0x2D (Eagun's 2F) |
| 0x4A outside | `D2_out` | 0 0x4B, 1 world map |
| 0x4B reception | `D2_pc_1F` | 1 0x4A, 2 0x4C |
| 0x4C-0x5E areas 1-10 and rest stops | `D2_valley_1` ... `D2_cloud_4` | each area's door 1 to the next rest stop, each rest stop's door 0 to the next area |
| 0x5F crater colosseum | `D2_crater_colo` | 0 0x5E |

Not walkable: 0x385 (`M3_shrine_1F_rl`) is Celebi's purification
ceremony, linked only from the purification menu (`floorLink(0x385, 0)`
in the relive menu) and skipped as recorded in relic-forest.md; 0x8F
(`D2_present_room`) has no script, collision or entrance, and no floor
bank or decomp code links to it. The `_bf` battle fields (0xD0-0xD2,
0xD8-0xDC, 0xE4) belong to the battles lane.

## The chapter route (host)

`tests/test_story_route.c` plays a route file with one game state that
persists across floors (talk, touch, door, stand in a main-loop box, read a
PDA mail, hold a direction) and follows each script's own floor change.
Every flag a script writes is reported. The Agate route
(`private/routes/agate1.txt`, private because it names the save) starts
from a copy of `pokemon-colosseum.3641.gci` with every flag the M3/D2 banks
write cleared (no supplied save sits inside these chapters; see
`private/lanes/saves/INDEX.md`). 60 steps pass:

| Step | Original behaviour (floor, function) | Flags written by the scripts |
| --- | --- | --- |
| Arrival from the world map | 0x24 fn 5: arrival camera and dialogue | 0x420, 0xAFE |
| Eagun's house | 0x2C fn 5 (main fn 4 while 0xA14 is clear): Eagun meets Wes | 0xA14 |
| Relic Cave, three boxes | 0x25 fn 13, 14, 12: three trainer events (encounters 195, 196, 197); the third trainer falls into view (native B2) | 0xA15, 0x421, 0x422, 0x423, 0x424, 0x425, 0x426, 0xAA5 |
| Before the Relic Stone | 0x27 fn 4 and 7 (box -21,40 .. 19,50): an event with encounters 525 and 543; the script then moves Wes to Eagun's house | 0xDDF, 0x428, 0x429, 0xAFA, 0xDC7 |
| Eagun again | 0x2C fn 7 | 0xA16 |
| House B | 0x29 fn 6 (talk to person 0) | 0x42A |
| Next to Eagun | 0x2C fn 9 (native AA segment -40,6 .. -15,6): item 502, party cure (D8 mode 1) | 0x42B |
| Next to Eagun again | 0x2C fn 10: mail 0 (native AD) | 0x42C, 0xB2C |
| Reading mail 0 | the PDA read transaction | 0xDCB |
| Away from Eagun | 0x2C fn 14: Mt. Battle opens on the world map | 0x42E, 0xB3B |
| Mt. Battle arrival | 0x4A fn 9 | 0x42F |
| End of area 1 | 0x4C fn 27 -> 73, 66, 72 (box -37,-1485 .. 38,-1481): encounter 544 | 0x9BF, 0xB38, 0xA76 |
| Area 1 talk | 0x4C person 9, talk script 01000010 | 0x431 |
| Leaving area 1 | 0x4C fn 15 (keeps control), carries Wes to the reception; 0x4B fn 10 gives item 537 | 0x432, 0x433 |
| Back in Agate | 0x24 fn 4 -> 19 (box -153,72 .. -102,78) | 0xDCE, 0x435 |
| Eagun after Mt. Battle | 0x2C talk (fn 8), then main fn 4 waits for a direction (native 6F) | 0x9C3, **0xDCF**: the Shadow Pokemon Lab opens |

0xA76 is the hand-off: the Pyrite banks (0x0F, 0x12, 0x14, 0x19-0x22)
read it for the next chapter, and 0xDCF opens the Lab (cipher lane).

The sweep route (`private/routes/agate-sweep.txt`, 174 steps) then visits
every Agate and Mt. Battle floor and talks to every present person: 150
conversations run to their end, 4 take an original cancel branch (below),
none fails. People hidden by the current flags (the Mt. Battle challenge
trainers of areas 1-10, the cave trainers after their battles) are reported
absent. No message uses an unsupported control code (that fails the
script in `src/story.c`).

```text
C3_ROUTE_PDA=<pda.c3pd> build/test_story_route <newgame dir> <boot dir> <save.gci> <route> [report] [trace]
```

## Natives ported for these floors

| Native | Original | Port |
| --- | --- | --- |
| B2 | `fn_801821B8` (peopleFallAppear): show, clear flag 8, drop from 40 units at 1.3 per 60 Hz frame with motion 7, then motion 8, then 1 | `W_FALL` in `src/field_story.c`; the sound 0x49E is recorded as unrendered |
| D8 mode 1 | `fn_801C852C`: remove conditions 3 and 4 (poison, bad poison) from the party | status entry at record +0xE8, as relive_purify.c clears 0x3E |
| 6F | `scriptIsMoveButtonPush`: D-pad held or stick past 2 | the tick's pressed directions, or a walking/running hero (the scene turns the stick into motion) |
| F6 | `fn_801140DC`: write game-data status 5/7/8, the saved floor, previous floor and spawn | `c3_game_state_set_floor`; the reception points it at itself |
| 3C | pitch and roll of a field person | yaw kept, the tilt recorded as unrendered (Wes at Eagun's after the shrine event, a few degrees) |

## Shortcuts

| Area | Shortcut | Where recorded |
| --- | --- | --- |
| Battles 195, 196, 197, 525, 543, 544 (story) and 203, 219 (optional) | skipped as a win (result 2) by the route; on the console the field scene offers "skip as win". The battles lane owns them | `route_battle ... skipped_as_result=1` |
| Chapter checkpoint | built from save 3641 by clearing the lane's flags, 0xDCB and 0x9E3; flags of later chapters stay as the save has them | route line 2 |
| Mail 0 | the checkpoint save holds it as read, so the read transaction is a no-op; the route writes the PDA pack's own read flag (0xDCB) | `route_mail_flag ... checkpoint_already_read=1` |
| Menus 54, 55, 56, 57 | party/rename/challenge menus are not ported: the original cancel branch (house D 0x2E persons 0 and 1, Mt. Battle reception person 1, Agate person 6) | `route_talk ... status=partial unported_menu=..` |
| Relic Stone ceremony 0x385 | skipped (relic-forest.md) | `ceremony_scene=unported` |
| Presentation | fall sound, actor tilt, head aim, emotes, audio | `story_presentation` trace lines |

## Azahar (2026-11-16, build `agate-probe1`, `C3_ROUNDTRIP_PROBE`)

The round-trip probe walked every door with the route's chapter-point
saves (`agate-arrival.gci` for M3, `mtbattle-arrival.gci` for D2) in five
runs of at most eleven areas (the probe's limit). Every floor ran its
load/init/pre scripts (`story_prepared floor=N` for 0x24-0x2E and
0x4A-0x5F), each door's original script chose the next floor and
entrance, `used_bytes` stayed at 2.0-2.2 MB after every area, and each
run ended `roundtrip_exit result=0`. The top-screen frame of each floor
shows its original 3D scene (traces and frames: lane-agate worktree
`private/evidence/`).

## SD files

The 561 files `boot/f024*` ... `boot/f02e*` and `boot/f04a*` ...
`boot/f05f*` already on the master Azahar SD (180 MB; the list is in the
lane's shared notes, `agate-sd-files.txt`). `boot/f027.c3s` must be the
151,825-byte pack with the purification messages. No pack changed in
this lane: the work is the engine and the route.

## Not done

- The Mt. Battle 100-trainer challenge (natives 56 and the reception's
  menus, area trainers shown while it runs) is not playable.
- Battles are skipped as wins (battles lane).
- The ceremony scene 0x385, audio, the partner follower and door sounds.
- The chapters were played on the host and the floors loaded on Azahar;
  the full chapter has not been played through on Azahar by input.
