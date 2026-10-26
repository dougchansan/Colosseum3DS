# Independent closed RED hardware audit

Audit reads the already closed private capture and readback only. No console,
network client, input or source mutation is part of this audit.

| Binding | Bytes | SHA-256 |
|---|---:|---|
| Normal executable | 591824 | `d0ffcc71da8dee828f61dfd95678d9a592a1047e4d0dd4c7055c4f74b4e30c11` |
| Closed trace | 585311 | `153424760c9196072a6dbdad4c5c7e83680432a7ebd7e68c602358e78729df52` |
| Before/final GCI | 393280 | `7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23` |

Source `5519c82018fc58d3b8e8e9a50c1fa34e5ad388e9`, tree
`a7263591142305d48922a9e080db6f533743427a`; trace boot build `5519c82018fc`.
All hashes above were recomputed from local readback bytes. The GCI was also
compared byte for byte against the before snapshot. Backup remains absent.

## Event proof

| Profile | Event / target | Cue | Completion units | Emitted / peak / draws | Audio / cleanup | Color / camera restored |
|---|---|---:|---:|---|---|---|
| GREEN | 21 / 0 | 4 | 118000 | 369 / 232 / 1772 | finished / finished | 1 / 1 |
| GREEN | 22 / 1 | 5 | 180000 | 369 / 232 / 1772 | finished / finished | 1 / 1 |
| RED | 24 / 0 | 7 | 118000 | 369 / 232 / 1772 | finished / finished | 1 / 1 |
| RED | 25 / 1 | 8 | 180000 | 369 / 232 / 1772 | finished / finished | 1 / 1 |

RED starts explicitly record special `0x27`, stat 1 and delta -1. Growl event 23
(owner 2, cue 6) records units 230000, emitted 26, peak 25, cosmetic draws 200,
family cancellation, released owner, half-rate continuation and restored camera.
Its audio status is STOPPED, distinct from the four FINISHED defender cues.

The JSON retains exact sanitized trace records with original line numbers,
including starts/completions, preview turns, exits, guards, recovery and final
field/boot termination. Two preview guards each cover 122832 bytes and report
unchanged=1, matching before/after SHA1 and play_time_advanced=0. Six recorded
preview turns (including the reentry turn) and both exits report save_writes=0
and gsvm_resumed=0. WIN occurs on first-preview turn 4. Final field/boot errors
are empty. These records do not prove audible or original-game fidelity.

## Asset proof

11 base assets plus 20 supplemental assets minus duplicate audio = **30 distinct
battle assets**. Each local final file matches its declared readback byte count
and SHA-256, and independently matches the canonical dual RED/GREEN fixture.
The JSON publishes the hashes and match flags, not original pack bytes.

## Capture and navigation proof

The closed 8404-frame main recording recomputes to
`98a7b084f3d082eb9b6576cf130762d92e32f2d8370e65f063be40fe317b51c9`.
Full FFmpeg decode exits 0; ffprobe shows one H.264 video stream, 10/1 fps,
8404 frames and 840.4 seconds, without audio. Recorder summary reports
1051.687 wall seconds, 20 stale samples, zero decode errors and encoder exit 0.
All selected stills are individually fresh. The two excerpts fully decode and
retain explicit input frame ranges, wall UTC endpoints and speed factors.

The initial receiver closed with a live-image publication error after 1099
frames; the main receiver is a separate restarted segment. The same-boot NTR
refresh precedes preview; it is not a reboot/reload. The earlier public Willie
route safely fails after four steps on lost current authorization. The separate
14-step private route passes, with 3 neutral release packets per action and no
release errors; visible state transitions support receipt. These qualifications
are preserved rather than converting either interruption into a full pass.

The original Growl reference attempt closes at its watchdog bound with zero
selected frames. Its closed audit reports all checked hashes unchanged; no
original RED comparison is established. Local host/ARM results are separate from
hosted CI. Run40136640385 passed host/capture and ARM compilation; artifact
upload failed on GitHub storage quota, so the run is not green.
