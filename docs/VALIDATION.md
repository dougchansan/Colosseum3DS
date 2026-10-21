# Initial native build validation — October 21, 2026

## Result

The first native New 3DS application compiled, assembled its PICA shader and
linked successfully. This is an actual devkitARM build, not a make dry run.
Physical hardware execution has not been performed. The game itself is not
playable in this milestone.

The compiled C/Makefile source is the implementation at
[`6543c2b2`](https://github.com/dougchansan/Colosseum3DS/commit/6543c2b2df6c8f17284e77e55c2c6c1aaad84349).
Subsequent validation documentation and package-name corrections do not alter
that C source. The upstream decomp is pinned at
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

## Checks completed

| Gate | Result |
| --- | --- |
| Pinned source files | Eight exact SHA-256 matches for selected upstream sources, headers, manifest and notices |
| Host native-core tests | RNG golden sequence, collision helper, decompression, stat startup checks and missing-data handling passed |
| Pokémon source adapter | 30 checks passed |
| FSYS/LZSS | Six groups passed, including malformed input, overlap, window wrap, streaming and output limits |
| Original-input verifier | Three synthetic-fixture tests passed |
| AddressSanitizer/UndefinedBehaviorSanitizer | All host executables passed; LeakSanitizer was disabled because this sandbox cannot inspect process metadata |
| ARM compilation | All current native/decomp C compiled; C11 atomic guard linked |
| PICA shader | Assembled with Picasso |
| ARM link | Successful; no strong undefined symbols |
| File formats | ELF32 little-endian ARM EABI5 hard-float; valid 3DSX and SMDH headers |
| Physical New 3DS | Pending |
| Game title/battle/overworld | Not implemented |

ELF section sizes are 157,824 bytes text, 7,616 bytes data and 19,392 bytes BSS.
These describe the bring-up executable, not the future game's memory budget.

## Build outputs

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `Colosseum3DS.3dsx` | 181,088 | `75d5c4b93781f68bf25f3cc8f58d49376d70e2564d793f3101eff3b4d0a167a8` |
| `Colosseum3DS.smdh` | 14,016 | `aa4ea04bc11d878a77367dd4ac801367070b140108c7b16cedee49f83834daf6` |

The package contains original smoke-test geometry and compiled source code,
with licenses/notices. No disc images, original DOL/REL files or extracted game
assets are included.

## Toolchain record

Official devkitPro package downloads were verified against SHA-256 values in
the official package index and extracted into a task-local installation.
The complete package record is `config/toolchain-validated.json`.

| Component | Version |
| --- | --- |
| devkitARM GCC | 16.1.0-1 |
| Binutils | 2.46.0-1 |
| Newlib | 4.6.0.20260123-5 |
| devkitARM rules | 1.6.0-4 |
| libctru | 2.7.0-1 |
| citro3d | 1.7.1-2 |
| Picasso | 2.7.2-3 |
| 3dstools | 1.3.1-3 |

With that installation's `DEVKITPRO` and `DEVKITARM` set, the actual command was:

```sh
make -f Makefile.3ds -j2
```

## CI and private-reference evidence

The new repository's
[Actions run 39684365653](https://github.com/dougchansan/Colosseum3DS/actions/runs/39684365653)
failed before any runner executed a step. Retrying the native job had the same
result. No compiler log was produced, and the connector did not expose the
failure annotation. The root cause is unconfirmed; this must not be described
as a compiler failure, an Actions pass, or a diagnosed billing issue. Local
ARM validation above was completed independently of that runner failure.

The pinned private build has separate successful evidence:
[run 38674909620](https://github.com/dougchansan/pkmn-colosseum-build/actions/runs/38674909620)
at `eae433c46b8b968dd7dcab72cf85351c8b9c7966` verified all 26 canonical original
hashes before and after image creation, then published the digest recorded in
`source-lock.json`. The manifest is byte-identical to the pinned decomp's
manifest. See `PORTING.md` for the narrower in-session byte checks.

## Next acceptance step

Run the `.3dsx` on a physical New 3DS family device and complete the checklist
in `docs/3ds-build.md`. Missing `common.fsys` is expected for a data-free smoke
test and should be reported as NOT READY while rendering and controls remain
available. A passing archive probe is not evidence that gameplay works.
