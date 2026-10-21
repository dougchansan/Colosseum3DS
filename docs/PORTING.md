# Native New 3DS port decisions

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
build currently includes **three upstream translation units**, not the whole
game. File count is an inventory, not a gameplay-completion percentage.

## Concrete blockers to boot

| Area | Evidence in pinned source | Required native work |
| --- | --- | --- |
| Startup | `src/game/main.c` initializes OS, ARAM, DVD, GX, audio and accesses `0x800018xx` | Native boot orchestration and platform services; remove fixed-address assumptions |
| Threads | `src/game/gs_thread_exact_800F0030.c` switches PPC registers, LR, CTR and stack | Preserve cooperative scheduling semantics with a native context implementation |
| Modules | `docs/REL_MODULES.md`: FSYS callback calls `OSLink` and REL `_prolog` | Compile module code to ARM; register module entrypoints and relocate/convert data |
| Data tables | `common_rel` includes tables supplied by dtk extraction | Reproducible local extraction with explicit endianness and address-to-reference conversion |
| Renderer | GX/HSD draw code and TEV material pipeline | Vertex/index and texture conversion, skinning, material mapping, fixed PICA limits and caches |
| Audio | MusyX / GameCube DSP services | Decode/mix and submit through 3DS audio services |
| Save/input/timing | CARD, PAD, DVD async completion, OS timing semantics | Native services with equivalent lifetime/order behavior and safe save writes |

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

The pinned private tree contains all 26 configured originals. In this session,
24 smaller RELs were fetched through the GitHub connector and matched the
canonical content SHA-1 values. `main.dol` (3,779,808 bytes) and `common_rel.rel`
(1,369,520 bytes) were present but the connector returned empty file content;
their bytes could not be rehashed here. No 26/26 session verification is claimed.
Run `tools/verify_inputs.py` locally or the manual private-image workflow to
check every byte. The existing private build-image pipeline also has its own
original validation; its state is not inferred from the presence of files.

The private input repository does not contain the complete disc assets. A
verified extracted disc is required for `common.fsys`, other archives, textures,
models, scripts, audio and movies. `prepare_data.py` validates disc identity and
DOL hash and copies assets; it does not certify every copied file or make the
unimplemented game boot path available.

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
3. Hardware: Homebrew Launcher launch; native self-test PASS; rotating geometry;
   all controls; sleep/resume; HOME exit; missing and present FSYS diagnostics.
4. Game slice: actual model/material, scene and module behavior, with captured
   hardware measurements. A bootstrap screen alone does not pass this gate.
