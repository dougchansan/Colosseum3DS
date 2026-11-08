# Original trainer entry on New 3DS — 2026-11-08

**Build under test:** `feat/native-story-continuation` at
`d6491e6fbedd57f1678d8e102937300a538fd413` (embedded ID `d6491e6fbedd`), built with
the pinned `devkitpro/devkitarm@sha256:116afba8df84…` image (devkitARM r68-1 /
GCC 16.1.0, libctru 2.7.0-1, citro3d 1.7.1-2).

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `Colosseum3DS.3dsx` (installed; FTP readback before and after the run) | 741,680 | `baa1073a93ebcbe2a7358b805a07f26274b09d7dfda4947e5202b0773e75ae7f` |
| `Colosseum3DS.smdh` | 14,016 | `f3fad3feb7b42caab0a647bb392050bf72e4507593cded5a16819cc1c68e5a55` |
| `boot/willie.c3e` (private C3E1 pack; staged, read back, never overwritten) | 739,696 | `79b5cae8bd9cf3c2e06e32d399d082322cf8886fb9b75dcd4a379b7432bb0130` |

The pack was produced by `tools/prepare_battle_entry.py` from the canonical
GC6E01 `common_rel.rel` (SHA-1 `e8167618…`) and `main.dol` (SHA-1 `870e8b96…`).
It contains original table rows only and stays private.

The previous install (PR #10 candidate `5618ff8a…`) was replaced with
`tools/hardware.py deploy` (installed-hash check, local backup, staged upload,
readback). This build does not include PR #10's widescreen or battle camera.

## What ran

From the disposable WES profile (Outskirt Stand – Inside, 0:17), never the
completed save: HBL → title → Continue → shop exit → Willie's original
dialogue → encounter-10 handoff → **A: combat preview**. At that point
`c3_battle_preview_from_game_state` runs `c3_battle_entry_console_run` inside
the existing preview state guard, before the preview scene.

The session ran PR #9's trainer-entry action through
`c3_battle_frame_create_flow` with the original action tables, in
`_fightInitialize` order (kind 3 / buff 5 first, then buff 4). It used PR #9's
record services and the original Hero, runtime-trainer, party-sort and
FightPokemon constructors, with records built from the live save body.

Then: leave preview → field shows the same pending request → START+SELECT →
HOME → ftpd. The trace was read back before any relaunch.

Every input was one bounded Luma action with neutral releases and a fresh
NTR frame before the next action. The recording (private) is 5,287 frames
over 530.0 s, with 1 stale sample, 0 decode errors and SHA-256 `9664cdf5…`.

## Results (from [trace-excerpt.log](trace-excerpt.log))

| Check | Result |
|---|---|
| Entry admission and order | **PASS**: `trainers=1/1,10/0`, `preset_mode=0` (body+0xE404). Willie's flow ran first, then Wes's; `result=1,1`. |
| Trainer sequences | **PASS**: Willie gets loader row 38 (group 0x2DB) and maps to the stage model in slot 5; Wes gets row 1 (group 0x9D) and maps to slot 4. Each was created hidden, added to the grid and made visible. |
| Grid parameters | **PASS (traced)**: Willie (0,0), Wes (1,0). The stage still places models from `scene.c3v`; the grid values are recorded but not yet applied. |
| Player side from the live save | **PASS**: Umbreon level 26, 89/89; Espeon level 25, 69/69. These match the field status screen and the preview ([handoff.png](handoff.png), [preview-after-entry.png](preview-after-entry.png)). |
| Opponent side | **PASS**: two Zigzagoon at level 24, 52/52, personalities `2e8ed3ec`/`4ff92c53`, entry IDs 9 and 10. |
| ARM/x86 determinism | **PASS**: the opponent personalities, final RNG `b4422412` and 354 draws are identical to the host run over the same pack. |
| Timing / memory | Entry took 114.8 ms wall. Linear free memory after entry is 30,893,056 bytes, the same as the established baseline. |
| Isolation | **PASS**: `save_writes=0 gsvm_resumed=0 authoritative=0`; the 122,832-byte `preview_state_guard` is unchanged with `play_time_advanced=0`; the GCI read back after the run is byte-identical (`7e1cb315…`). |
| Field return | **PASS**: the same pending request, "No result or victory flag was written" ([field-return.png](field-return.png)). |

## Native choices and limits (not original observations)

- The session RNG seed `0x12345678` is the preview's labelled native seed. The
  original RNG state at battle entry is not modelled, so the opponent
  personalities are **not** a retail-parity claim.
- The original's uninitialized stack backing (the 0xB1C Hero image, Pokemon
  scratch and party-sort scratch) is zero-filled. The entry counter starts
  at 0.
- The Kaijou action (kind 1) that precedes the entries is not run; the stage
  owns venue presentation.
- Friendship filtering and the party-sort target-side graph are explicit
  failures. Encounter 10 reaches neither (kind 2 has floor flag 0x27 set but
  0x2E clear).
- The entry-built records are traced but **not yet consumed**. The preview
  still drives turns with its own state. Feeding these FightPokemon into
  active-fighter entry and original turns is the next gate.

No battle result, story flag, save write or native98 resume comes from this
work. Media here are unaltered recorder PNGs. Game assets, the pack, saves,
recordings and the full trace stay private.
