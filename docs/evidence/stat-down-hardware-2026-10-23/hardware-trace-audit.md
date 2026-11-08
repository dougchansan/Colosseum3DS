# Closed physical GREEN5b audit

The normal application is source `5b048c8b6299f8256a3392c66a1444deeca88f5d`, tree `1f50f8be1198acd91c5e1fbfcf1f99d70f77a0f7`. Its 589,112-byte binary has SHA256 `0c624b53fc899b4d32ae98a2380a3da0689a9f9a1a9b48f283e148c301c7c7d9`. The 686,002-byte trace has SHA256 `4ac473bf79e0782430ddc545a9ed73f53a278ebfe35e5965fd470b8b931c19d2`.

All manifest and supplemental readback entries verify. The base 11 and supplemental 17 entries duplicate only `audio.c3u`, giving **27 distinct effect files**, all independently hashed in the JSON audit.

## Reached effects and battle route

- GREEN special `0x29` executes event 21 on target 0/cue 4, then event 22 on target 1/cue 5. Lines 2282/2312 record units 118000/180000. Each reports 369 emitted, peak 232, 1,772 cosmetic draws, natural audio finished, cleanup finished, and actor color/camera restored. Cameras use motion 2/duration 118/draws 7, then motion 0/duration 180/cumulative draws 18. This proves the reached committed order, not universal target ordering or all variants.
- Growl event 23 on owner 2/cue 6 records STOPPED at 230000 units, 26 emitted, peak 25 and 200 draws. This is not natural audio completion.
- Tail Whip owner 3/cue 3 completes at 169440 units with controller/effect/audio done and owner restored. Reflect completes owner 1/cue 1 and owner 0/cue 2.
- Confusion attack (26 emitted/24 peak/134 draws) and defender (738/652/3302) complete twice. Bite defender completes at tick 380 with 82/81/325 and the HP/faint gate released.

There is **one WIN on turn 4**, followed by field return, preview re-entry at turn 0 and cancellation from the initial preview UI. Both exits have empty errors, `save_writes=0` and `gsvm_resumed=0`. Both 122,832-byte guards record unchanged state, equal before/after SHA1 and no play-time advance. Both returns record `vm_preserved=1 models=9`. Final field/boot termination has result 1 and an empty error.

The prior closed 2e9 save snapshot and final save are byte-identical: 393,280 bytes, SHA256 `7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`. The backup is absent. This comparison and the guards support the reached nonpersistent preview contract, not general save-system verification.

## Recovery and closed recording

I independently viewed native frames 9200 and 10010: they show field-to-battle/field **GPU busy recovery overlays**, each at a displayed wait of 0.0 seconds. Subsequent trace drains have `ready=1`, including field-to-battle at 26,951 microseconds and final field at 38,813 microseconds. Fresh HBL frame 10180 and FTPD frame 10680 show that the route recovered and exited. This is not a claim that GPU busy never occurs or that hangs are impossible.

The trace has one boot-build record and the continuous capture reaches HBL/FTPD after normal application termination. No reset is evident in this bounded route; this audit has no independent uptime counter. Recorder metadata says view-only/input packets 0, which describes the recorder rather than separately authorized control operations.

The silent MP4 contains 10,689 frames over 1,068.953 wall seconds at an encoded 10 fps. JSONL has exactly 10,689 records; only initial frame 0 is nonfresh. Decoder errors and encoder exit are 0. An independent full FFmpeg decode also exits 0. Actual video SHA256 `da558c640c8ae1f40721e2fc29f0e453b0c05ef36015230a97b4b2ca27abf134` matches metadata. There is no audio stream. The capture rate is not a hardware FPS measurement.

## Resources and measured costs

All nine GPU release reports record models/textures/bytes 0. Both battle-release snapshots have 30,893,056 linear bytes free; both field-restored snapshots have 15,233,408. Sampled battle linear free is at least 2,697,344 bytes (median 3,065,984). GPU buffers range from 27,335,672 to 27,704,312 bytes. These reached snapshots are not a general leak proof.

Across 515 periodic samples:

| Metric | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: |
| Draw CPU, microseconds | 61,350 | 79,714 | 137,903 |
| Full CPU frame, microseconds | 112,909 | 128,685 | 277,063 |
| Previous GPU, milliseconds | 6.723 | 12.176 | 48.530 |

The sampled full-CPU peak is 280,488 microseconds. Maximum command use is 148,308 words and particle commands 129,226, below capacity 262,144. Maximum sampled particles/vertices are 1,133/6,798; cumulative particle peak is 1,369. CPU and previous-GPU figures describe different stages and are not additive. Substantial CPU cost remains.

## Limits

No nonempty error, fault, timeout or overload records were found. Seven field-audio presentations remain explicitly unrendered; eight sendout records mark appearance effects, cameras and audio unsupported.

Growl owner continuation is asserted by a completion flag, without per-actor clip/clock telemetry. GREEN restoration flags do not prove every intermediate color frame or idle phase. The separate actual-resource host regressions verify those code paths: six profiles and 12 strict/sanitizer runs. This trace audit makes no claim of all variants, every cue fully played, audible parity, retail timing/shared RNG, original wet DSP mixing or complete retail story behavior.
