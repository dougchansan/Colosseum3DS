# Original-game battle entry observation

`tools/qualification/dolphin_gdb_probe.py` is a bounded observation client for an
**already running, operator-owned official Dolphin**. It does not launch an
emulator, boot a game, drive controllers, implement battle logic, or install a
new emulator backend. It records private candidate-boundary evidence from the
original game. Entry/return observations alone do not qualify a full battle.

## Feasibility and source evidence

The locally available official Windows executable is
`G:/gamecube-decomp/Dolphin-x64/Dolphin.exe`, SHA256
`5e076628b977603dcde9ddef5eedf10b5a9ce0b6208bd4e6dfa9039cdebf3433`.
The launch operator must verify that identity separately. The client does not
infer executable identity from its network protocol.

Reviewed Dolphin source exists locally under
`/home/douglaswhittingham/colo-linux-test/repo/lib/ModernGekko/vendor/dolphin/Source/Core/`.
`Core/Config/MainSettings.cpp:329` defines `General/GDBSocket` and line 330
defines `General/GDBPort`. `Core/Core.cpp:325` starts the stub during boot and
calls `CPUSetInitialExecutionState(system, true)`. This GDB startup path is not
behind an optional GDB compilation flag; Unix socket startup is excluded on
Windows. The archived Felk harness is not the applicable harness for this
official executable.

The reviewed `Core/PowerPC/GDBStub.cpp` SHA256, with LF line endings, is
`dbc772fd356f490cab2a6d8ed6ec77cbcd35f647d01e0b0b06542cbd750abae1`.
The client requires that exact source file (CRLF is normalized), rather than
guessing a generic PowerPC remote register layout:

| Dolphin source | Observation contract |
| --- | --- |
| `ReadRegisters()` | `g` returns only 32 GPRs, each eight big-endian hex digits; exactly 256 digits total. |
| `ReadRegister()` | `p40`: PC; `p42`: CR; `p43`: LR; `p44`: CTR; `p45`: XER. These are hexadecimal register IDs. |
| `ProcessCommands()` / `SendSignal()` | `?` returns the stub's SIGTERM stop report, `T0f40:PC;01:SP;`, with eight hex digits each for PC/SP; it does not mean the game exited. Resume/step traps must report signal `05`. |
| `HandleQuery()` | `qSupported` advertises `swbreak+;hwbreak+`. |
| `AddBreakpoint()` / `RemoveBreakpoint()` | `Z0`/`z0` add/remove Dolphin execution breakpoints without replacing guest instructions with trap bytes. |
| `InitLocal()` | Non-Windows local Unix socket option. |
| `Init()` | TCP server binds **`INADDR_ANY`**, not loopback. |

The client's loopback-only TCP connection policy does **not** restrict Dolphin's
listening interface. Prefer the Unix socket when using an applicable local
build. For Windows TCP, the operator must establish launch isolation and record
the existing firewall/default inbound policy and applicable allow rules before
launch. No firewall changes are made by this tool. `--isolation-evidence` is
required for TCP; its SHA256 is recorded. File presence is an operator assertion,
not automatic proof that firewall rules isolate the listener.

An operator launch has the following shape, using a dedicated private profile,
the verified movie and the original game image:

```text
Dolphin.exe -u PRIVATE_USER -m VERIFIED.dtm -e ORIGINAL.rvz -C Dolphin.General.GDBPort=PORT -C Dolphin.Interface.DebugModeEnabled=True
```

The client connects only after the operator prepares and verifies that launch.
It never reuses or modifies the normal Dolphin user profile.

The debugging setting is required for execution-breakpoint checks in the JIT.
The reviewed `JitCommon/JitBase.cpp` loads this setting and
`Jit64/Jit_Compile.cpp` gates breakpoint checks on it. Starting the GDB stub and
receiving `OK` for `Z0` alone do not establish that those checks are enabled.
An earlier bounded launch without this setting timed out without a target hit;
that attempt provides no constructor observation.

## Candidate locations

These source-derived locations remain **candidate boundaries until the observed
caller and surrounding game context have been validated**. A symbol name alone
does not identify a complete battle, floor transition, or caller-owned object.

| Plan name | Address | Candidate meaning |
| --- | --- | --- |
| `opponentTrainerCreate` | `0x801F9CBC` | Opponent trainer creation entry. |
| `postHeroPreRoster` | `0x801F9F78` | `fightTrainerCreateFightTrainerPokemonDataIdToPokemon`; post-hero/pre-roster candidate, with `r3` roster ID, `r4` Pokemon pointer, `r5` trainer ID to validate in context. |
| `floorStart` | `0x801F17B0` | Floor start candidate. |
| `floorInit` | `0x801F4860` | Floor initialization candidate. |
| `turnCount` | `0x801F68C8` | Turn count candidate. |
| `oneTurnPost` | `0x8020BBA4` | Post-turn candidate. |
| `resultSetter` | `0x801F3984` | Result setter entry. |
| `partyCopyback` | `0x801F86C0` | Party copyback candidate. |

## Private plan and invocation

Supply the original extracted DOL privately with its independently established
SHA256. The client maps its seven text sections and compares the original
instruction word against guest memory before installing/removing each execution
breakpoint. Instructions are never included in public artifacts or copied into
the capture JSON. A pin mismatch stops the observation.

The private JSON plan contains only these keys:

```json
{
  "schema": 1,
  "max_stops": 4,
  "max_seconds": 600,
  "probes": [
    {
      "name": "postHeroPreRoster",
      "capture_return": false,
      "objects": [{"name": "pokemon", "register": "r4", "size": 312}]
    },
    {
      "name": "postHeroPreRoster",
      "capture_return": false,
      "objects": [{"name": "pokemon", "register": "r4", "size": 312}]
    }
  ]
}
```

Repeated candidates capture consecutive occurrences. To capture returns, set
`capture_return` to `true` and add `caller_addresses`, an array of private source
callsite addresses. Every address must map to a pinned original direct linked
PowerPC branch targeting the candidate. The observed entry LR must equal an
admitted caller address plus four. The return PC must equal that LR and the
stack pointer must match its entry value. The return instruction and original
callsite instruction are verified by read-only guest reads. No arbitrary return
address, callback expression, seed arithmetic, or dynamic command is accepted.

Example client invocation (replace the placeholders with private absolute paths
and the pinned hash):

```text
python tools/qualification/dolphin_gdb_probe.py --tcp 127.0.0.1:55022 --dol PRIVATE_ORIGINAL.dol --dol-sha256 PINNED_SHA256 --gdb-source PRIVATE_GDBStub.cpp --plan PRIVATE_PLAN.json --private-root PRIVATE_PARENT --output PRIVATE_PARENT/NEW_CAPTURE --isolation-evidence PRIVATE_ISOLATION.txt --timeout 5
```

For a local Unix socket, replace `--tcp ...` with `--unix /absolute/private/socket`.
Only numeric loopback TCP addresses are accepted; DNS names, wildcard binds and
nonloopback destinations are rejected. All other inputs are checked before
connecting. This command never starts Dolphin.

## Capture and control limits

The exact outbound command whitelist is `qSupported`, `?`, `g`, the selected
`p` register reads, bounded `m` reads, admitted `Z0`/`z0` breakpoints, and bare
`c`/`s`. Register and memory writes, monitor/host commands, signal/address resume
variants, watchpoints, arbitrary breakpoints, raw interrupts, detach and kill
are rejected before any bytes are sent. The capture path uses continue only;
single-step is available in the constrained client API.

RSP handling validates checksum and ACK, handles fragmented packets, escaping
and bounded run-length encoding, and rejects malformed data, unsupported
layouts, error replies, EOF, missing ACK and unexpected stop PCs. It does not
retry malformed packets or negotiate no-ACK mode. Stop reports
accept only bare `Sxx` stops or Dolphin's exact `Txx40:PC;01:SP;` field layout;
unknown, duplicate or missing detailed fields fail closed. Detailed stop PC/SP
values must be aligned MEM1 addresses and agree with the subsequent `p40` PC
and `g` stack register values. Resume/step requires signal `05`; initial `?`
admits SIGTERM `0f` or SIGTRAP `05`. Both wire and expanded packets
are limited to 16 KiB. MEM1 reads are limited to
`0x80000000 <= address < address+size <= 0x81800000`, at most 4096 bytes per
operation and 256 bytes per RSP request. Object requests currently admit only
312-byte Pokemon snapshots, selected by an entry GPR and range checked before
reading. Return snapshots read the same entry object address, even if the return
register value has changed.

Each stop captures all 32 GPRs, PC, LR, CTR, XER and CR. The active RNG state is
read through the pointer at `0x80478C94`, then through that validated MEM1 pointer.
An invalid pointer fails closed. There is no fallback to `0x80478C90`.

New `postHeroPreRoster` entry and return snapshots also record `attest` from an
independent fixed savedata read: read the big-endian pointer at `0x8047ADB8`,
validate that four bytes at its pointee fit in MEM1, then read exactly four bytes
at offset zero. The snapshot retains `pointer_address`, `pointee_address`,
`offset: 0` and `data_hex`. The source map is
`gamedataGetStatus` (`0x80135938`) → `savedataGetStatus` (`0x80129280`) →
`fn_80128E24`, with identity helper `0x80128E04` and
`gamedataBiosGetGamedataAtttestPtr` (`0x80135B0C`). This is one pointer
dereference, independent of the captured Pokemon object and its output bytes.
Both pointer and pointee are read afresh for each snapshot; an invalid pointer
fails closed, with no fallback. Other candidate snapshots do not add this field.
Earlier captures lacking `attest` contain no independently observed attest
evidence. They must not be backfilled from Pokemon output or described as if
this read occurred.

Plans admit 1–8 sequential probes, at most 32 stops and 1–3600 seconds total
(defaults: 16 stops, 60 seconds). Commands are capped at 512 for the CLI.
Ordinary transport replies use `--timeout` (greater than zero, at most 30
seconds). Waiting for the first stop-packet byte after a continue uses the
remaining overall time budget; the movie may take several minutes to reach the
candidate. Packet contents must then arrive within the ordinary reply timeout.

The output must be a new directory inside an existing private parent and outside
Git checkouts. Existing output is refused. Complete JSON files are staged in the
private parent and the directory is promoted on success. `observation.json`
contains the bounded command audit and aggregate records; individual records use
`00-NAME.json`, `01-NAME.json`, and so on. Raw guest Pokemon bytes belong only in
these private captures. Never commit captures, private plans, original DOL
bytes, movie inputs, or source excerpts containing original instructions.

Success removes the observation breakpoints and leaves Dolphin paused at the
last observed boundary. Failure closes the connection and publishes no final
capture. A timeout while continuing may leave the owned emulator running, and
failure may leave an observation breakpoint installed. The launch operator must
terminate that owned process; the client does not send an unapproved interrupt,
kill, detach, or guest write as cleanup.

## Verification and qualification limits

Run `python -m unittest discover -s tests -p test_dolphin_gdb_probe.py -v`.
Tests use synthetic DOL text and local fake RSP peers; they exercise fragmented
packets, checksum/ACK errors, escaping/RLE bounds, EOF/error replies, time and
command caps, loopback/MEM1 policy, forbidden writes, the source-specific register
layout, pinned instructions, caller checks, and entry/return object/RNG capture.
They never connect to Dolphin or launch an emulator.

Only independently reviewed, noncopyrighted derived facts may be promoted into
public fixtures after live caller/context validation. A successful candidate
capture is not evidence of complete turn behavior, RNG draw order, full battle
equivalence, or 3DS runtime correctness. Those remain separate qualification
work. Every capture explicitly records `full_battle_qualified: false`.
