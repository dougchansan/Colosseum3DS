# Local-agent handoff: native first visit on New 3DS

Continue on `feat/native-outskirt-story`, stacked on
`feat/native-first-area-hardware` (PR #4). Preserve the upstream full-vector
texture-coordinate shader fix. The sandbox has completed fresh New Game → WES
→ original opening and arrival → first exterior event → shop TV → Willie
dialogue → explicit Save, then a new-process Continue → exit → original first
battle request. This is emulator acceptance; the combined build has not yet
passed on the physical console.

## Goal and save policy

Verify the same first visit on the New 3DS at **192.168.50.154**, using the
existing Rosalina/Luma input setup, ftpd, and BootNTR Selector Enhanced 3.6
capture setup. Reuse the already working local configuration and calibrated
navigation. Do not install a different CFW or reload NTR without an observed
need. The completed save remains unchanged and reserved until the fresh route
is physically confirmed. Back up the current primary save and `.bak` before
any fresh-game deployment; retain their original hashes and paths.

## Build and prepare

1. Update the branch and pinned submodules. Run source verification and host
   checks. The maintained first-visit host test accepts the private prepared
   boot directory; the public tests contain only synthetic fixtures.
2. Build with the pinned devkitPro toolchain. The tested sandbox full executable
   was 381,732 bytes with SHA-256
   `fe5567d2d60835044d08ae4e335d3d47e95b20cad60914ae501fedadb9f36a63`.
   A local rebuild can have a different build ID/hash: record its exact source,
   toolchain, configuration, and deployment readback instead of relabeling it.
3. Regenerate the complete private bundle using
   [native-first-visit.md](native-first-visit.md). The manifest at
   `docs/evidence/native-story-2026-10-22/assets.json` lists all 35 required
   packs, exact hashes, and converter commands. It totals 428,164,858 bytes.
   Old assets are insufficient: Wes requires genuine source animation 0 as
   clip 3, the departure NPCs require their real animation packs, and C3A must
   contain the collision type and vehicle attachment mapping.
4. With the application exited and ftpd active, stage and verify the new assets
   and executable. Use the existing `tools/hardware.py` deployment workflow
   for the executable, preserve the previous installation, and hash-readback
   the complete asset set. Do not upload original assets, disc files or saves
   to GitHub. Use the observed ftpd port from the private configuration.

## Physical acceptance route

Use fresh NTR screenshots after each bounded input pulse. Use
`tools/luma_input.py`, `tools/ntr_capture.py`, and the calibrated
`tools/console_navigate.py` workflow; unknown visual states must be inspected
before the next action. ftpd transfers files; controller input and video use
their existing separate transports.

1. Select **New Game → WES** with no completed card installed. Watch the
   original hideout movie and bike arrival. Confirm controller movement and
   starter-party HP at Outskirt Stand.
2. Move north at the original spawn's right side, then left into the approach
   trigger. Let Folly and Trudly emerge, complete the two original messages,
   board the vehicle and leave. Confirm control returns.
3. Walk around the lower end of the entrance ramp, then up through the door.
   Finish the TV report. Walk through the aisle to Willie and talk across the
   counter; the counter must still block walking. Finish his yes/no dialogue.
4. Save explicitly. Retrieve and hash the new GCI at the next ftpd session.
   Relaunch and Continue that same new save; confirm the party, shop entrance,
   and absence of a repeated introductory report.
5. Leave through the shop exit and complete the original challenge dialogue.
   The expected endpoint is **encounter 10, combat backend pending**. Do not
   report Willie defeated, invent a result, write `03DE`, or unlock later areas.
6. Check clean exit and HOME suspend/resume on this combined build. Record the
   `platform_memory` heap/linear values and renderer completion counters from
   `boot-trace.log`. Keep physical frame-rate measurements distinct from NTR
   delivery rate and sandbox timing.

If a crash or stall occurs, preserve the exact executable/asset hashes, fresh
top and bottom screens, trace, dump if produced, and last controller action.
Reproduce from the earliest failing checkpoint. Fix the concrete cause and
attach the matching rerun evidence; do not bypass story natives or silently
advance flags to make the route pass.

## PR and next implementation

Open a new draft PR against `feat/native-outskirt-story` with any fixes and the
physical evidence. Keep the tested source commit and binary hash explicit;
later documentation commits are not a substitute for that binary identity.
Include a concise pass/fail table for the route above, the fresh save's hash,
and screenshots or an MP4. Leave existing PRs unmerged for review.

Only after fresh-game physical acceptance should the preserved completed save
be copied into a separate later-area test setup. Later floors and the complete
combat engine are not ported by this branch, so a loaded card alone is not a
playability result. The next implementation target is the original battle
state/action flow and validated result writeback described in
[first-battle-adapter.md](first-battle-adapter.md), followed by area-by-area
story and system coverage.
