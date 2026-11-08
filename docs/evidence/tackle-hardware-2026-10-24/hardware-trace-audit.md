# Independent closed Tackle hardware audit

This audit reads the closed local packet. It performs no input, console action,
network connection, new capture or gameplay modification.

| Binding | Bytes | SHA-256 |
|---|---:|---|
| Corrected normal executable |614292|`47138d399fa38afdb48b63b886643f4e872607834f1fe5b21d3e98f4aad34203`|
| Corrected closed trace |492514|`bcf65ed238a78d3222be2801924e5664f0287b446938ca246069fda7f94ce18e`|
| Before/final save |393280|`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`|

Source `c2712ebb0f6eb8de38c0ff9025011868bdaecd2f`, tree
`27c9da48c86459aac97f67815f5b0cf7bf2c236f`; boot build `c2712ebb0f6e`.
Hashes were recomputed from readback bytes; save identity was also checked byte
for byte against the before snapshot. All 33 battle files independently match
both base/supplemental readback records and canonical private fixture bytes:
11 base +23 supplemental −1 duplicated audio =33 distinct. No pack bytes are
published. [Audit JSON](hardware-trace-audit.json) retains each file identity.

## Reached effect lifecycle

| Event | Caster slot | Target | Attack cue / damage cue | Defender units | Emitted / peak / draws |
|---:|---:|---|---|---:|---|
|9|2|Umbreon0|3 /4|298040|134 /133 /964|
|11|3|Umbreon0|5 /6|298040|134 /133 /964|
|34|3|Espeon1|19 /20|176000|134 /133 /964|

All three complete with audio=finished, family_cancelled=1 and camera_restored=1.
Each attack cue starts at 130020 units, followed by source-stop 133 defender handoff
with 20 retained units, raw flags 2, bank/POSITION flags 1 and HP gate 0. No runtime
failure record exists in the corrected trace. Two 122832-byte preview guards have
matching before/after SHA1, unchanged=1 and play_time_advanced=0. Six preview turns
and both exits report zero save writes and GSVM resumes. WIN occurs on turn 4;
final field/boot errors are empty. The normal readback matches the successful
strict ARM build; the separately hashed 580224-byte probe is not the physical app.

## Followup and host evidence

The failed f6ef3a7 trace records two completed Umbreon effects then the later
ResetExcept refusal. The unchanged failure PNG remains visibly labeled as that
candidate. Independent source/DOL inspection traced completed faint release to
KizetuEffect mode3→801DB100 (owner active/model cleared). The fix preserves the
bounded released-faint predicate and changes only the obsolete Bite wait-owner
marker to active resource ownership. Real queued Bite→FAINT→Confusion→Tackle
chains for both enemy slots fail on the old stage and pass strict/ASan/UBSan on
the correction, with no focused source drift. Affected legacy Bite checks pass.

The JSON includes source/helper/DOL pins and current production hashes from the
host audit. Its historical broad base working-tree run had two in-run source
drifts, disclosed there; final focused checks bind later versions. Separately,
exact c2712eb CI passes the full 38-C/334-Python suite in strict and sanitizer
modes (67 optional skips each), source verification, 17+22 capture tests and
native compilation. Overall CI remains failure solely at artifact upload quota.

## Media, navigation and limits

All 19 corrected selections and the one failed-candidate selection are fresh and
copied unchanged; hashes/counters/UTC are in [manifest.json](manifest.json).
The corrected full video and both silent video-only excerpts fully decode with
FFmpeg exit 0. The main video has 7484 encoded frames /748.4 seconds but 821.234 wall
seconds, four stale samples, zero decode errors and encoder exit 0. Neither
capture cadence nor the 5× proxy measures game FPS. Sampled CPU peak 274471µs and
minimum recorded linear free 2067072 bytes remain performance limits.

The corrected Willie CLI fails safely after five authorized actions; fresh-frame
manual navigation completes the route. The corrected separate 14-action route
passes in 60.437 seconds. The failed candidate's nine-action Willie success is
kept separate. Actual recovery overlays and clipped field text prevent an
uninterrupted visual-transition claim.

Original Tackle frames 8890/8997 are existing Dolphin official-DSU captures, with
movie-prefix/executable/disc/card identities retained. Original announcement
identity is verified, but camera/effect-age alignment and full fidelity are not.
Native capture is physical NTR. No new original capture, audible/wet-DSP,
shared/global RNG, all-variant or full-speed acceptance is asserted.


The approved [contextual visual verdict](visual-verdict.json) scores 78/revise,
category match. Derived video frame 4854 captures the bright core, avoiding the
initial 45 comparison's late dust frame 4860. Original 8997 and native 4854 still
lack camera/pose/effect-age registration; this supports effect-family evidence,
not a 90+ parity pass or a reason to change particle parameters.
