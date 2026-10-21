# Local hardware workflow

`tools/hardware.py` uses Python 3's standard library. It collects FTP evidence
and replaces only the two application files under `/3ds/Colosseum3DS`.
The separate `tools/ntr_capture.py` captures both screens without an input
transport. `tools/luma_input.py` sends short explicit controller actions with
neutral release. Observed responses remain acceptance gates. These tools
do not prepare SD game data or claim a complete automated smoke test.

## Inspect before replacing

Copy `config/hardware.example.json` to `private/hardware/config.json`. Set the
console's displayed IP and ftpd port; keep passwords in the environment variable
named by `password_env`. `private/` is ignored. Keep evidence and captures there.
Use a new evidence directory for each invocation; old evidence is never replaced.

```sh
python tools/hardware.py diagnose --config private/hardware/config.json --evidence private/hardware/diagnose-01
python tools/hardware.py inspect --config private/hardware/config.json --evidence private/hardware/inspect-01
```

`diagnose` records independent TCP checks for the configured FTP port and NTR
port 8000, then attempts an actual FTP session. An open port does not prove
decoded video or input delivery. Input on UDP 4950 cannot be verified by a TCP
probe. `inspect` downloads both files through passive FTP and records sizes and
SHA-256 hashes. Every command writes `manifest.json`, including on a connection
failure, and returns nonzero on failure. A successful FTP diagnosis says nothing
about the running application.

Inspection also recognizes the known nested packaging location
`/3ds/Colosseum3DS/3ds/Colosseum3DS`. The manifest reports that location.
Deployment refuses that layout: do not silently move or replace the running
installation. Resolve its launch path and test the original build first.

## Build and replace an established direct installation

```sh
git submodule update --init --recursive
make verify-source
make test
make -f Makefile.3ds -j2
```

On Windows, clone with LF bytes for the pinned hash checks. If an already-clean
submodule was checked out with `core.autocrlf=true`, its file hashes can fail even
though Git reports no source edits. Re-check out that submodule with
`core.autocrlf=false` and `core.eol=lf`, preserving any local work first. Do not
change the expected hashes to accommodate CRLF conversion.

Populate `git_commit` with the full built commit, `build_configuration` with the
actual command/options and `toolchain` with actual compiler/library versions.
Populate `expected_installed_sha256` from the reviewed inspection manifest.
These are operator-provided provenance fields, not proof of a reproducible build.

Confirm the application exited and open ftpd before running:

```sh
python tools/hardware.py deploy --config private/hardware/config.json --source build-3ds --app-exited --evidence private/hardware/deploy-01
```

The command requires both direct installed files and their expected hashes.
It downloads a complete local backup before console writes, uploads unique
temporary names, downloads and compares every staged byte, renames old files
to recoverable backup names, installs both files and reads them back again.
Each application file is limited to 32 MiB. It does not touch `data/` or unrelated
SD files. This multi-file update is **not atomic**. Keep ftpd active throughout.

Before writes and around rename phases, the tool checkpoints its manifest.
On an error it reconciles uncertain renames, attempts rollback and verifies
both restored originals by readback. It retains `.backup-*` files and may
retain `.failed-*` or `.restore-*` files for recovery. `rollback_verified`
and `recovery_required` distinguish verified restoration from uncertainty. If the
connection drops or the process is interrupted during rename, stop and inspect
the manifest, local backup and recorded remote files before
relaunching or retrying. Manual recovery may be necessary. Never assume an
unacknowledged rename did not execute. Old backups are deliberately retained.

## Launch, observe, control, verify

With the installed NTR engine already loaded and no other stream client active,
use the optional capture tool on a Python environment that already has Pillow:

```sh
python tools/ntr_capture.py --host CONSOLE_IP --bind PC_LAN_IP --output private/hardware/capture-01 --duration 8
```

It binds receive-only UDP 8001 before requesting original JPEG streaming through
TCP 8000. Its only outbound messages are heartbeat and view-stream requests;
there is no controller, touch or UDP send path. Captures are bounded to 15 seconds.
Do not start another client while someone is viewing: NTR streams to the latest
requesting client. Closing a capture's sockets does not prove streaming is disabled
on the console. No NTR installation or firmware change is performed.

If an already-running stream still sends fresh UDP frames but the NTR control
port will not accept a new connection, add `--listen-only`. This mode sends
nothing and receives the existing stream without TCP control or reconfiguration.
Keep the same source-address checks and image freshness requirements; an open
UDP socket alone does not establish video.

The new output directory contains `top.jpg`, `bottom.jpg`, `both.png`, `frames.json`
and `capture.json`. Images publish through temporary-file replacement. Success
requires at least two decoded frames from each screen, with both latest frames
no older than three seconds at capture end. Open the images and compare visible
changes across separate captures before reporting application behavior. Frame
counts measure received video, not application rendering performance. Original
JPEG compatibility is the only supported protocol; lossless, KCP and audio are
outside this tool. Pillow is optional for the rest of the repository; no new
package is required for FTP or native builds.

1. With ftpd active, inspect or transfer and verify the files.
2. Exit ftpd; launch Colosseum3DS in Homebrew Launcher. FTP normally stops.
3. Observe both screens. Record self-test and archive status separately.
4. If streaming is available, save fresh decoded frames of both screens, record
   received-frame timestamps/counters, open the images, and confirm visible
   changes between captures. A frozen viewer window is insufficient evidence.
5. With visibility, tap A, release it, capture again and verify rotation paused.
   Then test the remaining controls one at a time, returning to neutral after
   each. Verify viewer button mappings; A/B and X/Y may be swapped by default.
6. Test START+SELECT exit and relaunch. Reopen ftpd only when another transfer
   is needed. Record HOME and lid tests independently, with NTR inactive where
   necessary to separate streaming restrictions from application behavior.

After enabling Luma/Rosalina InputRedirection, use the separate controller tool
only with visible screens and authorization to control the console:

```sh
python tools/luma_input.py --host CONSOLE_IP --bind PC_LAN_IP --button A --duration 0.15 --evidence private/hardware/a-01.json
python tools/luma_input.py --host CONSOLE_IP --circle 0.5 0 --duration 0.15 --evidence private/hardware/circle-01.json
python tools/luma_input.py --host CONSOLE_IP --cstick 0.5 0 --duration 0.15 --evidence private/hardware/cstick-01.json
python tools/luma_input.py --host CONSOLE_IP --button START --button SELECT --duration 0.15 --evidence private/hardware/exit-01.json
```

Actions last at most half a second and attempt three neutral releases in a
`finally` block, including after a hold-send error. UDP cannot guarantee delivery:
verify the response and neutral diagnostics in fresh captures. Stop on a release
error or lost visibility. Single buttons and START+SELECT are supported; other
combinations, POWER and touch are excluded. HOME is an explicit single-button
action for observed menu navigation; it also receives neutral cleanup. Analog coordinates are finite
values in [-1, 1], with right/up positive. The evidence destination is reserved
before any send and records sends separately from receipt verification.

The CLI defers SIGINT during the bounded neutral-release sequence and restores
the previous signal handler afterward. Cancellation during the hold or cleanup
is recorded as `cancelled` and returns nonzero; release-send failures remain in
`release_errors`. Luma preserves the last injected state during UDP silence,
so completed sends still require a fresh observed neutral diagnostic. Forced
process termination and a lost network cannot guarantee release delivery.

Record the exact console, launcher, Luma, ftpd, BootNTR Selector, loaded NTR
engine and viewer versions before diagnosing compatibility. Selector version
does not identify the engine. Do not replace the installed NTR automatically.
When operating under a view-only request, keep all controls physical and do not
start viewers that automatically forward input. If NTR control is unavailable,
request the physical launch/initialization step;
do not navigate blindly or claim a screenshot was captured.

Primary references: [ftpd](https://github.com/mtheall/ftpd),
[Rosalina](https://github.com/LumaTeam/Luma3DS/wiki/Rosalina),
[original NTR streaming](https://github.com/44670/BootNTR/wiki/Video-Streaming),
[NTRViewer-HR](https://github.com/xzn/ntrviewer-hr).

Run synthetic transfer failure tests with:

```sh
python -m unittest discover -s tests -p 'test_hardware.py'
```
