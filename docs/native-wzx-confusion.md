# Native Confusion source contract and qualification boundary

The current working tree implements a bounded Confusion presentation path for
Espeon and the admitted Zigzagoon defender: original WZX entry timing, source
part tracks, floor changes, GPT1 roots, selected sound requests, type12 scene
capture and indirect lookup, and the POINT+TRAIL particle adapter. This is a
nonpersistent preview. It does not execute combat effects, award a result, or
advance the waiting encounter-10 story request.

Normal build `e9968de76e97` has a [physical execution checkpoint](evidence/confusion-hardware-2026-10-23/README.md):
Confusion completes against both Zigzagoon, with all five damage emitters and
actual NDSP completion gates. The route includes Bite, sequential Reflect,
WIN, field return, re-entry/cancel and clean exit with NTR continuously active.
Executable/assets are read back and hash-verified; both live-body guards and
the saved GCI remain unchanged. This qualifies bounded execution and lifecycle,
not retail visual/audio fidelity or full-speed performance. The earlier
`749708b3bd33` [checkpoint](evidence/reflect-bite-hardware-2026-10-23/README.md)
used the explicitly incomplete Confusion fallback and remains separately scoped.

## Admitted source and files

The source contract is Count-Sheep/pkmn-colosseum commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`, with GC6E01 revision-0 DOL SHA-1
`870e8b9693ca780782d80f22a6a4572d8ba9458f`. The importer pin is
`dc71de837978a6a2fe8c5082f2840cfafb135235`. Move 93 uses these original members:

| Phase | Archive / member | Archive SHA-256 |
| --- | --- | --- |
| Attack | `wzx_nenriki_attack.fsys` / `0x07532000` | `ce7df34845f8cc5520242dec14538ee2c1c662e9b4217cc1002393eff3c0885d` |
| Damage | `wzx_nenriki_damage.fsys` / `0x05292000` | `b4bc2e6b2c5332cf2afc7a8cbfa26d86f9b37b69625be4d0de2143c64b1c3920` |

`prepare_wzx_confusion.py` checks the original member, node, GPT1 and mask
identities. `C3CF` retains the two type12 profiles and the original 128×128 I8
mask. `C3CS` retains all 15 typed entry nodes, separate particle-pack identities,
and four checked source-part POSITION tracks. Neither descriptor substitutes a
compact C3T envelope index for an original source joint. Resource-sharing
`state=1` is a resource key, not a command to stop or omit an emitter.

The type12 source chain is `fn801364A8` case12 → allocation `fn8013F000` →
initialization `fn8013F114` → `distortionEffectStart` (`0x8013F344`), update
`fn8013F80C`, render `fn8013F410` and matrix update `fn8013F97C`. The preparer's
manifest pins the reached source files and records these dependencies; reaching
the original callback names is not a claim that the complete effect engine is
ported.

The recorded `private/confusion-sequence-v5` outputs are:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `confusion_sequence.c3s` | 13048 | `a2bd7564e9b1a04343cc6002782dd71bf26bf4a19de3bea70c491d20fe8c2f8f` |
| `confusion_type12.c3f` | 16640 | `21a0ebb2cb2b06decd1b69ac365f83e0c375b616529f8ec0929a5812d02b6e4b` |
| `confusion_attack.c3p` | 414688 | `c4fb9eab25cf99ed5814e34eb6ab7873204de336afd1975bc7ef1536a500d4bb` |
| `confusion_damage.c3p` | 160672 | `9570f60e0793e8ebc194c8f6cc07b16338dcbe4c225ab47a8980a2c94703aeef` |

The same four byte identities are staged in `private/confusion-integration-v1`.
Those preparation/host-fixture paths alone are not deployment evidence. The
e996 hardware checkpoint separately verifies these installed byte identities,
the C3CC camera and seven-cue audio pack. Original archives and prepared assets
remain private.

## Source clocks, roots and attachment

Authored WZX events advance at 60 source ticks per second. Owner animation uses
rate `.5`, independently of that event clock. The preparer checks DOL constants
`0x8047E348` and `0x8047E3C8`, used by the owner/direct-motion helpers. Source
POSITION tracks are baked at 120 samples per source-animation second and
interpolated separately from the event clock. The stage retains fractional
source time instead of rounding each caller update to a frame.

The tracks are Espeon idle and attack entry 1, source joint 62, and Zigzagoon idle
and damage entry 8, source joint 53. Type12's body-map selector is the original
idle table's index 2. Owner/root yaw, scale and original POSITION conversion
remain separate from particle raster scale.

| Phase / source tick | Retained entries |
| --- | --- |
| Attack 44 | GPT1 root 1; floor program; type12 for 90 ticks; sound `0x443` |
| Attack 130 | Original kind-1 node, resource words `[0,15,3]`; owner minimum ends at 145 |
| Damage 0 | GPT1 root 2; floor program; type12 for 100 ticks; sound `0x472` |
| Damage 12 | GPT1 root 3; sound `0xE9` |
| Damage 44 | GPT1 roots 7, 4 **and 8**; sound `0x473`; owner motion/HP gate |
| Damage 402 or later | Owner minimum plus outstanding source work/particles must complete before release |

This table groups all five attack nodes and ten damage nodes; nodes sharing a
tick retain source entry order. The five damage roots overlap in one system,
sharing its pools, clock and cosmetic RNG. Starting root 8 must not overwrite
or silently replace roots 2/3/7/4.

Attack particle flags are `0x0001`; the original attachment is ONESHOT: capture
its POSITION once at start. Damage flags are `0x10A1`, with CONTINUOUS POSITION
following for each root emitter. Following changes the emitter used for future
particles, not particles already emitted into source world coordinates.
Billboard/radius flags remain source-driven. Rendering applies the `.1`
source-to-native basis with zero translation; it does not attach the whole
emitted cloud again to a moving actor.

## Capture, indirect stage and particle primitives

The scene renderer owns capture, main scene, indirect composite and particles
inside its single GPU frame. Capture redraws the visible model set before the
main scene; the composite precedes particle submission. Original MObj
`render_mode & 0x60000000` selects OPA, TEXEDGE and XLU in source pass order
`1,4,2`. Native blend/alpha-test state cannot replace this classification:
custom pixel-engine overrides are independent and remain applied to the draw.
The source-invalid category `0x20000000` fails closed during capture.

The native capture is 512×256 RGBA8 plus D24S8, adapted from source 640×480.
Its 128×128 indirect grid uses 98,304 unshared vertices. The incremental owned
storage is **6,553,600 bytes**: 5,505,024 immutable vertex bytes and 524,288 bytes
each for color and depth. This is capture storage, not total application peak
memory or total particle/asset storage. Buffers are reused only after successful
nonblocking FrameBegin and retained until guarded GPU teardown completes.

The CPU type12 sampler follows the source mask tiling, nearest mask lookup,
integer indirect displacement and linear/clamped capture lookup. GPU grid UV
interpolation differs from separately truncated GX base UV by less than 1/128
capture texel. This bound is not bit-exact PICA/GX filtering, raster coverage,
capture-resolution or original-game visual parity.

Damage generator 4 reaches POINT+TRAIL. The mixed particle submission path must
retain ordinary quads alongside source line primitives; it cannot replace the
line with a billboard or discard the other active roots. The line adapter keeps
previous/current endpoints, source pixel width, trail alpha, clipping, texture
and depth/blend state. GPU cap/edge rules, subpixel snapping, exact interpolation
and the 45-degree major-axis tie remain qualification limits. The renderer's
four-bank registry accommodates Bite damage, Reflect, Confusion attack and
Confusion damage without interpreting a missing texture/bank as success.

## Audio and preparation

The optional `confusion_camera.c3c` adds the original procedural attack and
defender cameras. Both original headers have zero external camera resources;
this is not a fabricated camera DAT. The separate C3CC contract retains the
13-entry long FOV table, weighted selection and dynamic FOV segments. Attack
uses the Espeon motion part; damage uses the Zigzagoon part with normalized
44-tick motion delay. A separate cosmetic stream preserves accepted Bite RNG.
Pause retains time and cancellation restores the inherited view. An absent
camera pack is explicitly incomplete; a present invalid pack fails closed.

The checked private camera descriptor is 22,524 bytes, SHA-256
`6818a7908bc9ca4f392b6815681af191e9046fb35b7f44e71b57bb633d19a6a2`.
Its source fixtures do not establish original shared-RNG, PPC floating-point or
rendered visual parity. Bite's existing camera descriptor remains byte-identical.

The complete local bundle needs seven selected game requests: Bite `0x85/0x86`,
Reflect `0x220`, and Confusion `0x443/0xE9/0x472/0x473`. The Confusion requests
map to bank-5 FX `0x306/0x15C/0x321/0x322`. They are decoded from admitted source
macros/samples, not substituted tones. `private/battle-audio-confusion-v2/audio.c3u`
is 1,130,144 bytes, SHA-256
`61d83d60dd2ab6d7a2291b7f067e7a680a806e5aa5cba0b83f2b9b417649de7d`.
It matches the audio staged in `private/confusion-integration-v1`.

This remains selected dry 32,000-Hz mono PCM. Pitch sweep/vibrato use the source
integer recurrence at 15-ms low-job cadence with offline first-sample phase.
Linear SRC and musical semitone ratios replace original DSP polyphase SRC and
fixed-point pitch lookup. Original within-job/DSP phase, mixer gain/envelope,
reverb and wet mix remain unqualified. `0x472/0x473` auxiliary/reverb selections
are retained as dry metadata; no reverb tail is rendered. See also
[the audio contract](native-battle-audio.md).

```powershell
python tools/prepare_wzx_confusion.py --disc G:/gamecube-decomp/disc-extract-dump --parser G:/Colosseum3DS-story-hardware/vendor/hsd-importer --output private/confusion-new
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump --confusion --output private/battle-audio-confusion-new
python tools/prepare_battle_camera.py --disc G:/gamecube-decomp/disc-extract-dump --parser G:/Colosseum3DS-story-hardware/vendor/hsd-importer --confusion --output private/confusion-camera-new
```

Every output directory must be new. Keep manifests with the private outputs.
Stage all four Confusion packs and the matching seven-cue audio alongside the
admitted battle scene, actors, existing Bite and Reflect resources. A partial or
inconsistent present contract fails; fallback actor-only behavior must remain
explicitly incomplete. Preparation does not deploy assets or touch saves.

Host fixtures exercise production descriptor/stage/renderer paths, moving source
parts, event crossing, overlapping roots, cancellation and frame ownership.
Corrected attachment/lifetime and elapsed-time partition regressions remain
separate from raster and audiovisual parity. Actual e996 hardware visibly
renders the ring, POINT/trail particles and mixed target burst. Its reference
comparison scores 82/revise: synchronized camera/pose/effect-age comparison,
actual PICA/GX raster/filtering parity and audible fidelity remain required.
Other encounter moves retain their separately documented incomplete coverage.
