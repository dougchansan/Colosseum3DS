# Runtime trainer field construction

`c3_battle_runtime_trainer_create` extracts the original deterministic runtime
trainer constructor at801FA6D8. It resets a complete0x28E4-byte runtime object,
copies the caller's full0xB18 hero to offset4, and stores trainer ID, low-eight
controller ID, and the actual caller-supplied sequence identity. It does not
load or allocate that sequence, sort the party, create active fighters, resume
native98, or commit battle/story results.

`c3_battle_runtime_trainer_init` extracts the original801FA8CC array initializer
for one record. NULL inputs retain the original no-op semantics. Both APIs
stage the full backing image and preserve every unwritten byte; source/output
overlap is rejected as an explicit adapter restriction. A busy shared hero,
FightPokemon or Pokemon initializer aborts without changing either buffer.
The reusable `c3_battle_trainer_init_hero` uses the existing source heroInit
closure and rejects implicit NULL/global-hero access.

## Source authority and pointer ABI

Count-Sheep pin51bfe4314a218dfaf9592fc80723c50498d0ee55 is protected by the
source manifest and hardcoded SHA-256 file hashes in
`tools/prepare_battle_runtime_trainer.py`. Generated source stays in ignored
build/. The closure includes runtime create/init; FightOutPokemon init/array;
FightAction init; trainer enemy and out enemy arrays; FightWaza and FightItem
init; status-entry lookup/reset/initialization and table accessors; original
kaisuu min/max/Get; and the original empty fightFloorSetShadow function.

Byte-oriented status services follow the pinned original setter/getter bodies.
They expose host view pointers only for inline members. No nonzero self/member
pointer is stored by this initializer; all action/moto/current pointer fields
are reset to zero. The sequence identity is an opaque32 caller input, never a
narrowed host pointer. Zero is accepted only as an actual caller result, rather
than a substitute for an unimplemented sequence loader.

## Complete0x28E4 layout

| Offset | Member |
|---|---|
| 0 | u16 trainer ID; pad2..3 preserved |
| 4 | hero0xB18 |
| B1C | six FightPokemon0x154 |
| 1314 | two FightOutPokemon0x6E0 |
| 20D4 | one FightOutPokemon0x6E0 buffer |
| 27B4 | money multiplier1, flee byte0; pad27B6..7 preserved |
| 27B8 | koban u32=0 |
| 27BC | low8 controller; pad27BD..BF preserved |
| 27C0 | sequence identity u32 |
| 27C4 | action0x30 |
| 27F4 | twelve trainer enemy0x14 |

Nested status0x10 records preserve pad7,E,F. FightAction preserves pad2..3;
trainer enemy records preserve byte13; out enemy records preserve bytesA,B;
FightItem preserves its final four bytes. All nested Pokemon suffix/padding
uses the shared source initializer. The FightOutPokemon used-item field at
67C is deliberately preserved: original init does not write selectorFA.
No blanket zero fill replaces these original field writes.

## Proven pinned no-draw status dependency

FightWazaInit calls fn8011B2C0 for status3F because its original table row
selects waza status table2A. This is a real nonzero initialized field, not an
optional omitted branch. The pinned row resolves to category6, index0, type1,
selector2A and kaisuu selector0. The original kaisuu row0 contains min=max=-1.
The extracted kaisuuGetKaisuu returns the minimum before
its RNG call when the endpoints are equal, so status3F countdown becomesFF
and this pinned runtime closure consumes no RNG. Original table counts86/10
are derived from the complete pinned arrays and agree with original initialized
count globals. An unexpected RNG path marks failure and discards staging;
there is no fabricated successful RNG output for other table profiles.

## Remaining owner integration gate

Original fightTrainerCreateSequence801F8FD8 reads trainer selector9 and calls
801DE418, then hides the returned sequence through801DA4E8. This path waits
for actual resources, acquires an8C-byte sequence-pool entry, loads a model and
optional particle bank, links texture animation, registers the model animation
callback with sequence identity, sets transforms and shadow surfaces, and owns
release/reset on failure. Those resources, allocation and render callback
lifetimes remain an owner integration requirement. The pure field constructor
must receive the actual resulting identity; it does not claim that gate passed.

Original fightFloorSetShadow801F198C is independently a single return
instruction (blr), matching its empty source body. Its faithful extracted no-op
has no external side effects at this boundary.

## Verification and qualification limits

Host tests compare all10468 bytes to an independent original field-write map,
including the conditional status3F value, initial unknown bytes, every nested
record, identity/controller truncation, canaries, hero copying, no-ops, overlap,
and shared-initializer busy rollback. The public hero initializer receives its
own full-byte/canary check. Extraction tests cover table provenance and no-draw
arithmetic, complete closure, pointer-width lowering, CRLF normalization,
tampering and unauthorized repinning. Strict host and ARM compile checks and
ASan/UBSan pass. These checks establish the source boundary only. Original
runtime entry/return0x28E4 snapshots, independent r3..r7 and source hero inputs,
and unchanged original RNG are checked in the bounded comparison below.

## Original field-constructor comparison

The [numeric comparison](battle-runtime-trainer-original-check.json) records two
actual original calls at `801FA6D8` from `8020D3B0`, returning at `8020D3B4`.
The observer captures all 10,468 initial and final bytes using three fixed views
of 4,096, 4,096 and 2,276 bytes, retaining their entry-derived addresses. A
separate 2,840-byte Hero view and the original register arguments provide inputs;
sequence identity comes from entry `r7`, never from the expected object.

Both native calls match every runtime byte, preserve every source Hero byte,
and consume zero RNG draws. Trainer/controller pairs are 10/0 and 1/1. Their
actual nonzero sequence identities are retained in the numeric report. The
checker independently verifies the original call, manifest, complete view
coverage, return context and pinned status3F/kaisuu0 no-draw profile.

```text
python3 tools/qualification/check_battle_runtime_trainer_oracle.py --private-root /private/GC6E01 --capture-dir /private/probe --library build/libbattle_runtime_trainer.so
```

These observations qualify the two field-constructor calls. They do not
implement sequence/resource creation, active-fighter construction, complete
entry or source turn/result/story continuation. Raw object and source bytes
remain private.
