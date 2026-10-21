# Colosseum3DS

A native **New Nintendo 3DS / New 3DS XL / New 2DS XL** port project for
Pokémon Colosseum, based on Count-Sheep's decompilation and the maintainer's
private GC6E01 build inputs.

**Current milestone: native bring-up. The game is not playable.**
This version builds a homebrew application, executes selected real decompiled
routines on ARM, exercises the PICA GPU and controls, and probes the original
archive format. It does not boot the title screen, render game models, run a
battle, or load/save a game. Supplying disc files does not unlock gameplay yet.

The first `.3dsx` has been cross-compiled successfully with devkitARM GCC
16.1.0. Host and sanitizer checks pass. Partial physical-device validation
proves rendering, native self-tests, the missing-data diagnostic, sampled input,
and exit/relaunch. Guarded deployment/rollback, a real archive on console,
exhaustive controls, HOME behavior and lid sleep/resume remain unverified.
See the [hardware checkpoint](docs/hardware-validation-2026-10-21.md) and the
[historical initial validation record](docs/VALIDATION.md).

## What is implemented

This development branch adds a [native textured Eevee scene](docs/gekkoctr-model-scene.md)
through pinned GekkoCTR components. Its private preparation tool uses actual
skeletal animation; the game itself remains unplayable. A prepared local pack
selects the scene, while missing scene data retains the original smoke test.

| Component | Current behavior |
| --- | --- |
| Native source integration | Three explicitly selected translation units from a pinned, unmodified decomp submodule; exact source/header hashes checked |
| Pokémon stats | Actual `pokemonSetLevelBasisStatus` with a native record adapter: IVs, EVs, nature rounding, HP changes, fainting and Shedinja handling |
| Other game/library routines | Actual HSD random-number generator and GS collision side-plane helper |
| 3DS platform | New-model detection, speedup request, GameCube-style button mapping, Circle Pad/C-stick, monotonic time, SD reads and clean exit |
| Graphics | Citro3D/PICA shader and original rotating geometry for GPU validation; no GX/HSD backend yet |
| Data | Streaming, bounded FSYS reader and LZSS decoder with explicit big-endian fields and caller-owned output buffers |
| Input provenance | Pinned private build reference, canonical hash verifier and local extracted-disc preparation tool |
| Validation | Host regression tests, malformed-data tests, sanitizer job, and a devkitARM cross-build workflow |

The source snapshot is
[`Count-Sheep/pkmn-colosseum@51bfe431`](https://github.com/Count-Sheep/pkmn-colosseum/commit/51bfe4314a218dfaf9592fc80723c50498d0ee55).
That commit reports 100% matched and linked GameCube code. This project has not
independently rebuilt that entire claim. Matching GameCube code still includes
PowerPC assembly, fixed memory addresses, GameCube services and big-endian
binary data. Native ARM portability is a separate measure.

## Build

Clone with the pinned public source submodule:

```sh
git clone --recurse-submodules https://github.com/dougchansan/Colosseum3DS.git
cd Colosseum3DS
make verify-source
make test
```

Host checks need a C11 compiler, Make, Python 3 and Git. Capture image tests
and the NTR capture tool additionally use optional Pillow:

```sh
python3 -m pip install -r requirements-capture-test.txt
python3 -c "import PIL; from PIL import Image; print('Pillow', PIL.__version__)"
python3 -m unittest discover -s tests -p 'test_ntr_capture.py' -v
```

Pillow is not required for FTP tools or native builds. The dedicated CI capture
job installs the pinned dependency and requires its import before running both
image-publication tests, so a missing Pillow installation fails that check.
The New 3DS application
needs the official devkitPro `3ds-dev` group, including devkitARM, libctru,
citro3d and 3ds-tools:

```sh
make -f Makefile.3ds -j2
```

Outputs are `build-3ds/Colosseum3DS.3dsx` and
`build-3ds/Colosseum3DS.smdh`. The `Native port checks` GitHub Actions workflow
also cross-compiles and uploads `Colosseum3DS-native-bringup` after a successful
ARM job. Its artifact contains the application and notices, without disc data.
The workflow records compiler/package versions; the container currently uses
the official devkitPro image pinned by digest in the workflow.

Copy the `.3dsx` and `.smdh` to `sd:/3ds/Colosseum3DS/` and launch using the
Homebrew Launcher on a New 3DS family device. A stock original 3DS/2DS is not
the target. See [3DS build and hardware checks](docs/3ds-build.md).

Controls for this bring-up application:

- Circle Pad rotates the test geometry; A pauses automatic rotation.
- X retries the `common.fsys` directory probe.
- START + SELECT exits.
- The bottom screen displays native self-test, input and archive status.

## Use the private build inputs

The private input reference is
[`dougchansan/pkmn-colosseum-build@eae433c4`](https://github.com/dougchansan/pkmn-colosseum-build/commit/eae433c46b8b968dd7dcab72cf85351c8b9c7966).
It contains the original DOL and 25 RELs. These are useful as byte-identity and
data-extraction references; their PowerPC code cannot execute on ARM.

Using an account that already has access, check out that private repository
beside this one and verify it without modifying it:

```sh
git clone https://github.com/dougchansan/pkmn-colosseum-build.git ../pkmn-colosseum-build
git -C ../pkmn-colosseum-build checkout --detach eae433c46b8b968dd7dcab72cf85351c8b9c7966
python3 tools/verify_inputs.py --orig ../pkmn-colosseum-build/orig/GC6E01
```

The verifier requires all 26 canonical hashes from the pinned decomp. The
normal ARM build does not need private originals, a GameCube compiler, or a
private access token. An optional manual `Verify private reference inputs`
workflow performs the same check inside the verified private build image
pinned by digest in `source-lock.json`;
the Colosseum3DS repository must have read access to that GHCR package.

The private repository does **not** contain the full disc filesystem.
For the archive probe and later asset work, use an extracted **GC6E01 revision
0** disc with `sys/boot.bin`, `sys/main.dol` and `files/`, including
`files/common.fsys`. For example, stage it to a new SD directory:

```sh
python3 tools/prepare_data.py --disc /path/to/extracted-GC6E01 --output /path/to/SD/3ds/Colosseum3DS/data
```

This checks the region/revision and original DOL hash, then copies `files/`
without overwriting an existing destination. It does not verify every asset
hash or convert game models/textures. The runtime checks the `common.fsys`
directory; decoded member support is available to subsequent loaders.

## Next game milestones

1. Extract the canonical translation-unit graph and replace GameCube startup,
   absolute memory accesses and PowerPC cooperative thread switching.
2. Provide native `common_rel`/map module registration and endian-aware tables.
   ARM cannot run the original PPC REL code or its `_prolog` functions.
3. Implement the first HSD model/texture loader and a constrained GX/TEV to
   PICA renderer, then display one actual game scene with measured memory use.
4. Bring up title/menu flow, timing, audio and save services; then a complete
   battle and an overworld scene.
5. Profile on physical New 3DS hardware before setting a performance target.

See [porting decisions and blockers](docs/PORTING.md),
[the Pokémon source slice](docs/pokemon-slice.md), and
[the FSYS reader](docs/data-format.md). No playable-release date or frame-rate
claim is made at this stage.

## Source and asset boundaries

Only source, build configuration and synthetic test fixtures belong in this
repository or its build artifact. Original game files and extracted assets
stay in private/local input directories. The upstream source carries its own
license and provenance notices; see [THIRD_PARTY.md](THIRD_PARTY.md).
