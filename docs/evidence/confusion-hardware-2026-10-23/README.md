# Confusion physical execution checkpoint

Normal SD-launched source `e9968de76e9785577f891bd43ef5d43b7464076a`
(`e9968de76e97` on screen), tested October 23 HST / October 24 UTC, 2026.
The retrieved executable is 533,508 bytes, SHA-256
`7b52974dfebcedb96443afed17f0d796ee0c5dbe0f3446c9d16adffea2d34567`.
Pinned strict ARM normal/probe builds and [CI 40063078573](https://github.com/dougchansan/Colosseum3DS/actions/runs/40063078573) pass.
Later PC-capture/documentation commits are not implicitly hardware-qualified.

Saved WES Continue -> shop exit -> Willie -> nonpersistent preview:
Bite A / Confusion B, then Bite A / Reflect -> WIN -> pending field ->
re-entry -> Bite B / Confusion A -> cancel -> field -> START+SELECT ->
Homebrew Launcher -> HOME -> FTPD. NTR stayed active without reboot or reload.
The exact executable and all six Confusion/camera/audio files were retrieved
and hash-verified after the run; their identities are in `manifest.json`.

Both target runs complete attack phase with 26 particles, peak 24 and 134
cosmetic draws. Damage completes at 402,000 timeline units with 738 particles,
peak 652 and 3,302 draws. All five damage roots execute (2@0, 3@12, 7/4/8@44).
Actual sound buffer telemetry completes requests 0x443, 0x472, 0xE9 and 0x473
with distinct cue identities. Both phases report `audio_done=1`.
Attack completion units vary with actual NDSP completion (221,820 / 221,340).
This is buffer-completion evidence; the NTR recording contains no audio.

Reflect completes sequentially for Espeon and Umbreon with distinct cues.
Three Bite defender sequences complete at source tick 380, each with 82
particles and 325 cosmetic draws. Both 122,832-byte preview guards are
unchanged and report zero play-time advancement. The retrieved GCI remains
SHA-256 `7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`.
No Save action was issued. The 652,782-byte trace ends `boot_end result=1 error=`;
SHA-256 `a5a26c71afe2713f250ac18de99ddf12ca0f8f411ad4dabe978fd98fdcb77afd`.

## Published media and limits

`hardware-confusion-reflect-win-silent.mp4` is an actual 200-second excerpt
starting at encoded second 265 of the 900-second recording. Screens were
sampled at 10 fps, including repeated frames; encoded FPS is not game FPS.
The original recorder produced 9,000 frames over 900.016 seconds with one
initial stale sample, zero decode errors and successful encoder exit. Original
SHA-256 `8f532f9c8c5c713bb859aa687171e435b2ca1d904c6915333a97ea475b721982`.
The recording begins after an earlier PC publication failure; it is not an
uninterrupted capture from application boot. All selected images have both
screens fresh. Frame indices, timestamps and counters are in the manifest.
The ring and burst images were extracted from video because one-second PNG
sampling misses their best frames. Published MP4s pass full FFmpeg decode.

`original-confusion-ring.png` is genuine Dolphin retail-reference footage,
not port output. The source is the admitted GC6E01 revision-0 disc, official
Dolphin 2603a, fresh WES Continue -> Willie. The original raw reference has
640x512 pixels with 16:15 sample aspect; it is not camera/pose/effect-age
synchronized with the native screenshot. Do not infer pixel parity from it.

`emulator-confusion-diagnostic-silent.mp4` uses the e996 boot probe (SHA-256
`d45287aec41b81beeb337dee6751bbc83ca0b4537dec283c3806a0109cc6fb35`).
It shows both Confusion phases completing, then progresses into Bite. The run
is capped: 2,501 frontend frames, exit 124, `completed=false`; no clean
full-preview exit or performance claim. Its private Azahar core removes a
shared memory-region cache race that crashes the stock software renderer.
Diagnostic core SHA-256 `1a5b14b4449be449533c36a869da6f09a0f5baf735815d1ab5eb4b8aede63c3e`;
stock core SHA-256 `64431df51bba1ec09f36499ec2cc072498076ade6fc9f0fb117c3637699ec631`.
Original source/core were restored byte-for-byte. This is explicitly modified
emulator evidence, not a stock-emulator pass. Video is 1,251 samples at encoded
30 fps / 41.7 seconds and has no synchronized soundtrack.

Visual verdict: **82 / revise**. Actual hardware renders ring distortion,
POINT/trail particles and the mixed target burst. Camera/pose/phase matching,
PICA/GX filtering and line raster parity, original HUD/shadows, dry-audio
fidelity, other nine move effects, send-outs and full encounter presentation
remain incomplete. Sampled command peak is 144,876/262,144 words; minimum
linear free memory is 5,117,056 bytes; logged interval full CPU peak is 273,014 us.
These sampled diagnostics are not exhaustive maxima or performance acceptance.
Transient GPU-busy recovery screens drain successfully; they are not proof
that HOME can recover an arbitrary hard GPU/system fault.

Raw traces, full recordings, source resources, prepared packs and saves stay
private. See [the implementation contract](../../native-wzx-confusion.md).
