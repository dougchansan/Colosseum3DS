# Genuine Colosseum card-file codec and native SD persistence

The save service reads, validates, decrypts, updates, and preserves the original
GC6E01 `pokemon_colosseum` memory-card file. It accepts the raw file or a standard
64-byte GCI directory entry followed by that file. Its encoder reuses the pinned
game's SHA-1 and feedback transform with explicit native-endian fixes.

This is a format and storage boundary. The port does not yet run the game's
complete state serializer, create a genuine new game, or resume gameplay from
an imported save. The service takes an **opaque, already serialized big-endian
game body** from its caller. A settings file or a synthetic test pattern is not
game progress. No user GCI or real progress record was available for this phase;
the tests below explicitly identify their generated data as a format fixture.

## Source and reproducibility

The authority is the executable code in
[`src/game/memcard.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/memcard.c)
at Count-Sheep commit `51bfe4314a218dfaf9592fc80723c50498d0ee55`.
The exact 157241-byte file has SHA-256
`3b0003c7d7971ba05b8fb1933dd5e687288cef1760ef0b664311264ba3ed3cc1`.
The generator refuses a different source. It emits a separate C translation
unit at `build/save_codec.c`; no generated game asset or `.inc` is checked in.

| Original function | Reused behavior |
| --- | --- |
| `SHA1Update`, `fn_801CC380`, `fn_801CBF64`, `fn_801CBBAC` | SHA-1 block processing, padding, and digest |
| `fn_801CBE44` | Payload encryption with digest-derived feedback |
| `fn_801CBCDC` | Payload decryption and SHA-1 verification |
| `fn_801CF320` | Encoder ordering, reserved words, encrypted-header sum, and random padding |
| `fn_801CF7E4` and surrounding card states | Verification and retry of an older slot |

`tools/prepare_save_codec.py` extracts the first six functions into a standalone
unit. The adapter in `src/save.c` implements bounded parsing and storage rather
than bringing across GameCube CARD threads and callbacks. Native changes to the
extracted code are explicit: SHA-1 message words are loaded big-endian, scratch
buffers are local and aligned, integer types have fixed widths, and helper
symbols are private. The original shared hash/key arrays become local arrays.
Array parameters use equivalent pointer spelling to avoid a GCC false positive
when it specializes the SHA-1 padding path. The upstream MIT license remains in
the pinned source submodule.

Some provisional declarations/comments in upstream `include/game/save/save.h`
do not agree with the actual functions. They are not the format specification.

## File and slot layout

All multibyte fields in the original card data are **big-endian**. The raw file
is exactly `0x60000` bytes, or 48 GameCube memory-card blocks. A GCI is exactly
`0x60040` bytes (393280 bytes). The optional GCI entry must identify game/maker
`GC6E01`, filename `pokemon_colosseum`, and a 48-block file. Other regions are
outside this pinned target.

| Raw file offset | Size | Contents |
| ---: | ---: | --- |
| `0x00000` | `0x6000` | Comment, banner, icon, and related card metadata |
| `0x06000` | `0x1E000` | Slot 0 |
| `0x24000` | `0x1E000` | Slot 1 |
| `0x42000` | `0x1E000` | Slot 2 |

Add `0x40` to these offsets when addressing the GCI envelope. The big-endian
32-bit sum of raw bytes `[0, 0x3A40)` is compared with the word at `0x3A40`.
An artwork checksum mismatch is exposed in `C3SaveInfo`; it does not discard an
otherwise authenticated game body because the original game can repair that
region.

| Slot offset | Size | Meaning |
| ---: | ---: | --- |
| `0x00000` | 2 | Valid/initialized bytes, both 1 |
| `0x00004` | 4 | Positive signed save generation |
| `0x00008` | `0x1DFD0` | Original serialized game body |
| `0x0000C` | 4 | Encrypted-header checksum; zero during payload hashing |
| `0x00018` | through the hashed body | Start of the feedback transform |
| `0x00020` | 4 | Reserved word cleared by the original write path |
| `0x1DFD8` | 20 | Random padding, written after encryption |
| `0x1DFEC` | 20 | SHA-1 of the normalized plaintext header and body |

The word at `0x0C` makes the first eight encrypted big-endian words sum to zero
modulo `2^32`. It is cleared before decryption and digest validation. The digest
covers `0x1DFD8` bytes. The first feedback key is the bitwise complement of that
digest; subsequent keys are SHA-1 of the preceding 20 encrypted bytes. The final
20-byte transform block overlaps the first four bytes of the padding, which is
then overwritten by the original write path. The adapter retains this boundary
exactly.

The reader checks all three slot headers before selecting a payload. It tries
positive generations from newest to oldest; if the newest payload fails its
digest it retries an older slot, records the rejected bit mask, and reports that
recovery occurred. Equal generations select the first slot. An initialized file
with no positive generation returns `C3_SAVE_EMPTY`. An invalid slot header
returns `C3_SAVE_SLOT_HEADER`; it is not silently skipped.

## API boundary

The public declarations are in `include/colosseum3ds/save.h`.

| API | Use |
| --- | --- |
| `c3_save_inspect` | Authenticate a memory buffer and report the selected slot, counts, recovery, and artwork checksum |
| `c3_save_decode` | Copy the selected authenticated serialized body to a caller buffer |
| `c3_save_encode_slot` | Encode a supplied real body, positive outer generation, and 20 padding bytes |
| `c3_save_update` | Replace the next slot in an existing image while preserving its envelope, artwork, and other slots |
| `c3_save_load_file` | Read and authenticate a raw/GCI file; use its backup if the primary is absent or invalid |
| `c3_save_store_file` | Validate and persist the entire file with a recoverable previous copy |

Inspection and decoding do not alter their input. Encoding, in-memory updates,
and body output leave their destination unchanged on failure. The native API
accepts unaligned input; the codec itself receives aligned bounded work memory.
The encoder reproduces the reserved-word normalization at body offsets `4` and
`0x18` but does not manufacture trainer data or update other semantic fields.

`c3_save_update` chooses the slot after the selected valid slot, wrapping modulo
three. Its generation is one more than the newest observed positive generation,
including a newer failed payload. It rejects overflow past `INT32_MAX`. A caller
must still supply the real internal game save count, card serial, and all other
updated state. Directly copying native little-endian structs into the body would
not meet this requirement.

The next integration dependency is the real game's state lifecycle. The pinned
[`hero.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/hero.c)
`savedataCreate` initializes multiple game subsystems, including trainer,
Pokémon, party/box, inventory, flags, and world state. `memcard.c` then accesses
that live state through `savedataGetStatus`, `gamedatasaveGetStatus`,
`gamedatasaveSetStatus`, `heroGetStatus`, and `memcardSetSaveId`. Those structures
and their pointer/endian boundaries need deliberate native adapters before the
game can continue, start a real new game, or save progress. The boot/title build
can report the presence and integrity of an imported card without claiming that
this later integration exists.

## SD persistence and recovery

The caller chooses a path such as
`sdmc:/3ds/Colosseum3DS/saves/pokemon_colosseum.gci` and creates its parent
directory. Raw files stay raw; GCI files retain their exact envelope. The service
has no console network access or physical memory-card access.

The transaction uses three filenames: the primary path, `.tmp`, and `.bak`.
This is intentional because libctru's overwrite rename deletes the destination
before renaming; it cannot safely stand in for POSIX atomic replacement. See
the pinned implementation in
[`archive_dev.c`](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/source/archive_dev.c#L961-L970).

1. Validate the new image and any current primary file.
2. Create `.tmp` exclusively; write, flush, synchronize, close, and compare its
   entire readback with the input.
3. While the verified primary still exists, remove an obsolete backup if
   necessary. Rename the primary to the now-absent `.bak` path.
4. Rename `.tmp` to the now-absent primary path and compare its readback again.
   Retain the verified previous file as `.bak`.

An error after step 3 can leave a valid backup and a temporary replacement.
`c3_save_load_file` validates the primary first and falls back to `.bak`; its
`used_backup` output makes recovery visible. An orphan `.tmp` is never silently
overwritten: a later store returns `C3_SAVE_PENDING_FILE`. Inspect and explicitly
recover or remove that file before retrying. A corrupt existing primary is
preserved for explicit recovery rather than replaced automatically.

Only one writer may operate on a given set of paths. The backend bounds its
working memory to one file readback buffer of at most 393281 bytes plus a
122880-byte codec work buffer, in addition to caller-owned input/output. Flush,
readback, and backups protect the previous verified bytes through the tested
failure paths. They cannot promise survival of physical SD failure or establish
FAT power-loss behavior. Tests on real New 3DS hardware remain necessary.

## Host checks and an independent golden vector

From the repository root:

```sh
python3 tools/prepare_save_codec.py
cc -Iinclude -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror \
  tests/test_save.c src/save.c build/save_codec.c -o build/test_save
./build/test_save
python3 tests/save_reference.py
```

The C test passed 87 checks, including standard SHA-1 vectors, fixed encrypted
bytes, endian/unaligned handling, malformed envelopes, every-slot header checks,
corrupt-newest fallback, all-payload failure, unchanged outputs on errors,
generation overflow, actual file readback, orphan preservation, and an injected
failure of the final rename followed by backup recovery. The same test passed
AddressSanitizer and UndefinedBehaviorSanitizer. The injected rename test is a
host test; it is not compiled into the 3DS executable.

`tests/save_reference.py` independently constructs the test vector with bytewise
XOR and Python `hashlib`, rather than calling the C codec. Its body is a simple
deterministic byte pattern, with generation `0x01020304` and padding `A0`–`B3`.

| Fixed quantity | Expected digest |
| --- | --- |
| Plaintext SHA-1 stored in the slot | `a54a31da326f8cfaeea2d405f243660dcb152641` |
| Entire encoded slot SHA-1 | `a2c9db0642fcf4c2a981b504f2e23e38dc32040e` |
| Entire encoded slot SHA-256 | `4856dfce8a39c6b80a171ba7645a59ac2625f3f338dbacccc2f04edb954cbecc` |

This checks an independently generated source-derived vector. It is not a
golden file exported from a real game session.

## Reproduce the actual ARM/SD probe

`tools/sandbox/save_probe.c` links the real service and generated codec on ARM.
It checks the fixed encoded-slot digest, generates the clearly labeled fixture
at runtime, and uses only
`sdmc:/3ds/Colosseum3DS/test-save/synthetic-format.gci` plus its backup, temporary
file, and a diagnostic `result.txt`. It never touches the separate harness
probe or a configured game-save path. A presses save; START exits.

Set `DEVKITPRO` to an existing local installation. From the repository root:

```sh
python3 tools/prepare_save_codec.py
"$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc" \
  -g -O2 -Wall -Wextra -Werror -std=gnu11 -mword-relocations \
  -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ \
  -I"$DEVKITPRO/libctru/include" -Iinclude \
  -L"$DEVKITPRO/libctru/lib" -specs=3dsx.specs \
  tools/sandbox/save_probe.c src/save.c build/save_codec.c \
  -lctru -lm -o build/save_probe.elf
"$DEVKITPRO/tools/bin/3dsxtool" build/save_probe.elf build/save_probe.3dsx
```

Prepare the isolated emulator harness as described in `tools/sandbox/README.md`.
Use a private persistent state directory and fresh output directories:

```sh
cd tools/sandbox
python3 run.py ../../build/save_probe.3dsx --state /absolute/private/state \
  --events save-probe-input.csv --out runs/save-write --frames 240
python3 run.py ../../build/save_probe.3dsx --state /absolute/private/state \
  --out runs/save-reload --frames 180
python3 run.py ../../build/save_probe.3dsx --state /absolute/private/state \
  --events save-probe-input.csv --out runs/save-replace --frames 240
python3 run.py ../../build/save_probe.3dsx --state /absolute/private/state \
  --out runs/save-final-reload --frames 180
```

On a fresh test directory the first write creates generations 10, 11, 12. The
first reload should report launch generation 12 and zero new writes. The next
write advances to 13 and retains the previous full GCI in `.bak`. The final
fresh-process reload must report 13 without any button input or emulator
savestate. For this exact fresh sequence, verify both files independently:

```sh
python3 verify_save_probe.py --expect-latest 13 \
  /absolute/private/state/save/Azahar/sdmc/3ds/Colosseum3DS/test-save/synthetic-format.gci \
  /absolute/private/state/save/Azahar/sdmc/3ds/Colosseum3DS/test-save/synthetic-format.gci.bak
```

The verifier checks the GCI envelope, all three encrypted-header sums, every
payload SHA-1, and every plaintext byte against the known synthetic pattern.
It does not invoke the C implementation and writes no save data. Inspect the
captured native screen and `result.txt` as well as the verifier output.

This sequence was exercised with the supplied Azahar libretro 2126.0 in its
New 3DS software-renderer mode. The actual ARM codec matched the independent
golden digest; first-write, replacement, and fresh-process reload succeeded.
An additional replacement reached generations `[13, 14, 12]` with backup
`[13, 11, 12]`, and the Python verifier authenticated both complete files.
This establishes ARM execution and persistence through libctru in that emulator.
It does not establish physical hardware behavior, a real imported progress
save, or the missing game's serialization and resume path. Keep generated GCI
files, emulator state, captures, and binaries private and outside commits.
