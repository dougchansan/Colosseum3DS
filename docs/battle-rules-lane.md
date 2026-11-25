# Battle rules from the original (round 2)

This lane brought the general story battle (`src/battle_general.c`) closer to
the original fight code in `vendor/colosseum`. Each rule below names its
source routine. What is still not ported is listed at the end of each section.

## Pack version 3 (`tools/prepare_battle_encounters.py`)

Version 3 keeps the version 2 layout and appends these sections, each taken
unchanged from the pinned common REL:

| Section | Source | Rows |
| --- | --- | --- |
| Side-0 CPU trainers | encounter slot 0 when it is not the hero (`fightTrainerCreate`) | 2 (525, test row 7) |
| Trainer AI data | `fightTrainerAiDataBiosGetPtr`, REL 0x9BD14 | 261 x 40 |
| AI value codes | `fightTrainerAiValueAddsubDataBiosGetPtr`, REL 0x118E4C (value +0) | 553 |
| Natures | `pokemonSeikakuDataBiosGetPtr`, REL 0x121614 | 25 x 40 |
| Heart gauge filters | `pokemonDpFilterDataBiosGetPtr`, REL 0x1219FC | 6 |

The prepared pack is 572,564 bytes. Older packs still load: with one, 525
stays refused, the opponent picks moves at random, and Hyper Mode is off.

## Encounter 525

Eagun's Pikachu against Cipher Peon Skrub's Shadow Hitmontop has a CPU trainer
on side 0. The engine builds side 0 from that trainer's roster, both sides use
the trainer AI, and the hero only watches. Nothing is snagged or written back.
The result is 2 when side 0 wins and 3 when side 1 wins, and the field script
branches on it. The 3D and bottom-screen scenes run the battle without input.

## Turn rules

- **Order** (`fightFloorSortFightOutPokemonPtrArySub`,
  `fightFloorCmpfightOutPokemonNimbleness`): the action bracket comes first
  (item 1, Call 2, switch 3, move 4), then move priority when either move's is
  non-zero, then speed, with `fn_800E0C54() & 1` breaking ties.
- **Speed** (`fightOutPokemonGetNowNimbleness`): paralysis shifts it right by
  2, Macho Brace shifts it right by 1, and Quick Claw acts first when the
  turn's shared roll is under 20 x 0xFFFF / 100. The roll is drawn at battle
  start and again at each turn end.
- **Targets** (`fn_8022B2CC`, range ID at move row +3): 3 is a random foe, 4
  is both foes, 6 is every other Pokémon, and 2, 5 and 7 are the user's side.
  When the chosen target is gone, a random live foe is hit.
- **Damage order** (`fn_80232110`): burn halves after the /50. The screens
  apply x2/3 when two Pokémon are on the defender's side in a double battle,
  else x1/2. A both-foes move is halved against two. Then min 1, then +2.
- **End of turn** (`fn_80213270`): Reflect and Light Screen wear off first.
  Then, in speed order, each Pokémon takes poison (/8), bad poison
  (max/16 x count) and burn (/8), and Hyper Mode reminds.
- **Not ported:** weather, abilities, the other held items, and the other
  end-of-turn effects (Leech Seed, Wish, Perish Song and the rest). The engine
  does not model those moves.

## Trainer AI (`fn_802342CC`, `fn_8023A308`, `fn_8023A740`)

Each trainer row (+6) names an AI row. The rolls run in this order: switch
(+7), item (+8, drawn but trainer items are not modelled), combo (+6, a stub
in the original). Then each Pokémon in speed order picks a target and a move.
Each can be random (+0x11 for the target, +0x12 for the move) or scored.

The score adds these AI codes, clamped to ±200:

- 0x3C: the move can hit.
- 0x3F / 0x40: resisted / no effect.
- 0x42: one of the AI row's preferred types.
- 0x44: the highest damage estimate.
- 0x45 / 0x46 / 0x47: knocks the target out.
- 0x4A: a both-foes move against two.

Retail jitter is on (`lbl_8047882E` = 1), and the best score wins with random
ties (`fightTrainerAiGetValueAryMaxBanme`). The switch value adds code 6 when
no move affects a foe and code 7 when a move would knock one out.

**Not ported:** the per-move `fightTrainerAiWazaValue/Hit/Damage` functions.
The damage estimate is the engine's formula at the full roll. Also not ported:
codes 0x41, 0x43, 0x48, 0x49, 0x4B and 0x4C, the special cases of the scored
target choice, and switch codes 1–5 and 8.

## Forget-a-move prompt (`evolutionWazaLearn`)

A level-up move that finds four moves known now asks the bound prompt. This
applies to the battle writeback and to purification's `_expRecover`. The
questions run in the original order:

1. Forget a move? (0x4243)
2. Choose the move. An HM can't be chosen.
3. Forgot it (0x4248, plays sound 0x468), then learned the new move (0x423D).

Declining leads to "give up?" (0x4242) and then "did not learn" (0x4241).
`platform/3ds/move_forget_scene.c` shows these original messages from the PDA
pack on the bottom screen. A battle scene binds it.

## Shadow Pokémon

Hyper Mode applies to the hero's Shadow Pokémon in fight kinds that allow it.
The chances come from the nature row for the dark level
(`pokemonGetDarkPokemonLevel`).

- **Entry:** Shadow Rush can trigger it (`fn_8022EDEC`).
- **Snapping out:** each action has a 1/256 chance to end it.
- **Misbehaviour:** another move, refusals, a self-hit, no action, item misuse
  or wanting to return (`fn_8022A504`).
- **Shadow Rush:** a critical hit 90% of the time in Hyper Mode.
- **End of turn:** a reminder line.
- **Call:** CALL on the action menu (`fn_80212D6C`).

The heart gauge drops once per battle when the Pokémon is sent out (filter 0)
and again on a successful Call (filter 2). The gauge is written back to
+0xDC. Experience is banked only when the gauge is under 60% (`fn_802232F4`).

**Not ported:**

- The nature rate (kake/waru) for the gauge drops; the table isn't located,
  so drops use 1/1.
- Writing Hyper Mode back to the record.
- Returning to the trainer as a real switch.
- The battle text, which is the engine's own English. The original IDs are
  in the source comments.

## Checks

`make -f tests/battle-general.mk battle-general-check` runs the battle checks:

- `test_battle_general`: side-0 CPU trainer.
- `test_battle_turn_rules`
- `test_battle_trainer_ai`
- `test_move_forget`: with `private/rel/pda.c3pd` it also renders the five
  original messages.
- `test_battle_hyper`

The private sweep on the version 3 pack, from the completed save: 559
admitted encounters, all writebacks checked.
