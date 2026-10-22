# First battle: exact encounter data and a preserved pending request

## Implemented boundary

`tools/prepare_battle.py`, `include/colosseum3ds/battle.h`, and `src/battle.c`
provide a bounded data adapter for original GC6E01 encounter 10. This is a
verified battle request and team-data milestone. **There is no native combat
backend yet, and Willie cannot be defeated through this adapter.**

The adapter copies the original encounter records and snapshots the current
player party without modifying the save body. Its only nonempty request state
is `C3_BATTLE_WAITING_BACKEND`. It has no completion, victory, reward, or story
flag setter. In particular, it never writes flag `0x03DE`, heals the party,
consumes RNG, applies damage, awards experience, or resumes the waiting GSVM
native. Missing or invalid battle data produces a specific error while leaving
the previous output object unchanged.

## Original source path

All source analysis is pinned to
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).

The GSVM battle native uses `fn_801CA5C4` in
[`gs_range_801C766C.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_801C766C.c).
Its normal route calls `fn_8020DAD0`, defined in
[`fight_candidate_8020DAD0.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_candidate_8020DAD0.c).
That function coordinates the original encounter index, floor state, trainer
name, audio, wipe, transition to the battle floor, return transition, result,
and defeat handling. It is not a standalone damage calculator or a complete
portable battle loop.

The actual battle thread, `fn_801EFA08`, is in
[`battle_main.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/battle/battle_main.c).
It drives `fightActionCreateAndFlowFifo`, including the command/action stages
7, `0xB`, and `0xD`, and repeatedly drains the presentation FIFO. It also uses
GS threads, timers, menus, camera work, move-sequence initialization, and the
original FightFloor/FightSide/FightTrainer/FightPokemon state. These dependencies
are not supplied by the existing field renderer or by the new data adapter.

The source's trainer construction is especially useful:
[`fightTrainerCreateFightTrainerDataIdToHero` and `fightTrainerCreateFightTrainerPokemonDataIdToPokemon`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_trainer_range_801F7F80.c)
copy the saved hero for the player trainer and create an opponent party from a
starting roster index, advancing through consecutive records until species zero.
The eight values at trainer-record offset `0x14` are item IDs, not party IDs.
The adapter follows the actual roster-start field at offset `0x04`.

## Verified encounter 10

These values were read from the admitted original `common_rel.rel`; none are
invented team members or generated progress.

| Field | Original value |
| --- | --- |
| Encounter index | 10 |
| Fight kind / fight type | 2 / 1 |
| Fight-floor table index / actual battle floor | 5 / 200 (`0xC8`) |
| Player trainer / input device | 1 / 1 |
| Opponent trainer / input device | 10 / 0 |
| Opponent name message ID | `0x1014` |
| Opponent trainer kind | 26 |
| Trainers per side / party capacity / active per trainer | 1 / 6 / 2 |
| Opponent roster start | 238 |
| Roster 238 | Internal species 288, level 24, move IDs 33, 45, 0, 0 |
| Roster 239 | Internal species 288, level 24, move IDs 33, 39, 0, 0 |
| Roster 240 | Original species-zero terminator |
| Trainer-kind prize multiplier | 5; no reward is calculated by this adapter |
| Item use / running / experience | Enabled / disabled / enabled |

The record layouts and rule bytes come from the original
[`fight_encount_exact_8020DD44.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_encount_exact_8020DD44.c),
[`fight_encount_exact_8020E020.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_encount_exact_8020E020.c),
[`fight_exact_8020E1A4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_exact_8020E1A4.c),
[`fight_trainer_db_range_801FBD10.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/fight_trainer_db_range_801FBD10.c),
and [`common_rel.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/common_rel/common_rel.c).
Internal species numbers must not be interpreted as National Pokédex numbers.

## Private preparation and format

```sh
python3 tools/prepare_battle.py --disc /path/to/extracted-GC6E01 --output /private/output/willie.c3b
```

The converter admits the existing verified GC6E01 main DOL and the 1369520-byte
common REL with SHA-1 `e816761813900aa0fd4f87d012b3b2a19d2f003e`. It checks REL
section layout, table counts, source indices, the two-trainer slot mapping,
roster termination, and double-battle limits. It refuses existing output paths
and restricts repository-local output to the ignored private directory.

`C3B1` is exactly 644 bytes. Its 128-byte big-endian header contains the magic,
version, total size, encounter index, source commit, common REL digest, and a
payload SHA-1, followed by reserved zero bytes. The payload retains original
big-endian records in this order:

| Record | Bytes |
| --- | ---: |
| Encounter | 56 |
| Fight floor | 24 |
| Fight kind | 32 |
| Fight type | 8 |
| Player and opponent trainer records | 104 |
| Opponent trainer kind | 12 |
| Two side records | 40 |
| Two Pokémon records and their original terminator | 240 |

The loader validates exact bounds, header pins, checksum, supported profile,
and structural invariants transactionally. The checksum detects corruption;
source admission is performed by the converter, not by treating an editable
header digest as authentication. The JSON preparation report records each
original span's table, index, file offset, size, and SHA-256.

No dialogue, textures, models, raw records, private pack, or original source
files are committed by this change. The extracted pack from the sandbox was
644 bytes with SHA-256
`873052ebb92bcf11f2dc9deed182362bf941020adcc945bd994c38c7ea0f4319`.

## Runtime integration contract

Load the private pack with `c3_battle_load`. `c3_battle_summary` exposes the
reviewed encounter, team, and rule fields without interpreting native pointers.
On GSVM native 98, obtain the current body through `c3_game_state_body` and call
`c3_battle_request` with the three original native arguments. The request owns
copies of all six original 0x138-byte party slots, including unknown fields,
held items, moves, PP, HP, status, personality, and unused slots. It does not
recreate the player's starters or overwrite an imported team.

**Do not call `c3_story_resume` after successful request creation.** Success
means a valid pending request has been captured. Keep the native call waiting
and report the missing combat backend explicitly. Do not route to the original
post-battle script by fabricating a return value or interpreting a menu choice
as a victory. Only a later backend with a tested result/writeback contract may
resume that continuation.

## Validation performed

`tests/test_battle.c` passed 2025 checks using the private retail pack: all
truncated lengths, oversized input, source/header/checksum corruption,
unsupported profiles, transactional failure, exact retail encounter/team
fields, complete current-party copying, unchanged save-body bytes, invalid
party rejection, and preservation of fainted HP. Its structural fixtures are
synthetic and are never installed as game data.

The same checks passed under AddressSanitizer and UndefinedBehaviorSanitizer
with LeakSanitizer disabled because this sandbox cannot inspect the required
`/proc` entries. `src/battle.c` compiles for ARM11 with the pinned devkitPro SDK
and `-Wall -Wextra -Werror`. This is data/request validation, not battle gameplay
or hardware acceptance.

## Next genuine combat milestone

The next useful implementation is a deterministic, headless adapter around
the original battle state and action-flow functions, with an explicit event
sink for presentation waits. It needs the original opponent generation and RNG
semantics, species/move/ability/item data, command selection and targets, speed
ordering, accuracy/damage/status effects, AI, fainting/replacement, and result
writeback. The opening player team already contains more move behavior than
Willie's two attacks; hard-coding only his attacks would not make this battle
faithful.

First validate fixed-seed command sequences against original execution,
including HP, PP, stat stages, RNG advancement, turn order, defeat, experience,
and rewards. Then connect the native menu and presentation event sink, and
finally resume the original GSVM return path. Until that backend exists, the
entry request remains blocked and later story flags remain the original
script's responsibility.


The next headless foundation now provides isolated source-derived RNG and the
original action-queue control slice. It does not provide combat callbacks, an
initialized opponent, a battle result or GSVM resumption. See
[native-battle-foundation.md](native-battle-foundation.md) for the exact ABI,
source authority, validation and remaining qualification gates.
