# Original PowerPC RNG qualification

The existing native battle RNG matched **72 executions of original GC6E01 DOL
instructions in Dolphin's interpreter**. Eight fixed seeds exercise HSD
`next16`; those seeds crossed with eight nonzero moduli exercise the original
bounded helper. All returned values, final seeds and draw counts agree with the
native API. No public RNG or queue interface changed. This does not qualify
opponent initialization, battle ordering, AI, damage, presentation randomness,
or any result/writeback path.

The numeric observations are in [rng-original-vectors.json](rng-original-vectors.json).
The actual engine identities and archive hashes are in
[rng-engine-build.json](rng-engine-build.json). Original instructions, requests
containing those bytes, raw emulator results and executables remain private.

## Original identity and execution

The original `main.dol` has SHA-1
`870e8b9693ca780782d80f22a6a4572d8ba9458f` and SHA-256
`7e6c00a3bd632126d5466cbab856c63ac3c52a5da20b60dba7e756538152c9f4`.
The runner rejects a different DOL before extracting instructions.

| Function | Entry / bytes | Original function SHA-256 |
| --- | --- | --- |
| HSD next16 | `0x801ADCD8` / 52 | `05f415ce187e4092ca9202e96dfbbd7cea17b0de3b604cb80d44aa2d397729f1` |
| Bounded random | `0x800E0C04` / 80 | `e05f6da5b648b3aefe44cc04667324a3ce255a2eda8e4a279d4aaac4da3917b7` |

`prepare_rng_oracle.py` specializes the pinned GPL semantic sidecar without
editing the dependency. It retains Dolphin's `SingleStepInner()` instruction
executor and its GC BAT/MSR setup. It expands the installed-code sandbox to
1 MiB and restricts execution to the two exact function ranges above. The
bounded request installs the original contiguous DOL text between the functions
(839,944 bytes); intervening addresses are excluded by the PC guard. The installed
text must remain unchanged. No recompiled game function, arithmetic callback,
substitute remainder calculation, or custom PowerPC interpreter supplies an
original result.

The harness supplies `r1=0x81700000`, `r13=0x80480820` and return sentinel
`LR=0x817FF000`. It writes a big-endian pointer at the real SDA seed-pointer
address `0x80478C94`, pointing to the fixture seed at `0x81600000`. The modulus
is passed in `r3`. This is isolated fixture memory, not a full-game RNG state.
The original bounded helper executes both real `bl` calls into HSD and its
original inline `divwu`/multiply/subtract remainder sequence. Host UI stubs are
linked for Dolphin initialization; they do not implement RNG dependencies.

Every fixture's complete instruction-PC sequence is checked: 13 steps for
next16 and 46 for bounded random. Read-only instrumentation immediately before
HSD's original `blr` records its actual `r3`, seed memory and caller LR. Bounded
calls return to `0x800E0C20` and `0x800E0C28`, respectively. The seed pointer,
stack pointer, SDA register and saved `r30/r31` must also survive. A failed PC
guard, exception, alert, changed code, unexpected path or state mismatch aborts
qualification.

For example, seed 1 returns 41 from next16 and leaves seed 2,745,024. Bounded
random from seed 1 consumes two draws and leaves seed 3,357,800,067. Modulus
65,536 returns 41, confirming the first draw occupies the low half; modulus
`0xFFFFFFFF` returns 3,357,737,001. The vectors retain both actual draw records.

## Coverage and limits

Seeds are `0`, `1`, `0x80000000`, `0xFFFFFFFF`, `0x7FFFFFFF`, `0x12345678`,
`0xDEADBEEF` and `0xFFFF0000`. Moduli are `1`, `2`, `3`, `65535`, `65536`,
`0x7FFFFFFF`, `0x80000000` and `0xFFFFFFFF`. Zero modulus is tested only against
the native transactional rejection contract; the original undefined invocation
is deliberately absent.

The bounded source remains marked CANDIDATE upstream. These results newly
qualify this bounded native arithmetic slice against the admitted original
instructions for this reproducible corpus. They do not change upstream source
status or establish exhaustive equivalence for all states.

The available interpreter build is **locally modified**, not the old semantic
pilot's pristine attested environment. Actual component HEADs are ModernGekko
`884c20505d7179160f8bb01d9db0f723c53b09cb`, RecompCore
`13e492094902644b0d113c586300d358640f9e19` and DolRecomp
`fa0cf619e8d7eb8cba7eaf55267a12caaebb46aa`. The relevant local Interpreter.cpp
change adds zero-opcode recovery conditional on a static-recomp core; the RNG
functions contain no zero opcodes, the harness uses the interpreter, and all
original instruction paths were checked. Other local changes and the existing
compiled cache are disclosed and bound by source/diff/static-archive hashes.
This run does not claim a clean dependency rebuild or the earlier three-lane
native-original/candidate attestation.

The resulting interpreter sidecar SHA-256 is
`6ba19d23456c9ce215a6110b06a013ca907942469df9045ffd8eeaceab46fedc`.

## Reproduction

Use the admitted private DOL and the existing compatible x86 Linux ModernGekko
Ninja build with `core`, `uicommon` and their dependencies already built. The
build helper reads its existing compile/link settings, builds only this
separate sidecar, and records its actual inputs. It does not update dependencies
or silently claim their checkout is clean. In the recorded environment the build
directory was `/home/douglaswhittingham/colo-linux-test/repo/lib/ModernGekko/build`.

```sh
python3 tools/prepare_battle_rng.py
python3 tools/qualification/prepare_rng_oracle.py \
  --output private/rng-rerun/dolphin_rng_oracle.cpp
python3 tools/qualification/build_rng_oracle.py \
  --build /path/to/existing/ModernGekko/build \
  --source private/rng-rerun/dolphin_rng_oracle.cpp \
  --output private/rng-rerun/dolphin-rng-oracle
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude -Ibuild \
  tools/qualification/rng_native_vectors.c src/battle_rng.c \
  -o private/rng-rerun/rng-native-vectors
python3 tools/qualification/qualify_rng.py \
  --dol /private/disc/sys/main.dol \
  --oracle private/rng-rerun/dolphin-rng-oracle \
  --native private/rng-rerun/rng-native-vectors \
  --private-output private/rng-rerun/execution \
  --vectors private/rng-rerun/vectors.json
python3 -m unittest discover -s tests -p test_rng_original_vectors.py -v
```

The discovered Python suite executed six tests with zero skips on the recorded
Linux host. It compiles the current native API against freshly generated pinned
source and checks all 72 recorded observations, plus native zero-modulus
rejection. It also tests source-pin rejection, fixture identity/coverage,
unmapped DOL ranges, and rejection of mismatched result IDs, missing steps,
wrong instruction counts and incomplete draw traces. On a host without a C compiler, the native replay class
reports a setup skip and its two methods do not execute; the four
qualification-input tests still run. These public
regressions replay recorded original observations; rerunning Dolphin itself
requires the private DOL and external interpreter build.
