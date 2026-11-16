# Relic Forest (Agate Village, Celebi shrine)

Agate Village (floor 0x24, `M3_out.fsys`) leads through the Relic Cave
(0x25, `M3_cave_1F_1.fsys`, Agate door 6) to the Relic Forest and its
shrine (0x27, `M3_shrine_1F.fsys`, cave door 1), where the Relic Stone
stands. All three floors are surveyed floors (`f024`, `f025`, `f027` packs
from `tools/prepare_tour.py`).

## What runs (Azahar, completed save, round-trip probe)

Route Agate -> cave -> forest -> cave -> Agate, each door fired through its
original transition script: every floor loaded, each door's script chose
the floor and entrance, there were no script errors and `used_bytes`
stayed flat. In the forest the probe stood Wes at the Relic Stone (A-button
surface, source 1) and pressed A: function 9 checked flag `0x42B` and the
number of Shadow Pokemon whose heart is open (native `f2`), found none (the
save has purified all 48), and showed message `0x3BCA`; the dialogue closed
and control returned.

## Purification at the Relic Stone

A at the stone with a heart-opened Shadow Pokemon in the party runs
function 9 -> native `c2` = `preReliveSetParameter(0, 0)` ->
`preReliveMain` (`src/game/pokemon_relive.c`). The port runs it as a menu
program of the story task (`src/field_story.c`, program `0xC2`):

| Step | Original | Port |
| --- | --- | --- |
| Decision | `preReliveMain`: party Shadow Pokemon whose purify flag is clear, heart-open count, item `0x219` | `c3_relive_counts` / `c3_relive_action` (`src/relive.c`) |
| Prompt | message `0x3B0F` | same |
| Choice | `menuPokemonOpen(7)` | bottom-screen party list (`field_scene.c`, `RelivePick`): only a Shadow Pokemon with heart gauge 0 can be chosen; B cancels |
| Ceremony | floor 0x385 (`M3_shrine_1F_rl.fsys`): Celebi's scene | **not ported**: skipped, logged as `ceremony_scene=unported` |
| Purification | `reliveCeremonyAll` -> `pokemonDarkRelive`, `_expRecover` | `c3_relive_purify` (`src/relive_purify.c`), `c3_battle_pack_recover_exp` (`src/battle_general.c`) |
| Messages | `0x3B10` (first move, if any), `0x3B0B` (stored experience), `0x44CE` per level reached, `0x3B0C`, `0x3B0D` (rename?) | same; msgctrl `0x32` = nickname, `0x39` = move name, `0x2F` = number |
| Rename | nickname keyboard | **not ported**: "yes" takes the "no" branch (`story_menu_program ... nickname_entry`) |

`c3_relive_purify` does what `reliveCeremonyAll` does to the record: heart
gauge to -100 (purified), status entry `0x3E` removed, stored experience
paid out (level-ups recompute stats and learn level-up moves into a free
slot; with four moves the move is reported as skipped), friendship +
stored friendship + 70 (at most 255), the national ribbon, and the purify
flag `0x4EE + shadow id - 1` (checked against the Shadow table by
`tools/prepare_game_state.py`). Not ported: the evolution check after a
level-up, the forget-a-move prompt and the hero's ex-ribbon byte.

The shrine's glow while a heart-opened Pokemon is in the party (native
`f1`, `preReliveDisplayHokoraParticle`) is accepted and not drawn.

The messages after the purification live in floor 0x385's bank
(`M3_shrine_1F_rl.fsys` member `0x0DAE0A00`); `tools/prepare_story.py`
adds it to floor 0x27's story pack (`FOREIGN_MESSAGES`).

### Azahar check (2026-11-16, build `relive-probe2`)

Completed save, probe route `start 024`, `shadow 0 7`, `024 6`, `025 1`,
`027 i1`, `027 0`, `025 0`, `024 0` (`shadow <slot> <id>` makes that party
member a Shadow Pokemon with heart gauge 0, 20000 stored experience and 30
stored friendship, in memory only; the probe saves only to
`probe-unused.gci`). Trace:

```
story_relive mode=0 action=4 remaining=1 ready=1 item=0
story_message ... id=03b0f
relive_pick_open party=6 cursor=0 pickable=1
relive_pick slot=0
relive_purify slot=0 name=KYOGRE species=404 level=100 exp=1250000 dp=-100 friendship=255->255 move0=WATER SPOUT pool_exp=20000 ribbon=1
story_message ... id=03b10 / 03b0b / 03b0c / 03b0d
story_menu_program native=c2 step=nickname_entry status=unported_cancel_branch
```

Control returned and the route went on through the cave, Agate and the
world map. The level-up path (message `0x44CE`, new moves) was not reached
on Azahar because the save's party is level 100; `tests/test_relive.c`
covers it on the host (console save: Umbreon 26 -> 34, one move skipped).

## Not ported

| Path | Original | Port |
| --- | --- | --- |
| The ceremony scene | floor 0x385, Celebi and the camera, before the purification | skipped |
| The Pokemon-menu hook | function 11 (link kind 4) calls native `11` = `fn_800190D8(0x219, function 10)`, an entry of the Pokemon menu's 8-row hook table; choosing it runs function 10: `preReliveSetParameter(1, index)` and flag `0xE04` | kind-4 links are not run, native `11` is unported. The port already handles mode 1 (forced purification of a given slot) in program `0xC2` |

0xD1 (`M3_shrine_1F_bf.fsys`) is the shrine's battle field and 0x323
(`ex_shrine.fsys`) an event set; neither is a walkable floor.

## SD files

`boot/f024.*`, `boot/f024_*`, `boot/f025.*`, `boot/f025_*`, `boot/f027.*`,
`boot/f027_*` from the warp-tour preparation (see `tools/prepare_tour.py`).
`boot/f027.c3s` must be prepared again with this `tools/prepare_story.py`
(151,825 bytes) for the purification messages.
