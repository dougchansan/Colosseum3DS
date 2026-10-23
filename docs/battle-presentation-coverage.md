# Encounter-10 presentation coverage and source closure

This is a source trace, not evidence of rendered original move effects. The
committed event/timeline layer exposes stable move/effect IDs and ordered visual
state changes. It does not decode or execute the original effect scripts,
particles, camera tracks, or sound sequences. Renderer scaffolding must not be
reported as original-effect completion.

Bindings below were read from the admitted GC6E01 revision-0 common REL
(SHA-1 `e816761813900aa0fd4f87d012b3b2a19d2f003e`) and DOL
(SHA-1 `870e8b9693ca780782d80f22a6a4572d8ba9458f`). Source contract:
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

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
For all twelve, original WZX script playback, source camera playback and move
sound playback remain unsupported in this slice. No per-move camera or SE ID
is asserted without parsing its selected sequence.

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
callbacks is not PDT particle decoding/playback. The native preparation path
has no qualified PDT/effect interpreter; embedded members, generator scripts,
materials, blend modes and lifetimes must be decoded and bounded before use.

Sound entry kind 5 calls `fn_80166AB8(resourceId,0,0)` or starts the
`fn_80165668` thread according to flags. Each selected script must therefore
be parsed to extract its SE dependencies, then resolved through the original
sound-bank mapping. The vendored MusyX `runtime/seq.c`, `synthmacros.c`,
`synthdata.c`, `synth_vsamples.c` and hardware layer are available references,
not a native 3DS sound runtime. No qualified offline conversion of the required
sequence/macros/sample banks is currently connected to these events.

`wazaSequenceCamera.c` owns the original camera path. Its pattern selector
`wazaSequenceCameraGetPattern__Fbi` (`0x801D2B4C`) uses the eight-entry
`lbl_80371F60` and thirteen-entry `lbl_803721C0` tables, flags, time and a
fallback random selector. Authored arena cameras are a separate resource.
Any native cosmetic random selection must use an explicitly separate RNG;
calling the original helper against combat RNG would invalidate consumption.

The next concrete effect gate is to parse and hash the table-selected WZX roots
and their embedded model/particle/camera/SE payloads, then implement the required entry handlers
through the existing private preparer/native renderer. Keep unknown handlers
as explicit unsupported errors. Do not treat fixed timeline durations or an
attack clip alone as successful original effect execution.

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
These checks qualify the event boundary, not the unimplemented original effects
or hardware presentation.
