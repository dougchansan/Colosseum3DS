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

## Not ported: purification at the Relic Stone

The shrine's two purification paths both end in code the port does not
have:

| Path | Original | Port |
| --- | --- | --- |
| A at the Relic Stone with a heart-opened Shadow Pokemon in the party | function 9 calls native `c2` = `preReliveSetParameter(0, 0)` -> `preReliveMain` | native `c2` unported: the floor stops with `Unported native c2` |
| The Pokemon-menu hook | function 11 (link kind 4) calls native `11` = `fn_800190D8(0x219, function 10)`, an entry of the Pokemon menu's 8-row hook table; choosing it runs function 10: `preReliveSetParameter(1, index)` and flag `0xE04` | kind-4 links are not run, native `11` is unported |

`preReliveMain` (`src/game/pokemon_relive.c`) decides from the party's
Shadow Pokemon, the heart-open count and whether the hero holds item
`0x219`; its purification action stores the chosen Pokemon and moves to
floor 0x385 (`M3_shrine_1F_rl.fsys`, no collision or script), the
purification ceremony scene, before the Pokemon is purified
(`pokemonDarkRelive`). Porting it needs the Pokemon-menu selection, the
0x385 scene and the purification rules; until then a save with a
heart-opened Shadow Pokemon must not press A at the stone.

0xD1 (`M3_shrine_1F_bf.fsys`) is the shrine's battle field and 0x323
(`ex_shrine.fsys`) an event set; neither is a walkable floor.

## SD files

`boot/f024.*`, `boot/f024_*`, `boot/f025.*`, `boot/f025_*`, `boot/f027.*`,
`boot/f027_*` from the warp-tour preparation (see `tools/prepare_tour.py`).
