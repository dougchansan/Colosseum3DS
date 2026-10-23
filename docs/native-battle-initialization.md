# Encounter-10 party-parameter initialization

This document describes the bounded initializer milestone following PR6's
RNG/action queue. The current branch also integrates an experimental interactive
actor-motion preview; that later milestone does not expand initializer qualification.
`c3_battle_init_prepare` takes the pending encounter-10 request and produces
source-derived opponent **party parameters**, while retaining all six original
player records byte-for-byte. It does not produce complete PokemonBios capture
metadata or FightPokemon/FightOutPokemon objects. The field
preview now calls it on a private encounter request with an explicitly diagnostic
RNG/ID boundary. Native 98 stays pending and no story result/writeback API is added.

## Stacked ownership and later presentation integration

PR6 records original-instruction RNG helper qualification and physical shop
Save/fresh-process Continue acceptance. Preserve those independent results;
they do not qualify full opponent construction or combat. This branch consumes
the shared interfaces and retains the stacked dependency without rewriting the
base branch's evidence. The initializer tests below are historical milestone
results, not current-head full-checkout CI or hardware acceptance.

The current preview renders source-derived arena/trainer/Pokémon geometry and
selected authored motions, with typed events and a development HUD. Its field
handoff drains/releases the shared renderer, preserves the live story VM and
pending request, then restores field rendering without rerunning entry scripts.
Original effects, battle camera sequences, audio, original fonts/UI and actor
shadows remain separate presentation gates. See
[native preview and controls](native-battle-preview.md),
[private scene preparation](battle-assets.md),
[source resource manifest](battle-scene-manifest.json), and
[presentation coverage](battle-presentation-coverage.md).

## Exact starting boundary

The caller must supply the original opponent trainer ID and RNG state **after
opponent heroCreate and immediately before roster generation**. These are not
substituted with Wes's trainer ID or an arbitrary battle-entry seed. The current
caller-supplied diagnostic seed/ID make the initializer testable; integrating
trainer creation and the original shared RNG boundary remains required.

For each of the two roster entries, the adapter consumes pokemonCreate's two
initial personality draws, two IV draws, and ability draw even when the roster
subsequently overrides those values. It then applies the original IV/EV/ability
and friendship fields, and runs the source-derived sex/nature/non-shiny
personality rejection loop. The first personality draw is HIGH16, the second
LOW16. This order is explicit rather than depending on C operand evaluation.
The canonical DOL instructions at 0x80124168/170 and 0x8012444C/454 were inspected:
the first return is shifted left by 16 before the second draw. This inspection
is not an execution-based oracle for the entire initializer.

Final stats use the existing `c3_pokemon_recalculate` adapter, which compiles the
pinned `pokemonSetLevelBasisStatus` routine. Player HP, fainting, PP, statuses,
unknown fields, and slot order are not recalculated or normalized. The request
snapshot is preserved in the result. Ordinary new opponent HP starts full.

The admitted profile is non-Shadow, item-free, with four explicit move overrides
and zero PP Ups. A request needing learned-move fallback, PP-Up handling,
Shadow caches, or held-item initialization is rejected instead of silently
substituting values. These are initialization admission limits, not claims
that the original complete turn engine has been qualified for those moves or abilities.

All changes are staged in a private output and RNG copy. Invalid data, overlap,
draw-counter overflow (including during the second opponent), or personality
retry exhaustion leaves the caller's output and RNG unchanged. The native retry
limit is 65,536 attempts; the source itself has an unbounded rejection loop.
No success status here authorizes the waiting GSVM call to resume.

## Source authority

All original sources use Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

| Source | Role |
| --- | --- |
| `src/game/fight_trainer_range_801F7F80.c`, 0x801F9F78 | Roster override order and final personality/stat reset |
| `src/game/fight_trainer_db_range_801FBD10.c` | Signed roster fields and original offsets |
| `src/game/pokemon_range_8011F5FC.c`, 0x801240C4 / 0x80124410 | Constructor draw sequence and personality filters |
| `src/game/pokemon_range_exact_801248C4.c` | Ability fallback and original stat-reset semantics |
| `src/game/pokemon_range_8012795C.c` | Existing compiled original stat routine |

The test reference extractor pins the full personality source to Git blob
`eeec67fe8096f20bb0a725788377a0c84f10f93c`, with CRLF normalized to LF. It extracts
the original filter and sex helpers, changing only Rand32 to explicit observed
HIGH-first order. The test supplies species accessors and an independent LCG.
This verifies the filter's source parity, not the entire original constructor
call graph or original PowerPC execution. Full opponent generation, trainer
creation, and FightPokemon initialization still need that qualification.

## Private preparation

From a verified local disc extraction:

```sh
python3 tools/prepare_battle_init.py \
  --common-rel /private/extracted/files/common_rel.rel \
  --output /private/prepared/willie.c3i
```

Only the canonical 1,369,520-byte common REL with SHA-1
`e816761813900aa0fd4f87d012b3b2a19d2f003e` is admitted. The preparer checks section
layout, table counts, encounter/trainer mapping, and the terminated roster. Its
output and JSON manifest must be outside the source checkout and must not
already exist. Keep the pack private; it contains original game tables.

C3I1 is 768 bytes: 128-byte big-endian header plus 640-byte payload. Header
fields include source/common pins, payload SHA-1, and the SHA-1 of the exact
516-byte C3B1 encounter payload. This binding rejects mismatched encounter and
initialization packs. Checksums detect damage; they do not authenticate an
untrusted preparer's provenance.

The payload contains the species ratio/abilities, original-order base stats and
initial friendship, 25 nature-rate records, and 358 base-PP entries. No generated
Pokemon, RNG state, save, or original executable code is emitted.

## Tests

```sh
make battle-init-check
python3 -m unittest discover -s tests -p test_prepare_battle_init.py -v
# Optional private-data test; player records are synthetic preservation fixtures:
./build/test_battle_init /private/prepared/willie.c3i /private/prepared/willie.c3b
```

The normal `make test` dependency now runs the initializer suite. ARM compilation
picks up the new source via the existing source discovery. No new runtime or
Python dependency is required. The test-only extracted reference is not shipped
as a runtime callback.

The focused sandbox run passed 961,608 synthetic checks. Its canonical-data run
passed 964,771 checks, normally and under ASan/UBSan with leak detection disabled.
It covered fixed seeds, all natures, requested and fixed sex cases, retained
random IVs, overrides, packet corruption, profile mismatch, explicit unsupported
fields, transactional failure, and preservation of damaged/fainted player bytes.
The Python run passed eight tests and skipped the full-upstream-file extraction
test because this sandbox held selected materialized source slices, not a full
checkout. Required Makefile generation/CI checks the real pinned source.

The sandbox's isolated build used materialized baseline interfaces/stat code and
an OpenSSL SHA-1 bridge; these support files are not committed. It is focused
validation, not a claimed full repository build. The actual repository build
links the existing generated save codec. See PR CI for full-checkout validation.

For diagnostic roster-boundary seed 0x12345678 and supplied opponent trainer ID
0x11223344, the canonical-data run consumed 352 draws and ended at 0x2C53DED8.
Roster 238 used 25 personality attempts and roster 239 used 146. These are test
inputs/results, not a claimed RNG state observed in the physical game's battle.

## Remaining authoritative integration

The field preview now exercises command selection and computed turns against a
private initialized request, with controller-driven actor motions. This uses the
bounded preview coordinator, uniform legal opponent policy and diagnostic RNG;
it does not qualify original AI, complete-turn parity or the shared game RNG at
encounter entry. Rendering does not strengthen the initializer's original-binary
qualification.

Qualify original opponent hero/trainer creation and its RNG boundary, construct
full FightPokemon/FightOutPokemon records and legal active-slot mappings, then
compare complete turns and scheduling against original execution. Continue to
reject unsupported imported-party behavior. Complete original effect/camera/audio
presentation and validated party/experience/reward persistence remain gates.
Only a qualified backend may resume native98 and let the original story script
own progression, including victory flag0x03DE. This preview never writes that
flag or returns its outcome to the waiting native call.

Current platform observations and their limits are maintained in
[native-battle-preview.md](native-battle-preview.md) and the
[actor-motion evidence](evidence/battle-rendering-2026-10-22/README.md).
The corrected normal c9a43ddc13be executable passed the physical pending-encounter
route, computed preview WIN, field restoration/re-entry and clean exit. Live
body guards and GCI hashes verify preservation. This does not qualify original
AI/full turns, missing original presentation effects or result writeback.
