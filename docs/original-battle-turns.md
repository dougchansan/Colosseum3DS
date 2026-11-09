# Phase: original battle turns (plan, 2026-11-08)

The trainer entry now runs through original code and seeds the battle
([story status](story-completion-status.md)). Everything after it is still the
nonpersistent preview engine (`src/battle_preview.c`). This phase replaces
that engine with original turn code paths for the encounter-10 profile
(2v2 trainer battle, no items, no switching, no status at entry). No result
is accepted and nothing resumes native 98 until an increment explicitly
qualifies it.

## Original pipeline (pinned decomp, `vendor/colosseum/src/game/`)

The battle thread `fn_801EFA08` (`battle/battle_main.c:631`) runs each stage
as `fightActionCreateAndFlowFifo(kind, buff, table)`, already ported as the
resumable action frame. Each stage stops when the result global
(`fn_801EF634`) is set.

| Stage | Kind / function | Native status |
|---|---|---|
| Pre-load, pre-entry | `KaisiPreSubLoad` 0x8020C840, kind 2 `KaisiPre` 0x8020C15C | missing |
| Trainer entry | kind 3 `KaisiNyuujouTrainer` 0x8020D1FC | **ported, on hardware** |
| Send-out (active entry) | kind 4 `KaisiNyuujouPokemon` 0x8020CA98 → `fightOutPokemonCreate` 0x802068C8 | missing |
| Start-of-battle setup | kind 5 `KaisiPost` 0x8020BFA0 (+ kind 6 weather init, `fightSeqInit`) | missing |
| Command input | kind 7 → `fightTrainerSelectFightAction` 0x801F923C → AI `fn_802342CC` or GC menu | missing (AI argmax only: `battle_rules.c`) |
| Execution | kind 0xB 0x8020BBFC: sort `fightFloorSortFightOutPokemonPtrAry` 0x801F3B24 / nimbleness compare 0x801F3CE8, two passes, Focus Punch check `fn_80211A00` | missing |
| Move use | kind 0x13 `OutWaza` 0x8020B72C → `fn_802128D0`, target `fn_8022B2CC`, script interpreter `fn_80211B94` with opcode map `lbl_8027A00C` | missing (damage core `fn_80232110` ported) |
| Faint / win-loss | opcode 0x24 `fn_80222C44` → `fightFloorSetFightResultId` | result setter only |
| End of turn | kind 0xD → `fightSeqPost` → `fn_80213270` (residuals `fn_80230568`, `fn_802317E4`) | missing |
| Battle end | kinds 0xE–0x11 (`Syuuryou*`), party copyback `fightTrainerBackFightPokemonToTemotiPokemon` | missing |
| Story resume | native `fn_801CA5C4` → `fn_8020DAD0` returns the result to GSVM; `src/field_story.c` parks at `W_BATTLE` | waiting |

## Move-script engine (increment 1 scope, measured)

`tools/analyze_battle_scripts.py` reads the opcode table `lbl_8027A00C` from
the pinned decomp and derives each handler's script-pointer advances and
jump operands from its canonical (`*exact*`) source, following helper calls.
It then disassembles the scripts in the canonical `main.dol`, starting from
`lbl_80379BFF[effect]` for the given moves plus the faint sub-script
`lbl_80378798`. It writes nothing into the checkout.

For encounter 10's moves (Tackle 0, Growl 18, Tail Whip 19, Bite 31, Struggle
48, Reflect 65, Confusion 76, Return 121, Taunt 175, Helping Hand 176, Snatch
195, Secret Power 197 � move effect IDs) the static closure over every branch
is **46 opcodes in 18 scripts**, with no unresolved lengths and no run-time
script selection reached. The scripts follow the Gen-3 battle-script layout;
for example the ordinary hit script is the Surf/underwater guard, then attack
canceler, accuracy check, attack string, PP reduce, critical, damage, type and
damage adjustment, animation/HP/message steps, secondary effect, faint check
and move end. This makes each opcode's meaning cross-checkable, but the
original handler source stays the authority.

Design constraints found:

- Script operands embed absolute GameCube addresses of battle globals (for
  example the hit marker `0x8047B618` and damage multiplier `0x8047B63E`).
  The native interpreter needs an explicit, validated address map to native
  storage; unknown addresses must fail, never alias host memory.
  For encounter 10 the operands reference exactly 10 battle variables
  (`0x8038FF76`, `0x8038FFFC`, `0x80478D7B`, `0x80478D7D`, `0x80478D80`,
  `0x8047B600`, `0x8047B618`, `0x8047B627`, `0x8047B63C`, `0x8047B63E`) and
  2 read-only message tables used by opcode 0x13 (`0x80279D70`, `0x80279DC0`).
- The `WS_STATUS_GET*/SET*` opcodes (0xF9�0xFD) go through `statusGetStatus` /
  `statusSetStatus` with field IDs, so the FightOutPokemon/floor status model
  must cover the IDs these scripts use.
- Several decomp files keep stale duplicate bodies under `#if` guards; only the
  units selected by `configure.py` (the `*exact*` splits) are authoritative.


## Interpreter status

`src/battle_script.c` lowers the interpreter loop `fn_80211B94` over the
private C3W1 pack (`tools/prepare_battle_script.py`: script region, effect
table and the initialized-data windows from `main.dol`):

- Top-level move calls run the end-of-move sequence in source order: faint
  mark pass, the faint sub-script `lbl_80378798` inline with saved PC/state,
  four post-faint passes, then cleanup (attacker field 0xD9, move-cleanup
  pass, the original global clears and the three attacker/move status
  writes) and the exit flag masks. Nested calls stop on state 1 or 2 and skip
  the cleanup.
- Script variables resolve through three GameCube memory windows (`.sdata`
  `0x80478D78`, `.sbss` `0x8047B600`, `.data` `0x8038FF5A`) with the original
  big-endian layout; the interpreter's PC/state/action slots and every other
  address are refused.
- Waits (`fightMainWaitFrame`) suspend the interpreter; the caller resumes it.
- Lowered opcodes: goto (0x28), byte/halfword compares (0x29, 0x2A), set byte
  (0x2E), OR word (0x35), pause (0x39), skip (0x3A), return/ends (0x3C, 0x3D,
  0x3E, 0xF6, 0xF7) and the status get/set family (0xF8�0xFD). Every other
  opcode fails with its original handler name.

On the original scripts the ordinary-hit script runs its Surf guard and stops
at `WS_HITCHECK` (Tackle) or `WS_WAZAKOUKA_CHECK` (Surf), and Growl sets the
stat-change variable and reaches the shared stat-down script, each exactly
where the disassembly predicts (`tests/test_battle_script.c`). Battle-logic
opcodes, the FightOutPokemon status model behind `statusGet/SetStatus`, and
targets are the next lowering steps; nothing on the console calls the
interpreter yet.

## Battle records

- `src/battle_status.c` serves `pokemonGetStatus`/`pokemonSetStatus` for the
  FightPokemon/FightOutPokemon selectors 0xCB�0x123 from a table generated
  from the pinned accessors (`tools/prepare_battle_status_fields.py`): 65
  value fields and 11 address-of fields, with getter/setter agreement
  asserted. The move fields 0xDA�0xE4 and 0x123 are computed and still to be
  lowered. Records live in an arena addressed by synthetic guest addresses
  (`0x90000000`+), so stored record pointers and pointer arithmetic behave as
  on the GameCube, with every access bounds-checked.
- The same generator now covers `wazaGetStatus` (move data rows by ID and
  the FightWaza record at FightOutPokemon `+0x49C`; 41 of 52 statuses, the
  rest are AI/animation function pointers) and `fightFloorGetStatus` (floor
  data rows and the 0xA4E8-byte FightFloor record `lbl_8046DD90`: attacker
  `+0xA474`, defender `+0xA478`, active array `+0xA4C4`; 34 of 95 fields,
  the rest are computed from encounter tables). Where the original routes a
  selector's setter to a different field (`wazaSetStatus` case 10 calls the
  case-11 setter) the setter is not modelled. Hand-lowered computed
  selectors: used/original move (`0xDA`/`0xDB`, floor `0x37`/`0x38`) and their
  types (waza `0x33`/`0x34`). `c3_battle_target` resolves the fixed
  `fightTargetGetPtrAsNowFightType` kinds (attacker 0x11, defender 0x12, ...).
- Each trainer record holds two active FightOutPokemon (0x6E0 bytes) at
  `+0x1314` and one more of that size at `+0x20D4`.
- `src/battle_fight_out.c` lowers `fightOutPokemonCreate` (send-out
  construction): the trainer module's compiled original `fightOutPokemonInit`,
  the FightPokemon links, and types/ability from the species rows via
  `pokemonGetTokuseiDataId`. Over the real entry, Umbreon/Espeon/Zigzagoon get
  types 17/17, 14/14, 0/0 and abilities 28, 28, 53
  (`tests/test_battle_fight_out.c`).


- The trainer records live inside the FightFloor record: FightFloor `+0x14`
  holds two FightSide records (0x5230 bytes: side data ID `+0x00`, six
  condition slots `+0x04`, two trainers of 0x28E4 at `+0x64`, spikes flag
  `+0x522C`). `c3_battle_floor_init` lowers `fightFloorInit` (condition slots,
  `fightSideInitAry` with the trainer initializer, the thirteen target fields
  with their message-variable side effects, fields 0x50-0x5B, encounter ID,
  turn 1); message variables (`msgctrlSetValue`, 58 IDs) live in the state,
  with message strings kept as tagged references.
- `src/battle_state.c` builds the battle memory from a finished entry: the
  FightFloor arena initialized for the encounter, with each side's entry-built
  trainer placed in that side's first trainer slot, the battle result
  (`lbl_8047B5D8`) and the borrowed species/move/floor tables. The entry
  stores FightPokemon source links as native party identities; the state
  relocates each to that party slot in the trainer's embedded Hero (`+0x34 +
  slot*0x138`) and refuses anything else. Over the real entry every link lands
  on the party Pokemon the FightPokemon copied.
- Lowered validity checks: `pokemonCheckValid` (species row name, species
  count, attest bytes, bad-egg flag), `pokemonCheckFightOut` (also species
  412, egg flag, HP 0), and the FightPokemon/FightOutPokemon forms (battle
  result 1, source/copy links, entry ID, caught flag, FightOutPokemon field
  0x120). All four entry-built battlers are valid and in battle after
  send-out.

- Conditions: the DOL-table pack (now version 2) adds the condition table
  `lbl_8035BBA8` (86 rows) and its count. `c3_battle_condition_present`
  lowers the generic slot test `fn_8011B67C` (row kind/slot/selector through
  `statusGetStatus`, Pokemon/FightPokemon/FightOutPokemon and FightWaza) and
  `c3_battle_condition_check` the category dispatch `fn_802026E4`
  (Pokemon slots at PokemonBios `+0x64`/`+0xE8`, FightPokemon `+0x13C`,
  FightOutPokemon `+0x15C` x52). Real rows of every category are exercised on
  the entry-built records.

- Attack-canceller pieces (`src/battle_canceler.c`), by guarded lowering:
  every branch condition is evaluated as in the original, bodies taken by
  status-free battlers are lowered, and a held condition whose body is not
  lowered yet stops with a named reason. The status canceller `fn_8022F2F8`
  runs its 15-step machine (step counter `lbl_8047B644`, reset per move by
  `fn_802128D0`): step 0 clears conditions 0x15/0x28
  (`fightOutPokemonWriteJoutaiDataId`), then sleep, freeze, Truant, recharge,
  flinch, Disable, Taunt, Imprison, confusion, paralysis, attraction, Bide,
  thaw and the Shadow check. `fn_8022A6C8` is the Shadow Hyper Mode canceller
  and `fn_8022EDEC` the Shadow reverse check; both return 0 for non-Shadow
  battlers (`pokemonIsDarkPokemon`: dark ID set and DP >= 0). Encounter 10's
  fight kind 2 allows Hyper Mode (kind row +0x16 = 1); computed floor fields
  0x14/0x16/0x18/0x1A/0x31 come from the encounter, type and kind rows.

## Increments

1. **Execution and end of turn through original code.** A minimal
   FightFloor/FightOutPokemon state built by `fightOutPokemonCreate` from the
   entry FightPokemon. The original sort/nimbleness ordering, the two-pass
   execution loop, `OutWaza`/`fn_802128D0`/target resolution, the script
   interpreter with only the opcodes that encounter 10's moves reach
   (including `fn_80222C44`), and `fn_80213270` with residuals gated to the
   no-status profile. Kind 7 stays a native stand-in that only writes original
   kind-0x13 FightAction records (player menu choices; the AI side is a
   labelled stand-in). Data: action tables `lbl_80375D30`/`lbl_80375CA8`, the
   move-effect script index `lbl_80379BFF` and script bytecode (from the DOL),
   move rows, and type, stage, accuracy and critical-hit tables, packed
   privately like the entry pack.
2. **Original AI.** Replace the stand-in with `fn_802342CC` → `fn_8023A308`
   (usable moves, target pick, move scoring through the
   `fightTrainerAiWaza*` modules), ending in the ported argmax.
3. **Send-out and start-of-battle stages** (kinds 2, 4, 5, 6) instead of the
   stage's own introduction.
4. **Battle end and result transaction** (kinds 0xE–0x11, party copyback),
   then the exactly-once native 98 resume with the original return value.
   Only this increment may advance the story, and only from an authoritative
   battle.

## Acceptance for each increment

- Host tests over private original tables, including byte-level comparison of
  touched records where an original observation exists (the Dolphin oracle
  in [battle-entry-oracle.md](battle-entry-oracle.md)).
- A New 3DS run on the disposable profile with a host prediction over the
  same save that matches event for event, with the save unchanged until
  increment 4 defines a genuine save boundary.
- Native choices (seed, stand-ins, gated branches) stay labelled in traces and
  docs.
