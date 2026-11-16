# Link play: Battle Now, Colosseum Battle and trading

This page records what the original game does for its multiplayer modes, read
from the pinned decompilation in `vendor/colosseum`, and how the port carries
them onto 3DS local wireless. Paths under "Original" are relative to
`vendor/colosseum/src/game/` unless they say otherwise. Each later section says
plainly what is original, what is an approximation and what is missing.

## Original

### How Battle Mode is entered

- The title's top menu is `fn_800210F0` (`gs_title.c:2694-2890`). It opens
  window 0xAA and switches on the choice in `lbl_8047A360`: 0 new game,
  1 continue, **2 `menuColosseumBattle(0)`**, **3 `menuColosseumBattle(3)`**,
  4 options.
- `menuColosseumBattle` (0x8005CEE8, `menu/menuColosseumBattle_exact_8005CEE8.c`)
  stores its argument in `_CB.m_eBattleMode` (word 0 of
  `savedataGetStatus(0, 0xE)`) and starts the menu floor 0x395. The menu loop
  is `menuColosseumBattleMain` (0x80059BDC, `menu/menuColosseumBattle.c:511`).
- The Battle Mode state is `MenuCBBattle` (`menu/menu_middle.c:335-355`):
  `m_eBattleMode`, `m_eBattleType`, the rule index (+8), `m_eColosseum`,
  `cpuRank` (+0x10), `m_nBattleCount`, then `trainers[4]` and `players[4]`.
- Battle modes (asserts at `menuColosseumBattle.c:327-333`, `:610-615`):
  0 `BATTLEMODE_COLOSSEUM`, 1 `BATTLEMODE_BATTLEYAMA100` (Mt. Battle),
  2 the versus branch for human players, 3 the title's choice 3, **Battle Now**.
- Battle types: 0 single, 1 double, 2 `BATTLETYPE_MULTI` (four trainers,
  2 against 2).
- The menu sets flag 0x8AE to 1 (single) or 2 (double/multi); story mode
  asserts it is 0. Battles start through floor 0x397
  (`menuColosseumBattle.c:1510-1512`).

### Battle Now (mode 3)

- Menu state 0xAF picks single or double (`m_eBattleType`); menu 0xBC picks
  one of four entries into `cpuRank`. The rule index and venue are forced
  to 0 (`menuColosseumBattle.c:764-790`).
- `fn_80069C0C` case 3 (`menu/menu_middle.c:650-685`) draws two **different**
  trainers from the Mt. Battle table (colosseum 6) through
  `staCPU[type][cpuRank][rand & 7]` and the global RNG `fn_800E0C54`. The
  human player is handed the first trainer's party under a random name
  (messages 0x2EFE, 0x2EFF, 0x2F00); the CPU opponent gets the second. Every
  held item on both sides is cleared. Fight kind 0xC, no intro WZX.
- So Battle Now is one human against the CPU with random preset teams, single
  or double. There is no team picker.

### Colosseum Battle (mode 0), Mt. Battle (mode 1) and versus (mode 2)

- Venues 0-5 (state 0xAE), `menuCBBattleKinds = {1,1,1,1,2,2,2}`. A Colosseum
  is 8 matches (`COLOSSEUMMATCH_MAX`, match 7 uses fight kind 0xE); the
  opponent is `table[colosseum*16 + type*8 + count]`
  (`menu/menu_middle.c:398-430`). Mt. Battle is colosseum 6 with rule 2,
  100 matches, CPU teams raised to the player's highest level
  (`menu_middle.c:691-705`).
- The player's team is the saved story party
  (`heroBiosCopy(..., savedataGetStatus(0,2))`, `menu_middle.c:814`), so these
  modes need a memory card save.
- **Rules.** `menuRulePresets[3][0x54]` (`menu/menu_range_8007109C.c:1446`)
  plus six custom rules at `_CB+0xC9DC` (`fn_8006B420`). Fields: s16 @0
  minimum level, s16 @2 maximum level, s16 @4 level-sum cap, s16 @6 minimum
  Pokemon count, byte @0xC duplicate species allowed, byte @0xD duplicate items
  allowed, s32 @8 item mode with a disable table at +0x18, s16 @0x14 time limit
  (×60). The three presets:

  | preset | levels | level sum | min. count | dup. species | dup. items |
  |---|---|---|---|---|---|
  | 0 | 1-100 | 600 | 1 | yes | yes |
  | 1 | 1-50 | 300 | 6 | no | no |
  | 2 | 1-100 | 600 | 6 | no | no |

- **Per-Pokemon legality**, `fn_80076398` (`menu/menu_exact_80076334.c:266`):
  stats/EVs legal (EV total at most 510, each at most 255); Mew (0x97) and
  Deoxys (0x19A) need their event flag; **no Shadow Pokemon, no eggs**; item
  0xAF is always banned; every move valid. Team checks in `fn_80076F2C`.
- **How many Pokemon each player brings**, `fn_80077DB8`
  (`menu_range_8007109C.c:1823`) with asserts at `menu_middle.c:612-648`:
  single 3, double 4, multi 2; 6 when the mode is versus (2) with rule 0.
- Poke Coupons are paid in state 0x105.

### Extra players

- Each `MenuCBPlayer` (`menu_middle.c:335`) holds `inputDevice` @0x24 (a
  controller ID 1-4, `menuCBBios_ControlerIDtoPortID`), `slot` @0x28 (play
  order), the `hero` record @0x2C (0xB18 bytes) and `rental` @0xB44.
- Single/double versus uses players 1 and 2; multi uses controllers 1-4
  (`menuColosseumBattle.c:1294-1357`). The 2-against-2 pairing comes from
  `menuCBPlayerOrders = {0,1,2,3},{0,2,1,3},{0,3,1,2}` in state 0xB5
  (`:324`, `:1452-1468`) and reaches the fight through
  `fightEncountDataBiosSetGSInputDevice` (`menu_middle.c:739-751`).
- A GBA could act as a controller: team order through
  `gbaCommandEntryPokemon` (`menuCB_range_80062948.c:1962`), move and switch
  menus through `menuFightOpenGBAMain`/`menuFightOpenGBAIrekae`
  (`menuFight.c:115/124`).

### GBA link and trading

- The transfer UI is `fn_800849B4` → `fn_80084A8C` (`menu/cardesavedata.c:3528-4100`),
  with command bits 1 trade checks, 2 check party against the rules, 8 retry,
  0x10 slot 0 uses the story save, 0x20 extra block, 0x40 coupons; it checks
  the GBA's region.
- Importing GBA teams into versus only runs with debug flag `lbl_80478958`
  (`fn_80089028`, set by `dbgToolBattleDebugSetAGBConnectionMode`).
- The GBA trainer record is 0x278 bytes (`gbaCommunication.c:1684`): name,
  gender, trainer ID and six 100-byte Ruby/Sapphire Pokemon. `fn_8008BBDC`
  converts GBA→GC and `fn_8008AE18` GC→GBA (personality, OT, nickname and
  OT charset, species, item, EXP, friendship, moves/PP/PP Ups, EVs, contest
  stats, Pokerus, met data, IVs, egg and ability bits, ribbons, status,
  level, mail, stats).
- **The GC Pokemon record is 0x138 = 312 bytes** (`menuPokemonChange.c:337`,
  `menuColosseumBattle.c:110`) — the same record `src/inventory.c` and the
  save use.
- **Trade rules**, state machine `fn_80031B4C` (`gs_npc_event.c:721+`):
  `fn_8002EE74` refuses eggs (message 0x43E1) and bad eggs (0x44BE);
  `fn_8002EA5C` refuses a held item that fails `itemDataBiosCheckImportable`
  (0x43DD) and refuses when no healthy non-egg Pokemon would be left
  (0x44E8). On completion both Pokemon get **friendship 70 (0x46)**, then
  `memoDataSet` and the evolution check (`stateFunctionEvolution`). Saving
  is `stateFunctionSaveReport` (0x8002DD24), which saves the memory card in
  step with the GBA and restores the 0x1DFD0-byte save image on failure.
- Shadow Pokemon cannot be traded until purified (general game knowledge; the
  check is not located in the decompiled trade code).

### RNG

- One global LCG, `x = x*214013 + 2531011`, returning bits 16 and up
  (`src/hsd/random.c:28-46`), seeded once at boot from `OSGetTime`
  (`fn_800E0C78`). There is no per-battle reseed: the original never had to
  agree on randomness with a second machine.

### What the decompilation does not contain

- Menu layouts and text for menus 0xBE, 0xBC, 0xD0, 0xB4, 0xE4 are disc data
  (`colosseumbattle_menu.fsys`, `colosseumbattleconnection_menu.fsys`);
  `toolbattle_menu` (REL 163) is not integrated.
- The GBA multiboot program is absent.
- The Shadow-Pokemon trade check, and what menu 0xBC's four `cpuRank`
  choices are called on screen.

## Port design

On 3DS every player is a separate console (or Azahar instance), so a
controller port becomes a **player slot in a link session** and the GBA cable
becomes a **trade between two consoles**. The port adds a network layer the
original never needed, and keeps everything above it to the original's rules.

### Link session (`src/link_protocol.c`, `platform/3ds/link_uds.c`)

- Star of up to four nodes over libctru UDS (3DS local wireless). The host is
  player 0 (controller 1); clients get player 1, 2, 3 in join order
  (controllers 2-4). Clients talk only to the host, which relays.
- Reliable ordered messages: sequence numbers, cumulative acks, retransmission
  every 120 ms, a 32-packet window, messages up to 2048 bytes in 480-byte
  fragments. Keepalives every 250 ms, a 6 s timeout, a BYE on exit.
- Handshake: HELLO carries the protocol version and the 16-byte build ID
  (`C3_BUILD_ID`); the host refuses a different version or build, a full
  session or one that has already started. Every node learns the roster.
- The protocol module has no libctru calls; the host tests drive four
  sessions over a loopback transport with loss, duplication and reordering
  (`tests/test_link_protocol.c`).

#### Wire format

Big-endian: `'C' 'L'`, kind (1 DATA, 2 ACK, 3 PING, 4 REJECT, 5 BYE), flags
(DATA: 1 first fragment, 2 last), seq u16, ack u16; DATA adds type, origin
player, destination player (0xFF all), a reserved byte, payload length u16
and the payload.

### Lockstep battles (`src/link_battle.c`)

Battles never stream state. Every console runs the same engine
(`src/battle_general.c`) on the same inputs:

1. The host draws one 32-bit seed and sends the setup (mode, battle type,
   rule preset, player order, seed). Every player sends the records of the
   Pokemon it brings; the host relays them, so every console builds the same
   battle with `c3_general_battle_begin_link`.
2. Each turn, each console sends the commands for the positions its player
   controls to the host. The host sends back the complete set for all four
   positions; every console runs `c3_general_battle_turn_link` with it.
3. After each turn every console hashes the battle state (turn, outcome, RNG
   seed and draw count, actives, screens, every Pokemon's HP, status, PP,
   stages and volatile flags; event text is excluded because it is written
   from each player's own point of view) and logs `link_turn turn=N
   hash=XXXXXXXX`. Consoles send their hash to the host, which stops the
   battle with a desync message if any differs.

The engine's own RNG (`src/battle_rng.c`, the HSD generator) is the only
source of randomness in a turn; automatic players in probe builds choose with
a separate local generator so they cannot disturb it.

Each console shows the battle from its own side: a side-1 player sees a
mirrored copy (`c3_general_battle_mirror`), so its Pokemon stand nearest the
camera and the messages call the other side "Foe".

### Battle Now over link

- Original: one human against the CPU with two random preset teams. Over link
  the CPU's place is taken by the other players, and each player is handed a
  random preset team the same way: two (or four) different trainer parties
  drawn with the shared seed, held items cleared, single or double as chosen
  by the host. With four players the battle type is multi (2 against 2,
  `menuCBPlayerOrders[0]`), each player controlling one position with
  2 Pokemon.
- Approximation: the Mt. Battle trainer table (`staCPU`, colosseum 6) is not
  in the port's prepared battle pack, so the preset teams are drawn from the
  story trainer parties the pack does hold (`tools/prepare_battle_encounters.py`).
  The `cpuRank` choice has no meaning without a CPU and is not offered.

### Colosseum Battle over link

- Original versus branch (mode 2): each player brings Pokemon from the saved
  story party; the rule preset decides levels, level sum, count and
  duplicates; `fn_80076398` rejects Shadow Pokemon, eggs and item 0xAF.
  The number of Pokemon per player follows `fn_80077DB8`: single 3, double 4,
  multi 2, all 6 with rule 0.
- The port applies the same rule checks to each console's own party before
  the battle (`c3_link_rules_check`) and refuses an illegal team with the
  original's reason; the refusal reaches every console.
- Team choice: the original lets each player pick which Pokemon to bring in
  its team-order menu (disc menu data the decompilation lacks). The port
  takes the party's first legal members in party order
  (`c3_link_pick_team`); reorder the party in Story Mode to choose.

### The title menu

`fn_800210F0`'s choices 2 and 3 (COLOSSEUM BATTLE, BATTLE NOW), which said
"not available" before, now open a lobby (`platform/3ds/link_lobby.c`):
HOST A BATTLE / JOIN A BATTLE (and HOST A TRADE / JOIN A TRADE under
Colosseum Battle). The host sees who has joined, picks SINGLE, DOUBLE or
MULTI (MULTI needs four players) and, for Colosseum Battle, the rule preset
with L/R, then starts; every joined console follows. Colosseum Battle and
trading need a Story Mode save, as the original needs a memory card save.

### Trading

- Two players each pick a party Pokemon, both confirm, and the 312-byte
  records swap. The original's trade checks apply: no eggs, no bad eggs, not
  the last healthy non-egg Pokemon, and no Shadow Pokemon (record +0xD8 with a
  non-negative heart gauge at +0xDC, the same test as `src/inventory.c`).
  Both received Pokemon get friendship 70.
- The trade changes the console's game state in memory only. **Only the
  explicit Save writes the card**, as everywhere else in the port: after a
  trade the screen offers SAVE THE GAME or NOT NOW (the next Save, e.g. in
  Story Mode, then keeps the new party). `c3_game_state_trade_pokemon` puts
  the received record into the slot that was given away.
- Each side's CONFIRM carries the hashes of both records it agreed to, so a
  changed offer needs a new yes and neither side can swap something the
  other did not see.

## Testing

### Host tests (no console, no original data needed)

```
make build/test_link_protocol build/test_link_battle
./build/test_link_protocol
./build/test_link_battle [<battles.c3x> <newgame.c3g> <save A.gci> <save B.gci>]
```

`test_link_protocol` runs the session over the loopback transport: the
handshake with four players, refusals (build, version, locked, full),
120 messages in order through 25% loss with duplication and reordering,
relaying between clients, BYE, a silent client and a lost host.
`test_link_battle` plays Colosseum battles (single, double and 2-against-2
with four consoles) over a lossy loopback with a synthetic pack, checks that
every console logs the same hash after every turn, and that a forced desync
and a dropped player are reported. With the private arguments it also plays
Battle Now (2P single, 2P double, 4P multi) with the real pack and a
Colosseum battle with two saves' parties.

### Several Azahar instances (unattended)

The probe build boots straight into a link session, plays from a script and
writes `link-probe.log`; it never writes a save.

```
make -f Makefile.3ds BUILD=build-3ds-link C3_EXTRA_CFLAGS=-DC3_LINK_PROBE
```

Each instance is a portable copy of Azahar in `G:\Colosseum3DS-emu\link\pN`
with its own user folder; its SD card holds
`sdmc:/3ds/Colosseum3DS/link-role.txt` (`role host|join`, `players N`,
`name`, `mode echo|battle-now|colosseum|trade`, `type single|double`,
`rule 0..2`, `slot N`) and, for Colosseum and trading, a **copy** of a save
at `saves/pokemon_colosseum.gci`. The private runner
`private/run/link_run.ps1 -Mode <mode> -Players <2|4> [-Saves a.gci,b.gci]`
starts `azahar-room.exe` and the instances, joins each to the room through
UI Automation patterns (no keystrokes or mouse), waits for every log to end
and collects logs and frames under `private/evidence/<mode>-<n>p/`.

### Azahar notes

- Every copy needs its own console identity, or the room refuses it: the
  CFG username (block 0xA0000), console ID (blocks 0x90000/0x90001) and
  `user/sysdata/mac.txt` (`private/run/link_identity.py`).
- Under Azahar's UDS a 10-byte datagram makes the receiver's
  `udsPullPacket` fail and is lost; the session pads its control datagrams
  (ACK, PING, REJECT, BYE) to 32 bytes. Data packets were unaffected.
- `udsSetNewConnectionsBlocked` is not used; the session's own LOCK refuses
  late joiners.
- Another program on this machine (a Switch emulator's room server) held
  127.0.0.1:24872, Azahar's default room port, so the runs use port 24901.

## Results

### Battle Now (stage 3)

| run | consoles | turns | hashes | outcome | evidence |
|---|---|---|---|---|---|
| 2P single, 3D stage | 2 Azahar | 37 | all 38 equal, final E5CC8C50 | side 1 won; each console reports its own result | `private/evidence/battle-now-2p/` |
| 4P multi (2 v 2), 3D stage | 4 Azahar | 4 | all 5 equal on four consoles, final 2338636A | side 0 won | `private/evidence/battle-now-4p/` |

Original: the two-different-trainers draw, held items cleared, the per-type
team sizes (single 3, double 4, multi 2), `menuCBPlayerOrders` pairing.
Approximation: the preset parties come from the story trainers in the pack
(the Mt. Battle `staCPU` table is not prepared); the stage is a story
Colosseum floor (encounter row on battle floor 26) and its trainer model
stands in for the opponent players; turn rules are the port's general engine
(see `include/colosseum3ds/battle_general.h`), not the original battle
scripts.

### Colosseum Battle (stage 4)

| run | consoles | saves (copies) | turns | hashes | evidence |
|---|---|---|---|---|---|
| double, rule 0, 3D stage | 2 Azahar | console save, campaign save | 25 | all 26 equal, final 8203A059 | `private/evidence/colosseum-2p-double-3d/` |
| single, rule 0, headless | 2 Azahar | console save (2 Pokemon, Lv25-26), downloaded save (6 Pokemon, Lv100) | 3 | all 4 equal, final 5EA5A0E8 | `private/evidence/colosseum-2p-rule0/` |
| single, rule 1 (Lv50) | 2 Azahar | as above | - | refused on both: "Choose 3 Pokemon." | `private/evidence/colosseum-2p-lv50-refused/` |
| title-menu lobby, scripted presses | 2 Azahar | console save, campaign save | 16 | all 17 equal, final 164E2928 | `private/evidence/lobby-colosseum-2p/` |

The headless run is the documented fallback: the Lv100 team's lead species
has no prepared 3D model (`tools/prepare_battle_stages.py` covers the story
encounters' species), so both consoles played the same lockstep turns
without the stage. The saves on the emulated SD cards were byte-identical
after every run (`saves.txt` records each source's SHA-256).

Original: rule presets and the team-size table, the legality checks, the
whole-party case for rule 0, player 1 hosting (controller 1). Approximation:
team choice is the party's first legal members; the eight-match CPU
Colosseum and Mt. Battle (single player against the CPU) are not part of
link play and are not ported; Poke Coupons are not paid.

### Trading (stage 5)

| run | consoles | saves (copies) | result | evidence |
|---|---|---|---|---|
| probe trade | 2 Azahar | console save, downloaded Lv100 save | slot 0 Umbreon Lv26 <-> slot 0 #404 Lv100; record hashes C955B011 / BFDAAA83 match across consoles; friendship 70 on both; no save written | `private/evidence/trade-2p/` |
| title-menu lobby trade + SAVE, scripted presses | 2 Azahar | as above | the same swap; SAVE wrote `link-probe-save.gci` on each console, which reloads with the received Pokemon (held item kept); `saves/pokemon_colosseum.gci` byte-identical before and after | `private/evidence/lobby-trade-2p/` |

Host: `test_link_trade` (synthetic records, 20% loss) and, with the private
arguments, a trade between the two saves followed by the explicit Save into
a scratch file that reloads with the traded Pokemon.

Original: refusing eggs, Shadow Pokemon (until purified), the last healthy
Pokemon and items that cannot be traded; friendship 70 on completion; the
312-byte record unchanged otherwise. Approximation:
`itemDataBiosCheckImportable` is reduced to "item ID below 0x15D" (items
that exist in Ruby/Sapphire); the partner is another Colosseum3DS save
rather than Ruby/Sapphire, so no GBA conversion (`fn_8008BBDC` /
`fn_8008AE18`) is needed.

## What is still missing

- **Real hardware**: everything above ran on Azahar; UDS on two physical
  3DS consoles has not been tried (`udsSetNewConnectionsBlocked` and the
  10-byte frame workaround may behave differently there).
- **Battle rules**: link battles use the port's general battle engine (a
  documented Gen-3 approximation in `src/battle_general.c`), not the
  original battle scripts. Lockstep holds whatever the rules are, because
  every console runs the same code on the same inputs.
- **Team choice**: no team-order menu; each player brings the first legal
  members of their party. The rule fields for item mode, the item-disable
  table, the time limit and the Mew/Deoxys event-flag check are not applied.
- **Single-player Battle Mode**: the CPU Colosseum (8 matches per venue) and
  Mt. Battle (100 matches) are not ported; Battle Now against the CPU is
  replaced by Battle Now between players.
- **Presentation**: the opponent players are shown with the stage
  encounter's trainer model, not their own player characters; the stage is a
  story Colosseum floor, not the Battle Mode floor 0x397.
- **Trading**: no trade evolution check (`stateFunctionEvolution`), no
  strategy-memo update (`memoDataSet`), no GBA (Ruby/Sapphire) partner, no
  trade animation; trades are between two players only.
- **Four-player Colosseum** was proved in the host tests (synthetic pack)
  and four-player Battle Now on Azahar; a four-instance Colosseum run with
  four saves was not done.
- **Lobby UI** was driven only by scripted presses in the probe build, not
  by a person at the console.

