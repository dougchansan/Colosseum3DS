# Physical Tail Whip execution checkpoint

Normal source `5539f43fa3a6f2f71fb3dc6c69b326e0904e3059`, tested October 23 HST /
October 24 UTC, 2026. Pinned strict normal and boot-probe ARM builds pass;
[CI 40069102672](https://github.com/dougchansan/Colosseum3DS/actions/runs/40069102672)
passes host, sanitizer, capture and native jobs. The installed normal executable
was retrieved after the run: 549,388 bytes, SHA-256
`ad7189047ee7fe934136250943ec2b938bfdac945790fe950adeb3c9f0ae3a93`.
Later documentation commits are not implicitly hardware-qualified.

Saved WES Continue -> shop exit -> Willie -> nonpersistent preview:
Snatch/Reflect, Snatch/Reflect with enemy Tail Whip, Bite/Confusion,
Bite/Confusion -> turn-4 WIN -> pending field -> re-entry/cancel -> field ->
START+SELECT -> Homebrew Launcher -> HOME -> FTPD. NTR remained enabled without
reboot or reload. Snatch and the other unsupported moves retain their explicitly
incomplete presentation; their presence in this route is not effect acceptance.
The admitted roster gives Tackle/Growl to Zigzagoon A and Tackle/Tail Whip to B.
Both slot adapters are host-tested; this physical Tail Whip runs on B only.

## Trace and state preservation

Tail Whip event 20/owner 3 completes once, at 174,780 timeline units / 2.913 s.
The trace records controller/model ended, audio done and owner restored. Actual
NDSP cue 3 remains paired with sound 0x1DB and reaches SDK DONE/status 3 at
92,279 samples, exactly the selected pack's frame count at 32,000 Hz.
The controller, drawable, C3TC camera and eight-cue audio bank were retrieved
and hash-verified. The sound is selected dry PCM; this silent video cannot prove
speaker audibility, wet mix, clipping acceptability or retail audio fidelity.

Reflect completes Espeon then Umbreon. Two Confusion sequences complete both
phases, including five damage roots, 738 emitted particles, peak 652, 3,302
cosmetic draws and actual audio completion. Bite retains its 380-tick defender
completion with 82 particles and 325 draws. Turn 4 ends after Confusion faints
the last opponent; no second Bite presentation is claimed.

Both 122,832-byte preview guards are unchanged and report zero play-time
advancement, save writes and GSVM resume. Re-entry restores HP 89/69/52/52.
The retrieved GCI is byte-identical to predeploy: SHA-256
`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`.
The 1,112,844-byte trace ends `boot_end result=1 error=`; SHA-256
`04c1e68b4a82ccb3f31afe863d0d363daa939908ee7a2cbe7d229d02c8138dbe`.

## Actual footage and its clock

`tail-whip-silent.mp4` is a 14-second excerpt starting at encoded second 698
of `hardware-5539f43-live`. The close view and blue band are actual console
output. `win-return-exit-silent.mp4` starts at encoded second 120 of the separate
finish recording and contains WIN, return, re-entry/cancel, HBL, HOME and FTPD.
These are silent NTR screen recordings, not rendered mockups or game-FPS tests.
Both originals and excerpts pass full FFmpeg decode. Selected PNG timestamps,
source recordings, freshness, counters and all published hashes are in the manifest.

The first recording has 10,914 frames over 1,200.016 wall seconds, four stale
samples and zero decode errors. Recorder scheduling/encoding gaps mean its
encoded 10-fps clock compresses some wall time: **use JSONL UTC/wall timestamps,
not sample/10, for source timing**. The finish recording has 3,883 frames over
388.359 seconds, one initial stale sample and zero decode errors. There is a
receiver restart between recordings; the installed NTR setup remains loaded.
Original SHA-256 values are `4bb4ad62906b9a5fdc16c088f43d99ce2b0a2ad4128757ecbf71aadad427352f`
and `de72f62524adb60fe89fa7beff5abc91cef4c87d0c6aae3850dc0b6e30867df9`.

## Qualification limits

This qualifies bounded hardware execution and lifecycle. Tail Whip camera
admission and pack identity are logged, but its motion number/matrices are not.
Motion 2 can be inferred from the separate seed and source selector; it is not
a direct log observation or physical coverage of all four camera motions.
The host camera fixtures cover those branches and preserve Bite/Confusion pack
bytes. The earlier two command-only original Dolphin attempts remain excluded.
A subsequent [verified original Tail Whip reference](../tail-whip-original-reference-2026-10-23/README.md)
now includes the explicit announcement, female actor HUD, cyan rings and
before/after frames. That original HUD/caption does not establish an A/B slot.
The native B-slot identity above remains grounded in this run's native trace.

The structured comparison is **76/100, revise**, with category match true:
cyan-ring correspondence is visible, but camera/RNG variant, effect age,
viewport and display aspect differ. This is not pixel/retail visual parity or
a demonstrated geometry defect. Synchronized source-detail/frame-phase
comparison is still required. The original reference also exposes separate
green defender stat-down streaks/tint for both allies; those generic visuals
remain missing from the current native caster slice. Typed DEFENSE-fell
messages alone do not close that effect gap.

Periodic command maximum is 111,256/262,144 words; minimum logged linear free
memory is 4,627,328 bytes. Sampled full CPU maximum is 239,742 us; the distinct
logged interval peak is 275,608 us. These sampled diagnostics do not establish
all-frame high-water marks or full-speed performance. Original HUD/shadows,
remaining eight move effects, send-outs, source shared-RNG/GSmodel endpoint
parity and retail audiovisual fidelity remain incomplete. An arbitrary hard
GPU/system fault may still require physical recovery.

Raw traces, complete recordings, source/prepared resources and saves stay private.
See [the source contract](../../native-wzx-tail-whip.md) and the separately scoped
[Confusion checkpoint](../confusion-hardware-2026-10-23/README.md).
