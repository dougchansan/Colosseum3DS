# Local-agent handoff: New Game through Outskirt Stand

Continue the native first-area checkpoint from branch
`feat/native-boot-title-save`. The tested runtime commit is
`d2af94a8f1b3f0afaa54abe4870ba65052c9b91f`; the subsequent evidence commit
documents the same runtime. Read
[native-first-area-validation.md](native-first-area-validation.md)
and [native-boot.md](native-boot.md) before changing it.

The sandbox has verified the connected UI, original opening/arrival, movement,
explicit Save, whole decoded save body, and fresh-process Continue. Your next
task is physical New 3DS acceptance using Doug's existing setup: Luma/Rosalina
InputRedirection, ftpd at `192.168.50.154`, and the already-installed BootNTR
Selector Enhanced 3.6 streaming engine. Use fresh captures to choose actions.
Do not replace or update the installed CFW/NTR as part of this checkpoint.

## Isolate the work and build

Keep the other agent's PR #2 worktree intact. From an existing authenticated
clone, create a separate worktree and follow-up branch:

```sh
git fetch origin feat/native-boot-title-save
git worktree add ../Colosseum3DS-first-area -b feat/native-first-area-hardware origin/feat/native-boot-title-save
cd ../Colosseum3DS-first-area
git -c core.autocrlf=false -c core.eol=lf submodule update --init --recursive
make verify-source
make test
make -f Makefile.3ds -j2
```

Use the locally built `build-3ds/Colosseum3DS.3dsx` and `.smdh`. The normal
build selects the native flow when `/3ds/Colosseum3DS/boot/` exists and retains
the earlier model route. Record the actual commit, submodule pins, toolchain
and output SHA-256 values. If you need the exact tested source, use a separate
worktree at `d2af94a8f1b3`; do not reset an agent's working branch.

## Prepare and transfer

Prepare the six asset groups with the exact commands and SD filename table in
[native-boot.md](native-boot.md). Use the existing private extracted GC6E01
revision 0 disc. The private build repo supplies verified code originals but
does not by itself supply the full movie/model asset filesystem. The required
field set is four map packs, Wes, two arrival packs, and `outskirt.c3f`.

Copy `config/hardware.example.json` to a private config, set host
`192.168.50.154`, retain the ftpd-displayed port, and record the actual commit
and build configuration. With the application exited and ftpd active:

```sh
python tools/hardware.py diagnose --config private/hardware/config.json --evidence private/hardware/diagnose-native-01
python tools/hardware.py inspect --config private/hardware/config.json --evidence private/hardware/inspect-native-01
```

Use the observed installed application hashes in `expected_installed_sha256`.
The deployment tool supports the established direct installation; resolve a
reported nested installation before deployment. Preserve the existing real
`saves/pokemon_colosseum.gci`, `.bak`, any `.tmp`, and useful `boot-trace.log`
in a new local evidence directory through read-only FTP. Record absence only
from a successful directory listing, and verify downloaded bytes/hashes.
The application deploy tool backs up its two executable files, **not saves**.

```sh
python tools/hardware.py deploy --config private/hardware/config.json --source build-3ds --app-exited --evidence private/hardware/deploy-native-01
```

Automate private boot-asset transfer with the established local FTP client.
The existing application deploy command does not install `boot/`. Its
`read_remote` helper has a 32 MiB application-file bound and cannot verify the
304,066,812-byte opening pack. Use streaming `STOR` and streaming `RETR`
SHA-256 verification for large media, with temporary remote names and retained
backups before replacing an existing boot asset. Verify the complete filename,
length and hash set against the preparation manifests while the application
remains exited. Do not copy test GCI fixtures into the real save path.

## Observe and control the console

Get the PC LAN address from diagnosis field `ftp_local_source_address` and
set `PC_LAN_IP` accordingly. With NTR already loaded, no competing stream
client, and the relevant application screen visible:

```sh
python tools/ntr_capture.py --host 192.168.50.154 --bind "$PC_LAN_IP" --output private/hardware/title-native-01 --duration 8
python tools/luma_input.py --host 192.168.50.154 --bind "$PC_LAN_IP" --button A --duration 0.15 --evidence private/hardware/a-native-01.json
python tools/luma_input.py --host 192.168.50.154 --bind "$PC_LAN_IP" --circle 0.5 0 --duration 0.15 --evidence private/hardware/move-native-01.json
```

Use new evidence paths for every action/capture. Inspect `both.png`,
`capture.json`, `frames.json`, and input release results. Capture success
requires fresh decoded frames from both screens, not just an open port.
`--listen-only` is for a stream already sending UDP when TCP control cannot
reconnect. The tools use the existing JPEG NTR stream and Luma input protocol.
The emulator CSV timing and the old calibrated `--target model` navigation
route are not physical-console navigation for these new screens.

## Acceptance and follow-up PR

Observe the original logos and title, open New Game, confirm the existing-save
warning when applicable, choose WES, and allow the hideout movie and bike
arrival to complete. Check Circle Pad/D-pad movement, both starter Pokémon,
and X/START → Save → explicit YES. The saved checkpoint is the area's entrance,
not Wes's current coordinates.

Exit with START+SELECT, return to ftpd, and download the saved GCI plus trace
before relaunching: launch truncates `boot-trace.log`. Relaunch, choose Continue,
and capture the restored name, party and entrance. Exit/download again; without
a second explicit Save, the GCI hash must remain unchanged. Also check
sleep/resume, movie PCM/audio sync with the console's existing DSP setup,
loading time and field responsiveness. External music/SFX, NPC dialogue,
battles and exits remain unavailable and should be recorded as such.

If hardware reveals a fault, fix that fault on the follow-up branch and repeat
the affected acceptance step. Open a draft PR against
`feat/native-boot-title-save`, with exact commit/build/asset hashes, fresh NTR
captures, input evidence, and before/after save verification. Keep original
assets, GCI data, credentials and emulator dependencies private. Do not merge
PR #2 or the first-area PR as part of this task. After this acceptance gate,
the next engine slice is the Outskirt Stand NPC/interaction scripts and its
first battle.
