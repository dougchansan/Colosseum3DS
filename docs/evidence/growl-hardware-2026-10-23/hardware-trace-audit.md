# Closed hardware trace audit — normal 2e9ec7108d16

Binary: 572800 bytes, SHA256 `389cc6fedc4aca83c5918af147ca77d910c556ac8306045fdca947d485238a7f`. Source tree `3cb124f0c05a442b7f54f90e6b03536134068dbd`. Trace: 4207 lines, SHA256 `861c8df3a66eb4d31190add4f3c701444bf8378d7230314a7eb88899a45057c7`. Every entry in the existing snapshot manifest verifies; the JSON additionally hashes all 24 battle assets.

The trace contains **one WIN on turn 4**, followed by field return, re-entry and cancellation at the initial preview UI. It does not contain four wins. Two `preview_exit` records have empty errors, `save_writes=0`, `gsvm_resumed=0`; both guards cover 122832 bytes with `unchanged=1`, equal before/after SHA1, and `play_time_advanced=0`. Both field returns record `vm_preserved=1 models=9`. Final `field_end result=1 error=` and `boot_end result=1 error=` are clean termination records.

## Effects actually recorded

- Reflect completes caster owner1/cue1 and ally owner0/cue2 (lines1452/1480).
- Tail Whip owner3 completes at units167220 with controller/effect/audio done, cue3 and owner restored (line2183).
- Growl event23 owner2 uses camera motion2, duration244, FOV pattern5, 7 cosmetic camera draws (line2193). Resources start at units130020. Cue4, stop230000, emitted26, peak25, cosmetic draws200; family cancellation, STOPPED and camera restoration are recorded (line2223). Sampled live particles23/vertices138 and actual voice528 playing at sample24560 precede stopped voice sample50373/explicit_stop1.
- Confusion attack completes twice (26 emitted/24 peak/134 draws each); defender twice (738/652/3302 each), with type12, line/particle roots and sound requests recorded. This is the reached owner/target route, not every camera or effect variant.
- Bite attack reaches source local142/165; defender completes tick380 with82 particles/81 peak/325 draws and HP/faint gate released (line2952).

## Owner continuation and audio qualification

Line2223 asserts `owner_released=1 motion_continues_at_half_rate=1`. There is no per-actor clip/clock trace proving the subsequent continuation or natural366 idle transition directly on hardware. The actual-resource host stage regression verifies those code paths separately. This audit does not infer a visual observation from the completion flag.

Growl is **STOPPED**, not naturally FINISHED. Voice samples prove selected cue playback and explicit stop, not original MusyX keyoff/release-tail parity. Recording is silent; no audible or all-cues-fully-played claim follows.

## Save, resources and performance

Final save:393280 bytes, SHA256 `7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`; backup absent. Trace guards and zero-write counters verify the nonpersistent preview contract; this audit has no separate pre-run save byte comparison.

All 9 `render_gpu` release reports record models/textures/bytes0. Both battle releases have linear-free30893056; restore returns to15233408. These snapshots demonstrate the reached cleanup route, not a general leak proof.

Across408 periodic samples: draw CPU min/median/max61245/79441/137175us; full CPU frame112485/127960.5/218878us, sampled peak275785us; previous GPU6.794/11.989/48.030ms. Max command141240 words is below capacity262144; max particle-command122158. GPU-buffer bytes27331064..27699704. Max sampled particles1071/vertices6426, cumulative live peak1369. These are trace samples, not a realtime/FPS claim; 10fps NTR capture is an evidence sampling rate.

## Closed recording and remaining limits

Recording8369 frames over839.594 wall seconds, one stale sample, decoder errors0, encoder exit0; independently full-decoded MP4 exit0. Actual SHA256 `d5a83ac8b06b1f7300ff2bbc77a60e1476fda60f541b01ab71eaf2871b7dda74` matches recording metadata. No audio stream. Recorder metadata says view-only/input packets0/TCP commands0; this describes the recorder, not separately authorized controller operations.

No nonempty error/fault/timeout/overload records were found. Seven field-audio presentations remain explicitly unrendered and eight sendout records explicitly mark appearance effects/cameras/audio unsupported. No full battle/story, retail timing/RNG parity, all variants, or original wet audio claim is supported.
