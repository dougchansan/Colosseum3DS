# Native original game state

`src/game_state.c` owns the original GC6E01 big-endian save body and connects it
to the memory-card codec and SD transaction. It initializes a new WES game from
verified original data, loads an authenticated raw card file or GCI, inspects
the trainer and party, changes supported fields, and explicitly saves the entire
body. Unknown imported fields remain byte-for-byte intact through supported
edits, except the two reserved words normalized by the original encoder.

This is a source-derived state adapter. The native build still does not execute
every game subsystem that can modify that state. A successful import does not
promise that its map, battles, events or party actions are implemented. The
scene layer must check its supported floor and spawn before offering Continue.
No user-supplied progress GCI was available during this implementation, and the
generated GCI has not been loaded by the original GameCube executable.

## Original sources and private preparation

All sources are pinned to Count-Sheep commit
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).
[game-state-sources.json](game-state-sources.json) records exact Git blob hashes
and byte counts. The upstream files are unmodified in the source submodule.

| Source | Behavior carried across |
| --- | --- |
| [`hero.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/hero.c) | `savedataInit`, `savedataCreate`, subsystem order, initial checkpoint, inventory and money |
| [`hero_savedata_exact_80129280.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/hero_savedata_exact_80129280.c) | `heroCreate`, trainer-ID draw order, trainer name and starter capture fields |
| [`hero_pokemon.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/hero_pokemon.c) | `heroPokemonGetBlacky`, `heroPokemonGetEifie`: species, level, moves, experience and friendship |
| [`pokemon_range_8011F5FC.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/pokemon_range_8011F5FC.c) | `pokemonCreate`, `pokemonCreateRndFit`: IV draws, male/non-shiny personality filtering |
| [`pokemon_range_8012795C.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/pokemon_range_8012795C.c) | Actual compiled upstream stat-recalculation code through `c3_pokemon_recalculate` |
| [`pokemon_range_exact_80128CC0.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/pokemon_range_exact_80128CC0.c), [`gamedatasaveBios.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gamedatasaveBios.c) | Section and scalar offsets; no native struct overlay |
| [`memo_candidate_8025FF18.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/memo_candidate_8025FF18.c) | 500 initial memo random words, then two observed starter records |
| [`gs_flag_exact_801908D4.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_flag_exact_801908D4.c) | Assign bit positions to all 3,593 original flag definitions |
| [`memcard.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/memcard.c) | Zeroed fresh-body path, original card artwork, slot lifecycle and codec |

The remaining initializers listed in the manifest cover PC boxes, empty
Pokémon, mailbox, daycare, battle rules and metadata, e-reader data and ribbons.
Their persistent defaults are included; runtime callbacks and graphics are not
serialized.

From the repository root:

```sh
python3 tools/prepare_game_state.py --disc /absolute/path/to/extracted-disc \
  --output /absolute/private/path/newgame.c3g
```

The tool verifies GC6E01 disc 1 revision 0, the 3,779,808-byte `main.dol` SHA-1
`870e8b9693ca780782d80f22a6a4572d8ba9458f`, and the 1,369,520-byte
`common_rel.rel` SHA-1 `e816761813900aa0fd4f87d012b3b2a19d2f003e` before
extracting data. Original strings, species/move/nature tables, rules, flags and
memory-card artwork remain private. The prepared file is 176,722 bytes. It is
not a save and contains no generated trainer ID, final starter stats or game
progress. Install it as `sdmc:/3ds/Colosseum3DS/boot/newgame.c3g`.

The runtime checks its exact version, source pin, size, section layout, payload
SHA-1, starter bounds, nature ratios and flag ranges. This checksum detects
damage; it is not a signature proving that somebody else ran the official
preparer. Provenance comes from verifying the original inputs locally and
retaining the generated JSON record. Existing outputs are not overwritten.

## New Game semantics

`c3_game_state_new` follows the cold fresh-state path in `memcard.c` and
`savedataCreate`. It initializes all persistent sections, including
`memoInit`'s 1,000 RNG draws, then creates the trainer and both starters. Its
explicit 32-bit seed makes tests reproducible and lets the platform supply a
session seed. The original HSD recurrence is `state = state * 214013 + 2531011`
modulo 2^32, returning the full upper 16 bits.

Draw order is explicit: memo and trainer ID draw the low half first; Pokémon
personality construction draws the high half first. Retail PPC instructions at
`0x80124168`/`0x80124170` and `0x8012444C`/`0x80124454` confirm the latter.
Relying on C expression evaluation order would produce different Pokémon on ARM.

The original name-entry limit is seven characters. NULL or empty name uses
`WES`, matching the original preset. Custom native input is printable ASCII
and may not contain only spaces. Storage uses original big-endian 16-bit GSchar
units. Imported non-ASCII names keep their actual bytes; the current short
summary displays unsupported characters as `?`.

| Initial field | Original value |
| --- | --- |
| Attestation | `0B 03 02 02` |
| Trainer sex, money | Male, 10,000 |
| Saved floor / previous / spawn | 2 / 1 / 1 |
| First party member | Umbreon, species 197, level 26, experience `0x4A6F` |
| Second party member | Espeon, species 196, level 25, experience `0x3FF4` |
| Both starters | Male, non-shiny, friendship 220, catch floor `0xFE`, ball 4, zero EVs |
| Umbreon moves | `0x2C`, `0x122`, `0x10D`, `0x121`, original maximum PP |
| Espeon moves | `0x5D`, `0xD8`, `0x73`, `0x10E`, original maximum PP |
| Empty Pokémon | OT sex 2, mail `0xFF`, Shadow gauge -100 |
| Partner-name placeholder | Original message `0xFA2`, `N/A` |
| PC boxes | Original BOX message with number substitution: BOX1, BOX2, BOX3 |
| Opening-complete flag `0x08D0` | Zero |

Inventory retains source insertion order: `0x16×2`, `0x0D×5`, `0x0E×2`,
`0x0F×2`, `0x10×2`, `0x12×2`, `0x11×2`, `0x17×2`. Other persistent
initializers use verified tables and source defaults, not blank placeholders
for later serialization.

New Game changes memory only. If an older card is loaded, its envelope, artwork
and slot generations remain available until an actual Save. The new body keeps
loaded options and the logical card serial. A fresh native file has no physical
GameCube card, so its logical serial starts at zero. The service does not
provision or impersonate a physical card.

## Opening completion and resume checkpoints

[`movie.c`, `fn_80035E04`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/movie.c)
waits for the movie to finish, directly sets `0x08D0` to 1, then calls
`floorLink(1, 0)`. This is a one-bit type-3 flag at bit position 4,439. The
portable accessor retains LSB bit numbering inside big-endian words.
`GSflagTest` means `GSflagGet(id) != 0`; it does not interpret a second
condition-ID namespace or dispatch events.

`floorLink` in [`floor.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/floor.c)
updates runtime transition state, not the persisted checkpoint. The opening is
floor `0x3A0`, so entry into floor 1 has runtime previous floor `0x3A0`, spawn 0.
The initialized saved checkpoint remains 2 / 1 / 1 until a script or savepoint
changes it.

The native explicit Save checkpoint can deliberately store 1 / `0x3A0` / 0
after entry. That is a native checkpoint extension using the original fields,
not a save automatically made by the original movie. Neither this API nor the
original resume function stores arbitrary player coordinates. Continue uses
saved floor, previous floor and spawn through `floor.c:fn_80113FE8`; the floor's
original position list supplies position and direction. Typed state edits do
not themselves run floor scripts or clear per-floor/per-area flags.

## API and persistence

Initialize `C3GameState state = {0}` and eventually call `c3_game_state_free`.
Public operations return 1 on success, 0 on failure. The optional error buffer
holds `C3_GAME_ERROR_SIZE` bytes. Calls on one state must be serialized.

| API | Operation |
| --- | --- |
| `c3_game_state_new` | Source-derived new state from seed, name and prepared data; no write |
| `c3_game_state_load`, `c3_game_state_decode` | Authenticate and own an existing raw/GCI body; failure preserves old state |
| `c3_game_state_summary` | Name, trainer ID, money, play time, counts, options, checkpoint, party |
| `c3_game_state_body` | Read-only original bytes; pointer expires after successful replacement/save or free |
| `c3_game_state_set_floor` | Original floor / previous / spawn fields |
| `c3_game_state_set_options` | Stereo/mono and original no-vibration option |
| `c3_game_state_add_play_time` | Nonnegative finite seconds, serialized as original big-endian float |
| `c3_game_state_get_flag`, `c3_game_state_set_flag` | Checked original bit fields, including two-word fields |
| `c3_game_state_save` | Encode full body and recoverably store; commit memory/counts only on success |

Loading without a prepared pack allows inspection and typed edits to known
fields. Flags require original definitions. Validation rejects unsupported
region attestation, invalid trainer/options/time fields and invalid present
party records; it does not validate every opaque subsystem. Intact unknown
fields are preserved.

Save increments the inner game count and outer rotating-slot generation,
retains older slots, and uses the `.tmp`/`.bak` protocol in
[save-format.md](save-format.md). Allocation/storage errors leave the live body,
RNG and counts unchanged. Initialization, import, menu selection and opening
playback make no implicit save. The caller selects the path and creates its
parent directory.

The body remains big-endian; byte access and `memcpy` of float representations
avoid native alignment and pointer-size assumptions. Live state owns about
693 KB with prepared data; full file/body copies bound peak save memory to a
few MB. File-sized arrays are not placed on the 3DS thread stack.

## Verification

From the repository root:

```sh
make build/test_game_state
./build/test_game_state
./build/test_game_state /absolute/private/path/newgame.c3g
```

The private-data run passed 257 checks under AddressSanitizer and
UndefinedBehaviorSanitizer: exact starters, defaults, memo words, opening flag
and checkpoint, malformed prepared data, cross-word flags, unchanged state on
failure, unknown-byte preservation, complete encode/decode, counters and pending
file preservation. Default CI uses explicitly synthetic tables, never original
game data.

For seed `0x12345678`, independent calculation gives trainer ID `0x1C465252`:

| Pokémon | Personality | IVs: HP/Atk/Def/SpAtk/SpDef/Speed | Stats in that order |
| --- | --- | --- | --- |
| Umbreon | `0x3F5932E4` | 3, 15, 18, 28, 25, 28 | 86, 37, 66, 47, 79, 46 |
| Espeon | `0x0990627B` | 30, 21, 19, 5, 25, 21 | 75, 42, 39, 71, 52, 71 |

Build the ARM probe with the same devkitARM/libctru as the application:

```sh
export DEVKITPRO=/absolute/path/devkitpro
"$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc" -g -O2 -Wall -Wextra -Werror \
  -std=gnu11 -mword-relocations -march=armv6k -mtune=mpcore \
  -mfloat-abi=hard -mtp=soft -D__3DS__ \
  -I"$DEVKITPRO/libctru/include" -Iinclude -Icompat/include \
  -Ivendor/colosseum/include -L"$DEVKITPRO/libctru/lib" -specs=3dsx.specs \
  tools/sandbox/game_state_probe.c src/game_state.c src/decomp_pokemon.c \
  src/save.c build/save_codec.c -lctru -lm -o build/game_state_probe.elf
"$DEVKITPRO/tools/bin/3dsxtool" build/game_state_probe.elf build/game_state_probe.3dsx
```

Prepare the Azahar frontend as in [tools/sandbox/README.md](../tools/sandbox/README.md).
Use a separate state directory and stage the pack under its emulated
`save/Azahar/sdmc/3ds/Colosseum3DS/boot/`. Then:

```sh
python3 tools/sandbox/run.py build/game_state_probe.3dsx --state /absolute/test-state \
  --events tools/sandbox/game-state-probe-input.csv --out /absolute/new-run-write \
  --frames 240 --seconds 45 --sd-file 3ds/Colosseum3DS/boot/newgame.c3g \
  --sd-file 3ds/Colosseum3DS/test-game-state-01/newgame.gci
python3 tools/sandbox/run.py build/game_state_probe.3dsx --state /absolute/test-state \
  --out /absolute/new-run-reopen --frames 120 --seconds 30 \
  --sd-file 3ds/Colosseum3DS/test-game-state-01/newgame.gci
python3 tools/sandbox/verify_game_state_probe.py /absolute/private/path/newgame.c3g \
  /absolute/test-state/save/Azahar/sdmc/3ds/Colosseum3DS/test-game-state-01/newgame.gci
```

The supplied Azahar 2126.0 software renderer, configured as New 3DS, passed both
runs: first initialized and saved once; second loaded count 1 with zero writes.
The probe uses a fixed seed, opening flag, entry checkpoint and 3,661.25 seconds
solely for development verification. It never touches the application save path.
The Python verifier uses independent integer formulas and `hashlib`, without
calling the C initializer or codec. It reconstructs all dynamic fields over the
original prepared defaults and compares all 122,832 decoded body bytes.
Expected body SHA-256:
`4e312dd74fbe4f0d915c6ea07a202b1ac78c7ab00d572f772a98ef7df44b4205`.

This establishes ARM execution, cross-endian state creation and SD persistence
across processes. Complete game-engine behavior, physical 3DS power-loss
guarantees and acceptance by an original GameCube remain separate validation.
