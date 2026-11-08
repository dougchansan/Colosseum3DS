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
