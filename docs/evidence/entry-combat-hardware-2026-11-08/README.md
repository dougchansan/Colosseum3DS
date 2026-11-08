# Entry records driving combat on New 3DS — 2026-11-08

**Build under test:** `integrate/entry-widescreen` at
`1d4036be0b6a2de4ab3b2ce6ce6c66eac6a26e77` (embedded ID `1d4036be0b6a`). This
branch merges PR #9 (`feat/native-story-continuation` at `1b71c0b`) with PR #10
(`feat/widescreen-top-screen` at `99659db`), then adds the entry-to-combat
handoff. Built with the pinned `devkitpro/devkitarm@sha256:116afba8df84…`
image (devkitARM r68-1 / GCC 16.1.0, libctru 2.7.0-1, citro3d 1.7.1-2).

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `Colosseum3DS.3dsx` (installed; FTP readback before and after the run) | 744,544 | `72a88c8eede48887b41d72b9b62e50695f6afd8b901cb6b4b58d60d00d687399` |
| `Colosseum3DS.smdh` | 14,016 | `f3fad3feb7b42caab0a647bb392050bf72e4507593cded5a16819cc1c68e5a55` |
| `boot/willie.c3e` (private C3E1 pack, unchanged) | 739,696 | `79b5cae8bd9cf3c2e06e32d399d082322cf8886fb9b75dcd4a379b7432bb0130` |

The previous install (the PR #9 entry candidate `baa1073a…`) was replaced with
`tools/hardware.py deploy`, which checks the installed hash, keeps a local
backup, uploads to staging and reads back.

## What ran

The disposable WES profile was used (Outskirt Stand – Inside, 0:17; GCI
`7e1cb315…`), never the completed save. Route: HBL → title → Continue →
shop exit → Willie's original dialogue → encounter-10 handoff
([handoff.png](handoff.png)) → **A: combat preview**. The scene ran the
original trainer entry, then started the battle from its records
([preview-from-entry.png](preview-from-entry.png)). Both turns were played
with the first move of each Pokémon on the first live opponent: Umbreon Bite
and Espeon Confusion, both on Zigzagoon A in turn 1 and on Zigzagoon B in
turn 2. Then: leave preview → field → START+SELECT → HOME → ftpd. The trace
was read back before any relaunch.

Each input was one bounded Luma action with neutral releases, followed by a
fresh NTR frame before the next input. The private recording has 5,822
frames over 582.2 s, with 1 stale sample and 0 decode errors (SHA-256
`20815760…`).

## Prediction

Before the run, `tests/test_battle_entry_preview.c` ran on the PC over the same
GCI (`C3_ENTRY_SAVE`), the same pack and the same preview tables, with the
same command policy. It predicted:

- **Turn 1:** Confusion → Zigzagoon A −52, which faints. Bite → Zigzagoon B −38. Tail Whip → Umbreon's and Espeon's Defense fall. RNG at 368 draws.
- **Turn 2:** Confusion → Zigzagoon B −14, which faints. WIN. RNG at 373 draws.

## Results (from [trace-excerpt.log](trace-excerpt.log))

| Check | Result |
|---|---|
| Entry | **PASS**: `result=1,1`. Willie (loader 38, slot 5) ran first, then Wes (loader 1, slot 4). RNG `b4422412` after 354 draws. Took 122.0 ms. Linear free memory 30,893,056 bytes. |
| Handoff | **PASS**: `entry_preview_begin source=entry_fightpokemon hp=89,69,52,52` and `preview_begin … source=entry_fightpokemon rng_draws=354`. Combat starts from the entry's FightPokemon and RNG, not the roster initializer. |
| Player side | **PASS**: Umbreon level 26 at 89/89 and Espeon level 25 at 69/69. These are the saved party bytes, enforced byte for byte. |
| Opponent side | **PASS**: Zigzagoon level 24 at 52/52 ×2, personalities `2e8ed3ec`/`4ff92c53`, taken from the entry records. |
| Turns vs prediction | **PASS**: every event text, every HP value and the RNG counts (354 → 368 → 373) match the PC prediction exactly ([turn1-bite.png](turn1-bite.png), [turn2-win.png](turn2-win.png)). |
| Isolation | **PASS**: `save_writes=0 gsvm_resumed=0` throughout. The 122,832-byte `preview_state_guard` is unchanged with `play_time_advanced=0`. The GCI read back after the run is byte-identical (`7e1cb315…`). The field shows the same pending request ([field-return.png](field-return.png)). |
| PR #10 in the same build | **PASS (visual)**: the top screen fills the full 400×240 with no letterbox, and the camera sits behind Wes's team. |
| Battle frame rate | **FAIL (unchanged)**: about 8 frames per second (`battle_render frames=` advancing about 8 per 1 s sample). Not addressed here. |

## Native choices and limits (not original observations)

- The entry session seed `0x12345678` is the preview's labelled native seed.
  The original RNG state at battle entry is not modelled, so the opponent
  personalities and turn rolls are **not** retail-parity claims. The
  preview's `seed` field records the RNG state at the handoff.
- The turn engine, enemy policy (uniform legal) and stage model placement are
  still the preview's own. The entry grid values are traced but not applied.
- The original's uninitialized stack backing is zero-filled. The Kaijou
  action is not run.
- No battle result, reward, story flag, save write or native98 resume comes
  from this work. The media here are unaltered recorder PNGs. Game assets,
  the pack, saves, recordings and the full trace stay private.
