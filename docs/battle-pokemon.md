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

These synthetic checks do not independently qualify the whole constructor
against the original executable. Original entry/return differential fixtures
with the initial 312 bytes, RNG seed, real trainer ID, attest and table/message
context are required before claiming that qualification.
