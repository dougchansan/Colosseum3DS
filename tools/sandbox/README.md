# Isolated Azahar test harness

This small Linux x86-64 libretro frontend runs the native `.3dsx` in a private
New Nintendo 3DS emulator, records genuine screen output, and supplies a bounded
controller script. It is a development tool, not a general RetroArch frontend.
The emulator and its SD files remain local; no physical console is contacted.

## Prepare and build

The initial verified inputs were the user-supplied
`azahar-libretro-linux-x86_64-2126.0.zip` and `RetroArch-1.22.2.zip`. The latter is a
source archive. Only the core shared library and eight public build headers are
extracted; RetroArch is not built. C compiler, GNU make, Python 3.9 or newer,
`libEGL.so.1`, and `libGL.so.1` are needed. Pillow is optional for PNG conversion
and required for the pixel verifiers below.

Start at the repository root. All subsequent command snippets in this document
run from `tools/sandbox`:

```sh
cd tools/sandbox
python3 prepare_frontend.py --azahar /absolute/path/azahar-libretro-linux-x86_64-2126.0.zip --retroarch /absolute/path/RetroArch-1.22.2.zip
make
python3 run.py /absolute/path/Colosseum3DS.3dsx --out runs/boot --frames 180 --seconds 45 --capture-every 30
```

This 180-frame command is a short smoke test. It does not cover the full boot
sequence or title loading. See [the native boot instructions](../../docs/native-boot.md)
for asset staging and a longer integrated run.

`prepare_frontend.py` extracts exact named entries with size bounds, rejects
symbolic links, preserves different existing files, and writes SHA-256 provenance
to `dependencies.json`. It performs no downloads or system installation. The
supplied Azahar core reports version 2126.0 and explicit `3dsx` support.

The default is Azahar's **Software** PICA renderer. The core option selecting the
console model is `citra_is_new_3ds=New 3DS`, and resolution is native 1x. This
configuration booted the existing native bringup app and passed its core tests.
The optional `--gl` path uses a surfaceless desktop OpenGL context, with Mesa
software rendering forced. It was verified on llvmpipe with the bringup app, but
a separate CPU-framebuffer-only console probe produced black captures in that
path. Use the default software renderer for boot movie and framebuffer testing.

## SD and persistence

The default private state directory is next to these tools. `--state DIR` selects
another persistent directory. The emulated SD root is:

```text
DIR/save/Azahar/sdmc/
```

For example, native `sdmc:/3ds/Colosseum3DS/boot/title.c3t` is a host file at
`DIR/save/Azahar/sdmc/3ds/Colosseum3DS/boot/title.c3t`. Stage private game assets there
before running the app. Fresh runs reuse this directory without restoring an
emulator state. XDG directories and the libretro system directory are also
contained under the selected state directory.

The optional native probe verifies SD reads, writes, readback, and reopening in a
new emulator process. It is deliberately **not a Colosseum save codec**:

```sh
make probe DEVKITPRO=/absolute/path/devkitpro
python3 run.py sd_probe.3dsx --events persistence-input.csv --out runs/probe-write --frames 90
python3 run.py sd_probe.3dsx --out runs/probe-reopen --frames 90
```

`persistence-input.csv` presses A once. The second run should show the increased
launch count with zero new writes. The probe uses only
`sdmc:/3ds/Colosseum3DS/harness-probe.bin` and does not touch a game save.

## Deterministic controller input

Input CSV has no header; comments begin with `#`:

```text
# start_frame,duration_frames,libretro_mask,left_x,left_y,right_x,right_y
40,3,0x0100,0,0,0,0
100,3,0x0100,0,0,0,0
140,20,0x0400,0,0,0,0
170,20,0,16384,0,0,0
```

Pass it with `--events input.csv`. These are libretro button masks, not libctru
HID masks. Frame numbers start at zero and count `retro_run()` calls. Buttons are
held in the half-open interval `[start, start + duration)`. Otherwise all inputs
are released. Overlapping rows OR their buttons; the last active row supplies
the axes. Axes range from -32768 to 32767; native deadzones still apply.

| Button | Mask | Button | Mask |
| --- | ---: | --- | ---: |
| A | `0x0100` | B | `0x0001` |
| X | `0x0200` | Y | `0x0002` |
| START | `0x0008` | SELECT | `0x0004` |
| UP | `0x0010` | DOWN | `0x0020` |
| LEFT | `0x0040` | RIGHT | `0x0080` |
| L | `0x0400` | R | `0x0800` |
| ZL | `0x1000` | ZR | `0x2000` |

Scripted A presses paused and resumed the native bringup animation; L and the
circle pad also reached the app. Repeated runs can differ in the precise app
frame at which an input arrives, because native scheduling and core timing are
not controlled by this frontend. Treat the script as deterministic frontend
input, not a cycle-exact execution replay.

## Live controller input

Use `--live-input /absolute/controller.packet` to control a running session
after inspecting its actual screens and application trace. The file may be
absent when the run starts. Write one packet, replacing the file atomically:

```text
sequence,duration_frames,libretro_mask,left_x,left_y,right_x,right_y
```

The packet itself has **no header**. For example, `1,12,0x0100,0,0,0,0` holds A
for 12 frontend frames. Each sequence must be a strictly increasing positive
64-bit integer. Duration is 1..36000 frames, the button mask is 0..65535, and
axes are -32768..32767. The packet must be a regular file of at most 256 bytes;
symbolic links and other file types are rejected. Keep it outside `--out`.

The frontend reads the packet before each `retro_run()`. A valid newer sequence
starts on that frame, replacing the complete controller state. Its duration
expires to neutral. Repeated sequences, stale sequences, and invalid packets
never renew a hold. Once live input has been accepted, it keeps ownership of
the controller: an old `--events` CSV hold cannot resume after expiration.
Before the first live packet, the existing CSV behaves normally. This allows
a fixed boot sequence followed by adaptive movement and dialogue input.

This example sends an A pulse using a temporary file in the same directory:

```python
from pathlib import Path
import os
import tempfile

packet = Path("/absolute/controller.packet")
sequence = 1  # Increment for every subsequent command in this run.
with tempfile.NamedTemporaryFile("w", dir=packet.parent, delete=False) as output:
    output.write(f"{sequence},12,0x0100,0,0,0,0\n")
    output.flush()
    os.fsync(output.fileno())
    temporary = output.name
os.replace(temporary, packet)
```

A newer packet with duration 1 and all five input values zero cancels a current
hold, then remains neutral. Use a distinct packet path per run or remove an old
packet before launch; an existing valid packet is accepted on frame zero.

`live-input.csv` records every accepted sequence with its **actual** start frame:
`sequence,start,count,mask,left_x,left_y,right_x,right_y`. Each row is flushed
before the affected `retro_run()`, and `core.log` records acceptance and expiry.
Wait for that acknowledgement before treating a command as submitted. Confirm
the resulting game action with screenshots and the application trace; frontend
frames do not guarantee a particular native game update. `run.json` hashes and
checks the accepted log. Its bounded initial/final packet snapshots are only
observations of proposed input, not evidence of acceptance.

The `record_run.py` wrapper also accepts `--live-input` after `--`, so adaptive
controller runs can produce the same actual screen recording as CSV runs.

### First-visit route with trace feedback

With the complete private asset set prepared as described in
[native-first-visit.md](../../docs/native-first-visit.md), run the maintained
controller route from this directory:

```sh
python3 first_visit_route.py --mode fresh \
  --app /absolute/Colosseum3DS.3dsx --state /absolute/private-state \
  --run /absolute/new-fresh-run --video /absolute/new-fresh.mp4 \
  --frames 21000 --seconds 180
python3 first_visit_route.py --mode continue \
  --app /absolute/Colosseum3DS.3dsx --state /absolute/private-state \
  --run /absolute/new-continue-run --video /absolute/new-continue.mp4 \
  --frames 10000 --seconds 120
```

Fresh mode refuses an existing primary or backup GCI. Back up an existing SD
outside this test state before preparing a fresh run. Continue mode uses the
GCI created by that fresh run; do not substitute the completed card. Each
command needs new run and video paths. The route advances through bounded
controller packets, observes the native diagnostic trace, and records actual
framebuffer stages and `controller-result.json`. It never writes guest memory
or story flags. A route failure remains a failure even if frames were recorded.

The fresh route ends after explicit Save and clean exit. Continue loads that
checkpoint, exits the shop, and expects the original encounter-10 request to
remain pending at the missing combat backend. This is not a battle victory.
Use the accepted `live-input.csv` as the exact command record for a capture;
the helper's feedback and host scheduling can produce different frame numbers
on another run. These commands automate the isolated emulator only. See the
[hardware handoff](../../docs/native-story-handoff.md) for physical input,
ftpd deployment, NTR capture and the completed-save acceptance gate.

## Evidence and run bounds

Each `--out` must name a new directory. It contains:

- `run.json`: content, core, harness, and input SHA-256 hashes; options; limits;
  exit status; elapsed host time; capture hashes.
- `core.log`: actual emulator and harness output.
- `frames.csv`: captured libretro frame number, callback count, dimensions, and
  input state at capture time.
- `live-input.csv`, when enabled: accepted sequence numbers, frontend start
  frames, durations and full controller state; bounded packet snapshots are
  retained when present.
- `frame-NNNNNN.ppm`: actual 400x480 combined top/bottom output in the default
  layout. Optional PNG copies are produced when Pillow is installed.
- `last.ppm`: the last capture received, not a fabricated success screen.

For evidence involving private game assets or save files, repeat
`--sd-file 3ds/Colosseum3DS/relative-file` for each relevant file. The run record
then includes its size and SHA-256 before and after execution, or records its
absence if the application is expected to create it. Paths are relative to the
emulated SD root and cannot escape it. Asset bytes are never copied into the
record. Without this option, `run.json` does not identify the SD asset contents.

For the game application, also pass
`--app-trace 3ds/Colosseum3DS/boot-trace.log`. The wrapper requires a newly
written trace, archives it as `application-trace.log`, and fails a run that
reports a negative field/boot result or a failed save. Explicit guest memory
faults and ARM aborts in the core log also fail the run, even if the emulator
continues producing frames. These checks are additional to inspecting the
actual route and screenshots; an unported battle request is a documented
boundary, not a completed battle.

### Record actual emulator output

With FFmpeg available, this records the combined screens directly from the
frontend callbacks, while retaining the same trace, input and binary evidence:

```sh
python3 record_run.py --video /absolute/new-progress.mp4 -- \
  /absolute/Colosseum3DS.3dsx --state /absolute/private-state \
  --out /absolute/new-run --frames 9000 --seconds 120 \
  --events /absolute/route.csv --gl \
  --app-trace 3ds/Colosseum3DS/boot-trace.log \
  --sd-file 3ds/Colosseum3DS/saves/pokemon_colosseum.gci
```

The MP4 samples every two 60 Hz frontend frames and plays at 30 fps without
audio. Frames are held when the core supplies no replacement image, as on the
frontend display. This is actual emulator output, not generated imagery or a
measurement of physical-console performance. The adjacent `.recording.json`
binds the video hash, dimensions, frame count, encoding completion, and tested
executable hash to the run record. Existing output files are never overwritten.

The wrapper enforces a wall deadline and terminates the child process group if
the core fails to return. Runs are also bounded by frame count. A deadline ending
the run before its requested frame count returns status 124, even if it produced
useful captures. `run.json` records completed frames, interruption, core-requested
shutdown, and whether the run completed. A core-requested normal shutdown can
complete before the frame limit. Missing final status after a hard stop stays
unknown rather than being reported as a successful full run. Audio is consumed
and counted but is not saved or assessed. Touch and motion input are disabled.
No savestate API, network service, or physical-console input is used.

A successful process exit and captured pixels establish emulator execution only.
They do not establish game correctness, genuine save compatibility, or physical
New 3DS performance. A black frame is still a frame: inspect the PNGs and native
diagnostics when judging a result. Host elapsed time is not a hardware benchmark.

## Native texture and color-combiner checks

The source-only graphics probes use the actual title renderer and shader with
original colored geometry. No game asset is needed. They test the otherwise
easy-to-miss agreement between pixel rows, UV origin, screen rotation, and the
two endpoints of the title's color-gradient combiner:

```sh
python3 -m pip install -r ../../requirements-capture-test.txt
make -f native_probe.mk DEVKITPRO=/absolute/path/devkitpro
python3 run.py probe-build/title-corner.3dsx --out runs/corners --frames 90 --seconds 30
python3 verify_title_probe.py corners runs/corners/last.ppm
python3 run.py probe-build/title-combiner.3dsx --out runs/combiner --frames 90 --seconds 30
python3 verify_title_probe.py combiner runs/combiner/last.ppm
python3 run.py probe-build/stage-material.3dsx --out runs/stage-material --frames 90 --seconds 30
python3 verify_title_probe.py stage runs/stage-material/last.ppm
python3 run.py probe-build/arrival-material.3dsx --out runs/arrival-material --frames 90 --seconds 30
python3 verify_title_probe.py arrival runs/arrival-material/last.ppm
python3 run.py probe-build/arrival-fade.3dsx --out runs/arrival-fade --frames 90 --seconds 30
python3 verify_title_probe.py arrival-fade runs/arrival-fade/last.ppm
```

The corner probe expects red/green across the top and blue/yellow across the
bottom. The combiner probe enables backface culling and maps each texture channel
between 0.2 and 0.8, so both endpoints must survive. The verifier checks four
interior pixels against expected RGB values with a one-unit tolerance. Both
checks passed using the supplied Azahar software renderer. They establish this
native rendering path in the emulator, without asserting physical GPU accuracy
or complete title visual parity.

The stage probe adds two separate model packs, with the transparent pack listed
first to exercise the shared renderer's opaque pass. A distinct second texture
uses horizontally reversed UVs. The top half subtracts its two-texture result
from an opaque gray background; the bottom half adds it with ONE/ONE factors.
Eight interior pixel checks passed with the supplied software renderer, including
both UV channels and both framebuffer equations. This probe also caught and
verified the fix for a stale citro3d vertex-buffer binding between model packs.
It contains no game geometry or private images.

The arrival probe checks the GX rational specular curve, the owning joint's
effect on the highlight, the original GREATER-zero alpha cutout, and reflection
UVs generated from camera-space normals. A second reflection rectangle rotates
the UVs using both off-diagonal affine terms. Five interior pixel checks passed
with the supplied software renderer; the two specular values were exactly
`(81,73,68)` and `(112,88,76)`. Its fade variant makes the first highlight
saturate, then applies a half-brightness black overlay. All five checks passed,
including `(127,95,63)` at that saturated surface. These runs each completed
90 requested frames without interruption. The probe checks aggregate submitted
draw/vertex counts and GPU storage as well as the pixels.

The specular implementation uses a second additive draw. At filtered
fractional-alpha edges, saturation can occur after alpha blending rather than
before it, unlike GX. Binary-alpha interior pixels and opaque surfaces avoid
that particular difference; the probe does not establish pixel-exact parity
at fractional-alpha edges. See [the renderer notes](../../docs/native-scene-renderer.md)
for the source equations, lifetime rules, supported material combinations,
and exact arrival-camera end condition. These are emulator checks, not physical
New3DS performance or image-equivalence measurements.

## Sources and clean repository contents

The adapter follows the public libretro API from the supplied RetroArch header
and Azahar's [2126.0 libretro frontend](https://github.com/azahar-emu/azahar/tree/2126.0/src/citra_libretro).
Relevant upstream files are `core_settings.cpp`, `environment.cpp`,
`libretro_window.cpp`, and `citra_libretro.cpp`. Upstream licenses remain with
the separately supplied dependencies.

Commit the harness and probe sources, setup/run/verifier scripts, makefiles,
this README, and small input examples. Keep the core, extracted headers, build
products, private assets, state, and run captures outside commits. The actual
save-codec probe has its own instructions in `docs/save-format.md` at the
repository root; the simple counter probe above does not replace that test.
