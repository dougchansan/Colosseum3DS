# Native boot and title checkpoint

This branch plays the two original boot logos and renders the original animated
title scene in a native New 3DS executable. It is a boot/title integration
checkpoint. The original main menu, playable New Game/Continue, game-state
serialization, external scene music and attract demo are not connected.

It builds on the repaired PR #2 head
[`07d4df55cb25`](https://github.com/dougchansan/Colosseum3DS/commit/07d4df55cb25ae4ce4ade12904f2cf1348cd418f).
The prior Eevee/Gekko route and its repairs remain present. The boot work is a
separate branch and does not modify the physical console or its installed files.

## What runs

| Stage | Native behavior | Current boundary |
| --- | --- | --- |
| TPC logo | Reads the original prepared 75-frame movie at nominal 30000/1001 cadence, centered at 320×240; streams optional stereo PCM to NDSP | Sound requires the console's DSP firmware; sandbox audio has not been validated |
| Genius Sonority logo | Plays the full 130-frame movie timeline and transitions to the title; late frames are skipped to preserve source timing | Original external BGM `0x04D1` is not connected; an empty THP audio track does not mean the original scene is silent |
| Title entrance | Live source geometry, three original animation clips, original camera, material alpha, UV scrolling, visibility and supported TEV effects | Offline state sampling and texture resizing are explicit fidelity approximations |
| PRESS START | A/START rising edge waits one second, then validates and decodes the configured card file | Prompt/diagnostics use the bottom console; the original menu/widget engine is not ported |
| Original menu boundary | Reports floor `0x3A1` and leaves the animated title visible | No fabricated New Game/Continue screen or invented progress state |
| Save backend | Original slot codec, checksums, corruption fallback, raw/GCI byte compatibility, verified SD writes and retained backups | Genuine game-state serialization/deserialization must be connected before playable saves can be claimed |

The boot order and input rules come from the pinned
[`movie.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/movie.c)
and
[`gs_title_8001EF78.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title_8001EF78.c).
The two logos have no ordinary button-skip branch. A/B/START advances the title
entrance through the original half-second fade sequence. The natural entrance
runs clips 0, 1 and then looping clip 2. `openingdemo.thp` is a later New Game
cutscene; `autodemo01.thp` is the attract movie, not an initial boot logo.

## Prepare private assets

Use an extracted GC6E01 disc 1 revision 0 with `sys/boot.bin`, `sys/main.dol` and
the complete `files/` tree, including `files/movie` and `files/title.fsys`.
The supplied set was checked against all 1,872 FST file sizes; its DOL and 25
canonical REL hashes match the pinned build manifest. The converters separately
verify the DOL, exact movie hashes, title archive hash and pinned parser files.

Clone this branch with its pinned submodules. Movie conversion needs host
FFmpeg. Title conversion needs Python 3, NumPy and the pinned importer source;
Blender is not needed for the title.

```sh
git submodule update --init --recursive
make verify-source
python3 tools/prepare_boot_media.py --disc /path/to/GC6E01 --output private/boot
python3 tools/prepare_title.py --disc /path/to/GC6E01 --output private/boot/title.c3t
```

Both tools refuse to overwrite existing output. Keep the manifests beside the
prepared assets. Copy the prepared directory's contents to the SD layout:

| Prepared output | SD destination |
| --- | --- |
| `private/boot/tpc.c3v` | `/3ds/Colosseum3DS/boot/tpc.c3v` |
| `private/boot/gs_logo.c3v` | `/3ds/Colosseum3DS/boot/gs_logo.c3v` |
| `private/boot/title.c3t` | `/3ds/Colosseum3DS/boot/title.c3t` |
| Optional real save exported as GCI | `/3ds/Colosseum3DS/saves/pokemon_colosseum.gci` |

The three prepared assets occupy about 48 MiB. Movies are streamed through one
153,600-byte video buffer and a small PCM queue. The title has 31 rigid joints,
43 batches, 17,277 expanded vertices / 5,759 triangles and 20 texture descriptors.
Its 18,353,048-byte pack contains 3,439 states sampled at 60 Hz. Current host
resident title allocations total 19,678,020 bytes before GPU storage. That
figure is not total application RAM; it excludes driver, framebuffer, command,
stack and other allocations. See [C3V1](boot-media-format.md) and
[C3T1](title-format.md) for exact formats and fidelity limits.

## Build and run

```sh
make test
make -f Makefile.3ds -j2
```

The normal output remains `build-3ds/Colosseum3DS.3dsx`. It includes the existing
Gekko model-preview route and the new title route. A separate focused target,
`make -f Makefile.3ds C3_BOOT_ONLY=1`, writes `build-3ds-boot/`; it omits the C3M
preview dependency and always enters boot diagnostics. Both use the same movie,
title, input and save implementations.

The normal build selects boot when `/3ds/Colosseum3DS/boot/` exists. Missing or
malformed required packs then produce an explicit error. Without that directory,
the original C3M preview/smoke-test selection still applies. The runtime writes
an event trace to `/3ds/Colosseum3DS/boot-trace.log` with its embedded build ID,
movie completion counts, title transitions and card-check result.

START+SELECT exits. Circle Pad, C-stick, D-pad, face buttons, shoulder buttons
and digital trigger values reach the GameCube-style native input structure;
the diagnostic shows them. Title-specific actions use rising edges. A press
must last long enough to be observed by an application frame: the current
renderer has not been profiled on physical New 3DS hardware.

The title renderer owns Citro3D while active. It uses a bounded vertex arena,
static shader storage with a correct DVLB lifetime, validated texture sizes,
and GPU completion before arena reuse or teardown. It follows the title's
material subset directly in Citro3D because the previous Eevee adapter's
8,192-vertex / 16-texture capacities do not fit this scene. The native executable
does not contain a GameCube CPU emulator, original PPC DOL execution or REL
execution.

## Reproduce the sandbox checks

The optional [Azahar/libretro harness](../tools/sandbox/README.md) uses the supplied
prebuilt core; it does not build all of RetroArch. It runs an isolated New 3DS SD
directory, accepts bounded controller scripts, captures actual pixels and binds
the executable, emulator, input and selected SD asset hashes into `run.json`.
It returns failure when its deadline interrupts an incomplete requested run.

Example after preparing the harness and staging the private assets:

```sh
python3 tools/sandbox/run.py build-3ds/Colosseum3DS.3dsx \
  --state /path/to/private-emulator-state --out /path/to/new-run \
  --frames 1620 --seconds 60 --capture-every 80 --gl \
  --events tools/sandbox/boot-natural-input.csv \
  --sd-file 3ds/Colosseum3DS/boot/tpc.c3v \
  --sd-file 3ds/Colosseum3DS/boot/gs_logo.c3v \
  --sd-file 3ds/Colosseum3DS/boot/title.c3t
```

Software PICA captures the CPU-rendered boot videos reliably. The optional
surfaceless OpenGL/llvmpipe path is much faster for the live title; in this core
it can show black CPU-only output before PICA starts. Evidence therefore uses
software movie captures plus completed OpenGL title/input runs. Neither host
elapsed time nor emulator frame rate establishes physical-console performance.

The native save probe is deliberately separate from game progress. It generates
a deterministic synthetic body, checks an independent ciphertext golden, writes
and replaces a full GCI through libctru, and reopens it in a fresh process.
The independent verifier authenticates every slot and body byte, including the
retained backup. An integration test may stage that explicitly synthetic file
at the normal card path to exercise START/card decoding; that does not turn it
into a playable Colosseum save. See [save format and reproduction](save-format.md).

## Work still required

1. Connect the original memory-card task's decisions, main menu at floor
   `0x3A1`, and genuine state serializer/deserializer. Validate a real existing
   save and a newly created game by reload before claiming playable saves.
2. Render the original menu/widget layer and complete its input routing. The
   current bottom-screen prompt proves native input and state transitions only.
3. Supply the user's own `/3ds/dspfirm.cdc` for NDSP validation, then integrate
   the external GS-logo and title MusyX sequences. The current sandbox reports
   the missing firmware explicitly. Exact music-bank/resource mappings are in
   [boot-audio.md](boot-audio.md); decoding a sample bank alone is insufficient.
4. Compare the sampled title against original output and profile the build on
   the physical New 3DS, including SD stalls, HOME, lid sleep, GPU/CPU time and
   memory. Actual APT events pause application time; slow active I/O does not.
5. Add the attract movie and advance into an actual gameplay scene through the
   native engine adapters. A complete decompilation supplies source; it does
   not replace the missing platform and runtime integration.

Only source, metadata and narrowly scoped validation evidence belong in this
branch. Original disc files, converted packs, GCI files, emulator dependencies
and emulator user state remain private and are excluded from build artifacts.
