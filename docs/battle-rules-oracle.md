# Original-instruction battle rules qualification

The current native result combiner and AI argmax matched **482 executions of
original GC6E01 PowerPC instructions in Dolphin's interpreter**. The corpus
checks returned values, stored result, final seed and actual HSD draw count.
These isolated synthetic fixtures do not establish complete battle entry,
turns, trainer-side discovery, rewards, live result writeback or story progress.

Numeric observations, original function hashes and interpreter build identities
are in [battle-rules-original-vectors.json](battle-rules-original-vectors.json).
Original instructions, requests containing those instructions, raw instruction
traces and emulator executables remain outside Git. Native implementation and
source-extraction boundaries are described in [battle-rules.md](battle-rules.md).

| Primitive | Independent original coverage | Admission limit |
| --- | --- | --- |
| Result combination | All 128 combinations of current/requested 0..7 and draw byte 0/1; 18 representative cases using original draw bytes 2/127/255 | Synthetic floor/encounter/kind records; native accepts boolean draw flag only |
| AI argmax | 336 cases: 14 arrays, random flag 0/1/2, eight seeds | At most 16 signed i32 values; no complete AI command or move scoring claim |
| Hero win predicate | Source extraction and native source tests only | Caller supplies an already resolved hero-host-side boolean; original floor/trainer/target resolver graph remains unqualified |

The array corpus includes empty input, values below -200, exactly -200,
unique maxima, tied maxima, signed i32 endpoints, 16 distinct values, 16 tied
values, and 16 below-sentinel values. Seeds are 0, 1, 0x80000000, 0xFFFFFFFF,
0x7FFFFFFF, 0x12345678, 0xDEADBEEF and 0xFFFF0000. Random flag exactly 1 consumes
one actual HSD draw only when the original tie list has multiple entries;
flags 0 and 2 consume none. Entries exactly -200 participate in ties; an empty
list or a list entirely below -200 returns -1 without drawing. Observations
include final seeds and read-only instrumentation at the original HSD return.

## Original execution boundary

The runner admits `main.dol` SHA-1
`870e8b9693ca780782d80f22a6a4572d8ba9458f` and records its SHA-256. It installs
untouched original contiguous text independently from the entry address:

- Result combiner entry `0x801F3984`, installed span
  `0x801EF62C..0x8020E4B4`.
- AI entry `0x802397B8`, installed span `0x800E0C54..0x802398E4`.

The specialization increases the maximum code size to 2 MiB. Only the actual
installed span is protected/excluded from memory-difference reporting. The
strict PC guard admits the exact target/helper intervals declared in
`prepare_battle_rules_oracle.py`, including the real result getters/setters,
status getter, encounter/kind lookups, RNG wrapper and HSD next16. Gaps in the
installed text cannot execute. Dolphin's `SingleStepInner()` supplies every
instruction; no candidate C callback, instruction substitution or new PowerPC
executor supplies original results.

Result fixtures install the original 94-entry status jump table unchanged at
`0x803754AC`. The status 0x23 -> 0x1A -> 0xD path reads a synthetic floor's
encounter index, an original-ABI encounter table and a kind table with the
selected draw byte. Both SDA count globals contain pointers to big-endian
count words; the table globals contain pointers to their respective arrays.
These records model the dependencies reached by the admitted function, not
complete valid in-game floor objects.

Every fixture starts with `r1=0x81700000`, `r13=0x80480820` and
`LR=0x817FF000`. All callee-saved registers r14..r31 receive distinct sentinels
and must survive. PC and LR must return to the sentinel. Seed-pointer memory
at `0x80478C94` points to seed `0x81600000`. The AI array/floor fixture starts
at `0x81601000`. The one-draw wrapper returns from HSD to `0x800E0C64`.

Exceptions, alerts, out-of-range PCs, changed installed code, step limits,
truncated memory differences, changed callee-saved/stack/SDA registers,
unexpected RAM writes, incomplete draw traces and fixture identity mismatches
all reject qualification. Full RAM differences may contain only the stack
frame plus the result halfword or seed word as appropriate. Each result also
records an instruction-count and SHA-256 of its complete original PC sequence;
raw sequences remain private.

The existing interpreter libraries were built from a **locally modified**
ModernGekko/RecompCore tree. The public report retains component commits,
interpreter-source hashes, dependency diff hash, actual static archive hashes,
compiler identity, host-stub hash and engine binary/source hashes. This is an
existing-library sidecar build, not a clean dependency rebuild or the older
semantic pilot's pristine attestation. Host stubs initialize Dolphin and do not
supply battle arithmetic. The inherited local zero-opcode recovery only applies
to a static-recomp core; this harness uses the interpreter and original paths.

## Reproduction and regressions

Use an existing compatible Linux ModernGekko Ninja build and the admitted
private DOL. Build products and raw execution files must remain private:

```sh
python3 tools/qualification/prepare_battle_rules_oracle.py \
  --output /private/battle-rules/dolphin_battle_rules.cpp
python3 tools/qualification/build_rng_oracle.py \
  --build /path/to/existing/ModernGekko/build \
  --source /private/battle-rules/dolphin_battle_rules.cpp \
  --output /private/battle-rules/dolphin-battle-rules
python3 tools/qualification/run_battle_rules_oracle.py \
  --dol /private/disc/sys/main.dol \
  --oracle /private/battle-rules/dolphin-battle-rules \
  --private-output /private/battle-rules/execution \
  --vectors /private/battle-rules/vectors.json
python3 tools/prepare_battle_rules.py
python3 tools/prepare_battle_rng.py
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -shared -fPIC \
  -Iinclude -Icompat/include -Ivendor/colosseum/include -Ibuild \
  src/battle_rules.c src/battle_rng.c -o /private/battle-rules/native.so
python3 tools/qualification/check_battle_rules_oracle.py \
  --vectors /private/battle-rules/vectors.json \
  --library /private/battle-rules/native.so
python3 -m unittest discover -s tests -p test_battle_rules_oracle.py -v
```

The recorded WSL Ubuntu run executed all five regression methods with zero
skips. The native replay builds strict-warning C from freshly extracted pinned
source in a temporary directory and compares all 482 original observations.
Input tests reject source-pin changes, malformed corpus/identity/draw records,
wrong register/PC paths and unexpected writes. Hosts without a POSIX C compiler
skip the native replay method explicitly; four input tests still execute.
Replaying numeric observations needs no private DOL or emulator, while
regenerating original observations requires both.
