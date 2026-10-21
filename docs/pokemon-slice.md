# Native Pokemon stat slice

`src/decomp_pokemon.c` compiles the original `pokemonSetLevelBasisStatus`
routine from Count-Sheep's pinned decompilation:

- Repository: <https://github.com/Count-Sheep/pkmn-colosseum>
- Commit: `51bfe4314a218dfaf9592fc80723c50498d0ee55`
- Source: `src/game/pokemon_range_8012795C.c`
- Source git blob: `a1d9ce6bba5a91824c6a8afbd76e06ef1738fafb`
- Header: `include/game/pokemon_evolution.h`

The source is included directly from the pinned submodule and remains
unmodified. Its accessor calls are renamed within this translation unit and
connected to the native `C3Pokemon` record. This runs real decompiled game logic
for IV/EV/base-stat scaling, nature adjustment, the Shedinja HP case, and HP
preservation as the level changes. No copied game tables or assets are needed
for the synthetic test inputs.

## Public API and data boundary

`c3_pokemon_recalculate(pokemon, target_level)` accepts explicit base stats,
IVs, EVs, and nature multipliers. Stat order is HP, Attack, Defense, Speed,
Special Attack, Special Defense. The adapter maps that order to the original
game's selectors, whose Speed entries come last. The supplied species ID is
the game's internal ID; its Shedinja comparison is `0x12F`.

Use zero old maximum HP and zero current HP to initialize a record. For an
existing Pokemon, provide its actual maximum HP and current HP. Raising the
level retains the number of HP lost, and an already fainted Pokemon stays at
zero HP. The native wrapper rejects malformed inputs and HP wraparound after
an excessive level decrease without changing the caller's record.

The adapter does not implement the full `pokemonGetStatus` interface. It
supports only the selectors called by this pinned routine, and an unexpected
selector produces `C3_POKEMON_ACCESSOR_ERROR`. It does not load PokemonBios,
species tables, nature tables, or save files. Those require the original data
loading and endian-conversion work before this API can receive real game
records.

The retail nature/species accessor signatures lack a context argument. The
adapter protects its temporary context with a C11 atomic flag. Simultaneous or
reentrant calls return `C3_POKEMON_BUSY`; there is no spin or blocking wait.
Each call modifies a private working copy and commits it only on success.
Callers still own synchronization for their own input records. This small
serialized adapter can be replaced with a full native game-data service as
more of the engine is integrated.

## Validation

`tests/test_pokemon.c` uses a synthetic base-stat vector of six 100s, six IVs
of 31, 252 Attack EVs, 252 Speed EVs and 6 Special Defense EVs. Its independently
calculated level-50 neutral results are `[175, 152, 120, 152, 120, 121]`; at
level 100 they are `[341, 299, 236, 299, 236, 237]`. The nature case produces
167 Attack and 136 Speed through the original routine's integer truncation.
Tests also exercise initial HP, damage preservation, fainting, Shedinja,
input limits, and HP-wrap rejection.

Passing these checks proves this source slice works with the native adapter.
It does not establish battle-engine boot, rendering, save compatibility, or
hardware performance.

## Upstream porting observations

The pinned commit describes a verified 100% matched and linked tree. Its
README and root `report.json` contain older measurements, and this port has
not independently rebuilt the GameCube matching report. The authoritative
verification remains a fresh `build/GC6E01/report.json`, canonical DOL/REL
hashes, and `tools/verify_decomp_rules.py` on a checkout with original inputs.

Matching GameCube code still includes registered PowerPC assembly, including
GSthread register/context switching. The original entrypoint initializes
GameCube OS, GX, ARAM, audio and DVD services and writes absolute GameCube
memory addresses. These need platform replacements for a native 3DS build.
The source also uses big-endian serialized structures and an ILP32 ABI;
`compat/include/dolphin/types.h` fixes integer widths for native host tests.

Do not glob all upstream `.c` files into the game. `configure.py` selects
specific source ranges through include-carve units and per-object defines;
many files overlap or retain intentionally inactive old scaffolding. A full
native import must reproduce that canonical selection while replacing the
platform-specific units.
