# Story battles on their 3D stage

With Options > BATTLES = PLAY, a story battle other than encounter 10 runs on
the general engine (`src/battle_general.c`). When the private 3D assets cover
it, it now plays on its original battle floor in 3D on the top screen, with
the command UI on the bottom screen; otherwise it falls back to the
bottom-screen battle as before.

## Pieces

| Piece | Role |
| --- | --- |
| `tools/prepare_battle_stages.py` | Private converter: one stage per battle floor, one pack per PKX model and trainer sequence, `index.c3n` (format in the file's docstring and below) |
| `src/battle3d_index.c` | Reads `index.c3n`: floor → stage descriptor, species/trainer → pack and PKX entry clips |
| `src/battle_general_view.c` | Turns a turn's event log into presentation events (one per logged line; a batch ends before each send-out) |
| `platform/3ds/battle_general_stage.c` | Composes arena + four positions + two trainers, plays the events |
| `platform/3ds/battle_general_3d.c` | The scene: stage on top, `battle_general_scene.c`'s command UI on the bottom |
| `platform/3ds/field_scene.c` `play_story_battle()` | Drains the GPU, releases the field's models, runs the scene, restores the field |

Assets live flat in `sdmc:/3ds/Colosseum3DS/boot/battle3d/`. The generic hit
effect and battle sounds come from the encounter-10 set in `boot/battle/`.

```
python3 tools/prepare_battle_stages.py --disc <extraction> --common-rel <common_rel.rel> \
    --parser <HSD importer at the pinned commit, LF checkout> --output private/battle3d --approximate
```

The parser must be an LF checkout of the pinned commit (with
`core.autocrlf=true` the pin check fails); `git archive` of that commit works.
`zstd` must be on PATH (WSL has it).

## What is shown

- Stage: the battle floor's arena scene (the floor's FSYS scene DAT) and its
  authored lens; the camera position is the native command view used by the
  encounter-10 scene (behind the hero's side), not the original camera
  sequences.
- Actors on battleGridUpdate's one-trainer grid (singles on the centre line).
  The hero trainer stands off the camera's centre line (approximation).
- Clips from the original PKX animation entries: send-out (idle with a short
  scale-in, approximation), attack (entry 2 for physical types, entry 1 for
  special types and status moves; Gen-3 type rule), hit (entry 8), faint
  (entry 10, held, then hidden), trainer win/loss (entries 5/10).
- Every damaging hit plays the original generic damage effect (WZX 04E32000:
  hit model, particles, floor flash, sound 0x86). It holds the timeline only
  up to its HP gate.
- Swapping in a different species reloads the stage (about 2.5 s on a New 3DS).

Not shown: move-specific WZX effects (the encounter-10 ones are bound to
those exact models), status-condition effects, the original intro WZX and
camera sequences, Pokémon cries, battle music.

## Coverage of the prepared set (2026-11-13)

All 37 battle floors used by encounters, 224 of 259 Pokémon models, 63 of 65
trainer models. A battle is 3D when its floor, opening Pokémon and both
trainers are prepared; a later switch to an unprepared species ends the 3D
stage and the battle continues on the bottom screen.

Approximations, all recorded per floor/model in `battle3d.json`: arenas over
a 10 MB decompressed budget have animated models capped to 31 samples or
frozen (floors 7, 29); dropped second texture layers and nearest blends on
several floors; 37 Pokémon models over the 64-track pack limit have their
closest skin tracks merged (worst 3.5% of model size); 16 models sampled at
30 Hz to stay under 4 MB. Still skipped: 35 Pokémon models (animated texture
blend, emboss, alpha-mask layers, IK, singular transforms, unsupported
animation channels), trainer sequence 74 (reflection material).

Phenac: floors 7, 8 and 9 and all their trainers are ready; encounters 14,
15, 20, 21, 22, 23, 33, 450 and 469 are fully 3D; the others there wait on
species 74, 88, 320, 326, 332, 344, 361, 362, 372.

## Checks

- Host: `make -f tests/battle-general.mk battle-general-check` runs every
  admitted encounter's logs through the view and the presentation timeline,
  and the index reader on a synthetic index.
- `-DC3_GENERAL_3D_PROBE=<encounter>`: boots a fresh game's party into that
  encounter on its 3D stage, both sides on the opponent policy; writes
  `general3d-probe.log` and top/bottom captures, no save.
- Encounter 22 (Phenac Pre-Gym, floor 9): Azahar and the New 3DS both play 5
  turns to a win, damage effect on all four positions, 3056 frames with no
  GPU stall on hardware, stage load 2.5 s, 23.6 MB linear memory free.
- The field path (release, battle, restore) is built and wired but has not
  yet been played from a save on the console.
