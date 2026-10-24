# Encounter-10 presentation coverage and source closure

The [e996 hardware checkpoint](evidence/confusion-hardware-2026-10-23/README.md)
adds actual Confusion execution against both Zigzagoon, complete source-root
and NDSP gates, unchanged state/save guards and clean exit with NTR active.
Its sampled CPU timings and 82/revise visual comparison leave performance and
retail parity unaccepted.

The [Growl caster slice](native-wzx-growl.md) now has a physical checkpoint at
build `2e9ec7108d16`: Zigzagoon A's Growl announcement, actor motion, curved
peach-pink glow and spark particles, followed by WIN, field return,
reentry/discard and exit. Reentry and exit recovery overlays remain part of that
record, not uninterrupted-transition acceptance. Its checked source schedule
starts model, root-18 AppSRT particles and SE `0x210` at tick 130 and applies
family/model/floor/camera stop at tick 230; source owner motion continues
independently. Hardware playback and explicit stop do not establish original
release-tail timing or audible mixing in the silent recording.

This is a source and bounded-execution trace, not evidence of complete encounter presentation.
The optional [Bite slice](native-wzx-bite.md) executes its admitted attack and
defender model, floor, GPT1, sound and camera bindings. Reflect executes its
sequential caster/ally model, live attachment, floor, particles and sound bindings.
The [Confusion slice](native-wzx-confusion.md) now implements its checked
15-node attack/damage contract, source part tracks, floor, overlapping GPT1
roots, capture/indirect composite, mixed POINT+TRAIL primitives and selected
audio. Its separate `C3CC` camera descriptor supplies move-93 procedural
motion/FOV bindings, including the defender's 44-tick source-track delay.
Build `749708b3bd33` completed both effects, a preview WIN, return, re-entry/cancel
and clean exit on hardware with NTR active on October 23, 2026 (HST). Its trace
and byte-identical preview guards qualify that bounded run; they do not establish
retail visual parity, performance acceptance or complete encounter coverage.
The 749 build used Confusion's incomplete fallback. Its acceptance remains
historical; the later e996 checkpoint separately qualifies the implemented
Confusion path on hardware. Other moves retain explicitly incomplete presentation.

Bindings below were read from the admitted GC6E01 revision-0 common REL
(SHA-1 `e816761813900aa0fd4f87d012b3b2a19d2f003e`) and DOL
(SHA-1 `870e8b9693ca780782d80f22a6a4572d8ba9458f`). Source contract:
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

The optional [Tail Whip path](native-wzx-tail-whip.md) additionally implements
the admitted Zigzagoon controller/model, center-null attachment, source camera
and selected dry sound. Its decoding/stage fixtures pass. A separate
[5539 hardware checkpoint](evidence/tail-whip-hardware-2026-10-23/README.md)
qualifies Tail Whip on B, WIN, lifecycle and state preservation; the earlier
e996 checkpoint does not cover it. The [original Tail Whip reference and
comparison](evidence/tail-whip-original-reference-2026-10-23/README.md), published
at documentation checkpoint `99d6d4a`, identifies the original cyan-ring caster
effect and separate green defender stat-down streaks. Its 76/100, revise verdict
establishes a category match, not matched camera/effect-age or retail parity.
`99d6` adds reference evidence; it is not a replacement hardware binary identity.

| Presentation slice | Current bounded qualification |
| --- | --- |
| Bite / Reflect | Admitted source execution and separately identified hardware checkpoints; no complete retail-parity claim. |
| Confusion | Implemented source-root/camera/composite/particle/audio closure; e996 hardware checkpoint covers both Zigzagoon targets. |
| Tail Whip | Caster controller/model/camera/selected sound; 5539 hardware B-slot checkpoint and 99d6 original-reference comparison remain separate evidence. Source5b additionally qualifies successful green Defense-down on both player targets. |
| Growl | Caster model, root-18 sparse-ID/AppSRT particles, floor, camera and selected sound/keyoff; 2e9 hardware A-slot glow/particle execution. Source5519 additionally qualifies red Attack-down on both player targets. |
| Seven remaining moves | Tackle, Return, Secret Power, Taunt, Snatch, Helping Hand and Struggle have incomplete original presentation; source camera/audio bindings and rendered-actor coverage vary by move. |

The [5b green Defense-down checkpoint](evidence/stat-down-hardware-2026-10-23/README.md)
adds shared special0x29 particles, actor color, source target cameras and direct
SE0x19B on Umbreon and Espeon. Both sequences and their restoration completed
on hardware; source guards and save bytes remained unchanged. The original
reference comparison is84/revise, so this is execution/category acceptance,
not retail visual parity. The [5519 red Attack-down checkpoint](evidence/attack-down-hardware-2026-10-24/README.md)
adds special0x27 particles, actor tint, source target cameras and directSE0x19A
on both player targets. Both red sequences restore color/camera and finish their
cues. Original RED visual/audio parity and full encounter presentation remain
incomplete.

## Move bindings

Move rows start at REL file offset `0x184C + 0x11C7C4`, stride 56.
`waza.c`'s `wazaDataBiosGetWazawzxdataId` (`0x8011C7D8`) reads
row `+0x30`; the combat effect field at `+0x1A` is a different identifier.
The dispatcher selector at `+0x1C` indexes DOL `lbl_8035C430`, stride 20;
its last two words are WZX type and variation callbacks.

`sequence.c::fn_801DDEE4` resolves ordinary variant-zero kind 1/2/3 through
`lbl_8036E150`, stride 28, three group/member pairs at row `+4`.
Archive names below come from `GSfsys.toc`, never inferred from move names.
These are the default attack/damage/special roots. The companion encounter
source trace checked the variant-zero species overrides for internal species
197, 196 and 288 against these twelve moves and found no matching override.
`lbl_80373750` (DOL file `0x370750`) has 16-byte rows: variant at +0,
PKX ID at +2, WZX move ID at +4, resource group/member at +8/+12. The
eligible scans are `[1,0x11C)` for kind 1, `[0x11E,0x12A)` for kind 2,
and `[0x12C,0x15E)` for kind 3. Nonzero-variant callbacks and overrides in
`lbl_80373210` remain unqualified. This qualifies variant-zero default roots
for the admitted encounter actors; it does
not qualify the scripts' embedded dependency closure or other species/moves.
Zero means no default root for that kind, not absence of all visual behavior.

| Move (ID) | Combat effect | WZX ID / selector | Type / variation callback | Default roots: kind group/member archive |
| --- | --- | --- | --- | --- |
| Bite (44) | 31 | 44 / 44 | `0x80211040` / `0x8021115C` | 1: `0xB6/0x034B2000` `wzx_kamituku_attack.fsys`; 2: `0x237/0x04E32000` `wzx_kamituku_damage.fsys` |
| Secret Power (290) | 197 | 290 / 290 | `0x80211040` / `0x8021115C` | 1: `0x555/0x0CA82000` `wzx_himitunochikara_attack.fsys`; 2: `0x556/0x0CA92000` `wzx_himitunochikara_damage.fsys` |
| Taunt (269) | 175 | 269 / 269 | `0x80211040` / `0x8021115C` | 1: `0x2D5/0x05842000` `wzx_chohatu_attack.fsys`; 2: `0x2DF/0x058E2000` `wzx_chohatu_damage.fsys` |
| Snatch (289) | 195 | 289 / 289 | `0x80210E5C` / `0x80210EC0` | 1: `0x2E5/0x05942000` `wzx_yokodori_attack.fsys`; 2: `0x2E6/0x05952000` `wzx_yokodori_damage.fsys`; 3: `0x66A/0x11502000` `wzx_yokodori_special.fsys` |
| Confusion (93) | 76 | 93 / 93 | `0x80211040` / `0x8021115C` | 1: `0x3CC/0x07532000` `wzx_nenriki_attack.fsys`; 2: `0x277/0x05292000` `wzx_nenriki_damage.fsys` |
| Return (216) | 121 | 216 / 216 | `0x80210074` / `0x80210118` | 1: `0x427/0x07AA2000` `wzx_ongaeshi_attack.fsys`; 2: `0x2A8/0x05582000` `wzx_ongaeshi_damage.fsys` |
| Reflect (115) | 65 | 115 / 115 | `0x80210888` / `0x80210990` | 3: `0x3DE/0x07632000` `wzx_reflector_special.fsys` |
| Helping Hand (270) | 176 | 270 / 270 | `0x8020F7B8` / `0x8020F8CC` | 1: `0x450/0x07D12000` `wzx_tedasuke_attack.fsys`; 3: `0x629/0x109D2000` `wzx_tedasuke_special.fsys` |
| Tackle (33) | 0 | 33 / 33 | `0x80211040` / `0x8021115C` | 1: `0x3A4/0x072E2000` `wzx_taiatari_attack.fsys`; 2: `0x230/0x03A42000` `wzx_taiatari_damage.fsys` |
| Growl (45) | 18 | 45 / 45 | `0x8020FAC0` / `0x8020FB30` | 1: `0x3AB/0x07352000` `wzx_nakigoe_attack.fsys` |
| Tail Whip (39) | 19 | 39 / 39 | `0x8020FAC0` / `0x8020FB30` | 1: `0x3A9/0x07332000` `wzx_shippowofuru_attack.fsys` |
| Struggle (165) | 48 | 165 / 165 | `0x80211040` / `0x8021115C` | 1: `0x3FF/0x07842000` `wzx_waruagaki_attack.fsys`; 2: `0x29A/0x054A2000` `wzx_waruagaki_damage.fsys` |

## Exact remaining execution dependencies

For all twelve moves, combat is supported by the experimental preview coordinator;
typed announcement, HP, status, miss, critical and faint events are available.
Complete encounter-wide WZX, camera and sound coverage remains unsupported.
Bite, Reflect, Confusion, Tail Whip and Growl have the bounded execution
described above. The remaining seven entries retain incomplete original-effect
presentation: Secret Power, Taunt, Snatch, Return, Helping Hand, Tackle and
Struggle. Their source bindings, selected cameras/audio and rendered-actor paths
must be qualified independently; a table entry is not rendered-effect evidence. Original send-outs/ball and light effects, trainer
commands, HUD/font assets and battle shadows also remain incomplete. The native
preview overlay is not the original HUD. No per-move camera or SE ID is asserted
without parsing its selected sequence, and no complete retail visual or
performance acceptance follows from the implemented slices.

The original normal dispatcher `fight_waza.c::fightWazaWzxTypeFuncNormal`
(`0x80211040`) calls `fightOutPokemonLoadWazaEffect` and
`fightOutPokemonWazaEffect` separately for attacker kind 1 and defender kind 2,
with targeting-dependent suppression. Reflect, Helping Hand, Growl/Tail Whip,
Return and Snatch use the distinct callbacks recorded above; replacing all of
these with one generic flash loses original behavior.

`fight_out_pokemon_suffix_80207C6C.c::fightOutPokemonLoadWazaEffect`
(`0x80208750`) gets WZX ID via `wazaGetStatus(...,0x1F,...)` and calls
`sequence.c::fn_801DDD28`. That loader resolves resource pairs, obtains the
FSYS member and invokes `wazaSequenceLoadData` (`0x801DC014`). The latter
parses common entry timing/links then dispatches model, particle, effect, sound,
and camera payloads. A model importer alone does not execute that data.

`wazaSequenceEntry.c::wazaSequenceEntryStart` has distinct entry kinds:
2 model, 3 particle, 4 effect and 5 sound. Particle start allocates through
`fn_801190DC` and uses original attachment/position/scale/rotation helpers
`fn_80118F04`, `fn_80118DE0`, and `fn_80118E8C`. Preserving PKX particle
callbacks alone is not particle decoding/playback. The native path now has a
bounded [GPT1 interpreter](native-battle-particles.md), checked bank/texture
registry, overlapping root emitters and the admitted quad/POINT+TRAIL adapters.
Bite, Reflect and Confusion execute their selected retained programs; unknown
reached instructions and unsupported raster profiles fail explicitly. This is
not a general PDT/WZX effect engine: other embedded members, scripts, materials,
blend modes and lifetimes still require their own decoding and execution gates.

Sound entry kind 5 calls `fn_80166AB8(resourceId,0,0)` or starts the
`fn_80165668` thread according to flags. Each selected script must therefore
be parsed to extract its SE dependencies, then resolved through the original
sound-bank mapping. The vendored MusyX `runtime/seq.c`, `synthmacros.c`,
`synthdata.c`, `synth_vsamples.c` and hardware layer are available references,
not a native 3DS MusyX runtime. A bounded [offline audio conversion](native-battle-audio.md)
and NDSP player are now connected to the admitted events: Bite `0x85/0x86`,
Reflect `0x220`, Confusion `0x443/0xE9/0x472/0x473`, Tail Whip `0x1DB`
Growl `0x210`, green Defense-down `0x19B` and red Attack-down `0x19A`. These eleven requests use
real admitted macros/samples with checked cue identities and buffer lifetime.
Dry mixing, linear SRC, source vibrato/sweep timing and original DSP/mixer/reverb
precision limits remain explicit; neither cue telemetry nor a silent recording
establishes audible retail fidelity. Encounter music `0x447` remains separate.

`wazaSequenceCamera.c` owns the original camera path. Its pattern selector
`wazaSequenceCameraGetPattern__Fbi` (`0x801D2B4C`) uses the eight-entry
`lbl_80371F60` and thirteen-entry `lbl_803721C0` tables, flags, time and a
fallback random selector. Authored arena cameras are a separate resource.
Any native cosmetic random selection must use an explicitly separate RNG;
calling the original helper against combat RNG would invalidate consumption.

The bounded Bite camera and Confusion `C3CC` paths now retain their selected
original pattern tables, body-map POSITION bindings and owner-clock delays.
Confusion's checked WZX `C3CS`, type12/mask `C3CF`, camera `C3CC`, two particle
packs and seven-cue audio are optional additions to the base scene, not evidence
that arbitrary move cameras/effects are supported. A present inconsistent pack
fails; absence must remain explicitly incomplete presentation. Preserve matched
descriptors and dependencies rather than installing one pack in isolation.

For the remaining seven moves, the next gate is still to parse and hash their
table-selected WZX roots and embedded model/particle/camera/SE payloads, then
implement their required entry handlers through the private preparer/native
renderer. For the current five slices, source-reference comparison, physical
GPU/audio acceptance and measured performance retain their own exact build and
asset identities. Keep unknown handlers as explicit unsupported errors. Do not
treat fixed timeline durations or an attack clip alone as successful original
effect execution.

## Verification

The host preview suite passes 1,355,632 checks (synthetic runs: 54 wins, 46 losses),
including typed sequence/HP metadata checks. Every turn of all 100 complete
synthetic encounters compares the entire combat state (including RNG seed and
draw count) with presentation disabled and enabled independently at 1, 17 and
60,000 millisecond steps. Event callbacks must start and finish each committed
sequence exactly once; final visual HP, faint visibility and outcome must match
combat. Both strict and ASan/UBSan runs pass. The independent timeline suite
passes strict C11 warnings and AddressSanitizer/UndefinedBehaviorSanitizer,
covering pause, exactly-once consumption, HP-before-faint visibility, replay
rejection, step-size independence and a bounded unresolved-resource wait.
These checks qualify the event boundary, not every original effect or hardware
presentation. Production Confusion fixtures separately cover 15-node admission,
owner rate, all five overlapping damage roots, live/frozen POSITION semantics,
capture pass classification, indirect sampling, POINT+TRAIL handling and sound
dependencies. Their bounded source/host coverage and precision limits are
recorded in [the Confusion contract](native-wzx-confusion.md); they do not qualify
the remaining seven moves, original HUD/shadows, unqualified original status visual/audio parity,
full send-outs or retail performance.
