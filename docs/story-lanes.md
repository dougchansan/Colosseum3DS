# Finishing the story: parallel lanes

Goal: every floor loads, every dialogue plays and the main story runs from New
Game to the credits, clear save and Continue on a New 3DS. See
[story-completion-status.md](story-completion-status.md) for the gates.

The work is split into lanes. Each lane runs in its own worktree and branch,
pushes, and opens a PR against `main`. The coordinator reconciles, rebases and
deploys to the console. Lanes never write to the console.

## Lanes

| Lane | Branch slug | Owns (floors by archive prefix) |
| --- | --- | --- |
| phenac | `lane/phenac` | S1, M1 (0x01-0x0E, 0x77, 0x80, Phenac battle fields): every interior, including the mayor's house 2F (0x06), the Pre Gym (native 61) and the Phenac Colosseum (0x0E) |
| pyrite | `lane/pyrite` | M2 (0x0F-0x23, 0x78, 0x8E): town, hotel, police, Pyrite Building, cave, Pyrite Colosseum (0x23) |
| agate | `lane/agate` | M3 (0x24-0x2E, 0x385) and D2 Mt. Battle (0x4A-0x5F, 0x8F) |
| cipher | `lane/cipher` | D1 Shadow Pokemon Lab (0x41-0x49, 0x8C-0x8D) and S2 Snagem Hideout (0x81-0x89) |
| under | `lane/under` | M4 The Under (0x2F-0x40, 0x79-0x7F, 0x8A-0x8B) |
| realgam | `lane/realgam` | D4 Realgam Tower (0x66-0x76, 0x7B-0x7C), T1 (0x7E), DNA key (0x387), ending, staff roll (0x39F), clear save, Continue |
| battles | `lane/battles` | Story battles end to end: no "skip as win", party writeback, EXP, loss and whiteout (`05960008`), snagging, colosseum tournament runs, `_bf` battle fields |
| systems | `lane/systems` | Message control codes, name entry (0x390), PC box (0x391), bag (0x38F), PDA (0x392), evolution (0x386), partner follower, audio hooks the story waits on |

A region lane owns its floors' packs, profiles and story checks. Shared engine
code (`src/story_*`, `platform/3ds/field_scene.c`, natives) is edited by any lane
that needs it, under the rules below.

## Rules that keep the lanes in sync

1. **Base**: branch from `origin/main` once the overnight integration has
   merged there; until then, from `origin/integrate/overnight-2026-11-16`.
   Rebase onto `origin/main` before each push. Check out with
   `core.autocrlf=false` and `core.eol=lf`.
2. **Claims**: before implementing a native, a shared helper or a change to a
   shared file's structure, read and append to the private ledger
   `G:\Colosseum3DS\private\lanes\claims.md` (one line: date, lane, item,
   status). If another lane has claimed it, use their branch's commit
   (cherry-pick) or wait; don't write a second version.
3. **Small commits**: one native, one floor or one fix per commit, so the
   coordinator can cherry-pick and resolve conflicts commit by commit.
4. **Evidence**: the floor loads on Azahar from a save at that point in the story
   (boot trace and frame), the scripts run to their end on the host, and
   dialogue shows no unknown control codes. Record shortcuts in the lane's
   doc, as `phenac-story.md` does; nothing is faked silently.
5. **Original only**: original 3D models, cameras and scripts from the disc,
   the pinned decomp and `vendor/gekkoctr`. No 2D stand-ins, and no extracted
   assets, saves or disc bytes in git.
6. **Azahar**: each lane uses its own portable copy,
   `G:\Colosseum3DS-emu\lane-<lane>`, and stops only processes it started (by
   PID). No simulated keypresses while the user may be at the desktop: use
   probe builds and traces.
7. **Push and PR**: push the lane branch and open a draft PR after the first
   working milestone; keep pushing as more lands, and mark it ready when the
   lane's floors are done. End PR bodies with the Claude Code attribution line.
