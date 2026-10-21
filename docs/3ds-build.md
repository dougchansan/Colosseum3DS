# New 3DS bring-up

This target compiles selected decompiled C code and a native ARM platform layer.
The top screen draws original test geometry with citro3d. The bottom screen
reports the core self-test, the data probe and mapped controller input. This
target does **not** execute the complete Colosseum game loop yet.

## Build

Install devkitPro's current `3ds-dev` package group using its supported installer
or package manager. It supplies devkitARM, libctru, citro3d, picasso and 3dsxtool.
Make sure `DEVKITPRO` points to that installation and `DEVKITARM` points to its
`devkitARM` subdirectory. Then, from the repository root:

```sh
git submodule update --init --recursive
make -f Makefile.3ds -j2
```

The outputs are `build-3ds/Colosseum3DS.3dsx`, `.smdh` and `.elf`. A successful
host build alone does not establish that these ARM artifacts build or boot.
An actual ARM build succeeded on October 21, 2026; the result, toolchain
versions and output hashes are in [VALIDATION.md](VALIDATION.md). The CI runner
and real-device gates remain separate.

## Run

Copy the `.3dsx` and `.smdh` into `sd:/3ds/Colosseum3DS/` and launch with an
up-to-date homebrew launcher on a New Nintendo 3DS, New 3DS XL or New 2DS XL.
The executable rejects original-model 3DS/2DS hardware. It requests New 3DS
speedup through libctru; the diagnostic does not claim a measured CPU speed.

Place locally prepared game data under `sd:/3ds/Colosseum3DS/data/` following the
repository's data-preparation instructions. Missing data is reported on-screen
and is never treated as successful game boot. Assets are not part of the test
geometry or ARM executable.

| Control | Bring-up behavior / intended GameCube mapping |
| --- | --- |
| A | Pause/resume test rotation / A |
| B, X, Y | Same letter; X also retries the data probe |
| Circle Pad | Rotate test shape / main stick |
| C-stick | Display analog values / GameCube C-stick |
| D-pad | GameCube D-pad |
| L / R | Digital shoulders, analog value 0 or 255 |
| ZL or ZR | GameCube Z |
| START | GameCube START |
| START + SELECT | Exit to the homebrew launcher |

The platform uses `aptMainLoop` for HOME/sleep lifecycle handling and a monotonic
tick clock. Render movement clamps long elapsed intervals after resume. The
GPU path checks initialization and allocation failures, releases resources on
exit, and displays failures instead of proceeding with invalid resources.

## Device acceptance record

For a hardware check, record the commit, exact device model, launcher version
and result for each of the following. Until that record exists, hardware
operation remains unverified.

1. Launch and observe a colored rotating octahedron on the top screen.
2. Confirm the selected native decomp/core self-test reports PASS.
3. Confirm a missing or malformed data set reports NOT READY.
4. Confirm a correctly prepared data set passes its probe; this is still a
   filesystem/format check, not evidence of gameplay.
5. Check all mapped buttons, Circle Pad, C-stick and shoulder values.
6. Enter HOME or close the lid, resume, and verify rendering/input continue.
7. Exit with START + SELECT and relaunch. Also close the application from HOME;
   that exit path must not wait for disabled VBlank callbacks.

## Primary platform references

- [devkitPro installation](https://devkitpro.org/wiki/Getting_Started)
- [libctru APT and New 3DS detection](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/services/apt.h)
- [libctru clocks and speedup](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/os.h)
- [libctru HID, including IRRST scanning](https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/source/services/hid.c)
- [citro3d render lifecycle](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/include/c3d/renderqueue.h)
- [official GPU example and build recipe](https://github.com/devkitPro/3ds-examples/tree/be2001fee08cf8ec7d3366e095e83f6f75da8942/graphics/gpu/simple_tri)
