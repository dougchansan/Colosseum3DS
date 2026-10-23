# Nonpersistent encounter-10 combat preview

This is an **interactive development preview**, not the complete original
Colosseum battle engine. It lets a player select the two starters' moves and
opponent targets, execute turns, and reach a computed win or loss. It does not
claim retail AI, original battle timing/RNG parity, full FightPokemon state,
complete original effects/cameras/audio, inventory, switching, Call, experience, rewards,
or story completion.

The field's original native98 remains suspended. Closing or completing the
preview returns to the same pending request. No saved HP/PP, money, experience,
reward, or victory flag is written. Native98 is never resumed with the preview's
outcome. This boundary remains intentional: PR6 records original-instruction RNG helper
qualification and physical shop Save/fresh-process Continue acceptance. Preserve
that evidence; it does not qualify complete battle execution or result handling.

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

## Native controls and actor-motion milestone

At Willie's pending encounter-10 screen press **A** to open the optional preview.
The top screen renders the original arena, battle variants of Wes and Willie,
Umbreon, Espeon and two independently animated Zigzagoon. A development HUD shows
HP bars, move/target selection, PP, messages and the preview result. The bottom
console supplements controls and diagnostics. A visible development indicator
states that original ball/move effects, camera sequences and audio are missing.

Use D-pad to select, **A** to confirm, **B** to revise, and **X** to request
leaving. Review both commands before confirming a turn. During committed event
presentation, message advancement waits for the visual event; X can request
leaving. At the result page, A/B returns to the pending field encounter.
START+SELECT requests safe application exit. Held dialogue input must be released
before battle commands are accepted. Items, switching and Call are visibly
unavailable.

Typed committed events drive source-selected attack/support poses, hit reactions,
HP movement, faint poses and trainer outcome poses. Presentation owns its HP and
visibility separately from the already-resolved combat turn; it does not parse
English messages or advance combat RNG. The duplicate Zigzagoon instances share
geometry/textures and retain independent animation state. These are original
actor motions, not complete original move effects.

The field caller drains pending GPU work, releases field render/model payloads,
loads the battle through the shared stage renderer, drains battle work on exit,
and restores field models against the live story state. The Citro3D owner and
frame-submission path remain shared. The existing story VM, NPC/script state and
pending encounter survive; restoration does not rerun entry scripts, reload a
checkpoint or create a new game. Paused battle time does not advance field
scripts. Allocation/missing-asset errors remain visible and return safely.
The original complete shader-output writes and responsive render recovery remain
required; CPU console initialization must not take the GPU-owned top framebuffer.

Current substitutions are explicit: the arena's authored static camera replaces
the original battle camera sequences; the HUD uses a native development font and
layout rather than original UI/font assets; original WZX/particle/support/ball
effects and audio playback are absent; projected original actor shadows are not
implemented. Do not describe actor motion, a static arena camera or the native
HUD as the complete original presentation. See [asset preparation](battle-assets.md),
[source resource manifest](battle-scene-manifest.json), and
[presentation coverage](battle-presentation-coverage.md).

The corrected normal executable from c9a43ddc13be passed the fresh WES route,
two controller-selected turns, fainting, a preview win, pending field return,
re-entry, cancellation and clean exit on physical hardware. Both complete live
state bodies were byte-identical around their preview calls; FTP readback also
confirmed the fresh GCI unchanged. The readable-HUD diagnostic probe separately
ran in Azahar, with its bounded outcome-UI timeout recorded explicitly.

[Exact binaries, actual screenshots/MP4s, measurements and remaining gates](evidence/battle-rendering-2026-10-22/README.md)
separate corrected hardware acceptance from older glyph-defect footage and
emulator-only observations. This does not establish complete original
presentation, original combat parity or story writeback.

## Private asset preparation

Existing `willie.c3b` and `newgame.c3g` remain unchanged. Prepare these two small
additional table files from the user's admitted GC6E01 revision-0 extraction:

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

Prepare the private original scene with the same pinned importer and geometry
pipeline already used for field/title assets:

```sh
python3 tools/prepare_battle_scene.py --disc /private/disc \
  --common-rel /private/disc-overlay/files/common_rel.rel \
  --parser vendor/hsd-importer --output /private/prepared/battle
```

Copy `scene.c3v` and the eight emitted `.c3t` packs to
`sdmc:/3ds/Colosseum3DS/boot/battle/`, preserving the other working boot assets.
The small C3BV descriptor binds three arena models and all six actor instances;
its shared Zigzagoon record retains independent pose ownership. Keep `scene.json`
and original/prepared assets private, verify staged/readback hashes, and retain
the local input verification record. The repository manifest contains identifiers,
counts and hashes, not game asset bytes. The current qualified private bundle is
`private/battle-render-v9`; its source/pack/descriptor bindings are recorded in
[battle-scene-manifest.json](battle-scene-manifest.json). Missing scene assets
produce an explicit error; the normal field/title path remains usable.

## Testing

```sh
make build/test_battle_preview
./build/test_battle_preview
./build/test_battle_preview /private/boot
python3 -m unittest discover -s tests -p 'test_prepare_battle_preview.py' -v
python3 -m unittest discover -s tests -p 'test_prepare_battle_scene.py' -v
make build/test_battle_presentation
./build/test_battle_presentation
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
`willie.c3b/.c3i/.c3p`, and opens the **same actor-motion preview/core** used by the field.
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
compare fixed-seed complete turns against original execution; complete original
WZX effects, camera/UI/audio/shadow presentation and validated party/reward writeback. Only that qualified
backend may return a result to native98 and allow the original story script to
own progression flags. The preview must never become a shortcut around these
requirements.

## Optional source-timed Bite attack addon

See [native-wzx-bite.md](native-wzx-bite.md) for preparation of the optional
`boot/battle/bite.c3w` and `bite_attack.c3t`, exact source timing and current
validation. The addon starts the original embedded attack model and floor-color
entry for Umbreon/Bite. It does not supply damage GPT1, sound, original cameras
or other missing battle systems. Native98 and all persistence remain unchanged.
