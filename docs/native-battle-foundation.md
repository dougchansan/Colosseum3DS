# Headless battle foundation

This is the first implementation slice after PR5's encounter-10 request boundary. It does not implement a playable battle, opponent generation, damage, AI, rewards or a GSVM result. The field UI continues to report combat backend pending, and native 98 remains suspended.

## Isolated RNG

`battle_rng.c` maintains a per-battle 32-bit seed and a diagnostic draw counter. `prepare_battle_rng.py` verifies source hashes and extracts the pinned HSD draw body and GS bounded-random candidate body. The only lowering changes are fixed-width names, private symbol names and seed-context binding; arithmetic is retained.

The HSD draw advances the 32-bit LCG and returns its high 16 bits. The bounded helper consumes two draws, placing the first in the low half and the second in the high half before unsigned modulo. A generic one-draw remainder is not equivalent. Zero modulus, invalid output/context overlap and draw-counter overflow are rejected without advancing state. Snapshot/replay and interleaved battle contexts do not alter the existing global core RNG.

The bounded GS function remains marked CANDIDATE upstream. The separate [original-instruction qualification](rng-qualification.md) now compares both native RNG helpers against 72 executions of the admitted DOL instructions in Dolphin, including returned values, final seeds and draw ordering. This qualifies the recorded corpus, not every possible state or the initializer/AI/damage/presentation call graphs. Those streams still require their own audits before gameplay integration.

## Fixed-size action queue

`battle_action.c` retains the pinned queue's 32 slots with one reserved slot (31 usable), 48-byte copies, flow/display return-value handling, FIFO index updates and wrap semantics. Records use canonical big-endian bytes and explicit 32-bit references; they never contain a host pointer. Those references are adapter handles, so an original-memory oracle needs a documented reference relocation map.

The source's non-obvious behavior is intentional: a flow return whose low byte is not 1 drops the current queue head, even when the action just appended was a different slot. Display pops before invoking the display callback. Display-local changes are not written back or requeued. A dispatch budget bounds each display call; remaining entries stay available for a later call.

Callback bindings are explicit. A known callback reference of zero retains the source's legitimate NULL behavior; an unknown nonzero reference fails closed instead of becoming a successful no-op. No actual combat callback is registered by this foundation. Synthetic callbacks are test fixtures only.

## Validation and authority

The queue test compares the adapter against a separately compiled extraction of the pinned source control flow. RNG tests compare extracted context bodies with the original HSD implementation and bounded candidate using fixed seeds, rollover, draw ordering and interleaved streams. The queue checks establish parity with the reviewed source slice, not original queue execution. The RNG additionally has the bounded original-instruction corpus described above. Neither qualifies complete fight semantics. The queue passed 41,568 differential checks in strict and ASan/UBSan runs; RNG passed 304,569 checks in each run. Both compiled as strict ARM11 objects. The combined host target also passed all 19 C suites; its optional Python skips are covered separately where their required tools are present. Full application ARM and CI checks are recorded with the follow-up PR.

Original BattleMain and FIFO sources include medium-confidence or candidate functions and a substantial ABI/dependency closure. The current battle record pack supplies encounter/team constraints, not species/move/item/ability/effect/AI execution tables. Do not use the queue tests to certify those unported functions.

## Subsequent gates

1. Admit the original table profiles and qualify fixed-seed opponent Pokemon generation, personality retry draws, move PP and FightPokemon initialization against original execution. Preserve all current player-party bytes.
2. Port command selection and legal targets, speed/priority ordering, move execution/effects, status, AI and fainting/replacements. Reject unsupported imported-party behavior explicitly.
3. Compare fixed-seed turn traces including HP, PP, stages, RNG advancement, defeat, experience and rewards. Audit presentation waits for RNG-stream effects.
4. Only then expose validated result/writeback and resume native 98. The original GSVM continuation owns victory flags and later story progress.

The existing semantic-oracle pilot admits only three relocation-free integer leaves. It is not presently an initializer/action-call-graph oracle; extend and qualify it or use original-game instrumentation before claiming original-execution parity.

PR5's separate physical route and remaining hardware limits are recorded in [the physical evidence](evidence/native-story-hardware-2026-10-22/README.md).

The separate [physical shop Save/Continue evidence](evidence/shop-save-continue-hardware-2026-10-22/README.md) closes the earlier persistence gap: the fresh WES primary save and all decoded party/checkpoint/flag fields remained unchanged after a load-only fresh-process Continue. This does not supply a combat result or qualify battle integration.
