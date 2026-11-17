# DNA disc puzzle and story mail (round-2 lane)

Two story systems the main route needs: the Shadow Pokemon Lab's DNA disc
door (native CC, floor 0x387) and receiving and reading P*DA story mail.
Both use the original rules and assets. Neither invents a flag.

## DNA disc puzzle

Source: `fn_801C8688` (native CC) in `vendor/colosseum/src/game/gs_range_801C766C.c`,
`fn_800884BC` / `gbaConvCheckIds` / `fn_80088428` in `src/game/gba/gba_conv.c`,
and the puzzle `fn_80087C64` in `menu/cardesavedata.c` (see
[realgam-story.md](realgam-story.md) for the screen).

| Step | Original | Port |
| --- | --- | --- |
| Disc check | Each item 0x200-0x211 maps to a species (pairs); all three must be on the 3x3 grid `lbl_8026F488`, else result 2 and no floor push | `src/field_story.c` native CC, the same table and order |
| Push floor 0x387 | `fn_800FF730(0x387)`, then the floor's main script runs the camera intro, the puzzle and the exit camera | CC waits with `view.menu_floor = 0x387` and `view.dna_species`; the field releases its models (as for a 3D battle) and runs `c3_dna_key_scene_run` (`platform/3ds/dna_key_scene.c`) on the top screen |
| Result | CC returns `result == 0` | `c3_field_story_dna_result(story, result)`; the PC's menu-floor return refuses to answer the puzzle |
| Flags | The Lab B2 door script writes `0x060` and `0xA4D` on 1, nothing on 0 | unchanged script |

The puzzle needs the floor-0x387 pack (`f387_obj0..11.c3t`, `f387_107c1800.c3c`,
`f387_107d1800.c3c`, `tools/prepare_dna_key.py`). Without it on the SD card the
field does not enable floor 0x387 and CC keeps the earlier recorded solve
(`story_menu_floor floor=387 status=unported_solved temporary=1`).

Screens: the puzzle is the original 3D grid on the 400x240 top screen
(D-pad moves, A picks, B undoes or cancels); the bottom screen keeps the field
UI. The 3x3 grid fits the 3DS top screen at the original camera.

## Story mail

Source: `mailMain.c` (`mailMainSendByScrpt`, `mailMainStartSe`,
`fn_801D2404`, `mailMainReceiveTerminate`, `chkMailSend`, `cbStep`),
`mail.c` (record getters), common script bank `0x596`.

How the original delivers story mail:

- **Scripts, not steps.** `cbStep` (a hero-step callback) and
  `mailMainSendAllMail` (debug menu) send mails whose record allows it, but
  every one of the 32 US records has send rate 0, so the step callback never
  sends one (`tests/test_mail_alert.c` checks this against the private pack).
- **Common function 21 (`0x05960015`) is the story dispatcher.** The exterior
  floors (Phenac, Pyrite, Agate, Outskirt, the Lab, Mt. Battle, the Under
  entrances, Snagem) call it; it sends mails 6, 8, C-F, 15-18, 1B and 1C when
  their story flags say so. Example: mail 0x0F goes out when `0xA7B` is set and
  `0xA7C` is clear; reading it sets `0xA7C`, which shows Snagem's 1F leaders.
  Function 18 (`0x05960012`) sends mail 0x0A. Floor banks send others directly
  (Snagem 1F mail 25, 2F mail 5, ...).
- **Arrival.** `mailMainSendByScrpt` (native AD) adds the mail once and, if no
  alert runs, plays the receive tone the player chose (mailbox `+0x443`) and
  blinks envelope `0x0C881200` for 600 frames (visible while `frames % 60 < 30`).
  A battle or floor change ends the alert.

Port:

| Piece | Where |
| --- | --- |
| Native AD delivery, `view.mail_received` counter | `src/field_story.c` |
| Alert state machine and `chkMailSend` | `src/mail_alert.c`, `include/colosseum3ds/mail_alert.h` |
| Tone | the P*DA ringtone player (`platform/3ds/pda_audio.c`), the saved tone |
| Envelope | the P*DA hub is the field's bottom screen (320x240): the original envelope at half size in the free right end of the header bar (`platform/3ds/ingame_menu.c`); pack image (6,5) from `common.fsys` (`tools/prepare_pda.py`) |
| Mail list, reading, read flags, hints, callbacks | existing P*DA menu ([ingame-pda.md](ingame-pda.md)) |

Shortcuts, recorded: the original rumble during the first 180 frames is not
reproduced (no rumble on 3DS). The envelope moved from the top-left of the TV
picture to the P*DA hub so it does not cover the 3D field.

## Host checks

| Check | Result |
| --- | --- |
| `build/test_field_story` | CC waits, species, results 0/1/2 (CC 1/0/0), invalid disc (no push), every disc pair, PC return refused; AD counts deliveries once, unread order, ID 512 fails |
| `build/test_mail_alert [pda.c3pd]` | 145 alert/send checks; with the private pack, 32 records, 0 step sends |
| `build/test_dna_key` | puzzle rules (realgam) |
| Lab crawl from 3641 (`test_story_crawl ... 41 0`) | B2 door: `crawl_dna species=9c,9f,99 presses=18 wrong_trio_resets=1 result=0`, then `0x060`, `0xA4D`; 63 flag changes as before. `C3_DNA_CANCEL_FIRST=1`: first visit result 1, no flags; the next visit solves it |
| Snagem crawl from the post-Pyrite checkpoint with `C3_CRAWL_PDA` and only `C3_CRAWL_FLAGS=a7b=1` (the world map's write) | mail 0x0F arrives at 0x88 and its read sets `0xA7C`; mails 7, 25, 5 too (`0xA2C`, `0xA88`); 1F leaders `0xA27`, `0xA28` follow; 75 flag changes |

The route player (`tests/test_story_route.c`) plays CC the same way
(`route_dna` lines) and already reads mail with `mail <id>`.

## Still open (other owners)

- The world-map script bank `01E00E00` (unowned) writes `0xA79`/`0xA7B`, the
  triggers for mails 0x0E and 0x0F. Until it runs, a route presets `0xA7B`.
- Native 62 never pushes floor 0x392 (P*DA) from a script; the bank scan finds
  only 0x391.
