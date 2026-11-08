# Isolated original-game controller reference

`tools/dolphin_dsu.py` supplies ordinary controller state through Dolphin's
official CemuHook DSU backend. It uses Python's standard library, loopback
`127.0.0.1:26761`, and pad0 only. It does not inject game memory, synthesize
Windows input, control a 3DS, or claim original/native RNG parity.

## Source contract

The reviewed official revision is
`5e7cc91d8c9a43ca189b288937f65c9763af9c22`. Its InputCommon CMake sources include
`ControllerInterface/DualShockUDPClient` unconditionally, and
`ControllerInterface.h` enables the backend on Windows. Relevant files:

- [Device/config implementation](https://github.com/dolphin-emu/dolphin/blob/5e7cc91d8c9a43ca189b288937f65c9763af9c22/Source/Core/InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.cpp)
- [Packed wire structures and CRC checks](https://github.com/dolphin-emu/dolphin/blob/5e7cc91d8c9a43ca189b288937f65c9763af9c22/Source/Core/InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPProto.h)

Packets are little-endian, protocol1001, with a16-byte header. CRC32 covers
the complete packet with bytes8..11 zeroed. PortInfo is32bytes; PadData is100.
Dolphin queries ports once per second and registers pad data on a separate UDP
socket. Reply to each socket's endpoint; both use the same client UID.

Only pad0 is connected. Input leases expire to neutral; registrations expire
after2.5seconds without renewal. DSU defines no unregister request; shutdown
sends neutral then removes local registrations. Logs report transmission and
fresh screenshots, never an acknowledgement that the game accepted a command.

## Isolated configuration

Use a new private user directory and a copied fresh GCI. Never point this tool
at the default Dolphin user directory. Preserve hashes of source and copied
cards, Dolphin.exe, the RVZ, and the action/session files. Disable cheats and
game patches. Do not select a game save action.

`Config/DSUClient.ini`:

```ini
[Server]
Enabled = True
Entries = TailLoopback:127.0.0.1:26761;
```

In `Dolphin.ini`, retain `EnableCheats=False` under `[Core]`, and set
`BackgroundInput=True` under `[Input]` for the owned hidden instance.
`GCPadNew.ini` includes:

```ini
[GCPad1]
Device = DSUClient/0/TailLoopback
Buttons/A = `Cross`
Buttons/B = `Circle`
Buttons/Start = `Options`
D-Pad/Up = `Pad N`
D-Pad/Down = `Pad S`
D-Pad/Left = `Pad W`
D-Pad/Right = `Pad E`
Main Stick/Left = `Left X-`
Main Stick/Right = `Left X+`
Main Stick/Up = `Left Y-`
Main Stick/Down = `Left Y+`
```

Dolphin reads the analog button bytes for Cross and the D-pad: A is packet
byte49, DOWN45, RIGHT46. Setting digital face-button flags alone will not drive
these bindings. Sticks are neutral128; inverted Y means0 is up,255 down.

## Bounded actions and evidence

Launch only an owned official Dolphin process with the isolated `--user`.
Start from neutral, without an active DTM (movie playback overrides controller
state). A separately inspected finite movie-prefix handoff is permitted only
with `PauseMovie=False`, `movie_handoff_ready:true` and the exact
`completed_movie_sha256` in the session manifest. This explicit operator gate
must follow inspection of a fresh post-prefix checkpoint; it is not automatic
proof of movie completion. Verify the subsequent DSU-driven transition too.
Enable PNG frame dumping. Create a private session JSON with
`owned_pid`, `ready:true`, `exe`, `exe_sha256`, `output_root`, `user_root`, and
`frames_dir`. The tool checks the process executable/command line, isolated
config, and a fresh complete PNG before sending each action.

An action file is a list such as
`[{"buttons":["A"],"duration_ms":100}]`. Default sticks are neutral. Maximum
action duration is500ms, input lease1000ms,32actions and120seconds per invocation.

```text
python tools/dolphin_dsu.py --session PRIVATE/session.json --actions PRIVATE/actions.json --evidence PRIVATE/actions.jsonl --seconds 10
```

Inspect a newly dumped PNG after release before authorizing another navigation
step. Record accepted menu changes separately from controller transmissions.
Keep neutral in cleanup; stop only the owned Dolphin PID, verify GCI hashes,
and label unsuccessful or incomplete captures explicitly. Capture speed and
proxy frame rate are not measured game FPS or hardware performance.

Run protocol and gate tests with
`python -m unittest discover -s tests -p test_dolphin_dsu.py`.
These tests validate bytes and failure handling; they do not prove game input
acceptance. An actual isolated Dolphin smoke capture is required separately.

The initial private smoke used the unchanged official executable (SHA256
`5e076628b977603dcde9ddef5eedf10b5a9ce0b6208bd4e6dfa9039cdebf3433`).
Fresh PNGs independently showed START advancing to the card-read message,
A advancing to the Story menu, and DOWN moving Continue to New Game. New Game
was only highlighted, never confirmed. The owned process was stopped and the
isolated GCI SHA256 remained
`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`.
This proves basic ordinary-controller acceptance, not a completed battle route.

A finite12680-record ordinary-controller prefix reached Espeon's main menu
after Umbreon CALL. With read-only movie EOF and `PauseMovie=False`, a fresh
DSU A press opened Espeon's move grid, proving actual controller handoff.
DSU DOWN selected Reflect; confirmation produced original Reflect visuals.
A subsequent isolated reference captured the female Zigzagoon's explicit
"TAIL WHIP!" announcement, cyan attack rings, actor poses before/after, and
both allies' DEFENSE-fell effects on the first nondamaging turn. The owned
process and persistent neutral DSU worker were stopped after capture, before
the1800-second bound; source/default/copied GCI hashes remained unchanged.
This qualifies an original-game reference, not native visual or RNG parity.
Use announcements and HUD identity, not effect appearance or screen position,
to label the original move and actor; an orange impact alone is not Growl proof.
Under slow dumping, short real-time presses can miss a game polling interval;
inspect screenshots after every action instead of assuming acceptance. The
maximum500ms press remains bounded but can also auto-repeat in fast menus.

Lease expiry supplies neutral while the server runs. An abrupt host/server
termination or lost UDP release cannot guarantee Dolphin discards its last
state; its official DSU device retains the latest packet. Use orderly cleanup
and the separate owned-process/session deadline as well as the input lease.
