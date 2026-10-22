# Native Eevee scene through GekkoCTR

This development scene displays one real Colosseum Eevee model with its actual
Idle skeletal animation. It does not implement HSD's game-driven scene loop,
title flow, battles, audio or saves. The branch is stacked on the unmerged
[hardware workflow PR #1](https://github.com/dougchansan/Colosseum3DS/pull/1)
at `459e4938b52917b1d8669cd600751e0fa6ba0fcb`; do not merge that PR implicitly.

## Native boundary and exact reuse

The path is extracted GC6E01 asset → offline checked conversion → C3M1 model
and skin palettes → native ARM CPU skinning → GekkoCTR portable scene/material
submission → its Citro3D/PICA backend. No PowerPC execution, guest pointers,
MEM1 arena, FIFO decoder or `gx_submit` is linked.

GekkoCTR is pinned at `0f2032c6d641c78ebf4e6a418ecb51e04c0c62d8` in
`vendor/gekkoctr`. Only `gx.c`, `gx_texture.c`, `gx_material.c`, `gx_scene.c`,
`ctr_gx_citro3d.c` and `p5_material.v.pica` are compiled. A textured draw passes
frontend validation, material cache lookup and backend state binding, copies
vertices into a distinct GPU-visible region, then calls `C3D_DrawArrays`.
Textures use the donor's actual RGBA tiling/upload and sampler implementation.
Roadmap text is not treated as implemented behavior.

The source submodule is unmodified. `prepare_gekko_backend.py` checks its pin and
exact edited source against Git before generating build-local adaptations:

- Raise total frame vertex storage from 256 to 8192, with checked exhaustion.
  Keep the 64-vertex frontend limit; draw triangle lists in chunks of 63.
  Every chunk retains its own storage until the GPU finishes the frame.
- Raise portable/backend texture tables from eight to sixteen, for Eevee's nine
  textures. Geometry is not dropped to fit a demo limit.
- Delete/drain the linked render target before freeing GPU buffers or textures.
  Track partial Citro3D initialization independently from completed startup.
  Do not wait on VBlank callbacks during shutdown. Clean up a failed texture
  staging allocation.

The existing application owns `gfxInitDefault` and shutdown. The model renderer
alone owns RomFS, Citro3D, shader/program, top target and frame boundaries while
the scene is active. The original smoke renderer is used only when no pack is
present, so they never initialize competing graphics contexts.

GekkoCTR and the optional importer are GPLv3. The combined executable follows
GPLv3 distribution/source requirements; existing MIT source notices remain.
See `THIRD_PARTY.md` and the retained donor licenses. Game assets are not covered
by those source licenses and are never committed or put into CI artifacts.

## Reproducible private asset preparation

Initialize source submodules, then use an existing extracted GC6E01 revision 0
disc containing `sys/boot.bin`, `sys/main.dol` and `files/pkx_eievui.fsys`:

```sh
git submodule update --init --recursive
python tools/prepare_model.py --disc EXTRACTED_DISC --blender BLENDER_EXECUTABLE --output PRIVATE_OUTPUT/eevee.c3m
make verify-source
make test
make -f Makefile.3ds -j2
```

Blender 5.x is an offline tool; it is not an ARM dependency. The converter pins
StarsMmd's importer at `dc71de837978a6a2fe8c5082f2840cfafb135235`. It verifies
disc identity and the canonical DOL SHA-1, rejects tracked parser edits and writes
to a new destination. `.c3m` files are ignored. Keep their JSON manifests private
unless selecting sanitized hashes/counts for the validation report.

The pinned importer describes the HSD geometry and raw animation channels.
Blender 5 channelbags replace its removed `Action.fcurves` entry point without
editing the donor. Its bake helpers and actual evaluated poses preserve the
source scale-inheritance behavior. A separate composition path checks every
weighted vertex's deformation against GX-world × IR-rest-inverse, with a `1e-4`
tolerance. The writer refuses a failed check or a motionless selected action.

The selected input is `pkx_eievui.fsys`, member `eievui.pkx`, containing one DAT:

The preparation tool admits only the reviewed archive hash below and reads at
most 1 MiB before parsing/decompression. A modified, oversized or unreviewed
archive is rejected before invoking the donor parser.

| Property | Value |
| --- | --- |
| Archive SHA-256 | `fe69d7d79f7871610c07ff8875925cdbe4459b4e547f34e253452c3dcad0afef` |
| DAT SHA-256 | `d4ae46e8fc0050aeb6742d74e587534feb2d7f31b74eba2474ed5a6bbe267ace` |
| Animation | `pkx_eievui_0_Idle`, source frames 0–40 |
| Bones | 66; 37 influence visible vertices |
| Geometry | 433 source vertices, 1944 expanded vertices, 648 triangles, 11 batches |
| Textures | Nine RGBA images |
| Prepared bytes | 299112 |
| Pack SHA-256 | `20aeca58879947b2ee58a2537a5365e858bd6b3f04aaad898eae56972592f625` |

Seven meshes with the source HIDDEN flag are omitted. The selected visible
vertices each have exactly one bone influence; multi-influence, shape-key or
unsupported texture-coordinate/material inputs fail with an explanation.
Triangle expansion preserves face-loop UV seams and winding. RGBA rows and UVs
are explicitly converted to the portable top-origin convention.

This first material subset is unlit, double-sided, single-texture modulation
with alpha testing. Original lights, specular effects, full GX/TEV combiners,
translucent blend equations and animated texture changes are not reproduced.
These simplifications are shown on screen and in the preparation manifest.
The 64×144 image is nearest-neighbor resampled to 64×256 before upload, preserving
the UV domain rather than changing repeat spacing through padding. Other images
already have supported power-of-two dimensions. Wrap modes remain explicit.

The preview loops a non-looping source Idle action at an explicit default 30
source frames/second. This preview rate has not been measured against the game.
The runtime interpolates baked skin-matrix coefficients between integer samples;
it does not substitute rigid model rotation for skeletal animation.

## C3M1 runtime boundary

All pack fields are explicitly little-endian, independent of big-endian HSD
offsets and GameCube references. The 128-byte versioned header carries counts,
canonical section offsets, all-animation bounds and archive identity. Sections
contain 32-byte vertices, 16-byte batches, 24-byte texture descriptors, row-major
3×4 skin palettes, then RGBA pixels. Native pointers are created only after
validation, and the model owns its buffers until renderer shutdown completes.

Limits are 16 MiB/file, 128 bones, 8192 expanded vertices, 32 batches, 16 textures,
240 sampled frames and power-of-two textures from 8 to 1024. Readers reject
nonfinite or excessive values, bad offsets/reserved fields, invalid bones,
non-triangle batches, overlaps, gaps, truncation and unaccounted trailing bytes.
Position/bounds magnitude is at most 10000; UV and matrix magnitude at most
1e6. Owned-buffer reparse aliases are rejected without freeing the source.
Synthetic tests cover these boundaries and submission beyond the former limit.

Place the prepared pack at `/3ds/Colosseum3DS/data/eevee.c3m`. Its presence is the
deterministic development-scene entry option. An absent pack retains the original
smoke test and archive behavior; a present but invalid pack reports a scene
error rather than claiming success. A pauses/resumes actual skeletal poses;
X retains the archive probe and START+SELECT exits. The camera is stationary.

For the known nested installation, use the isolated scene installer instead of
weakening the existing replacement tool's path checks. With the application
exited and ftpd active, supply a private config containing the full built
`git_commit`, toolchain/build command, and both reviewed original-file hashes:

```sh
python tools/install_model_scene.py --config PRIVATE_CONFIG --sourcebuilddir build-3ds --modelpack PRIVATE_OUTPUT/eevee.c3m --evidence NEW_PRIVATE_EVIDENCE --app-exited
```

It reuses passive FTP inspection/readback, backs up the originals, then creates
only `/3ds/Colosseum3DS/scene-<commit12>/` for the new executable and SMDH. That
directory must not already exist. The pack goes into the fixed application data
path; an existing pack is reused only when its downloaded hash matches exactly.
Every staged upload is downloaded and compared before any rename; all final
files are verified again. The original nested executable is never replaced or
relocated. A partial failure remains isolated with its journal and staging
files for recovery; the multi-file install is not atomic. Launch the recorded
scene directory through Homebrew Launcher and verify its on-screen commit.

## Benchmark and hardware acceptance

After 60 warmup frames, an uninterrupted unpaused run records 600 samples.
On-screen median and p95 cover wall frame interval, CPU skinning, CPU draw
preparation excluding frame-begin pacing, and the prior frame's Citro3D GPU time.
A CSV is written under this application's `data/model-bench-<run-id>.csv`.
The CSV binds samples to the embedded source commit and reports loading time,
draw/geometry counts and tracked memory.

Memory tracking includes owned model data/parsed arrays/palettes, temporary load
copy, linear vertex arenas, GPU texture bytes and texture upload staging. Its
peak is the maximum observed in that tracked subset. It excludes driver/command
buffers, render targets, shader internals and static BSS, and is not total process
memory. Offline Blender process peak is reported separately and is never a 3DS
memory measurement. Received NTR frame counts are not application FPS.

Hardware results, tested commit/binary hash, two distinct poses and benchmark
conditions belong in the accompanying hardware record. Closing the PC capture
does not prove NTR streaming has stopped; a streaming-disabled comparison must
be separately established or marked unmeasured.

The [physical validation record](gekkoctr-hardware-2026-10-21.md) contains that
measured checkpoint and the preserved originals/deployment evidence.

The next game-driven connection is an HSD joint/material traversal feeding the
same native renderer, with multi-weight envelopes, lighting and representative
battle scene state tested as separate milestones.
