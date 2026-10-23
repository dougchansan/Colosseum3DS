# Encounter-10 actor-animation milestone

The normal application now renders the original arena, Wes, Willie, Umbreon,
Espeon and two independently posed Zigzagoon, with controller-selected source
actor motions, HP changes, fainting, a preview win and safe field return.
**The original battle presentation and authoritative story battle remain
incomplete.** Original ball/move effects, camera sequences, audio, UI/font assets
and projected shadows are absent. The visible development indicator says so.

## Corrected physical run

Source: `c9a43ddc13be` (full identity in [verification.json](verification.json)).
Normal executable: **426624 bytes**, SHA-256
`9d64014f970b09939d8ec05a80911c5b4cc80b5763e34a50c1b55f3d88b7b344`.
The executable was backed up, staged, read back, installed, and read back again;
the same hash was retrieved after the final clean exit.

[Actual hardware MP4](hardware-controller.mp4),
[readable commands](hardware-readable-commands.png),
[preview win](hardware-readable-win.png),
[pending field return](hardware-corrected-return.png),
[re-entry](hardware-corrected-reentry.png),
[clean exit](hardware-corrected-exit.png), and
[final FTPD](hardware-final-ftpd.png).

The physical route was Continue fresh WES → shop conversation → leave shop →
original Willie challenge → encounter 10 → actor appearance → Bite/Reflect and
Bite/Confusion turns → both faint presentations → computed preview WIN →
the same pending field encounter → re-entry → cancel leaving → START+SELECT.
NTR remained available; no console reboot, NTR reload, or CFW setting change was
used. Remote navigation reopened FTPD after this corrected run. The earlier run
needed the owner to reopen FTPD after remote HOME input stopped responding.

Both physical preview calls compared the **entire 122832-byte live state body**
before and after, with identical bytes and SHA-1 in each pair. Field play-time
updates between separate calls are legitimate and produce different pair hashes.
The waiting VM and encounter remained live; entry scripts were not rerun.
Native98 never resumed and no preview result, party, reward or victory flag was
written. See [the downloaded trace](hardware-trace.log).

The 393280-byte fresh WES GCI was unchanged before, between and after the two
normal hardware runs: SHA-256
`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`.
No Save controller action was issued, and the completed-save original was not
loaded or changed. GCI files and prepared/original assets remain private.

The MP4 is a **silent, edited sequence of actual NTR samples**. Gaps between
bounded observations are omitted; source sample UTC/frame counters are retained
in [hardware-recording.json](hardware-recording.json). It contains no original-
game reference footage or generated scenes. Encoding/sample rate is not gameplay
FPS. The older recording and screenshots explicitly retain their earlier broken
HUD and must not be used as evidence of the corrected HUD.

## Emulator evidence

[Actual corrected-HUD probe MP4](emulator-controller.mp4),
[four-Pokémon HUD](emulator-readable-hud-four-pokemon.png),
[command selection](emulator-command-selection.png),
[first HP-zero event](emulator-first-faint-hp-zero.png),
[opposing hit](emulator-opposing-tackle-hit.png), and
[both fainted](emulator-both-fainted-pre-result.png).

Azahar 2126.0 executed the diagnostic ARM probe with isolated in-memory WES and
original prepared tables/assets. Its executable SHA-256 is
`a18616a6fbf5c6136a3cc0bbd9f58aae1772b110d17e00de6add4f8fb16d0024`.
This was built from the recorded `a99356029579` snapshot plus the attached
[source patch](emulator-tracked-source.patch.gz), **not relabeled as the normal
physical executable or a newer head**. The corrected renderer bytes match the
subsequently committed renderer. The patch's historical submodule `-dirty`
marker was stale Windows index metadata: all 4023 tracked upstream files were
later verified byte-identical to their existing pinned blobs before metadata was
refreshed. No source-pin exception was introduced.

The 900-frontend-frame smoke completed without guest faults. A bounded 300-second
controller run executed source attacks, matching HP updates and both faint
motions, and computed a turn-two win. Its time bound expired immediately before
the outcome presentation event. **Emulator outcome UI, field handoff and clean
application exit were not accepted by that probe.** The normal physical run
separately covers those gates. The emulator video is silent; software and
llvmpipe runs do not establish physical-console speed.

## Coverage and measured limits

| Area | Accepted evidence | Remaining boundary |
| --- | --- | --- |
| Original geometry / actor clips | All eight packs exported and every half-sample loaded through production sampling; six actor instances on hardware | Original-game visual comparison footage not captured |
| Introduction / send-out | Source idle appearance and typed visibility event | Ball effects, trainer introduction and original camera sequence absent |
| Admitted moves | Source actor-role bindings for all twelve; hardware Bite, Confusion, Reflect and opposing actions | Full original WZX/particle/material effect execution absent |
| HP / faint / win | Delayed HP bars, source faint clips and preview win; hardware return/re-entry | Original sequence timing is not qualified |
| Loss | Deterministic host encounters include 46 losses; source trainer loss clips exported | Rendered loss capture remains pending |
| UI / controls | Readable development HUD, move/target/PP/messages, cancellation; earlier normal run also exercised revision | Original font/layout/status icons and complete graphical battle interface absent |
| Lifecycle / failure | Drained field↔battle ownership, live VM/body preservation, normal exit; synthetic missing/malformed assets and partial restore cleanup | Physical missing-asset fault injection not performed |
| Audio / shadows | Explicit unsupported status | Music, cries, sound effects, original projected shadows absent |
| Combat authority | Independent diagnostic RNG and preview policy remain labeled | Original AI/full-turn qualification and result/writeback remain blocked |

[Physical measurements](performance.json): 817 battle samples, median CPU draw
80.532 ms (p95 82.758 ms), median previous GPU sample 12.998 ms, maximum 153 draw
calls / 60831 submitted vertices, and 16441080 GPU-buffer bytes. CPU draw timing
excludes animation sampling; these numbers are not complete frame times or an
FPS claim. Full-run allocator checkpoints peaked at 45665192 used bytes; normal
heap capacity was 95997952 bytes and linear capacity 33554432 bytes. Transition
checkpoints explicitly show field payload release before battle allocation.
Observed peaks are not continuously sampled allocator high-water measurements.
Controller-to-first-HUD observation was between 4.85 and 5.37 seconds, including load, first render, NTR delivery and half-second sampling. Native load-time instrumentation and streaming-off comparison remain uncollected.

Validation includes 22 native suites, strict and ASan/UBSan, and 1,355,632 preview
checks comparing presentation-disabled combat with 1/17/60000 ms timelines.
The 100 synthetic encounters retain 54 wins / 46 losses and identical entire
combat/RNG state. Host Python discovery has 213 cases with optional environment
skips; separate capture/shader/preparation runs cover the required tools. The
Windows preparation run executed 87 of 88 cases; its symlink-creation skip is
executed by Linux CI. Check the PR's exact-head full-checkout CI for final status.

The next presentation dependency is the original WZX interpreter and its PDT,
particle, camera and sound execution interfaces, not more guessed actor clips.
The authoritative story battle separately requires original AI/turn scheduling,
complete-turn original-execution comparison and validated result/writeback.
Keep this PR draft and native98 suspended.
