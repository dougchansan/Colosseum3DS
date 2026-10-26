# Deterministic FightPokemon construction prerequisite

`c3_battle_fight_pokemon_create` adapts original `fightPokemonCreate`
(0x80206AEC); `c3_battle_fight_pokemon_init` adapts `fightPokemonInit`
(0x802078F0). The extractor checks source commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55` and five LF-normalized source-file
hashes before emitting original code into ignored `build/`. It restores the
original GetStatus-return-to-Init argument and uses `uintptr_t` for host pointer
carriers. Generated original code is not published.

The caller supplies 340 initial big-endian backing bytes. The constructor
resets the embedded Pokemon through the existing source-derived initializer,
resets one status record and the four flags, stores the source reference,
copies all 312 borrowed source bytes, and stores the signed 16-bit entry ID.
Initialization performs the same reset without the final copy/reference/ID.
Both preserve every byte the source leaves unwritten and commit only after all
dependencies succeed. A busy or failing Pokemon initializer leaves output
unchanged. Calls use stack-local adapter state and consume no RNG.

Offsets below are hexadecimal:

| Offset | Size | Content |
|---|---:|---|
| 000 | 4 | Big-endian source reference |
| 004 | 138 | Complete Pokemon copy |
| 13C | 10 | One status record; preserve bytes +7 and +E..F |
| 14C | 2 | Entry ID, -1 after initialization |
| 14E–151 | 4 | CatchEntry, LevelUp, DarkOut, Hokaku flags |
| 152–153 | 2 | Preserved padding |

The source reference is an explicit nonzero opaque 32-bit handle. The caller
owns its identity and relationship to the borrowed Pokemon. It is never
dereferenced or computed by narrowing a host address. A future original-game
oracle must supply the independently observed original guest pointer. Native
callers supply their own stable source handles.

The original constructor does nothing when either destination or source is
NULL; the public adapter reports that no-op as success. With both present, a
zero handle or overlapping source/destination is rejected without mutation.
Overlap rejection is a native adapter restriction, not a claim that the
original constructor snapshots overlapping input. The borrowed source stays
immutable for the entire call. Adjacent, disjoint source/backing buffers are
accepted. Resetting a NULL destination is also the original successful no-op.

The eight status selectors CB..D2 are checked against pinned get/set routing
and FightPokemon accessors. `fn_8011B950` resets only the seven defined status
fields, preserving its three padding bytes. Pokemon copy is exactly 0x138
bytes. A zero species or otherwise invalid Pokemon is still copied: this
constructor does not perform Pokemon validity admission.

Direct original-DOL caller pins identify final action-loop calls at
0x8020D47C and 0x8020D570. Party selection also invokes this constructor
temporarily; caller address and surrounding context must distinguish them.
Willie's upstream sorting path consumes shared RNG and increments an entry
counter even when temporary FightPokemon snapshots are restored. This
deterministic slice does not qualify that path, sequence/trainer allocation,
full parent-to-final entry, battle behavior, native98 continuation, or hardware.
The original comparison below qualifies only its observed constructor calls.

Host verification covers all 340 bytes, full source immutability, nonzero
32-bit handle extremes, signed entry extremes, flags and padding, initialization
and idempotence, source no-ops, overlap in both directions, adjacent buffers,
canaries, and transactional rollback when a dependency mutates its staged
argument then fails. The failure variant compiles the same test with
`C3_TEST_FIGHT_POKEMON_INIT_FAILURE` and links without `battle_pokemon.c`;
production code has no test hook. Extractor tests check all five source pins,
CRLF normalization, lock tampering, explicit pointer lowering, and no RNG call.

## Original constructor comparison

The [numeric original comparison](battle-fight-pokemon-original-check.json)
records four original-DOL calls at `80206AEC`. Each native result matches all
340 destination bytes, preserves all 312 borrowed source bytes, and leaves the
independently observed RNG seed unchanged. Source handles come from each entry's
original guest source address, independently of the expected destination bytes.

Two observed callers are `80233F74`, inside temporary selection. Two are
`8020D47C`, inside final primary entry. Classification follows the verified direct
caller, not record order. These cases do not establish every caller, actor-role
mapping or context.

```text
python3 tools/qualification/check_battle_fight_pokemon_oracle.py --private-root /private/GC6E01 --capture-dir /private/probe --library build/libbattle_fight_pokemon.so
```

The checker cross-checks every original indexed record against the mixed capture
manifest, verifies original call instructions and entry/return context, and
checks the complete destination, immutable source and observed RNG. Original
backing bytes remain private. Party-selection logic and its inter-call RNG and
entry-counter changes remain unqualified, as do complete battle entry and turns.
