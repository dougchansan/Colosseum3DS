# Shadow Pokemon Lab and Snagem Hideout as story floors (cipher lane)

Floors: D1 Shadow Pokemon Lab, the exterior 0x41, the lab 1F to B3
(0x42-0x45), the garage (0x46-0x49) and its event sets 0x8C/0x8D; S2 Snagem
Hideout, the exterior 0x88 and the building 1F to 3F (0x84-0x86). 0x81-0x83,
0x87 and 0x89 are the opening's cutscene sets (no collision, no entrance
table; see [world-map.md](world-map.md)); 0xC9, 0xD6 and 0xD7 are battle
fields (battles lane).

## Where these chapters sit in the original

| Chapter | Start | Unlock | Save |
| --- | --- | --- | --- |
| Shadow Pokemon Lab | Agate house C sets `0xDCF` (Lab on the world map) | world-map row 3 | `pokemon-colosseum.3641.gci` (Agate PC, `0xDCC` set, no Lab flags) |
| Snagem Hideout | post-game: game cleared (`0x478`), Pyrite guild 0x15 sets `0xADD`, Pyrite person 0's rematch (encounter 444) sets `0xADE`, `0xADF` | world-map row 2 (`0xADF`) | `pokemon-colosseum.5007.gci` (post-game, after the Lab, before Snagem) |

Both saves are listed in the private `saves/INDEX.md`.

## Host story checks

`tests/test_story_crawl.c` plays the original scripts from a save with one
persistent game state: every person, touch, A-button surface, treasure,
the boxes the floor's main loop tests the hero against (native 3A) and the
spots in a trainer's line of sight (native A9), then the next door, and
follows the floor each script requests. Battles are skipped as wins and
logged, every text and flag change is written. Without arguments it checks
nothing private.

```text
test_story_crawl /boot <boot> pokemon-colosseum.3641.gci 41 0 lab.txt lab-after.gci 900
test_story_crawl /boot <boot> pokemon-colosseum.5007.gci f 0 pyrite.txt pyrite-after.gci 400
C3_CRAWL_FLAGS=a7c=1 test_story_crawl /boot <boot> pyrite-after.gci 88 0 snagem.txt snagem-after.gci 1500
C3_CRAWL_FLAGS=850=1 test_story_crawl /boot <boot> snagem-after.gci 88 0 snagem2.txt snagem-done.gci 300
```

`<boot>` holds `prepare_story.py`, `prepare_world.py` and
`prepare_story.py --doors-only` output for the floors (auto profiles with
`C3_FLOOR_SURVEY`); the Pyrite run also needs 0x0F, 0x14 and 0x15.

| Run | Result |
| --- | --- |
| Lab from 3641 | 17 battles (skipped), 63 flag changes. Every Lab story flag matches the post-Lab save 5007 (`0xA4C`-`0xA56`, `0xAE4`-`0xAE7`, `0x451`-`0x453`, `0xDCD`, `0xDD2`, `0x45B`, `0x45C`, `0xB22`, `0x457`) except door flag `0x63` and the optional B3 trainer `0xABF`, which that player skipped |
| Pyrite from 5007 | guild talk sets `0xADD`; the rematch (encounter 444, skipped) sets `0xADE`, `0xADF` |
| Snagem | 24 battles (skipped); `0xA27`, `0xA28` (1F), `0xDE5`, `0xA29`, `0xA2A` (2F), `0xA82`, `0xA83` (3F), `0xA86`, `0xB00`-`0xB02`, `0xA87` match `completed.gci` |

The Lab's own order, as the crawl plays it: the 1F switch (`0xA8C`), the
garage key `0x1FE`/`0x1FD` treasures, the exterior key slot (pause menu
Items, key `0x1FD` -> door `0x53`, three random DNA discs written to
`0x459`/`0x45A`/`0xAA1`), the disc treasures on B1/B3, the B1 disc machines
(native CD), the B2 disc door (native CC, `0xA4D`), the B2/B3 rooms
(native 3A boxes) and Ein (`0xA50`-`0xA54`).

## Engine pieces this lane added

- Treasure (`<prefix>.c3r`, `prepare_story.py --doors-only`): the common
  REL125 table (data+0x11BFAC, 0x1C rows: kind, gift type, count, rotation,
  taken flag, show flag, item, position). A in front of one not taken runs
  floorEventCtrlTresure(2) / floorEventGetTresure with its messages; native
  CB's row changes last for the session (common REL memory); native D6
  shows or hides a row. The treasure models (people info `0x0377`-`0x0379`)
  are not drawn yet.
- Key items: native 11 registers item -> script; native D0 opens the pause
  menu on Items after running the kind-4 events of the surfaces the hero is
  on; the used item's script runs with the item as its first argument
  after the event. The field scene shows the port's list of registered
  items the bag holds (A uses, B closes).
- DNA discs: natives CC (species grid check), CD (hologram, used disc via
  the under lane's signed item service), CF.
- Natives F3 (Shadow Pokemon table record, recorded only), BE/BF/C1 (light
  animations, unrendered), msgctrl 4E (species name), native 62 stub.
- `prepare_field_geometry.py`: linear HSD path splines (Lab B3).

Taken from other lanes instead of a second version: under 6bd8fa4 (native
DB), pyrite c4ce881 (native 97) and f96aa22 (print opcode), agate 7edd217
(native B2). The native 62 stub goes when systems fba85ea (PR 18) merges.

## Shortcuts and dependencies

| Item | Shortcut | Owner |
| --- | --- | --- |
| Battles | skip as win (result 2), nothing written to the party | battles |
| DNA puzzle floor 0x387 | native CC waits for the original puzzle (`c3_dna_key_scene_run`); only without the floor-0x387 pack is it taken as solved, see [dna-mail.md](dna-mail.md) | done (dna-mail) |
| World-map script bank `01E00E00` | not run: it sets `0xA55` (after `0xA54`), `0xA79`/`0xA7B`, `0xDFC`/`0xDFD` (from `0x884`/`0x885`), `0xA85` (from `0xB01`) | unowned |
| Mail `0x0F` | the exterior's dispatcher (common `0x05960015`) sends it once `0xA7B` is set; reading it sets `0xA7C`. The crawl reads mail with `C3_CRAWL_PDA`, so only the world map's `0xA7B` is preset, see [dna-mail.md](dna-mail.md) | world map (`0xA7B`) |
| Shadow Pokemon row 3 purified (`0x850`) | gates the Snagem exterior's last scene (`0xB01`, `0xA87`); preset in the crawl | battles / Relic Stone |
| PC box (native 62 on garage 1F) | returns at once | systems |
| PDA notes (native F3) | recorded only | systems |
| Treasure and DNA hologram models | not drawn | cipher |

## Azahar (probe build cipher-probe1, New 3DS mode)

Lane copy `G:\Colosseum3DS-emu\lane-cipher`, round-trip probe with route
files, saves 3641 (Lab) and the crawl's Pyrite result (Snagem). Each floor
reached control (`story_prepared`, `field_load_to_control`) with its
treasure rows loaded; frames in the private evidence folder.

| Run | Route | Floors |
| --- | --- | --- |
| lab1 | start 41; doors 41:5, 42:2, 41:4, 46:1, 47:1, 48:1, 49:2 | 0x41, 0x42, 0x46, 0x47, 0x48, 0x49 |
| lab2 | start 43 | 0x43 |
| lab3 | start 44; doors 44:3, 45:8 | 0x44, 0x45 |
| snagem | start 88; doors 88:1, 84:5, 85:0, 86:0, 85:1, 84:9 | 0x88, 0x84, 0x85, 0x86 |

## SD files

New or changed in `sdmc:/3ds/Colosseum3DS/boot/`: door packs `.c3d` and
treasure packs `.c3r` for 0x41-0x49 and 0x84-0x88, and the B3 set
`f045.c3a`, `f045.c3f`, `f045_*.c3t` (prepared with the linear spline
support, compressed with `compress_packs.py --codec zlib --max-texture 512`).
The other floor packs already on the SD are unchanged.
