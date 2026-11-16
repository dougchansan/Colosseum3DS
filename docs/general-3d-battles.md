# Story battles on their 3D stage

A story battle other than encounter 10 runs on the general engine
(`src/battle_general.c`). When the private 3D assets cover it, it plays on its
original battle floor in 3D on the top screen, with the command UI on the
bottom screen; otherwise it falls back to the bottom-screen battle.

## Pieces

| Piece | Role |
| --- | --- |
| `tools/prepare_battle_stages.py` | Stages per battle floor, a pack per PKX model and trainer sequence, `index.c3n` v2 (body-part bones, PKX timing marks) |
| `tools/prepare_battle_wzx.py` | Every WZX effect sequence as a C3WX pack with its effect models, particle banks and camera DATs; `wzx.c3n` (move roots, PKX overrides, variants, specials) |
| `tools/prepare_particle.py` (general path) | GPT1 banks as C3PT v2 particle packs |
| `tools/prepare_battle_sfx.py` | Every move sound, cry and battle UI sound as `sfx.c3s` (MusyX FX rendered offline) |
| `tools/prepare_wzx_camera.py` | The battle camera's DOL tables as `wzx_camera.c3k` |
| `src/battle_general_view.c` | A turn's log as presentation events (status/stat specials, recalls marked) |
| `src/wzx_player.c` | Schedules any effect sequence as the original does (link timing, owner motion delay, control nodes, hit/finish marks) |
| `src/wzx_camera.c` | The original battle camera: procedural, camera DAT and idle random camera |
| `src/particle.c` | The particle interpreter (all psInterpretParticle0 opcodes, generator shapes) |
| `platform/3ds/battle_general_stage.c` + `battle_general_wzx.inc` | The stage: arena, Pokemon, trainers, the WZX backend (models on body parts, particles, colour keys, owner actions, sounds), cameras |
| `platform/3ds/battle_general_3d.c` | The scene: stage on top, command UI below, music, sounds, per-turn effect loading |
| `platform/3ds/sfx_player.c` | Sound requests by id on NDSP channels 12-19 |

All assets live flat in `sdmc:/3ds/Colosseum3DS/boot/battle3d/` (about 360 MB).
Converters need the HSD parser as an LF export of the pinned commit
(`private/hsd-parser-dc71de8`) and `zstd` (WSL).

## What a battle shows

- Music: the encounter's BGM (encounter override, battle floor, trainer kind)
  and the floor's environment sound; the field's music returns afterwards.
- Intro: the battle floor's appear sequence, else the trainer intro (0x5F).
- Send-out: the Poke Ball's open then out sequences with the cry; recall the
  in sequence; faint the faint clip and the downin sequence.
- Moves: the attacker's own WZX sequence (PKX override for its species
  first) run to completion, then the defender's damage sequence; the HP drop
  waits for that sequence's damage mark. Each owner plays the PKX entry its
  sequence names, delayed as the sequence requires. Effect models on body
  parts, particles from body parts, floor and Pokemon colour keys, sounds.
- Status conditions and stat changes: their battle specials on the Pokemon.
- Cameras: each sequence's original camera (procedural or camera DAT);
  between them the idle random camera; the command menu shows the command
  view.
- End: trainer win/loss clips and wzx_win / wzx_lose.
- A different species sent out swaps only that pack (about 0.4 s).

## Coverage (2026-11-14)

- All 557 admitted encounters: floor, every Pokemon and both trainers.
- Effects: 347 of 354 moves convert completely (7 lose one model or root),
  1593 of 1594 particle launches, 265 of 274 effect models, 133 camera DATs.
- Sounds: 1005 of 1009 requests (4 music jingles remain).

## Approximations and gaps

- Effect types (battle_wzx_effects.inc): distortion uses the Confusion
  capture path (masks over 128 px filtered down, particles not captured);
  banners fill the view in front of the camera; shine is additive passes of
  the owner instead of a GX environment map; after-images use past
  positions with the current pose; leaf clones don't play the DAT's
  animation; lightning is untextured.
- Models that carry the Pokemon (Dig, Fly, Dive): the owner follows the
  effect joint's position, not its rotation or scale.
- Particles draw unlit, POINT as quads; 1536 particles per launch; AppSRT
  scale/rotation is identity.
- Models: approximations per pack in the reports (merged skin tracks,
  dropped texture layers, held blends, IK/joint-path handling, base shape
  for shape animations, specular dropped on TEV-lerp batches).
- Balls: the hero's Pokemon use their recorded ball; opponents the Poke
  Ball (trainer records carry none); balls past the fourth use the Poke
  Ball's faint sequence. Stat-change colours other than Attack/Defense are
  inferred; the camera's random stream is cosmetic; PPC float rounding is
  not reproduced.
- Sounds are mono, panned by the owner's place in the view (no reverb).

## Checks

- Host: `make -f tests/battle-general.mk battle-general-check` (view vs the
  presentation for every encounter, the index, the WZX player on every pack,
  the camera), `make particle-disc-check`, the converters' unit tests.
- `-DC3_GENERAL_3D_PROBE=<encounter>`: a fresh game's party fights that
  encounter in 3D with both sides on the opponent policy; writes
  `general3d-probe.log` and captures, no save.
- Azahar: encounter 22 (Phenac Pre-Gym, floor 9) and 53 (floor 10) play to a
  win with every move's sequences, ball send-outs, faints, specials and the
  original cameras (Azahar has no DSP firmware, so silent).
