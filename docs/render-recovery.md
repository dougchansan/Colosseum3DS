# Responsive render recovery

Native title and Outskirt Stand rendering now return `C3_RENDER_BUSY` after one
nonblocking frame-begin attempt. A busy draw does not overwrite the GPU vertex
arena, change bindings, submit commands, or release resources. The main control
path can therefore keep polling Luma/Rosalina input while waiting for the queue.
This changes application-owned rendering waits; it cannot recover every system
or GPU failure.

## Controls and timing

After three seconds, a CPU bottom-screen recovery view replaces the normal
console. It displays GPU readiness, elapsed wait and input status:

- **A:** request resume once the GPU is ready.
- **X:** write a diagnostic snapshot to the existing `boot-trace.log`.
- **START+SELECT:** request application exit. The request remains latched until
  the queue is idle and teardown is safe.

The loop sleeps between polls, updates the screen periodically and flushes
trace heartbeats. It performs no gameplay updates or save writes. Once a
watchdog-paused scene resumes, its delta-time baseline resets, avoiding a
simulation jump. Ordinary short GPU waits still count toward elapsed game time;
menu button edges consumed during those waits are replayed once.

A scene transition drains without claiming the user asked to quit. An exit
pressed and released during that drain is preserved and prevents the next scene
from starting. Errors, transitions and normal exits all retain model/menu/map
storage until guarded renderer teardown completes.

## Ownership and limits

With pinned Citro3D 1.7.1 / libctru 2.7.0, a successful nonblocking begin followed
by an entirely empty end establishes quiescence without adding GPU work.
`try_shutdown` leaves ownership intact while busy and deletes resources only
after that barrier. This requires sole Citro3D ownership and no frame-end hook
that generates GPU commands. Never free potentially referenced buffers merely
because a timeout elapsed.

CPU recovery remains best effort while APT/HID and other platform services
return. Citro3D's [APT suspend hook](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/base.c)
waits for GPU queue completion, so **HOME can still block during a hard GPU
wedge**. There is no verified public queue-cancellation operation here. A GPU
that never finishes, a blocked system service, a data abort or a kernel failure
may still require physical reset. NTR is not reloaded or modified by this code;
its continued availability is not guaranteed across a system crash/reboot.

## Implementation and validation

`render_recovery.c` provides the cooperative controller. Shared title/field
callers distinguish BUSY from errors and keep scene data alive until drain.
The earlier temporary rendering sequence, console markers and silent parking
loop have been removed; authored geometry, materials and fades remain in place.
The confirmed non-NULL texture-unit-one repair is retained.

Standalone graphics probes also treat BUSY as a skipped submission and guard
teardown. All five graphics variants and the actual-field probe compile/link
with the pinned ARM toolchain; their GPU pixel outputs have not been rerun.

Host tests execute the real recovery loop with deterministic clock/input/queue
fakes: **734 checks**, including live input while busy, trace snapshots and
heartbeats, short-wait button preservation, paused resume, deferred cleanup,
and one-shot exit requests during scene transitions. Executed production draw
fixtures check immediate BUSY return, no GPU-memory mutation while busy and
no teardown before idle. Eleven native suites and ASan/UBSan pass; all 134
Python tests pass with NumPy 2.4.3 / Pillow 11.3.0 and zero skips. These tests establish
control flow and resource ownership, not physical GPU behavior.

Ready implementation: `7a4a74613d01e3a4598da6e26c14f5bb701be83f`.
Normal full build: **315324 bytes**, SHA-256
`d068396046553294eb597f7e38996e3fa393865a8f36e90928e5bbe65fc2b1a1`.
It was committed before the pinned ARM build. Recovery/boot/model objects refresh
their build IDs even for incremental documentation-only rebuilds.
[Build identity](evidence/native-first-area-hardware-2026-10-22/recovery-build.json)
and [validation](evidence/native-first-area-hardware-2026-10-22/recovery-validation.json).

This executable has now been physically tested on the New3DS. The watchdog
entered CPU recovery after three seconds while NTR continued receiving fresh
screens. X appeared as `PAD0400`, A displayed “Resume request queued,” and
START+SELECT displayed “Exit requested.” X remained observable after exit was
pending, and the wait timer continued beyond 221 seconds. All four tested input
actions completed three neutral releases. No NTR reload or reboot occurred
between launch and these control checks.

The GPU queue still did not complete. This establishes CPU input/recovery
control during this rendering stall; **it does not establish a working title or
a completed app exit**. Exit remains deferred to avoid releasing in-flight
storage. The new trace snapshot contents have not yet been retrieved because
ftpd is not running while the app remains in recovery. HOME was deliberately
not tested against the known blocking suspend hook.

[Physical control captures](evidence/native-first-area-hardware-2026-10-22/recovery-inputs.json)
and [deployment verification](evidence/native-first-area-hardware-2026-10-22/recovery-deployment.json)
record the tested run. The wider
[first-area acceptance record](native-first-area-hardware-2026-10-22.md)
remains incomplete; no New Game/Save/Continue result is claimed.
