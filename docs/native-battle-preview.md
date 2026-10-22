# Nonpersistent encounter-10 combat preview

This is an **interactive development preview**, not the complete original
Colosseum battle engine. It lets a player select the two starters' moves and
opponent targets, execute turns, and reach a computed win or loss. It does not
claim retail AI, original battle timing/RNG parity, full FightPokemon state,
battle models/animations/audio, inventory, switching, Call, experience, rewards,
or story completion.

The field's original native98 remains suspended. Closing or completing the
preview returns to the same pending request. No saved HP/PP, money, experience,
reward, or victory flag is written. Native98 is never resumed with the preview's
outcome. This boundary is intentional while PR6 independently qualifies the
original RNG and while full battle execution/result handling remain unported.

## What executes

`battle_damage.c` compiles the pinned `fn_80232110` core damage arithmetic from
`vendor/colosseum/src/game/fight_range_80211A00.c` using explicit scalar handles
and validated accessor implementations. The source SHA-256 is enforced by
`prepare_battle_damage.py`. Extraction only renames symbols; it does not replace
integer arithmetic with a new formula. The owned game save is not an accessor
backing store. The admitted profile is item-free and weather-free with the two
starters and two ordinary encounter-10 Zigzagoon. In particular, Dark damage is
special, and Reflect modifies physical damage with the original integer order.

`battle_preview.c` supplies a **new, bounded preview turn coordinator**, not the
original BattleMain/FightAction execution path. It supports Bite, Secret Power,
Taunt, Snatch, Confusion, Return, Reflect, Helping Hand, Tackle, Growl, Tail Whip,
and forced Struggle. The current arena's Secret Power effect is an accuracy
reduction. Legal commands, remaining PP, priority/speed order, HP changes,
fainting, retargeting, confusion, flinching, stage reductions, screens, and
bounded turn/event counts are represented. Persistent ailments, extra party
members, held items, and other moves are refused at entry rather than silently
ignored. Attacks can target opponents only; arbitrary ally attacks are not
implemented. Status/secondary-effect and complete-turn behavior have not been
qualified against original PowerPC execution.

The enemy policy is explicitly **uniform selection among currently legal move
slots and living opposing targets**. This is not presented as Willie's original
AI. The preview has its own diagnostic seed (`0x12345678` in the modal scene).
It generates a low-half-first opponent trainer ID before invoking PR7's existing
roster initializer. This is not a claim to recover the original shared game RNG
at battle entry. Preview tests must not be used to qualify PR6's bounded RNG or
complete original constructor/action behavior.

Field entry creates a separate encounter-10 request from the current party; it
does not mutate or resume the field VM's pending request. Every turn operates
on a private copy. Invalid commands, unsupported accessors,
RNG counter overflow, and bounds failures leave the previous preview state and
RNG unchanged. The original request and all six player records remain identical.
Menu selection, target changes, and cancellation consume no RNG. Only explicit
turn confirmation executes commands.

## Native controls and rendering

At Willie's pending encounter-10 screen press **A** to open the optional preview.
The regular field/title path does not require the two new packs to be installed.
Missing or unsupported packs produce a visible error and a safe return.

The preview currently uses the native text consoles, with HP bars on the top
screen and commands/messages on the bottom. It is not the original graphical
battle interface. Use D-pad to select, A to confirm, B to revise or advance a
message, and X to request leaving. The confirmation page lets both commands be
reviewed before executing. START+SELECT exits the application.

The field caller acquires its existing GPU-idle barrier before the modal
CPU-console takes over the top framebuffer. Existing scene GPU storage is kept
alive. On return, the bottom console selection and top BGR8/double-buffer mode
are restored, and paused field time is not advanced by the preview duration.
Physical hardware acceptance of this modal transition is still required.

## Private asset preparation

Existing `willie.c3b` and `newgame.c3g` remain unchanged. Prepare these two small
additional files from the user's admitted GC6E01 revision-0 extraction:

```sh
python3 tools/prepare_battle_init.py \
  --common-rel /private/disc/files/common_rel.rel \
  --output /private/boot/willie.c3i
python3 tools/prepare_battle_preview.py --disc /private/disc \
  --output /private/boot/willie.c3p
```

Install the resulting 768-byte C3I1 and 1,024-byte C3P1 files into
`sdmc:/3ds/Colosseum3DS/boot/`, preserving current data and verifying FTP readback.
These contain original data and must stay private. Both formats are ignored by
Git. C3P1 includes the twelve original move rows, three type/ability records,
stat/accuracy ratios, critical denominator, terrain effect, and the damage
routine's held-item/type table. It binds to the admitted source/DOL/REL and the
exact encounter payload. Checksums detect damage, not malicious provenance;
retain the local original-input verification record.

## Testing

```sh
make build/test_battle_preview
./build/test_battle_preview
./build/test_battle_preview /private/boot
python3 -m unittest discover -s tests -p 'test_prepare_battle_preview.py' -v
```

The default test uses synthetic parameters, not original asset fixtures. It
checks original damage arithmetic boundaries, critical/Reflect behavior,
Dark-special classification, invalid-command/overflow rollback, deterministic
replay, immutable requests, PP exhaustion/Struggle, status and support moves,
complete wins/losses, and controller menu transitions. The optional private run
uses a freshly initialized WES body and the verified tables. It compares the
entire game-state body before and after each turn; it never loads or writes the
user's completed save.

For an isolated ARM input/rendering check, CI also builds
`Colosseum3DS-battle-preview-probe.3dsx` with `C3_BATTLE_PREVIEW_PROBE`. This explicit
compile-time entry creates an in-memory WES party from `newgame.c3g`, loads
`willie.c3b/.c3i/.c3p`, and opens the **same modal UI/core** used by the field.
It does not exercise the route from New Game through the shop, original GSVM
battle entry, or the field renderer transition. It writes only
`preview-probe.log`, not a GCI. The normal application has no probe auto-entry.

Validation reports must identify normal application versus probe, exact source
and binary hash, synthetic versus original input tables, host versus emulator
versus physical console, and the enemy/RNG policy. A preview win is not a
completed original Willie story battle.

## Remaining path to the real story battle

Qualify full trainer/FightPokemon initialization and the original RNG boundary;
implement original legal commands/targets, AI and action/effect scheduling;
compare fixed-seed complete turns against original execution; integrate actual
battle presentation and validated party/reward writeback. Only that qualified
backend may return a result to native98 and allow the original story script to
own progression flags. The preview must never become a shortcut around these
requirements.
