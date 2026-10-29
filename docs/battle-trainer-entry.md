# Cooperative original trainer-entry action

This phase extends PR9 at `cc136b470cddcbd9cf9248e121effc753a8b23d8`, which
is based on the local agent's PR7 `7898cf137e2655c006c0d6d8782e6df8b28475c9`.
It lowers `fightActionFlowKaisiNyuujouTrainer` (`8020D1FC`) into an owned typed
frame and connects its constructor operations to the existing native adapters.
It does not change PR7's graphics, menus, save code, source pins or RNG APIs.

**This is not live battle-entry acceptance.** The new action is not registered
with native98. Real scene resource/sequence/grid ownership, remaining accessors,
active FightOutPokemon entry, original complete turns and validated results
still have to be connected. No extra playable area, story flag, party result,
reward, GCI write, new hardware success or full-story completion is implied.

## Retained source control

`C3BattleTrainerEntryFrame` owns the complete original `hero[0xB1C]` backing,
trainer-loop index, IDs, device, side, encounter, quotas, and sequence child.
The initial stack bytes are explicitly supplied; no uninitialized native C
storage is read. `heroBiosCopy` writes the original 0xB18 bytes, not the four
trailing backing bytes. The same backing survives subsequent trainer calls.

Start reads the original action/floor/side locals once. For each admitted
trainer it checks the source data ID/device, selects preset-copy versus source
hero creation, and then starts the existing resumable sequence loader. A yield
returns to the scene thread. Resume continues after that child; it does not
repeat trainer generation, RNG consumption, archive requests, or queue admission.

After sequence completion it preserves the original runtime-constructor,
Hero lookup, validity, and party-sort order. The two party scans remain distinct:

1. Eligible, unentered fighters up to the active quota and maximum.
2. Remaining valid, unentered party records up to the maximum.

Both scans retain the six-slot cap, skip checks, and entry-counter call location.
The conditional friendship filter reads floor selectors 0x27 and 0x2E at the
original point, with short-circuit low-byte comparisons. Those globals are not
silently frozen before a resource wait. Finally the action counts fighting
trainers, resolves valid trainer sequences, adds them to the source grid,
updates it and sets sequence visibility in source order.

The source layout has two trainer slots per side; floor counts beyond two are
rejected as native admission errors. Original source false/NULL/return values
remain distinct from unavailable services and native errors. A genuine NULL
sequence result is still passed to the runtime constructor as the source does.
No missing service is replaced with a successful production stub.

## Native record/constructor binding

`battle_trainer_entry_native.c` supplies eight service bindings using an explicit
`C3BattleEntryNativeContext`. It maps stable opaque references to caller-owned
runtime backing rather than truncating host pointers or storing PPC addresses.
Bindings require unique identities and disjoint known mutable spans; duplicate
request contexts, unknown handles, malformed extents and unsafe output aliases
fail before mutation. Parent Pokemon scratch can deliberately be shared across
sequential hero contexts, matching reused caller-owned stack backing.

The bindings invoke the existing, independently qualified APIs:

- `c3_battle_trainer_create`: original parent constructor and supplied shared
  RNG/data/attestation/scratch, leaving the frame's four suffix bytes untouched.
- `c3_battle_runtime_trainer_create`: original full 0x28E4 reset and Hero copy,
  using the actual sequence child result, including a legitimate zero.
- `c3_battle_party_sort`: stage the runtime Hero and first two FightPokemon
  records with the owner's independently initialized sort scratch, shared RNG
  and entry counter. Publish only after success, without touching the rest of
  the runtime object. Stable party-slot references do not change when their
  contents are reordered.
- `c3_battle_fight_pokemon_create`: resolve the real source/destination slots and
  pass the low16 entry identity with explicit portable signed conversion.

The source entry counter returns the signed old low16 value and then increments
its stored low16 value. Tests cover all 65,536 starting values. No seed or
constructor scratch is fabricated from a preview or expected output.

Additional bindings expose only the source trainer selectors 43/44/45/4C, Hero
party lookup, and exact preset Hero copy needed here. Unimplemented scene/data
services remain required. `c3_battle_entry_native_bind` does not fill them or
silently register the action with gameplay. Every remaining callback and the
sequence services receive the native context; scene callbacks can use its
`owner` pointer.

## Invocation, ownership and cancellation

Configure a zero-initialized frame with `c3_battle_trainer_entry_setup`. Its
start/resume/abort functions directly fit a `C3BattleRoutineBinding` used by
PR9's `C3BattleActionFrame`. The source action is enqueued once by the outer
frame; resource waits do not reinsert, remove or replay it. Tables, native
runtime storage, shared context and callback owners outlive the invocation.

An allocated sequence stays locally owned until runtime construction succeeds.
Failure before transfer retains it for one release at abort. Successfully
constructed trainer/sequence pairs are recorded and handed to `abort_owned`
once if the unfinished entry is cancelled or fails. That callback schedules
session cleanup; it does not claim that GPU resources can immediately be freed.
As with the existing sequence adapter, the scene owner must establish GPU/
resource safety before calling destruction services.

Earlier successful constructor operations may already have changed battle
scratch or RNG. The complete entry action is NOT advertised as transactional.
On a native error, preserve diagnostics, stop the entry, finish owner cleanup,
and discard the partial session. Never reset a queue during a callback or turn
cancellation into a battle result. A returned successful frame transfers its
resource ownership to the session; a later abort does not destroy it again.
Reentrant start/resume/abort and resetting an active or unclean failed frame
are rejected.

## Source reference and executed validation

The reference generator pins Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55` and normalized `fight_action.c`
SHA-256 `df209c49abae9f308eb3c5bec9fd53212064191e69f9e36fb2ff5e7eee22950f`.
It extracts the original control body without rewriting branches or loops.
Test-only changes remove 31 local obsolete/inconsistent extern declarations
(the harness provides typed declarations), widen scalar-to-pointer conversions
through uintptr_t, rename the entry symbol, and seed its otherwise uninitialized
stack backing from an independent fixture. The source hash and pin are checked
before output writes. Generated references stay in temporary/ignored storage.

Executed focused checks:

| Check | Result |
| --- | --- |
| Entry versus original control, waits, errors, cancellation and queue integration | 246,218 checks passed |
| Native handle/backing/constructor-glue contracts, all counter values | 196,706 checks passed |
| New Python source-protection and compiled cases | 8 passed, zero skips |
| Unchanged PR9 continuation discovery cases | 10 passed, zero skips |
| GCC14.2 / Clang17 strict new suite | Passed |
| AddressSanitizer and UndefinedBehaviorSanitizer with leak detection | Passed for new and unchanged PR9 focused suites |
| GCC static analyzer on the two new runtime units | Passed |
| Mutant that regenerates a hero during resume | Rejected by regression |
| Mutant that returns the counter as unsigned low16 | Rejected by regression |

The control oracle uses scripted domain/resource owners; the native glue test
uses constructor spies to check calls and publication, not replacement game
algorithms. Therefore these tests establish control and adapter contracts,
**not new original-PowerPC execution, the whole constructor composition on real
inputs, live sequence loading, ARM linking, console execution or visual parity**.
The locally fetched public headers and reused action/continuation source were
checked against current Git blob identities. The workspace is a selected current
closure on an older materialized snapshot, not a complete current Git checkout.

```sh
python3 -m unittest discover -s tests -p 'test_battle_trainer_entry.py' -v
python3 -m unittest discover -s tests -p 'test_battle_continuation.py' -v
CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
python3 -m unittest discover -s tests -p 'test_battle_trainer_entry.py' -v
```

Standard `make test` discovers the new Python cases, which inherit CFLAGS and
LDFLAGS. The existing ARM wildcard includes both new src files and links the
actual existing constructors, not the test spies. No build-system change or
runtime registration is hidden in this phase. A full current-checkout build,
normal/boot ARM build and current-head CI remain required after applying it.

## Next integration gate

Supply actual scene/sequence/grid ownership and source metadata/accessors for
this action, initialize its native record contexts from real entry inputs,
and compare a complete ordinary Willie entry with original observations,
including all runtime records, shared RNG and counters across loading waits.
Then connect active-fighter (FightOutPokemon) entry and its animation/input
continuations. Only original turn/result/post-processing and a validated
session commit may later resume native98 and advance the story. The old
nonpersistent preview remains isolated throughout that work.
