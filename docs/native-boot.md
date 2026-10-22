# Native boot, New Game and Outskirt Stand

This branch connects the original boot logos and animated title to a native
main menu, source-defined New Game state, the original opening movie, the bike
arrival at Outskirt Stand, and a bounded field scene with Wes movement and
explicit save/Continue. The original menu renderer, NPC event graph, battles,
external MusyX music and later areas remain unported.

It builds on repaired PR #2 head
[`07d4df55cb25`](https://github.com/dougchansan/Colosseum3DS/commit/07d4df55cb25ae4ce4ade12904f2cf1348cd418f).
The Eevee/Gekko route remains present. This branch does not deploy to the
physical console; hardware validation is a separate local-agent step.

## Connected flow

| Stage | Native behavior | Boundary |
| --- | --- | --- |
| TPC and Genius Sonority logos | Original 75/130-frame movie timelines, streamed at nominal 30000/1001 fps | Embedded PCM needs DSP firmware; external BGM is not connected |
| Title | Live original geometry, three animation clips, authored camera and supported materials | Sampled animation and texture resizing; original window engine absent |
| Main menu | New Game, Continue, Options and title return; original menu text can be prepared privately | Bottom-screen native layout; battle choices report unavailable |
| Name choice | NEW NAME, WES, SETH, THOMAS; seven-character custom ASCII keyboard and confirmation | Original graphical keyboard/font absent |
| New Game | Original initial game-state body, trainer RNG, starters, items, storage, flags and other subsystems | See the exact initialization scope in [game-state.md](game-state.md) |
| Opening | Original 1,926-frame hideout movie, then flag `0x08D0` and entry to floor 1 | External BGM `0x0495` and four source SFX cues absent |
| Arrival | Original attached bike models and camera, source fade sequence, then flag `0x0AF8` | A native lowering of the reviewed entry sequence; no general GSVM |
| Outskirt Stand | Original map, Wes idle/walk/run, source camera and collision, native pause/party menu | NPC dialogue, dynamic NPC collision, battles and exits unavailable |
| Save/Continue | Explicit verified GCI save, recoverable backup and fresh-process reload | Native area-entrance checkpoint; original PC save interaction absent |

The flow derives from pinned [movie.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/movie.c),
[gs_title.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title.c),
[menuNameEntry.c](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/menuNameEntry.c),
and the privately admitted `S1_out` GSVM bank. The two logos and opening movie
have no ordinary skip action. A/B/START skips the title entrance through its
half-second fades. `autodemo01.thp` is an attract movie and is not connected.

## Prepare private assets

Use an extracted GC6E01 disc 1 revision 0 with `sys/boot.bin`, `sys/main.dol`
and the complete `files/` tree. The supplied disc was checked against all 1,872
FST sizes and the DOL plus 25 canonical REL hashes. Each converter also admits
its exact source archives/movies before decoding. The private build repository
alone does not contain the complete asset filesystem.

Initialize the pinned submodules. Host preparation requires Python 3, NumPy,
FFmpeg and the pinned HSD importer; Blender is not needed for this route.
Each command below requires a new output path; existing outputs are preserved.

```sh
git submodule update --init --recursive
make verify-source
python3 tools/prepare_boot_media.py --disc /path/to/GC6E01 --output private/logos
python3 tools/prepare_title.py --disc /path/to/GC6E01 --output private/title.c3t
python3 tools/prepare_game_menu.py --disc /path/to/GC6E01 --output private/menu.c3u
python3 tools/prepare_game_state.py --disc /path/to/GC6E01 --output private/newgame.c3g
python3 tools/prepare_intro_media.py --disc /path/to/GC6E01 --output private/intro
python3 tools/prepare_outskirt.py --disc /path/to/GC6E01 --output private/outskirt
```

Keep each preparation manifest privately beside its output. Install these files
under `/3ds/Colosseum3DS/boot/` on the SD card:

| Prepared files | SD filenames |
| --- | --- |
| `private/logos/tpc.c3v`, `private/logos/gs_logo.c3v` | `tpc.c3v`, `gs_logo.c3v` |
| `private/title.c3t` | `title.c3t` |
| `private/menu.c3u`, `private/newgame.c3g` | `menu.c3u`, `newgame.c3g` |
| `private/intro/openingdemo.c3v` | `openingdemo.c3v` |
| `private/outskirt/outskirt.c3f` | `outskirt.c3f` |
| All `private/outskirt/outskirt_*.c3t` files | Preserve every filename |

The opening pack alone is 304,066,812 bytes. It is streamed through one
153,600-byte video buffer and bounded PCM buffers. It is never read whole into
3DS RAM. Logo/title packs add about 48 MiB. Geometry packs are bounded separately
and a single shared GPU arena renders the multiple field models.

A user's existing save may be copied, with a backup kept, to
`/3ds/Colosseum3DS/saves/pokemon_colosseum.gci`. Do not copy a synthetic codec
fixture into that location as game progress. The semantic loader rejects an
invalid game body, and Continue is enabled only for the currently supported
floor and prepared assets. New Game requires confirmation before allowing a
later explicit Save to replace an existing card file. Creating the in-memory
game itself performs no save write.

## Build and controls

```sh
make test
make -f Makefile.3ds -j2
```

The normal output is `build-3ds/Colosseum3DS.3dsx`. It retains the earlier
Gekko model-preview route. A focused `C3_BOOT_ONLY=1` build uses the separate
`build-3ds-boot/` directory and always enters the boot route. Both use the same
movie, title, menu, game-state, field, input and save code.

The normal build selects this flow when the `boot/` SD directory exists.
Without it, the previous C3M preview/smoke-test selection still applies.
Missing or malformed packs produce an explicit error instead of silently
advancing the story.

- A/B/START advances the title entrance; A/START at PRESS START opens the menu.
- D-pad/Circle Pad navigates menus, A confirms and B returns.
- New Game asks for confirmation and a name. Choose WES and confirm it to play
  the original opening and arrival.
- Circle Pad/D-pad moves Wes. X/START opens Resume, Pokemon, Save and Main Menu.
- Save requires confirmation and records the area entrance, party and progress.
  Continue restores that checkpoint. It does not restore arbitrary coordinates.
- Main Menu requires confirmation before discarding unsaved in-memory progress.
- START+SELECT exits the application.

The runtime writes `/3ds/Colosseum3DS/boot-trace.log`: embedded build ID, movie
results, menu pages, save load/write outcomes, field transitions, mapped input,
Wes position and animation. Physical-console performance is not inferred from
host or emulator timing. Actual APT suspension pauses application time; active
SD/GPU stalls still count, while movement bounds a single stalled update.

## Verification and remaining work

`make test` covers the portable codecs, bounded parsers, native menu decisions,
New Game state, transactional save behavior and collision/movement adapters.
The optional [sandbox harness](../tools/sandbox/README.md) uses a supplied
prebuilt Azahar core, isolated SD state and scripted controller input. Its run
records bind executable/core/input/SD hashes and actual captures, and an
interrupted deadline is reported as an incomplete run.

The [game-state verification](game-state.md) includes a native ARM New Game save
and a fresh emulator process loading it. An independent Python implementation
checks all 122,832 decoded body bytes against the pinned initializer, including
the party and flags. Synthetic fixtures remain separate from this retail-data
check. Full-flow verification and exact limitations belong in the checkpoint's
validation record; hardware results must be recorded separately.

Next work is the Outskirt Stand NPC/script graph and first battle, original
window rendering, external MusyX music/SFX and physical New 3DS profiling.
The sandbox lacks the user's DSP firmware, so audible playback and A/V sync
are not established here. See [boot-audio.md](boot-audio.md) for source music
resource mappings and [outskirt-field.md](outskirt-field.md) for scene scope.

Only source, metadata and scoped validation evidence belong in this branch.
Original disc files, converted packs, GCI saves, emulator dependencies and
emulator user state stay private and are excluded from source/build artifacts.
