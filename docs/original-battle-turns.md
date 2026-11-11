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
195, Secret Power 197 — move effect IDs) the static closure over every branch
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
- The `WS_STATUS_GET*/SET*` opcodes (0xF9–0xFD) go through `statusGetStatus` /
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
  0x3E, 0xF6, 0xF7) and the status get/set family (0xF8–0xFD). Every other
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
  FightPokemon/FightOutPokemon selectors 0xCB–0x123 from a table generated
  from the pinned accessors (`tools/prepare_battle_status_fields.py`): 65
  value fields and 11 address-of fields, with getter/setter agreement
  asserted. The move fields 0xDA–0xE4 and 0x123 are computed and still to be
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

- The interpreter takes battle-logic opcodes from an external handler
  (`services.handler`), with public helpers for operand fetch, checked jumps,
  failure and the script variables. `src/battle_handlers.c` backs the
  interpreter services with the battle state (statusGet/SetStatus kinds
  5/6/7, fixed targets) and lowers `WS_HITCHECK` (0x00), the attack canceler:
  battle result, attacker in battle (else hit marker 0x80000 and
  `lbl_80375FBF`), the status canceller, Soundproof, PP, the Hyper Mode
  canceller, hit marker 0x2000000, Magic Coat, Snatch, Lightning Rod and
  Protect. On the original scripts, Zigzagoon's Tackle and Espeon's Confusion
  pass the attack canceler and stop at the accuracy check (`fn_802284B0`,
  0x80375F8F); a fainted attacker continues at `lbl_80375FBF` to move end.

- More hit-script opcodes: `0x01` accuracy check (`fn_802284B0` with the
  lock-on/never-miss checks `fn_80228DAC`, including their hit-marker
  clears, the move-block test `fn_80229934`, Protect, weather
  `fightFloorGetNowTenkouDataId`, held-item effects, the original
  accuracy-stage table (DOL pack v3: 33/100 ... 1/1 ... 3/1) and one draw
  from the shared random stream, which the battle state continues from the
  entry session), `0x02` attack string (`fn_802282D8`: presentation requests
  and, with no battler model, the nested pause sequence `lbl_80378964`, which
  the interpreter runs through a handler CALL) and `0x03` PP reduce
  (`fn_8022808C`, Pressure decided conservatively). Floor weather conditions
  read through the condition table (kind 7). On the original hit script
  Zigzagoon's Tackle hits on the entry stream's next draw, PP falls 35 to 34,
  and the script stops at the critical-hit check; the miss path stops at the
  type check it needs.

- `0x04` critical check and `0x05` damage calculation (`fn_80227C40`).
  - The critical check sums the original rate bonuses: Focus Energy
    (condition 0x0F), the high-crit move effects, Scope Lens, Lucky Punch on
    Chansey and Stick on Farfetch'd. It clamps the sum to the critical table
    (DOL pack v4: `lbl_80478D58`, 16/8/4/3/2). It draws only when the
    defender lacks Battle Armor and Shell Armor, and floor field 0x29 allows
    critical hits.
  - The damage step runs the pinned original arithmetic `fn_80232110` a
    second time (`build/battle_damage_state.inc`, prefix `c3s_src_`), with
    every engine service bound to the battle state
    (`src/battle_damage_state.c`).
  - Services not modelled yet reject the calculation by name: badge boosts,
    held items, Cloud Nine and Air Lock, solar weather flags, and ability
    and condition counts.
  - The step then applies the critical multiplier, FightWaza 0x2C, Charge
    and Helping Hand.

  The damage formula always calls
  `fightFloorGetValidFightOutPokemonCount(0, 1, defender, 0)`, so this
  increment also lowers:
  - `fightSideCreate`: the side data IDs from floor data field 3, set after
    `fightFloorInit`, as in `fightActionFlowKaijou`.
  - `fightSideCheckValid`, `heroCheckValid` and `fightTrainerCheckValid`.
    `heroCheckValid` (0x8012A130) was read from the DOL, because the decomp's
    `heroGetStatus` names the wrong callee: the name must not be empty and
    hero byte 0xA80 must not be 2.
  - `fightFloorLoopValidFightOutPokemon` with its count callback (modes 0
    and 1; mode 2 is refused).

  On the original data, Zigzagoon's Tackle deals 4 and Espeon's Confusion
  deals 37, with no critical hit on the next draw. Both equal the preview
  binding of the same arithmetic on the same records. The scripts stop at the
  type check (`0x06`, `fn_802274F0`).

- `0x06` type check (`WS_TYPE_CHECK` → `fn_802274F0(1, 1, 1, 0)`), and the
  accuracy miss path that calls it as `fn_802274F0(0, 0, 0, 1)`.
  - Same-type bonus (×1.5) and Levitate against Ground. The Ghost exception
    for an identified defender (condition 0x19) and Wonder Guard are lowered
    as well.
  - The type chart (`zokuseiGetWazaJoutai`: type row +8 + defender type × 2)
    feeds `fn_802279F8`, which applies ×0 / ×0.5 / ×2 and records the result
    as FightWaza conditions 0x41–0x43.
  - The FightWaza work copy is restored after the miss path's call; the
    floor/defender writes (floor 0x3B → attacker field 0xDE, defender 0xF3
    and 0xF4, attacker 0x108) follow the original.
  - Condition slot writes are now in the battle state: write
    (`fn_8011B2C0`), check write (`fn_8011B444`) and erase (`fn_8011B788`),
    with the condition-row and slot layouts read from the DOL accessors.
    Only kaisuu row 0 (min = max = −1, no draw) is modelled. The FightWaza
    rows 0x3F–0x45 all use it, and other rows are refused by name.
  - Miss: `fightWazaIsHit` fails, so the defender's hit move is cleared
    (0xF3 = 0, 0xF4 = 9) and the script takes the miss branch.

  On the original data:

  | Case | Damage | Conditions |
  |---|---|---|
  | Tackle (Normal Zigzagoon) | 4 → 6 (same type) | 0x3F (`fightWazaInit` default) |
  | Confusion (Psychic Espeon) | 37 → 55 (same type) | 0x3F |
  | Tackle retyped Fighting on Dark Umbreon | 8 (×2, no same-type bonus) | 0x41 |
  | Normal on Umbreon retyped Ghost | 0 | 0x43, attacker 0x108 set |

  Hits stop at `0x07` `WS_DAMAGE_LOSS` (`fn_802271E0`). A miss under
  evasion +6 continues on the miss branch to opcode `0x45` (`fn_80222370`).

- `0x07` `WS_DAMAGE_LOSS` and `0x08` `WS_DAMAGE_LOSS_ONLY`
  (`fn_802271E0(1|0, 1)`).
  - One draw from the shared stream scales nonzero damage to 85–100 %
    (`100 - draw % 16`, at least 1).
  - Unless the defender has condition 0x14: False Swipe (effect 0x65, 0x07
    only), Endure (condition 0x2C) or the Focus Band flag (0x11A) cap damage
    at the defender's HP − 1. Endure records FightWaza condition 0x46.
  - Focus Band itself needs a held item, which is refused, as is its
    survival message (floor 0x56).

  On the original data the next draw gives 85 %:

  | Case | Damage |
  |---|---|
  | Tackle | 6 → 5 |
  | Confusion | 55 → 46 |
  | Endure at 3 HP | capped at 2, condition 0x46 |

  After the set-byte step (`0x2E`), hits stop at `0x09` (`fn_80226FD4`).

- `0x09` move effect (`fn_80226FD4`).
  - **Hit** (`fightWazaIsHit`): the move's effect plays through the new
    `present_move` request: `fightWazaDoEffectFunc(move, attacker, target,
    count)`, whose WZX type and variation functions come from move-row
    fields 0x20/0x21. With hit-marker bit 0x80 (and not move 0x90 or 0xA4),
    the original runs the pause sequence `lbl_80378964` as a nested call
    instead. Either way it then increments the effect counters
    `0x8038FF5A` and `0x8038FFF9` (initially 0 in the DOL data window).
  - **Miss or no effect:** the status menus close (new request
    `C3_PRESENT_STATUS_MENUS_CLOSE`). The target's motion needs a battler
    model and is refused by name.
  - Without a `present_move` callback the step refuses rather than skipping
    the effect.

  On the original data, Tackle requests the effect of move 33 from
  Zigzagoon to Umbreon with count 0, and the counters become 1. The
  no-effect case closes the menus with no effect request, and bit 0x80
  gives a second 0x20-frame wait instead. Hits now stop at `0x0A`
  (`fn_80226F0C`).

- `0x0A` effect wait (`fn_80226F0C`, operand: signal byte).
  - The original polls `fn_801DA5C4(signal)` once per frame
    (`_threadSwitch`), scanning the running effect objects. Here that is
    the new `effect_signal` request: 1 reached, 0 not yet (the step waits
    one frame and asks again), −1 error. Without the callback the step
    refuses.
  - Signals 2 and 6 then close the status menus.
  - With hit-marker bit 0x80 (and not move 0x90 or 0xA4), the original
    skips the poll and tests `r29`, which still holds the interpreter's
    register. That path is refused by name.

  On the original hit script Tackle waits for signal 5 and the script goes
  on to `0x0E` (`fn_802266EC`). A signal arriving after two polls gives two
  one-frame waits.

- `0x0E` (`fn_802266EC`) reads the attacker's FightWaza (selector 0xD9),
  ignores the result and advances one byte. The 0x802266EC disassembly
  confirms there is no other call or store. Hits now stop at `0x5C`
  (`fn_8021E9F4`).

- `0x5C` (`fn_8021E9F4`, operand: target kind).
  - On a hit with hit-marker bit 0x80 and not 0x100, it calls
    `fightOutPokemonDamageEffect(target)`, then advances two bytes.
  - That function returns at once when the target has no battler model
    (selector 0xEE). This holds for the battle state, so the step is exact.
    With a model it blinks it eight times (3 + 2 frames each); that path is
    refused by name.

  After 0x5C and the skip opcode (`0x3A`), hits now stop at `0x0B`
  (`fn_80226914`).

- `0x0B` HP update (`fn_80226914`, operand: target kind).
  - **Hit, damage:** HP falls by the damage, to no less than 0. When
    hit-marker bit 0x20 is clear, the target also records:
    - the running damage total 0xF5
    - the source's relative target ID 0xF6 (the attacker for target 0x12,
      else the defender)
    - an addition to the attacker's AI damage memory for the target
  - **First hit:** the first damage of the move goes into 0x11B.
  - **Physical/special records** (type row +0 = 1 or 2): value and source in
    0x102/0x103 with 0x11C/0x11D (physical, not move 0xDC), or 0x104/0x105
    with 0x11E/0x11F (special). Hit-marker bit 0x100000 suppresses these
    and 0x11B.
  - **Healing** (negative damage) restores up to the maximum.
  - **After either:**
    - The step stores FightWaza 0x2E and the new HP.
    - If the target uses its Transform copy (0xD6 = 0xD7), it copies that HP
      to the original FightPokemon.
    - It asks the presenter whether the target's status menu is shown (new
      `C3_PRESENT_STATUS_MENU_SHOWN`); if not, it brings in the actor
      (`C3_PRESENT_ACTOR_BEGIN`). Then it starts the HP bar (new
      `C3_PRESENT_HP_BAR`).
  - **Miss:** 0x11B becomes 0xFFFF if it is unset.
  - **Refused by name:** Substitute (condition 0x14 without bit 0x100), whose
    message path is not lowered.
  - **New state pieces:**
    - `fightTargetGetPtr` (0x801F02AC) with its side lookup and the
      relative-target search over the target-kind table. That table is in DOL
      pack v5 (`TGC`/`TGT`, `lbl_80375AC8`, 30 rows).
    - `fightFloorCreateFightPokemonEnemyAryEnemySideAll`, the battle-start
      step that creates the AI damage memory. The start-of-battle stages
      (increment 3) are not lowered yet, so tests run that one step at setup.
      The HP update refuses when the attacker's memory was never created.

  On the original data:
  - Tackle takes Umbreon from 86 to 81 HP. Umbreon records 5 damage from
    relative target 15 (Zigzagoon A) as physical.
  - Zigzagoon A's memory lists Umbreon (initial HP 86, damage 5), then Espeon.
  - Espeon's Confusion records special damage from relative target 13.

  Hits now stop at `0x0D` (`fn_80226730`).

- `0x0D` critical-hit message (`fn_80226730`).
  - Only when FightWaza 0x2B is 2 (a critical hit) and the move hit.
  - Stores message 0x7631 in floor field 0x52 (critical message ID) and
    opens it through the new `C3_PRESENT_MESSAGE_OPEN` request.
  - When a window opened, sets work byte 7 (`0x80478D7F`), the same
    open-window flag the attack string uses.

  Without a critical hit the step only advances; Tackle on the original
  data goes on to `0x12` (`fn_8022622C`). With 0x2B forced to 2 before the
  step, floor 0x52 holds 0x7631, the message is requested and work byte 7
  is 1.

- `0x12` timed message close (`fn_8022622C`, operand: frames).
  - Only when work byte 7 says a message window is open:
    `fightMainWaitFrame(operand)` (a script wait, like the pause opcode
    0x39), then `fightMenuCloseMsg` (`C3_PRESENT_MESSAGE_CLOSE`), then the
    flag is cleared.
  - Advances three bytes either way. A zero-frame operand does not suspend
    the script, matching the original's immediate return.

  On the hit script the operand is 0x40. Tackle without a critical hit
  passes straight on to `0x0F` (`fn_8022631C`). With the forced critical
  hit, the message waits 0x40 frames, closes, and work byte 7 returns to 0.

- `0x0F` effectiveness message (`fn_8022631C`).
  - Clears work byte 7, then picks a message from the attacker's FightWaza
    conditions:
    - **Missed** (0x40, unless no effect 0x43 with work byte 6 below 3): the
      miss message for work byte 6 (`lbl_80279D18`: 0x753E 0x753F 0x7542
      0x7540 0x7541 0x7645 0x7645 0x7549).
    - **One result only** (fewer than two of 0x41..0x47, `fightWazaIsMix`):
      super effective 0x7637, not very effective 0x7636, no effect 0x7543,
      0x44 → 0x7632, 0x45 → 0x7647, Endure 0x75C9.
    - **Combined results:** no effect 0x7543 or 0x45 → 0x7647. Otherwise
      0x44 or Endure clear their conditions and run a nested sequence
      (`0x803791C7`, `0x803791BE`). The original then returns without
      advancing, so the step runs again from the start; here each resumed
      phase is that fresh run.
  - A chosen message goes into floor field 0x53 and opens through the new
    `C3_PRESENT_EFFECT_MESSAGE` request (`fightMenuWazaKoukaMsg(message,
    target, item)`). An opened window sets work byte 7.
  - **Refused by name:**
    - Focus Band (0x47), whose sequence needs its item.
    - Any message that would carry a held item.
  - The unused `fightOutPokemonGetNicknamePtr` call has no effect.

  On the original data:

  | Case | Result |
  |---|---|
  | Neutral Tackle | no message |
  | Fighting-typed Tackle | "super effective" (0x7637) on Umbreon |
  | Normal on Ghost-typed Umbreon | 0x7543 |
  | Endure | 0x75C9 |
  | Super effective into Endure | Endure sequence called at `0x803791BE` (stops at its unlowered `0x10`) |

  Each message closes through the next `0x12` after 0x40 frames. Hits now
  stop at `0x0C` (`fn_802267E8`).

- `0x0C` HP bar wait (`fn_802267E8`, operand: target kind).
  - When the target's status menu is shown, the original loops one frame at
    a time (`menuFightStatusWaitAnimeHP`) until the HP bar stops. Here that
    is the new `C3_PRESENT_HP_BAR_BUSY` poll; each resumed phase repeats the
    step from the start.
  - `fightOutPokemonSetWazaEffectDownFlag` after `fightOutPokemonIsHinsi`
    only reaches a battler model (0xEE), so without one it has no effect. A
    target with a model is refused by name.
  - The Substitute-break sequence (condition 0x14, `lbl_80379A22`) is
    refused by name, as in the HP update.

  On the original hit script the status menu is not shown, so there is no
  wait. With the menu shown and the bar busy for two polls, the step waits
  two frames. The effect wait for signal 6 follows (closing the status
  menus), and hits now stop at `0x15` (`fn_80224820`).

- `0x15` secondary-effect chance (`fn_80224820`).
  - Closes the trainer and battler status menus (new
    `C3_PRESENT_TRAINER_MENUS_CLOSE`, then `C3_PRESENT_STATUS_MENUS_CLOSE`)
    and the message.
  - The chance is move row 0xC, doubled by Serene Grace (ability 0x20). The
    original also forces 99 when `fn_8000817C` is set: that reads the debug
    menu's always-apply toggle (`lbl_8047A271`, `.sbss`, written only by the
    debug settings menu `fn_80007B30`), which is 0 in play.
  - With work byte 3 bit 0x80 on a hit, the original applies the effect
    unconditionally. Otherwise it draws once, even when no effect is pending,
    and a pending effect (work byte 3) applies when `draw % 100 <= chance` on
    a hit. Applying an effect is `fn_802249B8`, which is not lowered yet:
    both paths are refused by name.
  - Clears work byte 3 and `0x8047B625`.

  Tackle has no pending effect: one draw, then the script goes on to `0x19`
  (`fn_80224158`). A forced guaranteed effect is refused without drawing.

- `0x19` faint check (`fn_80224158`, operands: target kind, mode, jump
  address).
  - Closes the status menus and the message, as 0x15 does.
  - **Mode 0** checks the attacker (0x11, sequence `lbl_803786F4`), the
    defender (0x12, `lbl_80378703`) or kind 0x15 (`lbl_80378712`).
  - `fightOutPokemonIsHinsi` (0x802038A4, read from the DOL) is now a state
    query: fainted when the battler is not valid or its Pokemon has 0 HP.
  - **A fainted battler:**
    - gets 0x120 = 1;
    - if it is the player's (`fightOutPokemonIsGcHeroFightOutPokemon`:
      trainer data field 4 == 1, from the entry pack's trainer rows), sets
      hit marker 0x400000;
    - is appointed (floor 0x4B: message variables 0x1E and 0x1C, then
      FightFloor `+0xA494`) while its faint sequence runs as a nested call;
    - afterwards 0x4B is restored and the script advances seven bytes.
  - **Refused by name:**
    - the player's friendship loss when floor 0x27 is 1
      (`AdjustFriendshipOnBattleFaint`)
    - Grudge (condition 0x28)
    - Destiny Bond (hit marker 0x40)
    - mode 1's fainted-side branch

  On the original data:
  - Tackle leaves Umbreon standing and the hit script runs to its end at
    move end (`0x49` `fn_8021FAD4`, `0x80375FC5`), the same point a fainted
    attacker reaches.
  - Umbreon's Tackle on Zigzagoon A at 1 HP marks it fainted, appoints it
    and enters the defender faint sequence, which stops at its first opcode
    `0x1A` (`fn_80224060`).
  - Umbreon itself fainting stops at the friendship loss (floor 0x27 is 1
    in this encounter).

- `0x49` move end (`fn_8021FAD4`, operands: mode, stop stage).
  - The stage counter is `lbl_8047B627`, which the script's preceding
    set-byte step zeroes. One call runs stages 0..16 in a do-while that
    leaves as soon as the stage reaches 0x11. Stage 17 (mode 0: close the
    menus and the message) runs only when the command is entered at 17,
    after a reaction at stage 16. Mode 1 runs a single stage; mode 2 stops
    after the given stage.
  - **Kept as the original does:**
    - stage 11: the attacker in battle gets 0x115 = 1. Stage 10 has
      already refused a semi-invulnerable attacker, so no condition is left
      for `fightOutPokemonInitJoutaiKie`.
    - stage 14: last-move records. The attacker gets 0xEF, 0xF0 and 0xF1
      (move IDs, or 0xFFFF without hit marker 0x2000000). The defender gets
      0xF2 (the attacker's relative target ID), 0xF3 (the move) and 0xF4
      (FightWaza field 0x30).
    - stage 15: Mirror Move. The defender gets 0xF7, plus the mimic move in
      its enemy entry for the attacker.
    - stage 16: in a double battle, a move with target type 4 (both foes)
      sets hit marker 0x200 when no partner is left in battle.
    - stage 8 appoints each battler's held item (floor 0x56: message
      variable 0x29, then FightFloor `+0xA4BC`).
    - `fightFloorLoopValidFightOutPokemon` (loop flag 0) is a new state
      query that lists the battlers in the original's order.
  - **Refused by name, before anything changes or a number is drawn:**
    - each reaction that would start a sequence: Rage; thaw by a Fire
      move; Synchronize; the after-hit abilities of `fn_8022D6BC`; the
      status-curing abilities; Choice lock; the pending item (0xFB);
      Substitute ending; the spread move's second target
    - semi-invulnerable visibility, and visibility with a battler model
    - the attacker/defender swap (hit marker 0x1000)
    - held items wherever a stage inspects one (the Choice Band check,
      `fn_8022B5C8`, `fn_8022BB84`). Their effect IDs come from the item
      table, which the state does not hold yet.

  On the original data:
  - Zigzagoon's Tackle on Umbreon runs move end with no reaction applying
    and writes the records: 0xEF, 0xF0 and 0xF1 are 0x21 (Tackle); Umbreon's
    0xF3 and 0xF7 are 0x21 and its 0xF2 is the relative ID 0xF.
  - The script then reaches `0x3D` WAZAEND (`0x80375FC8`). Its first
    end-of-move pass (the faint mark, `0x8023011C`) is not lowered yet, so
    every hit now stops there.
  - A held item on Espeon, the partner outside the exchange, stops at stage
    8 after appointing the item.

- End-of-move passes (`fn_80211B94`) and the faint script (`lbl_80378798`).
  - The `end_of_move` service now receives the script, so a refused pass
    fails it with a named reason.
  - Each pass runs `fightFloorLoopValidFightOutPokemon(0, callback, &arg,
    0)` over the battlers.
  - **Faint mark** (`fn_8023011C`): a battler with 0x120 == 1 runs
    `lbl_80378721` inside the loop. Refused by name.
  - **Faint script:** opcode `0x24` (`fn_80222C44`, battle end check), then
    `WS_SEQEND`.
    - It sums each side's remaining HP (`fightSideGetFightPokemonNokoriHp`,
      a new state query over target kinds 5 and 4). A side at 0 would set
      the fight result (`fightFloorSetFightResultId`), and an already
      decided battle (`lbl_8047B5D8`, the state's `result`) takes the result
      branch. Both are refused by name.
    - With operand low byte 1, a battler out of battle would need the
      replacement check (`fightFloorIsIrekaeFightOutPokemon`) or
      `fn_80222EF0`'s cleanup. Refused by name.
    - Floor field 0x17 (Pokemon per trainer, fight-type row byte 1) is now
      modelled.
  - **Post-faint passes:**
    - A, `fn_8022E34C`: Intimidate (ability 0x16 with condition 0x3B).
      Refused when it applies.
    - B, `fn_8022E1F8`: Trace (0x24 with 0x3C). Refused when it applies.
    - C, `fn_80230088`: each battler in battle becomes the floor attacker
      (0x36: message variables 0x0F, 0x1A, 0x1F, 0x21, 0x20, then
      `+0xA474`). Its held item is appointed (`fn_8022BE2C`, floor 0x56), and
      then the saved attacker is restored. A held item's effect is refused.
    - D, `fn_8022EB9C`: Forecast. Refused on ability 0x3B.
  - **Cleanup:** `fightOutPokemonInitOneSelfTurn` clears 0x113..0x11F on
    every battler, then the interpreter's own end-of-call writes.

  On the original data:
  - Zigzagoon's Tackle on Umbreon completes the whole call (`C3_SCRIPT_DONE`,
    depth 0). Umbreon keeps the HP loss (86 -> 81) and move end's records.
    The turn's records are reset: 0x113..0x11F on all four battlers, the
    attacker's 0xF3 / 0xF4 (0 / 9), the damage, the hit marker and the
    effect counters.
  - Nothing is drawn or presented after the move script.
  - A Forecast battler in battle is refused at pass D.
  - The handler tests now stop by default where the move script ends, and
    check the turn's intermediate records there.

- Faint sequence (`lbl_803786F4` attacker, `lbl_80378703` defender,
  `lbl_80378712` kind 0x15): `0x1A`, `0x56`, `0x10` (the fainted message:
  0x7544 / 0x7545 / 0x7701), `0x12`, `0x1B`, then the return.
  - `0x1A` (`fn_80224060`) starts the faint and opens the battler's status
    panel (`fn_80265598`, the same request as `C3_PRESENT_ACTOR_BEGIN`).
    Refused: a Substitute (condition 0x14), which the original breaks
    first; and the free/faint effects, which only reach a battler model.
  - `0x56` (`fn_8021EE48`) plays the cry: new `C3_PRESENT_CRY` with the
    battler and its species. The original plays the species' voice (species
    data 0x61); the state holds no species table, so the presenter looks it
    up.
  - `0x10` (`fn_802262D0`) is `fightMenuOpenMsg(message)`. Work byte 7
    records an opened window.
  - `0x1B` (`fn_80223F1C`):
    - closes the status panel (`fn_8026532C`, as `C3_PRESENT_ACTOR_END`);
    - for every other battler in battle (`fn_80232FE4`), a condition aimed
      at the fainted one (0x16, 0x0A, 0x0E) is refused. Each opponent
      forgets it as a Mimic source (`fightOutPokemonSetOumuWazaDataId(..,
      0)`: 0xF7 and the entry's move). `fn_80203198` then finds the entry's
      move at 0 and only clears the entry (`fightOutPokemonEnemyInit`);
    - clears its Pokemon's status slot (`pokemonInitJoutai`, PokemonBios
      `+0x64`, a new state call);
    - erases condition 0x17, and 0x3E when it is held
      (`fightOutPokemonWriteJoutaiDataId`: by category, the Pokemon,
      FightPokemon or battler).
    - The Transform copies (`fightOutPokemonSetHensinPokemonStatusId` 0x7C
      / 0xC8) copy a field onto itself unless the battler is transformed
      (0xD5 != 0xD7). A transformed battler is refused. The battler-model
      calls are refused when a model exists.

  On the original data:
  - Umbreon's Tackle on Zigzagoon A at 1 HP runs the whole faint sequence:
    status panel, cry (species 0x120), "fainted" (0x7545), the 0x40-frame
    close, and the panel closing.
  - Zigzagoon's status slot is cleared. Umbreon and Espeon forget it (0xF7
    0 and no enemy entry for it), and floor 0x4B is restored.
  - The hit script then runs move end and WAZAEND. The faint mark pass
    stops at Zigzagoon's faint mark sequence (`lbl_80378721`: opcode `0x23`
    `fn_802232F4`), not lowered yet.
  - Endure + super effective now shows "endured the hit" (0x75C9), then
    "super effective" (0x7637), and reaches the end of the move script.

- Faint mark and EXP award (`fn_8023011C`, `lbl_80378721`: `0x23`
  `fn_802232F4`, return).
  - The faint mark pass now runs each fainted battler's sequence. The
    battler becomes the fainted (floor 0x46, `+0xA484`) and defending (0x42:
    message variables 0x10, 0x1B, 0x42, 0x44, 0x43, then `+0xA478`)
    Pokemon, and `lbl_80378721` runs as a nested call. The interpreter has a
    new `C3_SCRIPT_PHASE_FAINT_MARK` for this: the pass may push the call and
    return 2, and after the call returns it is resumed with arg 1 to
    continue its loop (the original calls `fn_80211B94` inside the loop).
  - Battle start's `fightFloorSetMeetEnemyFightPokemonEnemySideAll`
    (`0x801F3178`) is ported as `c3_battle_state_set_meet_enemies`. It runs
    just before the enemy arrays: each battler in battle records the
    opposing battlers' entry IDs (FightPokemon 0xCE) in its met list (0xFD,
    12 slots). Without it nobody would have met the defeated battler. The
    state's growth table (`C3_PACK_GRW`, 0x194-byte rows) is now loaded.
  - `0x23` EXP award. It applies when the defeated battler is not the
    player's and floor 0x24 grants EXP.
    - The share is base EXP (species data 0x10, `+6`) × level / 7, among
      the party Pokemon of the other side's trainers of type 1
      (`fightTrainerCheckCanGetExp`) that met it
      (`fightOutPokemonCheckMeetEnemyFightPokemon`).
    - ×1.5 for floor 0x2D, and ×1.5 for another trainer's Pokemon
      (`heroIsMinePokemon`: trainer ID `+0x14` and name `+0x18` against the
      hero's `+0x2C` and name). The message is 0x7534 for another
      trainer's Pokemon, else 0x7533.
    - Each recipient in turn:
      1. the EXP camera once (new `C3_PRESENT_EXP_CAMERA`);
      2. its status panel when in battle;
      3. message 0x7532, opened and closed (variables 0x0D nickname, 0x0E
         the 0x7533/0x7534 text as `C3_MSGREF_MESSAGE`, 0x2F the amount);
      4. its effort values (`pokemonGetEffortFromPokemon`: the defeated
         species' yields, totals capped at 510 and 255);
      5. the EXP added;
      6. in battle, the EXP bar (`C3_PRESENT_EXP_BAR`), a 0x40-frame wait,
         one-frame polls until it stops (`C3_PRESENT_EXP_BAR_BUSY`), and the
         panel renewed (`C3_PRESENT_STATUS_RENEW`);
      7. the panel closed.
    - The recipients are handed out across the waits from the handler
      state. The level-up menu buffer (`lbl_80478278`, `lbl_8047B64C`) is
      only read by the level-up sequence.
    - Refused by name: a level up (`lbl_80378724`, stat growth); a Shadow
      Pokemon (its EXP is stored in 0xC6 instead); held items (Exp. Share,
      Lucky Egg and Macho Brace need the item table).

  On the original data:
  - Umbreon's Tackle knocking out Zigzagoon A awards Zigzagoon's base EXP 60
    × 24 / 7 = 205, halved between the two that met it, ×1.5 = 153 each.
  - Umbreon goes 19055 -> 19208 (bar 1632 into level 26) and Espeon 16372 ->
    16525 (bar 900 into level 25). Each gains 1 Speed effort.
  - The faint script's battle end check then stops at the replacement check
    (`fightFloorIsIrekaeFightOutPokemon`): Zigzagoon A is out of battle and
    Willie has Pokemon left.
  - Umbreon one EXP short of level 27 is refused at the level up.

- Replacement check (`0x24` with operand low byte 1).
  - `fightFloorIsIrekaeFightOutPokemon`, over the valid trainers: a trainer
    that has a party Pokemon it can send in (`fn_801F8A18`) needs a
    replacement for each active slot out of battle (2 for the player, else
    1). A party Pokemon can be sent in when:
    - it is in battle-ready health (`fightPokemonCheckFightOut`);
    - it is not the FightPokemon of one of the trainer's battlers;
    - no battler has reserved it (0x121).
    The reserve check through a battler's switch action (0xFE, kind 9) is
    refused.
  - A needed replacement is refused by name: the choice is
    `fightTrainerSelectIrekaeFightPokemon`, which is the AI
    (`fightTrainerAiSelectIrekaeDasuFightPokemon`, 0x1224 bytes) for a
    computer trainer and the party menu for the player.
  - Without one, `fn_80222EF0` runs over the battlers. One out of battle
    with no replacement entry (0x121 < 0) that fainted (0x120) is cleared
    (`fightOutPokemonInit`, the existing record port), and the battle grid
    updates (new `C3_PRESENT_GRID_UPDATE`). Refused by name: a replacement
    entry (the send-out sequence `lbl_8037879E`) and FightPokemon 0xD2 (the
    re-initialization).

  On the original data:
  - Willie has exactly two Zigzagoon, both in battle. When Umbreon knocks
    out Zigzagoon A he has nobody to send in, so there is no replacement
    choice.
  - Zigzagoon A's battler is cleared (0xD5 0, 0x120 0), the grid updates,
    and the post-faint passes and the cleanup run over the three remaining
    battlers. The knockout turn completes (`C3_SCRIPT_DONE`), with Zigzagoon
    B still in battle and the result 0.
  - With Zigzagoon B also at 0 HP, the end check finds Willie's side with
    no HP (at this commit refused before any fight result was written; see
    the battle result below).

- Secondary effects (`fn_802249B8`, from `0x15`), confusion and flinch.
  - `0x15` now applies the effect instead of refusing it:
    - with work byte 3 bit 0x80 on a hit, `(0, 0x80)` without a draw;
    - after a draw at or under the chance, `(0, 0)`, or `(0, 0x80)` at a
      chance of 100 or more.
    When the effect runs a sequence, `0x15` returns `CALL` and finishes in
    its next phase with `fn_802249B8`'s PC + 1 and the byte clears, without
    drawing again.
  - **Prologue:**
    - Bit 0x40 aims the effect at the attacker; otherwise it goes to the
      defender. The target becomes floor 0x47 (message variables 0x12 and
      0x1D, then `+0xA488`) and the other battler floor 0x4B.
    - Shield Dust (ability 0x13) and Safeguard (side condition 0x4B) block
      the weaker effects without hit marker 0x2000. A target out of battle
      blocks, except for effects 0x0B and 0x1F. A Substitute blocks unless
      the effect is the attacker's own.
    - The effect's condition (`lbl_80279EF4`) already held blocks it.
  - **7, confusion:** condition 9 is written unless the target has Own
    Tempo (0x14) or is already confused, then the confusion sequence
    `0x8037951B` runs:
    1. `0x66` (`fn_8021DF80`), the status animation, only on a battler
       model;
    2. effect wait 7;
    3. "became confused" (0x756E) and the timed close;
    4. effect wait 6.
  - **8, flinch:** condition 0x11 is written unless the target has Inner
    Focus (0x27) or 0x112 is set.
  - **Refused by name:**
    - the status effects (below 7: sleep, poison, burn, freeze, paralysis);
    - the Inner Focus message (`lbl_80379868`);
    - every other effect kind;
    - writes of Pokemon-level conditions (categories 0x7C / 0xC8,
      `fn_801219F4`).
  - **Turn counts:** the battle DOL-table pack is now version 6, adding
    `KAC` (`lbl_80478B20`, 10) and `KAI` (`lbl_80315690`, s32 min and max
    per row). The state's condition writer draws the turn count as
    `kaisuuGetKaisuu` does: min when min == max, else min + draw % (max -
    min + 1). Confusion (row 7) lasts 2 to 5 turns; flinch (row 2) is
    always 1.
  - Willie's battle sets these effects with Bite (8) and Confusion (7);
    Struggle's 0xCE (attacker recoil) is still refused.

  On the original data:
  - A guaranteed confusion (0x87) on Zigzagoon's Tackle confuses Umbreon,
    drawing once for its turn count (4 in the test), and shows "became
    confused".
  - A guaranteed flinch (0x88) writes condition 0x11 without a draw.
  - A guaranteed 0x85 is refused in the status block. All three run to the
    end of the move script with work byte 3 and `0x8047B625` cleared.

- Battle result (`0x24`, `fn_80222C44`).
  - **How the result is set:** each side's remaining HP is summed over
    kinds 5 (side 1) and 4 (side 0). A side at 0 requests fight result 2
    (side 1) or 3 (side 0) through the existing port of the original
    setter, `fightFloorSetFightResultId` (`c3_battle_rules_combine_result`,
    floor 0x23 as its draw flag). The original's own combination rules
    decide the stored value; both sides at 0 can become a draw.
  - **A decided battle** skips the replacement check. It only reviews the
    self-destruct draw rule: result 7, with floor 0x34 and a move of
    effect 7, reads the rules option `fn_80077AF4` (refused).
  - **Scope:** the value is the battle's own variable (`lbl_8047B5D8`, the
    state's `result`). Nothing is written to the save or the story records
    here. Rewards and the result transaction remain increment 4.

  On the original data:
  - With Zigzagoon B already at 0 HP, Umbreon's Tackle knocking out
    Zigzagoon A runs the faint sequence and EXP award. The end check then
    takes result 2 from the original setter, and
    `fightFloorIsGcHeroWin(2)` with the player on the host side is 1. The
    passes and the cleanup finish the call.
  - A single knockout (Zigzagoon B still in battle) leaves the result at 0.

## AI move choice for Willie (map and plan)

Scope agreed for this port: the computer trainer's path for Willie's moves
only. Every other path is refused by name, as elsewhere.

- **Willie's battlers:** Zigzagoon A knows Tackle and Growl; Zigzagoon B
  knows Tackle and Tail Whip.
- **Entry:** `fightTrainerSelectFightAction` calls `fn_802342CC(trainer,
  fight type)` for a trainer whose data row has an AI ID. It runs:
  1. `fn_80234A0C`, the preparation (below);
  2. resets the action buffers of the trainer's battlers that may select an
     action;
  3. three percentage rolls, each always drawing once (`fn_800E0C54() %
     100`):
     - AI byte 0x26 (switch) leads to `fightTrainerAiSelectFightActionIrekae`;
     - AI byte 0x27 (item), only when floor 0x20 is 1, leads to
       `fightTrainerAiSelectFightActionItem` (0x694);
     - AI byte 0x25 (a third action) leads to `fn_8024E534`;
  4. for the battlers still without an action, sorted by speed
     (`fightFloorSortFightOutPokemonPtrArySub`), the move choice
     `fn_8023A308` (0x438). That reaches `fn_8023A740` (0xD58),
     `fn_8023B498` (0xED0) and `fn_8023C530` (0x56C), about 19 KB in the AI
     range.
  The debug messages around the rolls only appear with the debug menu flag
  `fn_80008164`.
- **Preparation `fn_80234A0C`:**
  - initializes the trainer's enemy records
    (`fightTrainerInitEnemyPokemonFightOutStatus`);
  - for each of its battlers in battle: HP percentage, level, base stats
    (data 5 + 7), the stat total × level, and the summed damage estimate of
    each type-1 move against each opponent (`fn_8023C370`);
  - flags the minimum and maximum holders on the trainer's per-entry fields
    0x52..0x56.
- **AI data:** `fightTrainerGetStatus(NULL, id, 0x1F..0x38)` reads AI row
  bytes `+0x00..+0x19`. The selector map was recovered from
  `jumptable_803757D8`. Port: `c3_battle_trainer_ai_value`, with the AI table
  from the entry pack (`C3_PACK_AI`, 261 × 40) and the AI ID from trainer
  row `+6`.
- **Willie (trainer 10, AI row 81):**
  - switch 0x26 = 0, item 0x27 = 25 (and floor 0x20 = 1), action 0x25 = 0;
  - damage estimates 0x2D = 1, type check 0x2A = 1, ability check 0x2B = 1;
  - random target 0x30 = 1, random move 0x31 = 1, average 0x35 = 80, PP
    0x37 = 10.
  So the item roll succeeds on about a quarter of turns and its routine has
  to be ported too (or refused at first).
- **Per-move AI routines:** move row `+0x1C` indexes `lbl_8035C430`
  (0x14-byte rows: value, hit and damage function, then the WZX type and
  variation functions). Static data:

  | Move | Value | Hit | Damage |
  |---|---|---|---|
  | Tackle (0x21) | `fightTrainerAiWazaValueTuujouIryoku` (0xA8) | `fightTrainerAiWazaHit000` (0xB0) | `fightTrainerAiWazaDamage000` (0x88) |
  | Growl (0x2D) | `fightTrainerAiWazaValueKougekiDaun` (0x10C) | `fightTrainerAiWazaHit018` (0xE8) | `fightTrainerAiWazaDamage018` (8) |
  | Tail Whip (0x27) | `fightTrainerAiWazaValueBougyoDaun` (0x10C) | `fightTrainerAiWazaHit019` (0xE8) | `fightTrainerAiWazaDamage019` (8) |

- **Planned rounds:**
  1. AI data accessor and this map (done).
  2. Preparation `fn_80234A0C` with `fn_8023C370` and Tackle's damage
     routine, plus the trainer's per-entry flags.
  3. The entry: rolls, action-buffer resets, the speed sort. The item
     routine is refused at first.
  4. The move choice `fn_8023A308` with the hit and value routines of the
     three moves and the random selection.
  5. The item routine (`fightTrainerAiSelectFightActionItem`).

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
