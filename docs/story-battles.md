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

The old "skip as a win" path (`field_battle_skipped`) runs only for an
encounter the pack refuses. Of the 277 encounters the prepared story
scripts request with a literal ID (369 call sites, scanned with
`prepare_battle_encounters.py --stories`), 276 are admitted. The one left
is listed under [Remaining gaps](#remaining-gaps).

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
  full moveset would need the forget prompt for is reported in
  `forget_prompt_skipped`; the prompt is not ported);
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
(`darkpokemonHypermodeFlag`) is not modelled in battle.

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

## Remaining gaps

| Gap | State |
| --- | --- |
| Encounter 525 (Agate, Eagun's Pikachu vs Cipher Peon Skrub's Shadow Hitmontop, fight type 0) | both sides are CPU trainers; the pack has only the opposing roster, so the field still reports result 2 (`field_battle_skipped`) |
| Encounters 0, 1, 7, 8, 9, 521, 524 | refused (test rows, link rows, empty rosters); no story script requests them |
| Turn rules | the general engine's documented Gen-3 approximation, not the original battle scripts; the opponent picks uniformly among legal moves (the original 16-slot AI scoring is qualified in `battle_rules.c` but not wired) |
| Bag in battle, escape | not offered |
| Move forget prompt, post-battle evolution | reported, not run |
| Hyper Mode, heart gauge drops in battle | not modelled |
| Snag presentation | the snagged Pokemon leaves with its faint sequence; the original throw and shake effects (`fightTrainerHokakuThrowEffect`) are not drawn |
