# Native main-story completion: active work

The objective is the original main story from genuine New Game through its
ending, credits, legitimate clear-save flow and a fresh-process Continue.
**That objective is not achieved.** The current native game supports the
Outskirt Stand exterior/shop, the first battle's result and the
[world-map destination choice](world-map.md). GSVM native 98 now resumes from
the outcome the battle scene reports (a hero win is the original result ID 2),
so the original Outskirt script sets its own flags, including Phenac's unlock
`0x3DF`. This is a deliberate shortcut: the result still comes from the
nonpersistent preview engine, and no HP, PP, EXP or money is copied back to
the party. Authoritative combat/result/writeback remains open.

## Newly verified prerequisites

- `battle_rules.c` executes pinned source result-combination and 16-slot AI
  maximum/tie-selection logic. Its focused suite covers 1,005,945 checks.
- An existing Dolphin interpreter sidecar executed 482 original-DOL fixtures:
  146 result cases and 336 AI cases. Native return values, result state,
  final seeds and draw counts match. See [the exact qualification scope](battle-rules-oracle.md).
- The side-aware win predicate has source tests only; original trainer/target
  identity resolution and full battle entry/turn/post-processing remain open.
- The temporary [pre-catch Pokemon constructor](battle-pokemon.md) now executes
  the pinned initialization, learned-move, personality and stat closure with
  staged 312-byte backing and RNG. Both ordinary Willie calls now match actual
  original entry/return observations in all 312 bytes, final seeds and draw
  counts (147 and 237). Shadow/cache and control-string names remain unsupported;
  parent catch/party creation and live battle integration remain open.
- A bounded [original-game observation client](battle-entry-oracle.md) verifies
  source instruction pins, callers, returns, private object snapshots and the
  independent save-attest pointer. The successful original constructor capture
  qualifies those two temporary pre-catch calls; full trainer/party and complete
  battle observations remain separate gates.
- The [parent trainer constructor](battle-trainer.md) now matches two original
  calls in all 2,840 hero bytes, all 312 scratch bytes and per-call RNG (386 and
  zero draws). Four [runtime Pokemon constructors](battle-fight-pokemon.md) match
  all 340 destination bytes, immutable 312-byte sources and unchanged RNG.
  These include temporary selection and final primary-entry callers. Party
  selection, inter-call RNG, sequence/FightTrainer creation and complete battle
  entry remain open.
- Two original [party-sort wrappers](battle-party-sort.md) now match complete
  trainer heads, six-record stack scratch, selected pointers, RNG and counters.
  The ordinary Willie case uses two draws and nine counter increments. Two
  [runtime trainer constructors](battle-runtime-trainer.md) match all 10,468
  bytes and preserve their 2,840-byte Hero inputs without consuming RNG.
  Alternate sorting branches, real sequence allocation, active fighters,
  complete entry and live stream ownership remain open.
- The [source index](story-source-index.md) inventories 196 lifecycle rows,
  594 events, 607 name rows, 1,835 TOC archives, 176 banks, 2,310 function slots,
  19,086 native sites and83 relocated lifecycle callbacks. It retains 163
  unresolved native sites, 13,352 unresolved argument words and 5,365 conditional
  branches. These are all-entry source inventories, not a mandatory-route or
  executable-runtime coverage claim. Original bytes and detailed output stay private.

## Original ending evidence

`movie.c` starts `movie/staffroll.thp` through `fn_800361C0`. The clear-save
path in `fn_80035F64` requires previous floor 0x76 and flag 0x476==1, then performs
the original guarded 0x478/save-prompt/return flow. The index locates flag 0x476
reset/read/write sites in floor 0x76's `D4_casino_colo.fsys` bank 0x0100.
Functions 12/15 call helper 16 before their flag writes. Caller literals 441/442
and selector flag 0x879 identify candidate final encounter dependencies; taken
branches, complete argument semantics and original-execution correlation are
not yet established. These observations never authorize granting those flags.

## Required remaining gates

| Requirement | Current evidence/status |
| --- | --- |
| Original shared RNG and full trainer/Pokemon/FightPokemon entry | Bounded parent, Pokemon, sorting and runtime constructors match original calls; live stream bootstrap, actual sequences, active fighters and complete entry remain required |
| Source-legal commands, AI scoring, ordering, effects, statuses, items and full-party replacements | Selected preview behavior only; authoritative complete turns still required |
| Terminal results, EXP/EV/rewards/capture/evolution and source party copyback | Isolated result rules qualified; complete result transaction and original witnesses still required |
| Exactly-once native 98 continuation and authentic story flags | Native 98 resumes once from the reported outcome (preview engine win only); the flags are written by the original script. Loss handling (whiteout script `05960008`, money halving) and party writeback are missing |
| All mandatory floors, actors, collision, natives, dialogue, media and interactions | Full source index now available; only floors 1/2 playable. Phenac City (floor 0x03, the only world-map destination Outskirt Stand unlocks) is viewable through the separate [developer floor loader](dev-floor-loader.md) only; that is not story progress |
| Source-legal party/action/capture/Shadow/storage/purification/loss branches | Some menu/stat/save foundations; original-compatible main-story behavior remains incomplete |
| Ending, full credits, clear-save and fresh-process Continue | Source guards identified; native sequence and full chronological acceptance missing |
| Native resource/lifecycle/playability through the entire story | Early-area/device subsets only; later content and full-run qualification missing |

All end-story gates remain open. Qualification of a helper, static index or one
winning transcript cannot establish completion. Each implementation increment
must preserve the complete objective and progress toward original execution.

The immediate critical path is source battle entry/state, legal complete turns
and genuine result/session commit, with independent original entry/turn/terminal
observations and source-owned RNG/events. Floor/callback/ending dependency
closure proceeds in parallel. Card writes occur only at original save
boundaries. Full-run acceptance uses a disposable private profile initialized
through real New Game; the production save remains protected.
