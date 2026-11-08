# Cooperative battle actions and trainer-resource waits

This continuation is based on PR7 source
`7898cf137e2655c006c0d6d8782e6df8b28475c9`. It preserves the newer inventory,
P*DA, effect, constructor and oracle work. The older PR8 Bag implementation is
not replayed over the newer inventory controller; its branch is left intact.
This is an additive continuation from the current upstream branch, not a
force-rewrite or merge of the stacked PRs. The decompilation pin remains
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

**No new map or completed story battle is claimed.** These modules provide the
resumption mechanism needed by authoritative entry actions. They are not yet
registered with live native98, and do not replace the nonpersistent preview.
Real resource/model/particle services, active-fighter construction, original
complete turns and validated result writeback remain integration requirements.

## Why the old callback interface is insufficient

The original display queue removes an action BEFORE invoking its callback.
That callback can yield through `_threadSwitch()` while its source stack stays
alive. Re-running the callback next frame would restart its effects; calling
queue display again would instead lose the popped action. A source return of
2 or 3 is not an asynchronous wait signal.

`battle_resume.c` adds an explicitly cooperative path alongside the unchanged
synchronous `battle_action.c`. `C3BattleActionFrame` owns the original 48-byte
local action for the entire invocation. The binding points to a caller-owned
**typed** routine frame, not a serialized pointer or arbitrary saved stack.

- FLOW enqueues once, remembers that exact slot, starts once, and later copies
  the completed action to the remembered slot only for low-byte return1.
  Other source returns drop the CURRENT head, preserving the unusual original
  behavior. Earlier entries are not silently substituted for the inserted one.
- DISPLAY pops once, before start, and retains its local record through waits.
  Display-local mutations remain private even if the old queue slot is reused.
- CREATE-FLOW retains the original sentinel lookup, padding/init behavior,
  return4 on no matching data, and caller-output publication at completion.
- RETURN, YIELD and ERROR are distinct. A YIELD includes resource, animation,
  input or child-wait classification. Start is never replayed on resume.
- A bounded drain returns immediately on YIELD. Zero budget performs no work.
  It cannot spin until a resource finishes or consume an entire animation in
  one scene tick. A paused scene simply does not step its invocation.

The data/kind/ref resolver requires exactly one supplied synchronous OR
resumable implementation for a nonzero callback reference. Unknown/duplicate
references fail before enqueue/pop. A known callback-zero record still has
its original successful null-callback meaning. Bindings are copied when an
invocation begins so a suspended frame cannot silently change routines.

The existing synchronous API and its callers are unchanged. New source C files
are found by the normal/boot ARM wildcard. Runtime registration is deliberately
absent until the corresponding owner services exist.

## Ownership, failure and nested actions

One scene-thread owner serializes all access. The queue and a FLOW caller's
output must outlive the invocation. Runtime lookup tables are borrowed during
admission; callback user state remains alive through return or abort. They
must not alias mutable frame/queue/action storage.

A caller may implement source-ordered nested work with a separate typed child
frame. It must suspend the parent, step the child, and resume the parent at
its actual continuation point. This API is not a complete nested scheduler;
independent producers may not drain or reset the same queue concurrently.

Errors are sticky and do not fabricate a source return. Beginning a callback
may already have caused external side effects; an ERROR is not a promise of
transactional rollback. The owning session must cancel every active owner,
wait for GPU/resource safety where required, then discard session state.

Abort calls the supplied routine cleanup at most once. It does not requeue a
popped action, erase the queue, return a victory, or commit a save. Abort of an
already-returned frame does not destroy a resource whose ownership has passed
to the enclosing session. A failed, started frame cannot be reset until its
abort protocol has run. Reentrant step/reset/abort calls are rejected.

## Source-derived trainer sequence loader control

`battle_sequence.c` lowers `sequenceWaitResource`, `fn_801DE418` and the hiding
part of `fightTrainerCreateSequence` to `C3BattleSequenceFrame`:

1. Validate the original resource index and table count. Invalid index0 or an
   out-of-range index returns original NULL without a resource request.
2. Use the original row1 fallback when either selected resource word is zero.
   A still-empty fallback returns NULL; it is not a fabricated loaded model.
3. Preserve the two initial state queries, conditional member lookup, and
   request-once behavior. Do not collapse the second query into a cached value.
4. Poll one original wait-loop iteration per step. Negative state produces the
   source diagnostic and yields, but does not issue another request. This
   source loop has no timeout; the enclosing application owns cancellation.
5. Once ready, resolve the resource, allocate the actual sequence, set index /
   narrowed group / flags metadata, then load. Failure releases its sequence
   exactly once. A trainer sequence is hidden before returning its handle.

`C3BattleSequenceServices` must be backed by real owner services at integration.
There is NO default allocator or successful model stub. Sequence-load failure
returns original NULL; missing services/native errors return ERROR separately.
The caller supplies the source selector9 result and validated resource table.
Native 32-bit handles are not narrowed host pointers or original PPC addresses.

The existing `sequenceLoad` closure still includes model/particle loading,
animation callback registration, transforms and release semantics. The new
loader control does not implement that closure. `load` is synchronous after
its resource wait, as in the source: a future asynchronous implementation needs
an additional typed continuation rather than returning success early.

Pending cancellation owns no sequence allocation because allocation/load run
after the resource wait. It deliberately does not cancel the shared archive
request, which may be needed by another actor. Successful sequence ownership
passes to the caller; resource lifetime must then follow the real scene owner.

## Source authority and reference tests

The generator hard-pins these normalized source files:

| File | SHA-256 |
| --- | --- |
| `src/game/sequence.c` | `5197f33a164dd2698f04c01272f0deaa7f02629f010856b535684277f5daa1dd` |
| `src/game/fight_trainer_range_801F7F80.c` | `29aaf8e5dbd3666f685043396def52d974285b2a2cd1388da7845b47ba9f5f2f` |

It extracts the three original control bodies into ignored build output.
Exactly two host-ABI changes are made: widen the mocked scalar accessor's
pointer conversion to `uintptr_t`, and reconcile the wrapper TU's u32 loader
prototype to its actual u16 definition when combined in one host TU. The call
already explicitly narrows to u16. Control flow, field writes, query order and
return values are not rewritten. CRLF normalization is accepted; arbitrary
source edits and unauthorized source-pin changes fail before output writes.

The tests compare service-call traces, yields, metadata, releases and results
against these extracted functions. Services are **scripted test doubles**, not
actual models, original binary execution or physical console evidence.

## Executed checks and reproduction

```sh
python3 -m unittest discover -s tests -p 'test_battle_continuation.py' -v
CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' \
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
python3 -m unittest discover -s tests -p 'test_battle_continuation.py' -v
```

Standard `make test` discovers these tests. Their C builds inherit the make
invocation's CFLAGS/LDFLAGS, so the existing sanitizer job exercises the new
modules as well. A host without a C compiler reports the three compile cases
skipped; the existing Linux make target already requires that compiler.

The focused sandbox runs passed:

- 116,808 queue/create-flow/resumption checks, compared with the unchanged
  current synchronous queue across all32 read indices, occupancies0..31 and
  representative full-width returns, plus wait/error/cancel cases.
- 8,259 sequence checks against extracted original source, covering row
  fallback, negative/busy polling, missing resource, allocation/load failure,
  metadata writes, cancellation and native error propagation.
- 3,256 integration checks carrying actions through resource and animation
  waits with start/load/hide/cleanup exactly once; services remain synthetic.
- The unchanged current queue's own compiled-source suite: 41,568 checks.
- All10 new Python discovery/source-protection cases, with zero skips in the
  equipped sandbox. GCC, Clang and ASan/UBSan validation are recorded separately
  in the PR; no full-project/ARM build is implied by focused host compilation.

## Next source integration step

Use these frames to lower `fightActionFlowKaisiNyuujouTrainer` at8020D1FC.
Its typed frame must preserve the trainer-loop index, full0xB1C hero scratch
(including original backing/padding), source IDs, device and pending sequence.
Resume AFTER sequence creation; never regenerate the hero or repeat RNG draws.
Reuse the already-qualified parent/runtime trainer, party-sort and FightPokemon
adapters, with their actual context and counters. Bind real sequence/model
ownership before admitting this action into the live session. Then lower active
fighter entry and animation/input waits using the same explicit continuation
contract, compare original complete entry observations, and connect legal turns
and source result handling. The existing story-completion checklist remains
open; no fake native98 continuation is enabled by this change.
