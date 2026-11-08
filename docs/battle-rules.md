# Isolated original battle rules

`battle_rules.h` exposes three bounded source-derived rules. They do not run a
battle, calculate AI scores, establish terminal state, award rewards, modify a
save, or authorize field-native `0x98` continuation. The preview and live story
result paths do not call these adapters.

## Source provenance

`tools/prepare_battle_rules.py` verifies Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55` against `source-lock.json` and
`config/battle-rules-source-files.json`. It verifies the complete normalized
UTF-8 source files before producing ignored `build/battle_rules_original.inc`.
Only private symbol names and opaque floor-pointer types change; arithmetic,
branches, comparisons and return values remain the extracted source bodies.
Vendor files remain untouched. No dependency was added.

| Original routine | Source | Original address |
| --- | --- | --- |
| `fightFloorSetFightResultId` | `src/game/fight_floor.c:1478` | `0x801F3984` |
| `fightFloorIsGcHeroWin` | `src/game/fight_floor.c:442` | `0x801F1DBC` |
| `fightTrainerAiGetValueAryMaxBanme` | `src/game/fight_range_80211A00.c:136` | `0x802397B8` |
| `fn_800E0C54` | `src/game/gs_math_random_exact_800E0C54.c:14` | `0x800E0C54` |

These source bodies and independently observed original PowerPC execution are
different evidence levels. The separate original-instruction corpus/checker
records its own executable pins, cases and limits. Neither qualifies complete
encounter entry, a turn, full AI, rewards, post-battle orchestration, or the main
story route.

`docs/battle-rules-original-vectors.json` records 482 admitted original-DOL
instruction executions matching the native adapter: 146 result cases (all 128
binary-flag combinations plus 18 nonbinary flag cases, mapped to the native
boolean admission) and 336 AI cases covering 14 arrays, three random-mode bytes
and eight seeds, including final seed and draw counts. The DOL SHA-1 is
`870e8b9693ca780782d80f22a6a4572d8ba9458f`. Replay with
`tools/qualification/check_battle_rules_oracle.py --library <native-library>`.
This corpus does not independently qualify the hero-side win predicate or its
unported side-resolution dependencies.

## Contracts

`c3_battle_rules_combine_result` accepts current/requested result codes `0..7`
and the original draw-enabled flag `0/1`. It returns both the resulting code and
the original setter's accepted byte. A success return from this C wrapper means
the rule executed; `accepted` may still be zero. Conversely, original current
code `1` accepts a different requested code while retaining `1`. Results must
not be interpreted as a generic zero/nonzero win flag.

The exact source handles equal-code early returns, current-code `1` precedence,
requests `0/1`, draw-disabled remapping and draw-enabled simultaneous outcomes.
For example, with draws enabled `2 + 3` and `3 + 2` become `7`, and `4 + 5` or
`5 + 4` become `6`. With draws disabled requested `7` maps to `3`, requested `6`
maps to `5`, and existing `3/5` retain precedence after requests `0/1` have been
handled. Consumers must preserve the order of original result requests.

`c3_battle_rules_hero_win` executes the original win predicate given an
**already resolved** `hero_is_host_side` flag. Only result `2` wins for a host
hero and only `3` for a guest hero. This adapter does not port
`fightFloorIsGcHeroHostSide`, discover the GC hero, enumerate trainers, resolve
`fightTargetGetPtr`, or validate the supplied side identity. The original
floor/trainer/target lookup remains an integration prerequisite. A scalar floor
handle `1` binds both result and predicate services; no host pointer is narrowed
to an original 32-bit address.

`c3_battle_rules_ai_argmax` accepts `0..16` signed 32-bit values. Count zero
permits a null array and returns `-1`; values strictly below the source's `-200`
sentinel also return `-1`. Values equal to `-200` participate in selection. A
unique maximum selects its index. For ties, every `use_random` byte except
exactly `1` selects the first tied index without drawing. Exactly `1` consumes
one HSD draw through extracted `fn_800E0C54`, then reduces its 16-bit return
modulo the number of ties. This is not `c3_battle_rng_bounded32`, which consumes
two draws. The caller supplies and exclusively owns the explicit `C3BattleRng`
even on paths that do not draw.

An atomic context guard rejects overlapping or reentrant adapter execution.
Arrays are copied before the source call. Invalid pointers/domain, output
overlap with the values or RNG, busy context, and required-draw overflow reject
without output or external RNG mutation. A local RNG snapshot stages source
draws; only successful execution commits seed and draw count. No-draw paths can
succeed with a draw counter already at `UINT64_MAX`. Caller-provided non-null
pointers must identify valid, suitably aligned native objects.

## Verification

The focused host suite executes 1,005,945 checks: all 128 result combinations,
all 16 result/side predicates, all 781 arrays of lengths zero through four over
signed extremes and sentinel boundaries with all 256 random-mode bytes,
five-through-sixteen-slot cases, unique/tied maxima, one-draw seed/count parity,
input preservation, canaries, invalid inputs/overlaps, overflow, independent
interleaved contexts and snapshot/replay. This is source-rule regression
evidence, independent of the separately produced original-instruction corpus.

Seven Python extraction tests verify unchanged extracted bodies, one-draw
wrapper selection, portable line endings, missing/truncated/modified source
rejection, manifest/lock rejection, malformed definitions and preservation of
an existing output on CLI admission failure.

Focused verification passed with strict GCC 13 warnings, AddressSanitizer and
UndefinedBehaviorSanitizer, GCC's static analyzer, and strict devkitARM ARMv6K
compilation. The root
integration owns complete host/source/native application gates. No physical
console gameplay claim follows from these isolated tests.

```sh
python3 tools/prepare_battle_rng.py
python3 tools/prepare_battle_rules.py
make build/test_battle_rules
./build/test_battle_rules
python3 -m unittest discover -s tests -p test_prepare_battle_rules.py -v
```
