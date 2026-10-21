# Local hardware workflow

`tools/hardware.py` uses Python 3's standard library. It collects FTP evidence
and replaces only the two application files under `/3ds/Colosseum3DS`.
Launch, screen capture and controller verification remain manual gates. This
first tool does not automate NTR, send input, prepare SD game data or claim a
complete automated smoke test.

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

Record the exact console, launcher, Luma, ftpd, BootNTR Selector, loaded NTR
engine and viewer versions before diagnosing compatibility. Selector version
does not identify the engine. Do not replace the installed NTR automatically.
If NTR control is unavailable, request the physical launch/initialization step;
do not navigate blindly or claim a screenshot was captured.

Primary references: [ftpd](https://github.com/mtheall/ftpd),
[Rosalina](https://github.com/LumaTeam/Luma3DS/wiki/Rosalina),
[original NTR streaming](https://github.com/44670/BootNTR/wiki/Video-Streaming),
[NTRViewer-HR](https://github.com/xzn/ntrviewer-hr).

Run synthetic transfer failure tests with:

```sh
python -m unittest discover -s tests -p 'test_hardware.py'
```
