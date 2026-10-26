# Temporary pre-catch opponent constructor

`c3_battle_pokemon_create_pre_catch` calls the pinned original
`fightTrainerCreateFightTrainerPokemonDataIdToPokemon` at `801F9F78`, with its
`pokemonCreate` (`801240C4`), initialization, learned-move, personality rejection,
and stat/nature arithmetic closure. This replaces neither the existing parameter
initializer nor the battle queue. It is not wired into native98 or save writeback.

The caller supplies the complete **312-byte initial backing** and receives the
staged result only on success. The original `PokemonBios` C header describes
fields through `FC`; the original caller copies `138` bytes. Padding, the final
two original-nickname bytes, condition padding, and the `FC..137` tail retain
caller bytes. No blanket zeroing substitutes for the original initializer.

Inputs are immutable borrowed canonical big-endian rows: the 80-byte roster,
its 284-byte species row, 56-byte move rows, the species' 101 growth thresholds,
25 40-byte nature rows, and two-byte nature-rate rows. The caller supplies row
counts and keeps all storage alive. A message callback resolves the original
species and optional roster nickname IDs to bounded plain GSchar strings.
Unknown or missing messages, control-code strings, unavailable moves or nature
rates, and every Shadow/cache roster are rejected. These admission restrictions
are temporary explicit gaps; callbacks must not synthesize missing original data.

`request.attest` is the four bytes at `gamedataGetStatus(0,1)`, **not a trainer
ID**. `request.opponent_trainer_id` is the separate original trainer personality
value generated after hero construction. Neither the hero ID nor a field inside
the initial backing can substitute for it. There are no invented capture dates.

The source `pokemonCreate` consumes its initial personality, two IV words, and
ability draw and runs learned-move initialization even when roster overrides
replace those results. Both personality draws explicitly execute the high-half
call first, matching the original PPC operand evaluation rather than depending
on host C evaluation order. Source sex, nature and rarity retries use the separate
opponent trainer ID. Retry exhaustion and draw-counter overflow roll back both
the full backing and caller RNG.

The wrapper uses byte accessors and `uintptr_t` for the source status API's
internal pointer-bearing values. No host address passes through a 32-bit integer,
and no original native struct overlays host memory. An atomic guard protects the
short-lived source service context. The caller must serialize its own input and
RNG ownership; callbacks must not mutate their borrowed inputs.

The endpoint is **before** the parent writes selector `C9` (roster ID at `D6`),
copies all 312 bytes into the hero party, and applies the separate catch setter
with floor, level, ball and trainer metadata. The constructor's catch fields
contain only the original initializer defaults. FightPokemon creation, Shadow
cache/state ownership, full party authority, AI, turns, results and writeback
remain separate integration gates.

## Verification

`tools/prepare_battle_pokemon.py` checks the locked decomp commit and the hardcoded
SHA-256 pins in `config/battle-pokemon-source-files.json`, then emits the original
closure into ignored `build/battle_pokemon_original.inc`. Its documented changes
are symbol isolation, removal of local extern declarations, pointer-width boundary
lowering, explicit original RNG ordering, and a bounded personality retry guard.
The BE service boundary preserves the source clamps, move aliases, PP-Up rules,
growth-derived level, HP adjustment, nature rates and string-copy limits.

`tests/test_battle_pokemon.c` checks an independent byte write map, full backing
canaries and retained tails, independently stepped RNG order/count, learned move
rotation and duplicate handling, dual nickname writes, IV/EV and held-item
overrides, sex/nature retries, low-PP rejection of PP Ups, and output/RNG rollback
after partial execution, missing data, overflow, unsupported Shadow or overlap.
`tests/test_prepare_battle_pokemon.py` checks source tampering, manifest repinning,
CRLF normalization and the extracted closure's boundary transformations.

## Original-game differential: ordinary Willie calls

`tools/qualification/check_battle_pokemon_oracle.py` replayed two actual original
entry/return observations for roster IDs 238 and 239. Both **complete 312-byte
results** match the native constructor, including the real initial stack-backed
padding and tail. Both final RNG seeds match. This qualifies these two ordinary
temporary pre-catch constructor cases; it does not qualify Shadow construction,
full party creation, FightPokemon, a turn, or battle results.

The checker validates entry `801F9F78`, caller `801F9E38`, return `801F9E3C`, the
original linked branch instruction, roster `r3`, opponent trainer ID `r5`, matched
stack pointers, full object address/extents, and the independent RNG and attest
observations. Attest comes from the captured dereference of global `8047ADB8`,
offset zero; it is not read from the constructor output. Each native call starts
with the captured entry backing and entry RNG, never a blank buffer or values
copied from the expected result.

| Roster | Entry seed | Return seed | Draws | Fit attempts | Matched bytes |
| --- | --- | --- | --- | --- | --- |
| 238 | `38CD1D69` | `008D26D2` | 147 | 71 | 312 |
| 239 | `008D26D2` | `471708E9` | 237 | 116 | 312 |

Draw counts have two independent checks: bounded distance along the known HSD
stream, and the original source call pattern of five initial draws plus two per
personality attempt. The latter applies the roster sex/nature constraints and
nonrare filter using the actual captured opponent trainer ID. The native draw
counter must equal that independently established count.

The private original inputs are pinned DOL SHA-1
`870e8b9693ca780782d80f22a6a4572d8ba9458f` (3,779,808 bytes) and common REL SHA-1
`e816761813900aa0fd4f87d012b3b2a19d2f003e` (1,369,520 bytes). Source table locations
are independently resolved from their original owners, not from observed results:

| Canonical input | File offset | Extent |
| --- | --- | --- |
| REL roster 238 / 239 | `A4888` / `A48D8` | 80 bytes each |
| REL species 288 | `1371D0` | 284 bytes |
| REL moves | `11E010` | 358 × 56 bytes |
| REL natures | `122E60` | 25 × 40 bytes |
| REL nature rates | `13FC7C` | 8 × 2 bytes |
| DOL growth 0 | `35B940` | 101 × 4 bytes |
| REL message `4EF` | `5EA06` | 20 bytes |

The checker also validates the original table counts and the corpus' species,
level, nickname and non-Shadow identity. Its accepted scope is deliberately these
two Willie calls. Original table bytes, message bytes, backing bytes and capture
JSON remain private. The emitted summary contains identities, hashes and numeric
comparison results only. The successful run used observation SHA-256
`d719ad266750b1f86d2c83ccd8121d7ecec3572a058a8bd34c3180d12066d607` and native
library SHA-256 `b9ed539749b68ea5bc0b6f6ff186ec6da377ec99924ca15e121828fe779fa769`.
The [numeric replay summary](battle-pokemon-original-check.json) records the
subsequent successful replay after adding the reusable initializer. It contains
no original backing, message or table bytes.

To rerun with an existing native shared library and private inputs:

```text
python3 tools/qualification/check_battle_pokemon_oracle.py --private-root /private/GC6E01 --capture-dir /private/probe --library build/libbattle_pokemon.so
```

`tests/test_battle_pokemon_oracle.py` uses public synthetic data to verify input
provenance and context rejection, independent input forwarding, message lifetime,
last-byte mismatch detection, final seed/draw-count mismatch detection, and
rejection before native execution when the observed seed contradicts the source
call pattern. Those tests contain no original table or Pokemon backing bytes.
