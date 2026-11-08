# Source party selection and sorting prerequisite

Three transactional boundaries adapt original party entry work:

| API | Original boundary | Observable changes |
|---|---|---|
| c3_battle_party_candidates | 80233DB0 | Candidate handles/count, entry counter; restores both FightPokemon records |
| c3_battle_party_select | 802331F4 | Selected handles, last candidates, shared RNG/counter; restores both records |
| c3_battle_party_sort | 801F99C8 | Selection plus original six-slot Hero party copy and scratch backing |

The extractor checks eleven source-file hashes at commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`. It retains the original random,
category quota, ordinary, unrestricted fill, roster ordering, category ordering
and type-score ordering branches. It also extracts validity, fight-out, Hero
validity and bounded type-score helpers. Raw table and target graph callbacks
supply source data; no callback chooses candidates, ranks Pokemon, or draws RNG.
Generated original code remains in ignored `build/`.

State carries the original 2840-byte Hero, two initial 340-byte FightPokemon
records, six initial 312-byte scratch records, selected/candidate identities,
the low16 entry counter and the shared RNG. All backing is big-endian bytes.
The wrapper's scratch must come from independent initial backing: original
entry SP minus `0x770`, size `0x750`, proved by the original `0x790` stack frame
and source/DOL argument projections. The native boundary does not fabricate
zero padding. Copying, initialization and temporary construction reuse the
existing source-derived Pokemon/FightPokemon APIs.

Request handles represent source object addresses or stable native identities.
In an original-game oracle the party slot handles are Hero address + `0x30`
+ slot * `0x138`. Native callers choose distinct nonzero identities for present
objects. Zero trainer, Hero or FightPokemon references represent source NULL
pointers. Party identities identify physical slots; copying reordered Pokemon
does not relabel their addresses. Internally pointer arrays use `uintptr_t`;
serialization maps each address explicitly back to its handle. Arbitrary stale
selected/candidate handles are preserved through no-op paths without being
treated as dereferenceable addresses.

Data consists of immutable raw rows and original counts: trainer 52 bytes,
AI 40, roster 80, species 284, type 44. Missing out-of-range rows have the
original null-getter projection. An unavailable in-range row is an error.
The global `battle_result` is original no-argument `fn_801EF634`'s BE16 value
at `0x8047B5D8`; it is not a property of the trainer pointer. `host_side` is
floor status14 from the mutable encounter row's trainer byte. The optional
target-side callback supplies only original `fightTargetGetPtr(2,...)` graph
projections, including zero results. It is required when the type-score branch
actually calls it; failure rolls back rather than guessing a side.

Every eligible, not-yet-selected Pokemon is temporarily constructed before the
enumerator applies its category filter. Rejected category candidates therefore
still advance the entry counter. The enumerator consumes no RNG. Each chosen
entry in the driver consumes one RNG16 value even for a singleton candidate
list. Counter increments wrap through original low16 storage; snapshot restore
does not undo the counter or RNG. The wrapper caps requested count at six;
direct driver/enumerator requests admit counts 0–6. Native admission also bounds
phase mode to 0–6, filters to 0–3, type rows to 18, and pointer scratch slots to
six. An unsafe source scratch index is an error, not a larger invented party.

Two decompiler storage artifacts are lowered explicitly: the five-word
candidate declaration becomes six pointer slots (the enumerator writes six),
and the second 85-word snapshot copied from word1 becomes an explicit 340-byte
buffer. Original linked calls and stack frames establish those extents.
Missing `pokemonCheckValid` and initializer pointer arguments are restored;
unwritten bytes stay intact. Source algorithms are retained rather than
replaced with an alternate shuffle or ranking implementation.

The adapter stages all state. Missing rows, callback failure, invalid handles,
overlap with metadata/borrowed rows, dependency failure, RNG overflow, or a
scratch bound violation leave the caller's state unchanged. Borrowed data must
remain immutable and callbacks must be serialized and side-effect-free.
`selected` and `candidates` expose internal scratch observations; candidate_count
records the last enumeration, preserving its prior value if none occurred. Driver
selection leaves Hero and wrapper scratch unchanged; enumeration also leaves
selected and RNG unchanged. Successful source early returns preserve all
source-backed state they do not write.

Host tests cover the ordinary two-Pokemon nine-increment/two-draw path, every
category filter, singleton RNG consumption, random/quota/fill branches, both
ordering paths, type-score data and side projections, full backing preservation,
initial scratch padding, stale handles, counter wrap, no-ops and rollback.
These synthetic tests are separate from the bounded original comparison below.

## Original wrapper comparison

The [numeric comparison](battle-party-sort-original-check.json) records two
original wrapper calls from the verified caller `8020D3EC`, returning at
`8020D3F0`. Independent entry inputs include the full 3,524-byte trainer head,
1,872-byte stack backing, 24 selected-pointer bytes, shared seed, counter,
result global and mutable encounter row. Canonical tables come from the pinned
original DOL/common REL.

Both native results match all 3,524 trainer-head bytes, all 1,872 scratch bytes
and all 24 selected-pointer bytes. Willie trainer 10 advances the counter from
0 to 9 and consumes two draws (`471708E9` to `8FF186AB`). Player trainer 1 takes
the source early return, preserving counter 11 and the seed with zero draws.
Both 340-byte temporary records restore exactly as part of the head comparison.

```text
python3 tools/qualification/check_battle_party_sort_oracle.py --private-root /private/GC6E01 --capture-dir /private/probe --library build/libbattle_party_sort.so
```

This qualifies these two wrapper calls. Alternate ordering/type-score branches,
target-side graph projections and unobserved candidate-array scratch still
need original witnesses. Driver/enumerator APIs have synthetic source tests,
not independent direct-call original qualification. Sequence allocation, active
fighters, complete entry, native98, turns/results and full story remain open.
