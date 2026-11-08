# Source-backed trainer-entry record services

This extends the [cooperative trainer entry](battle-trainer-entry.md) rather
than starting another preview. It fills **17** previously caller-supplied data
and predicate services with endian-explicit reads and source-derived logic.
The existing native constructor binder supplies the other eight record services.
Neither binder registers the action with gameplay or authorizes a battle result.

## Implemented closure

`battle_entry_records.c` reads encounter/trainer/species/side/type/kind tables,
action-data records and the runtime trainer/Hero/FightPokemon backing already
owned by `C3BattleEntryNativeContext`. The new context retains source floor,
result, mode and side identities supplied by the session owner. Reads remain
live between yields; the action itself preserves its original entry-time locals.
There is no hidden diagnostic seed, fake sequence identity or saved-state edit.

The bound services are action-data buff; selected floor fields; encounter and
absolute side lookup; side validity/status; encounter trainer/device; trainer
row validity; preset mode; sequence selector; runtime trainer validity; Pokemon
eligibility; existing-fighter lookup; GC-Hero category; fighting-trainer count;
valid trainer lookup; and signed grid parameters.

Important source details retained:

- Encounter rows are 56 bytes, trainer rows 52, species rows 284, side rows 20,
  type rows 8 and kind rows 32. Reads decode BE16/BE32 explicitly.
- Encounter/trainer/species/side limits are counts. The pinned type/kind BIOS
  getters compare with `>`: their supplied limit is an **inclusive maximum**.
  Backing must include that entire final row. A count cannot be passed unchanged
  as a maximum, and no invented boundary row is appended by this adapter.
- Trainer sequence selection is `(u16)` of the BE32 at trainer row +0x10 (the
  low half, bytes +0x12..+0x13), as `fightTrainerCreateSequence` narrows
  selector 9; it is not the AI field. Real rows: trainer 1 = 1, trainer 10 = 38.
  Encounter devices are read as BE32 and narrowed at the original caller's u16
  boundary. Action selection reads its referenced data's buff, not action.buff.
- Pokemon validity checks species, source name presence, attestation and the
  original invalid flag. Active eligibility adds the special species/egg/HP
  exclusions. Flag comparisons remain `==1`, not generic nonzero tests.
- An existing fighter is recognized by its stable source-party identity, both
  source and copied Pokemon validity, and signed nonnegative entry ID. Two
  Pokemon of the same species are not interchangeable. An unknown nonzero
  reference is a native error rather than a source NULL.
- Trainer validity includes the original battle-result guard and Hero checks.
  Fighting counts inspect all six fighter slots for each of two side slots.
  Grid offsets retain the original signed-byte representation and low-byte yaw.

The admitted context contains at most two runtime trainers total, matching the
current native constructor context, and only absolute target selectors 4/5.
It is not a general target graph, a four-trainer battle session or a parser for
arbitrary imported original memory. Other in-range encounters need a separate
stable identity; they are not aliased to the selected encounter.

## Binding and ownership

Create the ordinary native context as documented for trainer entry. Supply
validated, immutable original table buffers and exact source limits. Set
`records.native = &native` and `native.owner = &records`. Then call both:

```c
c3_battle_entry_native_bind(&services);
c3_battle_entry_records_bind(&services);
```

All callbacks still receive `&native`. The actual owner of remaining operations
is available as `((C3BattleEntryRecords *)native.owner)->scene_user`. Tables and
record contexts must outlive the invocation. They may be updated between
serialized calls only according to source ownership; no concurrent mutation is
supported. The caller must supply real allocations for every declared extent.

The validator rejects duplicate handles, unknown side members, incomplete
rows, invalid limits and overlap with known mutable records/descriptors. Scalar
output storage must not overlap borrowed records, tables, descriptors or RNG.
Failure preserves outputs. The record services themselves never mutate runtime
Pokemon, RNG, saves, graphics, counters or queue state.

**Still required:** friendship filtering when the source flags request it;
real sequence/model/resource ownership; grid registration/update; visibility;
and cancellation cleanup. These are not filled with success-returning stubs.
An integration regression deliberately requests friendship handling, injects
its failure and requires ERROR plus exactly-once cleanup. Disabling the original
flags to make the action pass is not an acceptable integration.

The existing trainer-entry action is not transaction-wide rollback. Earlier
constructor effects may already exist when a later service fails. The owner
must stop and discard the partial battle session after safe resource cleanup.

## Source and validation

`tools/prepare_battle_entry_records_reference.py` hard-pins twelve upstream
source files and the existing Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`. It extracts original Pokemon, Hero,
FightPokemon and trainer predicates plus grid control for independent host
comparisons. Generated references are temporary and not committed.

Host-only adaptations rename symbols, remove local legacy extern declarations,
widen pointer transports, and reconcile the ignored context argument on the
no-argument battle-result getter. Tests provide independent BE field getters;
they do not call the production predicates to calculate expected answers.
The selected explicit-argument FightPokemon inline bodies avoid conflicting
old-style declarations in another translation unit.

The new C suite passes **1,093,008 checks**, covering all 16-bit species IDs,
HP values and signed entry IDs, byte flags, duplicate identity, source limits,
malformed bindings, immutable output guards and suspended two-side entry.
Constructor and resource services in the integrated case are explicit test
spies; this does **not** independently execute the real constructor chain or
load actual models. No original game assets or saves are fixtures.

Eight Python discovery/source-protection tests pass with GCC14.2 and Clang17.
ASan/UBSan with leak detection and focused GCC static analysis pass. Deliberate
mutants for exclusive type/kind limits, duplicate-by-species lookup and generic
nonzero egg flags fail the regressions.

```sh
python3 -m unittest discover -s tests -p 'test_battle_entry_records.py' -v
CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
python3 -m unittest discover -s tests -p 'test_battle_entry_records.py' -v
```

The prior trainer-entry eight cases and published continuation ten cases also
pass in the same focused host/sanitizer runs. Standard `make test` discovers
these tests, and the existing ARM wildcard includes the new runtime file.
No full current-project build or ARM link was performed in this sandbox:
validation used selected current files and materialized constructor interface
declarations on the older source snapshot. Full-checkout CI remains a separate
gate. No original-PowerPC full-entry, physical console, frame-rate or visual
acceptance is claimed.

## Next integration

The next missing owner closure is now explicit: source friendship semantics
and real sequence/model/grid services, followed by complete entry capture.
Compose the existing real constructors with these record services using an
isolated actual encounter context; retain source RNG and backing identity over
waits. Compare complete resulting records and streams with original execution.
Then connect active FightOutPokemon entry and authoritative turns/results.
Native98, story flags and saved-party writeback remain untouched here.
