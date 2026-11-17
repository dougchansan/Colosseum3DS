# Story battles end to end (battles lane)

Every story battle request (GSVM native 98 or 66) now plays as a real
battle on the general engine (`src/battle_general.c`), in 3D on its original
battle floor when the prepared stage covers it (all 557 admitted encounters
do, [general-3d-battles.md](general-3d-battles.md)). The result goes back
to the waiting script with the original result ID, and the party, money,
bag and story flags keep what happened. This page lists what follows the
original, where it comes from, and what is still approximate.

## Flow

1. The field (`platform/3ds/field_scene.c`, `play_story_battle`) loads
   `battles.c3x`, builds the battle from the save's party, fills the Snag
   Machine's balls from the bag (`c3_battle_story_prepare`) and runs the 3D
   scene (the bottom-screen scene if the 3D stage fails).
2. `c3_battle_story_apply` (`src/battle_story.c`) writes the result back
   (below) and resumes native 98 with result 2 (hero won) or 3 (hero lost),
   or runs the whiteout.
3. Traces: `field_battle_engine ... mode=played result=1 code=<2|3>`,
   `field_battle_writeback ...` and one `field_battle_snag ...` per snag.

No battle is skipped as a win any more. When the field cannot play the
waiting encounter (no `battles.c3x` on the SD card, or the pack refuses the
encounter), `c3_field_story_battle_refuse` leaves native 98 waiting, the
bottom screen names the encounter and the reason ("Battle N cannot be
played in this build ..."), START quits, and the trace has
`field_battle_refused encounter=N reason=...`. Nothing is written and no
result is invented. The old shortcut (`field_battle_skipped`, result 2)
exists only in debug builds made with
`C3_EXTRA_CFLAGS=-DC3_STORY_BATTLE_SKIP_REFUSED`; the warp tour, campaign
and final-event probe builds keep their own documented auto-results.

Of the 277 encounters the prepared story scripts request with a literal ID
(369 call sites, scanned with `prepare_battle_encounters.py --stories`),
all 277 are admitted with pack v3, including the CPU-vs-CPU encounter 525.

## Chapter routes with real battles

The headless chapter harnesses play every battle on the general engine,
the same way the field does, when `C3_ROUTE_BATTLES=<battles.c3x>` is set
(`tests/route_battles.c`): the original encounter row, trainer and team
from the pack, both sides on the engine's policy, then
`c3_battle_story_apply` (writeback, EXP, prize, snagging, whiteout). A lost
battle is replayed with a healed party and the next seed until the hero
wins, as a player who whites out and comes back would (`attempts=` in the
report); `C3_ROUTE_BATTLE_MODE=any` applies the first outcome instead. A
refused encounter fails the run unless it is declared in
`C3_ROUTE_ENGINE_GAPS`. `tools/run_story_battle_routes.sh` runs every
region and checks each one's expected encounters
(`C3_ROUTE_EXPECT_BATTLES`, or `expect_battle` lines in a route file).

2026-11-16 run (pack v2, chapter saves from `private/lanes/saves`): 62
battles played, none skipped.

| Region (harness) | Encounters played on the engine |
| --- | --- |
| Phenac (`test_phenac_events`, 19 events from four saves) | 26 (Snagem grunts), 15, 17 (mayor's-house ambush), 20, 21 (town trainers), 22-25 and 36 (Pre Gym, Justy), 16, 19 (late mayor talks), 532-534 (late town trainers) |
| Agate and Mt. Battle (`test_story_route`, Agate route) | 195, 196, 197 (Relic Cave), 543 (Relic Forest), 544 (Dakim, Mt. Battle area 1), and 525 (CPU-vs-CPU with pack v3) |
| Pyrite (`test_story_route`, `tests/routes/pyrite.route`) | 169-172 (Pyrite Colosseum), 101, 126, 129, 164 (Miror B.) |
| Shadow Pokemon Lab, Pyrite revisit, Snagem Hideout (`test_story_crawl`) | 381, 382, 383, 386, 389, 397, 398, 401, 402, 405 (Ein), 392, 393, 394, 200; 55; 475 |
| The Under (`tools/run_under_chapter.py`, `tests/routes/under-venus.txt`) | 328 (Venus), 333, 336, 339, 342; the recipe then stops at the Subway Key pickup (under-2 lane) |

Realgam Tower's chapter route belongs to the realgam-run lane; its harness
can call the same helper.

## Fight kind rules (FightKindData)

Each encounter's fight kind row (`fight.c` FightKindData, pack entry
+88) now gates the result as the original does:

| Flag | Original use | Port |
| --- | --- | --- |
| `backSaveDataFlag` (+0) | `heroBiosCopy(save, hero)` after the battle (`fight_action.c`) | without it nothing is written: party, money, bag (kind 4, the Snagem intro battles) |
| `getExpFlag` (+8) | experience and the post-battle evolution check | no experience or effort without it |
| `getOkaneFlag` (+9) | prize money | no prize without it (Mt. Battle kinds 8 and 9) |
| `doCriticalAttackFlag` (+14) | critical hits | none without it (colosseum kinds 3, 5, 6, 7, Mt. Battle 8, 9) |

`okanePoolFlag` (+10, the colosseum kinds) has no reader in the decompiled
fight code; the prize is paid per battle.

## Party writeback

`c3_general_battle_writeback` copies back, per hero Pokemon that entered:

- HP and each move's PP;
- the persistent condition (record +0x64: 3 poison, 4 bad poison,
  5 paralysis, 6 burn, 7 freeze, 8 sleep, the bag's cure-mask numbering);
  a fainted Pokemon keeps none. A condition also enters the battle from
  the record (sleep with two turns left: the record's counter is not
  identified);
- experience (Shadow Pokemon bank it in +0xE0 instead), levels with stats
  recomputed, and each level's getWaza moves into a free slot (the move a
  full moveset needs is offered through the bound forget prompt; headless
  callers without a prompt report it in `forget_prompt_skipped`);
- effort: each fainted foe's species giveEffort (+0x90) to every
  non-Shadow Pokemon that faced it, capped at 255 a stat and 510 in all;
- prize money on a win (trainer kind base x the highest foe level).

The private sweep checks these invariants on every admitted encounter.

## Loss and whiteout

A loss with the encounter's zenmetu flag follows `fn_8020DAD0`: money
halved, flag `0x0E05` set, every party member restored (`fn_801D0AFC(1)`,
the story's party service), then `fn_80113FE8`: the point native A1 left,
else the last save's floor, previous floor and entrance
(`gamedatasaveGetStatus` 5/7/8, kept by `c3_game_state_save_point` apart
from the hero's current floor). The floor is left through the normal
departure; the trace shows `story_battle_whiteout floor=... source=...
flag=0e05 fade_script=05960008`. A loss without the zenmetu flag resumes the
script with result 3, as the original does.

## Snagging

- Command: the bottom-screen page offers the balls (X) when the bag has
  any. A ball goes only to a Shadow Pokemon of the opposing trainer; the
  turn order puts it with switches, before moves.
- Catch: `fn_80213A78` exactly: the Shadow table's catch rate (pack v2,
  common_rel 0x1439D8), ball multipliers in tenths (Ultra 20, Great 15,
  Net 30 on Water/Bug, Nest 40-level, Timer turns+10 up to 40, others 10),
  `(rate*mult/10)*(3*max-2*hp)/(3*max)`, x2 asleep or frozen, x1.5 for the
  other conditions, a catch at 255 or more, the Master Ball and encounter
  0x11 always, else four shake checks against
  `0xFFFF0/sqrt(sqrt(0xFF0000/odds))` on the battle RNG.
- A snagged Pokemon leaves the battle without experience; the trainer sends
  the next one, and snagging the last one wins.
- Result: the story apply step builds the PokemonBios
  (`c3_general_snag_record`), with `pokemonSetDarkPokemonStatus`'s Shadow
  state (data ID +0xD8, heart gauge +0xDC = the save's Shadow runtime table
  value at body+0x1B014+id*0x18+0x4A6, times 100), adds it to the first
  empty party slot or the first free PC box entry (`pcboxAddPokemon`: body
  +0xB88, three boxes 0x24A4 apart, restored), sets the Shadow row's snag
  flag (+0x12, the one the P*DA reads) and takes the balls from the bag.

Approximations in the record: the personality is drawn until it gives the
trainer row's nature (gender and ability fit are not drawn as
`pokemonCreate` does), the nickname is the pack's ASCII species name in
GSchar, the attest bytes are copied from the hero's first record, and
condition entry 0x3E (`fn_8011B950`) is not added. Hyper Mode
(`darkpokemonHypermodeFlag`) is modelled in battle for the hero's Shadow
Pokemon ([battle-rules-lane.md](battle-rules-lane.md)).

## Colosseum and Mt. Battle runs

The story colosseums (Phenac Stadium kinds 6/7 on battle floor 33, Pyrite
Colosseum kinds 3/5 on 34, Realgam kinds on 35/36, Mt. Battle kinds 8/9 on
24) are run by their floor scripts: each round is its own native 98 request
with dialogue and healing between rounds. Each round now plays with its
kind's rules (no critical hits; Mt. Battle pays no money). Rounds whose
encounter is computed at run time (33 sites, mostly Mt. Battle `f07d` and
the stadium) are played whatever ID the script passes.

## Checks

- `make -f tests/battle-general.mk battle-general-check`: synthetic fight
  kind rules, effort, level-up moves, conditions, snagging (forced and
  1-in-16 Poke Ball odds), record construction.
- Private sweep: `build/test_battle_general <battles.c3x> <newgame dir>
  <save.gci>` plays every admitted encounter to a result and checks each
  writeback, then replays every encounter with a Shadow Pokemon throwing
  Ultra Balls, builds each snag's record and stores it in the save.
  2026-11-16 (pack v2): 557 encounters each from the completed save
  (557 won), the Under chapter save 3641 (372 won, 184 lost, 1 draw) and the
  early Phenac save 35188 (185 won, 371 lost, 1 draw); snag pass 106, 100
  and 84 of the 110 Shadow encounters snagged.
- `build/test_field_story`: whiteout refusal without the flag table and the
  save point.
- `build/test_world_story <newgame dir> <world packs> <save> <report>
  battles.c3x`: every prepared floor's original scripts with real battles.
  From the early save 35188: 73 battles (21 won, 52 lost); every lost wipeout
  battle restored the party and left for floor 0x004, the Pokemon Center
  the save was made in. From the Under save 3641: 67 battles (49 won, 18
  lost). The only failed events are the existing unported natives (74, DB)
  and one native-33 argument count.

## Azahar runs from chapter saves (2026-11-16)

Probe build (`C3_EXTRA_CFLAGS=-DC3_ROUNDTRIP_PROBE`): the field accepts a
waiting story battle and the battle plays unattended, the hero throwing its
best ball at a Shadow Pokemon it may snag. `probe-route.txt` gives the
start floor and the story point (`flag`, `ball` lines change only the
probe's in-memory save). Traces and frames stay private
(`private/evidence/<run>/`).

| Save (copy) | Route | Battles | Result |
| --- | --- | --- | --- |
| 35188 (early Phenac, Lv25-27) | `start 3`, flag 0x3E6, 5 Poke Balls | 26 (Team Snagem Wakin, battle floor 7) | 3D, won in 4 turns, 1,252 EXP and a level, prize 135; the script then sets 0x3E7 |
| 35188 | `start 5`, flags 0x3E6-0x3E9, 5 Poke Balls | 15 (Folly), 17 (Trudly, battle floor 8) | 3D, both won; Makuhita (Shadow ID 1) snagged with a Poke Ball into party slot 2 |

The first mayor's-house run fell back to the bottom-screen scene: four
battle Pokemon packs (pkx308, 332, 370, 390) used raw HSD map modes the title
loader refused. `src/title.c` now loads REPLACE (5) as mode 0, which draws
it, and ALPHA_MASK (1) as MODULATE (an approximation); all 710 battle3d
models load.

## Remaining gaps

| Gap | State |
| --- | --- |
| Encounter 525 (Agate, Eagun's Pikachu vs Cipher Peon Skrub's Shadow Hitmontop) | plays with pack v3 (side-0 CPU trainer, [battle-rules-lane.md](battle-rules-lane.md)); an older pack still refuses it |
| Encounters 0, 1, 8, 9, 521, 524 | refused (test rows, link rows, empty rosters); no story script requests them |
| Turn rules | original order, speed, targets, damage order and end of turn for the modelled effects; the trainer AI from the original AI rows (per-move AI functions not ported); see [battle-rules-lane.md](battle-rules-lane.md) |
| Bag in battle, escape | not offered |
| Move forget prompt | asked (original messages) when a battle scene has bound it; post-battle evolution is still reported, not run |
| Hyper Mode, heart gauge drops in battle | modelled for the hero's Shadow Pokemon; the nature rate for the drops is taken as 1/1 |
| Snag presentation | the snagged Pokemon leaves with its faint sequence; the original throw and shake effects (`fightTrainerHokakuThrowEffect`) are not drawn |
