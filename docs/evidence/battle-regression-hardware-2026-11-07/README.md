# Physical battle-animation regression at PR #7 head — 2026-11-07

**Build under test:** PR #7 `feat/native-battle-initialization` head
`7898cf137e2655c006c0d6d8782e6df8b28475c9` (embedded ID `7898cf137e26`), normal
application, built locally with the pinned image
`devkitpro/devkitarm@sha256:116afba8df84…` (devkitARM r68-1 / GCC 16.1.0,
libctru 2.7.0-1, citro3d 1.7.1-2): `make -f Makefile.3ds C3_BUILD_ID=7898cf137e26`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `Colosseum3DS.3dsx` (installed + FTP readback after both runs) | 678,008 | `24ce8089f07ddcb80e8f144ed67af576dc54db725595d944e9ebd5cf954701a7` |
| `Colosseum3DS.smdh` | 14,016 | `f3fad3feb7b42caab0a647bb392050bf72e4507593cded5a16819cc1c68e5a55` |
| Battle-preview probe (built, not deployed) | — | `5565c5db46157555a9554c3ae01c45fab9bfb26d4a4fbeec900134294056ae7f` |

Device: New Nintendo 3DS, Luma3DS/Rosalina, Homebrew Launcher v2.4.3 (Rosalina),
ftpd v3.2.1, BootNTR stream (TCP 8000 / UDP 8001), Rosalina InputRedirection (UDP 4950).
The previously installed build (`2b9bde02…`, source `8b36f58`+trace) was replaced
through `tools/hardware.py deploy` (staged upload, byte readback, recoverable
`.backup-7afb7480…` copies). All 33 installed effect assets are byte-identical to
the c271 Tackle checkpoint readback; the base scene is the qualified `scene.c3v`
(`2c3eddcc…`). Nothing under `boot/` or `data/` was modified.

## Runs

Two complete runs on one console boot session, both from the disposable fresh WES
profile (Outskirt Stand – Inside, 0:17; GCI `7e1cb315…`), never the completed save:

HBL → title → Continue → shop exit → Willie's original dialogue → encounter-10
handoff (native98 waiting) → combat preview → 4 turns → preview WIN → pending
field return → reentry → leave/discard → START+SELECT → HOME → ftpd.

Commands (seeded preview policy `0x12345678`, same route as checkpoints c271/5519):
turns 1–2 Umbreon Snatch + Espeon Reflect; turn 3 Bite→Zigzagoon A, Confusion→B;
turn 4 Bite→B, Confusion→B. Every input was one bounded Luma action with three
neutral releases, followed by a fresh NTR frame (`fresh=true`, age < 1.5 s) before
the next action.

- Run 1 also relaunched the app to the title and opened Continue (save summary
  unchanged), but that relaunch truncated `boot-trace.log` (opened `"w"` per launch),
  so run 1 has visual evidence only.
- Run 2 read the trace back before any relaunch; the per-effect records below are run 2.

Recordings (private): run 1 9,085 frames / 908.6 s, 1 stale sample, 0 decode
errors, SHA-256 `57ab5b1e…`; run 2 5,615 frames / 561.6 s, 1 stale sample, 0 decode
errors, SHA-256 `d290673a…`. Capture is silent and its cadence is not game FPS.

## Effect matrix (hardware)

| Effect | Result | Evidence |
|---|---|---|
| Bite | **PASS** (bounded) | Run 2 trace: model ticks 142→165, SE 85 requested, model hidden + floor restored; damage 82 particles, `hp_faint_gate=released`. [bite-damage.png](bite-damage.png) |
| Tackle | **PASS** | Events 9/11 (Umbreon) 298040 units and 34 (Espeon, after faint) 176000; each 134 emitted / 133 peak / 964 draws, audio finished, family cancelled, camera restored — identical to c271. [tackle-umbreon-impact.png](tackle-umbreon-impact.png), [espeon-tackle-after-faint.png](espeon-tackle-after-faint.png) |
| Confusion | **PASS** | Both phases on turns 3 and 4 complete, `audio_done=1`; caster sphere and capture/composite damage visible. [confusion-caster.png](confusion-caster.png), [confusion-damage.png](confusion-damage.png) |
| Reflect | **PASS** (bounded) | Two instance completions (owners 1, 0); barrier visible. [reflect-barrier.png](reflect-barrier.png) |
| Tail Whip | **PASS** | Event 20 168720 units, controller/effect/audio done, owner restored. [tail-whip-ring.png](tail-whip-ring.png) |
| Growl | **PARTIAL** | Event 23 completes, camera restored, but `audio_status=stopped` (source tick-230 stop, not a natural finish). The glow is hard-clipped at floor level ([growl-glow.png](growl-glow.png)); not yet compared with the original. |
| Defense-down (GREEN) | **PASS** | Events 21/22 on Umbreon/Espeon: 369/232/1772, audio + cleanup finished, color and camera restored. [umbreon-green-defense.png](umbreon-green-defense.png), [espeon-green-defense.png](espeon-green-defense.png) |
| Attack-down (RED) | **PASS** | Events 24/25, same counts, color and camera restored. [umbreon-red-attack.png](umbreon-red-attack.png), [espeon-red-attack.png](espeon-red-attack.png) |
| Faint ×2 | **PASS** | Zigzagoon A (Bite) and B (Confusion) faint and leave the field. [zigzagoon-a-fainted.png](zigzagoon-a-fainted.png) |
| Stale Bite marker / faint wait | **PASS** | The post-faint Tackle (event 34) starts and completes; the f6ef3a7 failure point does not recur in either run. |
| Snatch | **PARTIAL** | Announcement + "snatched REFLECT" event only; original WZX presentation not implemented. |
| Miss (special 57) | **NOT TESTED** | No miss occurs on this seeded route. |
| Return, Secret Power, Taunt, Helping Hand, Struggle | **NOT TESTED** | Original presentation not implemented. |
| Audio | **PARTIAL** | Operator heard sound only during Pokémon attacks (per-move SEs). No BGM, cries, menu or jingles. NTR records no audio. |

These are execution/category results on hardware. No retail visual or audio parity is claimed.

## Isolation and persistence

- Every `preview_turn` and `preview_exit` record: `save_writes=0 gsvm_resumed=0`; no `preview_runtime_failed`, no nonempty `error=`.
- Both 122,832-byte `preview_state_guard` records: `unchanged=1`, `play_time_advanced=0`.
- The field shows the same pending encounter after WIN and after cancel ("No result or victory flag was written").
- The GCI read back after run 1 and again after run 2 is byte-identical to the pre-deploy snapshot (`7e1cb315…`). No `.bak` exists.
- Reentry restores fresh preview HP (89/69/52/52).

Story flags `0x0A05`, `0x03DC`, `0x03DD` are set in memory by the original field
script before the encounter request. They are not preview outputs and were not saved.

## Performance and memory (run 2 trace, 250 battle samples)

| Metric | Median | p95 | Max |
|---|---:|---:|---:|
| Full CPU frame (µs) | 129,007 | 192,737 | 246,850 |
| Draw CPU (µs) | 79,175 | 83,512 | 130,080 |
| Previous GPU (ms) | 11.3 | 13.6 | 45.9 |
| Draws | 151 | 231 | 1,271 |
| Live particles (peak per sample) | 232 | 652 | 1,369 |

**Full speed: FAIL.** The battle runs at ~7–8 fps, CPU-bound (draw preparation
≈50k vertices per frame). Linear free memory reaches a minimum of 2,231,936 B
during effects and returns to identical values at every stage across both battle
entries (30,893,056 after release, 15,233,408 after field restore), so there is no
linear-memory leak across entry/exit cycles. Renderer drains on every transition
report `ready=1`; no recovery overlay was visible this time.

## Other observations

- Leave-preview prompt wraps mid-word ("wil / l be discarded").
- Run 1 lost a D-pad press at 1.2 s spacing; 1.6 s spacing was reliable. This is a harness-pacing observation, not an application defect claim.

## Host checks at this head

Strict: all C programs + 479 Python tests OK (67 optional skips). The ASan/UBSan pass
**failed** at `battle-rules-original-check` because ctypes loaded an ASan library
without the runtime preloaded (PR #7 head CI was cancelled, so this was not seen).
The accompanying Makefile change preloads `libasan` (leaks off) for that Python
step; with it, ASan/UBSan and strict suites both pass (479 tests, 67 skips).

Media in this folder: 17 unchanged fresh recorder PNGs ([frames.json](frames.json)
has UTC, counters, ages and SHA-256) and a sanitized trace excerpt
([run2-trace-excerpt.log](run2-trace-excerpt.log), full trace SHA-256 `29aca321…`).
Game assets, saves, recordings and the full trace stay private.
