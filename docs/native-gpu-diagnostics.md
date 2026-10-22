# Native GPU shader correction and scene diagnostics

## Status

The physical first-title GPU stall was fixed upstream in
`4cce6c157b065757e9c6512a9ceea51baa96ba1c`. Both texture-coordinate shader
outputs now write all configured XYZW components. PR #4's normal build passed
live title rendering, HOME suspend/resume, and clean exit with NTR continuously
active. The tested executable SHA-256 was
`049a08a355e69828145465bdd3e6a990407917e3e72b74de9a41dd11f332fd72`.
See [the hardware report](native-first-area-hardware-2026-10-22.md) and
[render recovery](render-recovery.md). These physical tests were performed by
the local agent; this sandbox did not operate the console.

The earlier null-texture repair fixes the documented ARM11 read at address
`0x0000000C`. The recovery build `7a4a74613d01` established CPU responsiveness
during a still-busy GPU queue; it predates the successful shader correction.
This branch incorporates the actual fix and compiled-output regression.

This change fixes a separate scene-lifetime defect, adds passive completion
counters, and provides a standalone staged probe using the production renderer.
It does not free in-flight memory after a deadline or treat a responsive
recovery screen as a rendering success.

## Confirmed scene-lifetime defect

In pinned [Citro3D 1.7.1 `base.c`](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/base.c),
`C3D_Fini` does not clear `ctx->program`. `C3D_BindProgram` compares the old and
new program pointers before marking the shader code and configuration dirty.
The old native scene teardown destroyed a static shader program and then
recreated it at the same address on the next scene. That violates this SDK's
program-lifetime assumptions and can skip the next shader configuration/upload.
It is separate from a failure on the first title submission.

The shared renderer now keeps its Citro3D context, shader program, and DVLB alive
across scene replacements. Guarded scene teardown releases only the drained
render target, textures, and vertex arena. `c3_stage_renderer_fini`, called from
platform shutdown, finalizes Citro3D before freeing the program and DVLB. Partial
driver initialization and scene allocation failures have distinct cleanup paths.

The stage model limit is now 16. The combined 131072-vertex, 128-unique-texture,
and 32 MiB GPU-storage limits remain. This permits the interior's separate TV
model without requiring duplicate copies of the other scene models. The new
capacity fixture exercises 16 models, disjoint vertex ranges, texture sharing,
complete cleanup, and rejection of a 17th model before driver allocation.

## Completion evidence

The renderer counts `PSC0` memory-fill, `P3D` command-list, and `PPF`
display-transfer completion callbacks. These are advisory cumulative counters,
read without GPU IPC. In pinned
[libctru 2.7.0 `gspgpu.c`](https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/services/gspgpu.c),
the event thread calls `gxCmdQueueInterrupt` before the public event callback.
The instrumentation therefore preserves queue accounting and the SDK's VBlank
callbacks. [Queue completion code](https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/gpu/gxqueue.c).

Recovery trace snapshots now include submissions, completion counts, post-end
command words, draw calls, submitted vertices, and active scene allocations.
Command usage is sampled after `C3D_FrameEnd`; the earlier pre-end zero was a
cached SDK value. Compare counter changes around a submission, since the counts
are retained across scene replacements.

| Observed counter change | Next investigation |
| --- | --- |
| No fill completion | Clear/submission/service path; shader execution has not been established. |
| Fill completes, command-list does not | PICA command list, shader, attributes, geometry, or material state. |
| Command list completes, transfer does not | Output transfer and framebuffer ownership. |
| Expected completions arrive but begin stays busy | Software queue accounting or synchronization; inspect the exact event sequence. |

The counters alone do not prove correct pixels or identify a particular faulty
GPU register. A clear-only submission intentionally has no command-list event.

## Standalone probe

Build with the project's pinned devkitPro SDK from `tools/sandbox`:

```sh
make -f gpu_submission_probe.mk DEVKITPRO="$DEVKITPRO" C3_BUILD_ID="$(git rev-parse --short=12 HEAD)"
```

Use an identifier that describes the actual source being built; do not label a
dirty development build as a tested remote commit. The executable is
`gpu-probe-build/gpu-probe.3dsx`. It links no game-state or save implementation.
Its optional original title input is
`sdmc:/3ds/Colosseum3DS/boot/title.c3t`.

The default sequence is:

| Stage | Submission |
| --- | --- |
| 0 `clear` | Render-target memory clear and display transfer; no shader draw. |
| 1 `array` | Synthetic colored-corner quad through the production vertex-array path. |
| 2 `array-fade` | The same quad plus the production immediate fade overlay. |
| 3 `title-array` | Original title clip 0/sample 0, without fade. |
| 4 `title-fade` | Original title plus the production fade overlay. |
| 5 `scene-return` | Drained replacement of the title with a new synthetic scene. |

Before each submission, immediately after it, and after its queue drains, the
probe flushes a checkpoint to `sdmc:/3ds/Colosseum3DS/gpu-probe.log`. It pauses
briefly at each completed stage for NTR capture. The first three stages can run
without original title data, but the complete sequence requires that pack.

Optional `gpu-probe.txt`, in the same SD directory as the log, contains a start
stage `0..5` and optionally an original-title batch limit `1..64`. For example,
`4 64` starts with the original title and fade as the first GPU draw; `3 1`
tests the first original-title batch. A batch limit hides later materials while
retaining the original vertex arena and texture identities. Starting at stage 5
alone does not exercise a replacement; use the default sequence for that gate.

### Optional device isolation if a new rendering failure occurs

The next acceptance task is the [combined first-visit route](native-first-visit.md).
The following probe workflow remains available for a new, evidenced stall;
it is not an extra prerequisite after the already successful title repair.

1. Preserve the previous `boot-trace.log`, `gpu-probe.log`, executable identity,
   and any crash dump before replacing diagnostic files. Use the existing ftpd
   connection at `192.168.50.154` and its already configured port. Transfer the
   standalone probe to a distinct homebrew path, then RETR and verify its size
   and SHA-256 before launching it. Leave the existing title pack and saves in
   place.
2. Start with the default six-stage sequence. Capture fresh top and bottom NTR
   frames with the installed BootNTR Selector Enhanced 3.6 setup, including the
   build identifier and the first stage that stops progressing.
3. If busy, use X to append a current snapshot. A queues resume and
   START+SELECT requests guarded exit. A hard GPU fault can still require the
   owner's physical reset; no software timeout releases GPU-owned storage.
4. Retrieve the complete probe log at the next ftpd session. Compare the last
   `before-submit`, `submitted`, `completed`, and recovery counters. Preserve
   this log before another run, because the probe creates a new log each launch.
5. If the synthetic stages pass but the title fails, run a fresh `4 64` (title
   and fade first) and then a bounded batch isolation appropriate to the failing
   stage. Change one dimension per run and keep source, binary, config, pack,
   trace, and capture hashes together. Do not repeatedly redeploy a speculative
   shader rewrite without evidence from these boundaries.

## Sandbox validation and its limits

The development probe `gpu-probe-dev` compiled with `-Wall -Wextra -Werror` and
completed all six stages in Azahar with Mesa software OpenGL rendering. The
run reached its 1200 frontend-frame budget, recorded no explicit guest faults,
and ended with `gpu_probe_end complete=1 stages=6`. This is emulator integration
evidence, not hardware speed or physical acceptance.

The actual title submission used 5044 command words; its fade used 5212, out of
the SDK's 65536-word default command buffer. The sequence ended with six fill,
five command-list, and six transfer completions. These sampled submissions do
not support a command-buffer overflow diagnosis. They do not exclude a
hardware-specific command or synchronization defect.

The exact executable SHA-256 was
`ee541316d6e98e1a43d3f2c62926ff0111db23110fa78a9ddd9ee3c7f90e4cd7`.
The read-only title pack was 18353048 bytes, SHA-256
`f6fc95010631de9897a041b5cc6d46232bfd6b0bd3e784ad7065f5024cb9d166`.
See [run metadata](evidence/gpu-submission-2026-10-22/run.json) and
[probe trace](evidence/gpu-submission-2026-10-22/gpu-probe.log). The development
identifier is not a published-commit or hardware-test claim.

Ten focused renderer tests pass normally and under AddressSanitizer plus
UndefinedBehaviorSanitizer. LeakSanitizer cannot inspect this sandbox's `/proc`
and was disabled for that run; resource-count assertions separately verify
partial initialization and final cleanup. The recovery fixture passes 734
checks. Four harness-log tests cover real null reads, guest aborts, bounded
reporting, and tolerated optional service errors.

An older sandbox run, `new-game-save-final`, logged unmapped ARM guest reads at
`0x0000000C` while continuing to produce frames. Its prior visual success must
not be treated as CPU memory-safety acceptance. `tools/sandbox/run.py` now marks
such a run unsuccessful even when the frame budget completes. Historical
evidence has not been retroactively changed or reclassified as a clean run.

## Interior material additions

The original NPC alpha comparison `GREATER 127 AND GREATER 127` is now admitted
and uses the exported reference in `C3D_AlphaTest`. Unequal references in that
path and values above 255 remain rejected. The existing zero-reference path
continues to work.

The interior spotlight's original GX `COS2` angular attenuation is evaluated
on the CPU alongside its distance attenuation, following
[GXInitLightSpot in the pinned decompilation](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/dolphin/sdk_range_800BA1B4.c)
and [HSD light direction setup](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/hsd/lobj.c).
The cone direction dot is clamped before its angular polynomial, as in
[Dolphin's GX lighting implementation](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp).
Unsupported spotlight functions and non-infinite specular lights still fail
closed; this addition does not claim all GX lighting modes are ported.
