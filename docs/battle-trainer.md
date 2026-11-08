# Source parent trainer construction

`c3_battle_trainer_create` runs the pinned original parent at `801F9CBC`,
hero creation/initialization, party insertion/catch metadata, and trainer items
through byte-oriented services. It returns an original-format 0xB18-byte hero,
including all six 312-byte party slots. Trainer selector 2 is preserved as a raw
validation field (the actual Willie row contains81); it is not treated as party
size. It does not construct FightPokemon,
sort the party, resume native98, or write battle/story results.

## Inputs and transaction

Supply the original 52-byte trainer row and trainer ID/input device; the shared
RNG state immediately before the parent; the independently read four attest
bytes; bounded original GSchar messages; and a roster resolver including the
terminating species-zero row. A nonzero roster species uses the qualified
`C3BattlePokemonData` closure. Original item data is supplied as the original
itemGetStatus(NULL,id,1/2) projection and original item count; IDs undergo the
original indirection before these name/kind values are supplied.

Both hero and scratch are caller-provided backing images. The parent's original
local Pokemon buffer begins at entry SP minus 0x148 (post-prologue SP plus 8).
Supply all 312 initial bytes explicitly. The same scratch image carries across
roster constructors; success returns its final pre-catch contents including the
parent's roster-ID write. Catch metadata is applied to a full copy for party
insertion, leaving scratch's catch defaults unchanged. No unknown padding or
suffix is synthesized. Output/borrowed overlap is rejected. Failure, callback
failure, a busy context, unsupported metadata or RNG overflow leaves hero,
scratch and RNG unchanged. Callbacks must remain immutable and side-effect-free.

The player kind (trainer selector 4 equals 1) copies the independently supplied
saved hero, including unknown bytes; it consumes no RNG. A missing saved hero
matches the original no-op. Source zero trainer/selector 2 early returns likewise
succeed without changing bytes or RNG.

## Original source and ABI lowering

All source is Count-Sheep commit `51bfe4314a218dfaf9592fc80723c50498d0ee55`.
`config/battle-trainer-source-files.json` and hardcoded SHA-256 pins protect the
closure. `tools/prepare_battle_trainer.py` extracts parent validation and branch
logic, heroCreate/heroInit, heroCatchPokemon and first-invalid-slot insertion,
heroBiosCopy, item kind dispatch and original add/search/sort routines. Generated
source stays in ignored build/. The constructor itself is the shared
`battle_pokemon` source extraction, including its field-based pokemonInit.

The source pointer/scalar status ABI is carried by uintptr_t inside the host
boundary; original target pointers are never cast to host pointers. Item words
use four-byte structs so original pointer arithmetic and whole-record swaps
preserve BE bytes. Hero copy is lowered to memmove of the original 0xB18 bytes.
Original heroCreate explicitly draws first LOW16, second HIGH16, then the roster
uses its separately qualified personality sequence and the same RNG stream.

Hero status byte services follow hero_bios_exact_8012A450.c; Pokemon catch
services follow pokemon_data.c. GScharLenCpy writes eleven u16 units (22 bytes),
zero-padding its short strings; it does not clear larger name backing storage.
Attest is copied as version/generation/area/language-area, with no date added.
The parent sets floor0, current level, roster ball, and the generated hero's
OT sex/ID/name only during catch. It sets roster selectorC9 before catch,
tries up to eight items, then clears the eight badges.

## Boundaries and verification

Shadow caches and GSchar control-code strings are rejected. Item kinds outside
1..6 are rejected because the original helper leaves an output flag undefined
on its default path; no acceptance is claimed for that undefined context.
The wrapper admits at most seven roster calls: six occupied slots plus either
the terminating species-zero row or the original full-party rejection.
A seventh constructed Pokemon may consume RNG and update scratch before the
original full-party catch fails; all six accepted party records remain intact.

Host tests cover all six slots, independently calculated hero RNG ordering,
catch/OT metadata, scratch reuse, exact preservation of unknown bytes, item
counts/sorting, special trainer names, saved-player/no-op branches, reentrancy,
and transactional failures. The public shared Pokemon initializer has a complete
312-byte write-map/canary test and rejects reentrant calls. Extraction tests
cover closure membership, pointer/word lowering, CRLF normalization, source
tampering and unauthorized repinning. Strict host and ARM compile checks pass.
These synthetic checks are separate from original execution qualification.

## Original parent comparison

The [numeric original comparison](battle-trainer-original-check.json) records
two actual original-DOL parent calls from `8020D38C`, with verified return
`8020D390` and equal entry/return stack pointers. Independent inputs include all
initial hero bytes, the original stack-local scratch, save-attest bytes, saved
player hero and special-name selector. Canonical tables/messages come from the
pinned original DOL/common REL, not the observed output.

Both calls match every one of the 2,840 hero bytes and all 312 final scratch
bytes. Willie trainer 10 consumes 386 draws, with seed `743CCD37` becoming
`471708E9`. Player trainer 1 copies the saved hero and retains seed `8FF186AB`
with zero draws. These are independent call boundaries: the original executes
additional work between them, including party selection. The checker does not
require adjacent seeds or a particular branch order.

Run with private original inputs and the compiled native library:

```text
python3 tools/qualification/check_battle_trainer_oracle.py --private-root /private/GC6E01 --capture-dir /private/probe --library build/libbattle_trainer.so
```

The mixed original capture also contains runtime Pokemon constructor calls;
the checker retains their original manifest indices and verifies the individual
files against that manifest before selecting the parent calls. Earlier captures
without a final scratch observation remain explicitly unqualified for that
output. Raw original tables, messages, hero and scratch bytes remain private.
This comparison qualifies these two parent calls. It does not qualify Shadow
contexts, party sorting, sequence allocation, FightTrainer creation, complete
battle entry, native98 or story progression.
