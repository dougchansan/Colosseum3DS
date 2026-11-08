# Native New 3DS port decisions

## Current native checkpoint

The [native boot and first-area route](native-boot.md) now connects the original
logos, animated title, main menu, source-defined New Game state, hideout movie,
and Outskirt Stand arrival. The bounded first-area scene supports Wes movement
and explicit area-entrance save/Continue. These are selected native adapters;
the remaining NPC scripts, battles, other areas, original window engine and
external music have not become available merely because the GameCube build
matches. See [game-state.md](game-state.md) and
[outskirt-field.md](outskirt-field.md) for the exact behavioral boundaries.

## What the first milestone establishes

The port uses native ARM compilation with devkitARM and libctru, and PICA
rendering through citro3d. It does not run a PowerPC interpreter, JIT, DOL or
REL loader. The initial `.3dsx` links real selected decompiled C and a bounded
archive reader into a platform application. Hardware execution is a separate
acceptance gate from cross-compilation.

The source and private-input commits are pinned in `source-lock.json`. The
new native wrappers namespace selected symbols and supply explicit native
records. Upstream files remain unchanged. The `dolphin/types.h` overlay
uses exact-width integers because upstream `long` is 64-bit on a common host
and 32-bit on the original GameCube/target ARM ABI.

This overlay does not convert arbitrary pointer-bearing structs or serialized
data. FSYS fields are read explicitly as big-endian values. The Pokémon slice
does not pretend to be the full `PokemonBios` record, species database or save
format. Its caller supplies base stats and nature rates.

## GameCube completeness and ARM readiness

Count-Sheep commit `51bfe4314a218dfaf9592fc80723c50498d0ee55` reports 100%
matched/linked GameCube code with `verify_decomp_rules.py` passing. During the
initial inspection, the README/root report were stale and that commit had no
completed build verification check. This project has not independently rebuilt
the entire DOL and REL set, so it records the upstream claim separately from
its own native regression tests.

The upstream assembly registry contains 133 entries, including ten first-party
entries. These matching assembly implementations are not ARM code. The native
build directly includes three small upstream C slices and extracts the original
CARD codec separately. New native adapters also preserve reviewed source
semantics and explicit data layouts. This remains a selected subset of the
game. File count is an inventory, not a gameplay-completion percentage.

## Remaining requirements for the complete game

| Area | Evidence in pinned source | Required native work |
| --- | --- | --- |
| Startup | `src/game/main.c` initializes OS, ARAM, DVD, GX, audio and accesses `0x800018xx` | Extend the bounded native boot route into full service and scene lifecycle behavior |
| Threads | `src/game/gs_thread_exact_800F0030.c` switches PPC registers, LR, CTR and stack | Preserve cooperative scheduling semantics with a native context implementation |
| Modules | `docs/REL_MODULES.md`: FSYS callback calls `OSLink` and REL `_prolog` | Compile module code to ARM; register module entrypoints and relocate/convert data |
| Data tables | `common_rel` includes tables supplied by dtk extraction | Extend the admitted New Game/field extraction to remaining tables with explicit endianness and address-to-reference conversion |
| Renderer | GX/HSD draw code and TEV material pipeline | Extend the tested title/field subset to remaining materials, particles, dynamic shadows and scene effects; profile fixed PICA limits and caches |
| Audio | MusyX / GameCube DSP services | Decode/mix and submit through 3DS audio services |
| Save/input/timing | CARD, PAD, DVD async completion, OS timing semantics | Validate the transactional GCI service and active-time input route on hardware; extend async lifetime/order behavior to the full game |

Source links:

- [Game entrypoint](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/main.c)
- [PPC thread context](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_thread_exact_800F0030.c)
- [REL/data integration](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/docs/REL_MODULES.md)
- [Canonical build definitions](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/configure.py)

Do not glob all upstream `.c` files into the native executable. The canonical
build uses include-carve units and per-object preprocessor definitions, with
multiple units drawing disjoint functions from one owner file. A wildcard
build would duplicate code or enable inactive implementations. Subsequent
integration must derive its selected unit graph from `configure.py`.

## Private input verification checkpoint

The supplied extracted GC6E01 revision 0 disc now passes **all 26 canonical
original hashes**: `main.dol` and 25 RELs, including `common_rel.rel`. The complete
asset tree also has all 1,872 files at the FST-declared lengths. FST size admission
alone is not a content hash for every disc asset; each prepared movie/archive
has a separate pinned identity check before decoding. Repeat original-code
verification with `tools/verify_inputs.py --orig /path/to/GC6E01`.

The pinned private tree contains the same 26 configured originals. Its
existing private-image pipeline provides an additional verified result:
[run 38674909620](https://github.com/dougchansan/pkmn-colosseum-build/actions/runs/38674909620)
at the exact pinned private commit passed all 26 original hashes both before
building and inside the resulting image on October 14, 2026 (Hawaii time). Its
canonical manifest is byte-identical to the pinned Count-Sheep manifest. The
published image digest is pinned in `source-lock.json` and the manual verifier
workflow. This is prior CI evidence, distinct from the current local 26-file
rehash and converter admission checks.

The private input repository does not contain the complete disc assets. A
verified extracted disc is required for `common.fsys`, other archives, textures,
models, scripts, audio and movies. `prepare_data.py` validates disc identity and
DOL hash and copies assets; it does not certify every copied file. The focused
boot/field converters perform additional source admission and conversion for
the native route described above.

## Rendering and performance decisions

The initial target is one top-screen monoscopic view with diagnostics below.
Stereo, multiple game views and a fixed frame-rate guarantee are deferred
until an actual scene can be measured. The speedup call is a system request,
not a measured CPU-clock claim. Track frame time, GPU time, resident asset
bytes, decoder scratch and transient peak allocations when game content lands.

FSYS reads stream through a small cache and avoid an entire-archive allocation.
The LZSS decoder uses about 4.6 KiB of stack plus caller-provided output. Each
next loader must set its own output-memory limit before allocating from member
sizes, and must not trust a game archive pointer as an ARM address.

## Acceptance gates

1. Host checks: pinned source hashes; RNG/collision/stat results; malformed,
   truncated and overlapping compressed data; original-input verification.
2. Cross-build: actual devkitARM compile, PICA shader assembly and ARM link.
3. Emulator integration: original assets, scripted native input, complete
   opening/arrival, movement, explicit Save, and a separate process using
   Continue; retain captures and executable/input/SD hashes.
4. Hardware: Homebrew Launcher launch; all controls; sleep/resume; HOME exit;
   actual movie audio and A/V sync; bounded-memory and SD/GPU timing on New 3DS.
5. Game slice: actual NPC/script/battle/module behavior with captured hardware
   measurements. A first-area exploration checkpoint does not pass this gate.
