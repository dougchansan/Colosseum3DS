# Native battle effects: current execution boundary

The preview now has an executable, bounded GPT1 particle runtime and a Bite
defender integration. This remains a nonpersistent preview, not a completed
retail battle. It never consumes combat RNG or writes party/story/save results.

## Bite

The accepted attacker binding remains floor tick72, model and SE0x85 tick142,
stop tick165. These are the existing admitted local timings, preserved by the
handoff; they must not be described as newly verified retail timing parity.

The defender uses the corrected original runtime sequence table. The importer
omits its implicit point0: the runtime row is `[0,190,358]`. Earliest-entry
normalization adds22, giving this separate damage-local clock:

| Tick | Action |
| --- | --- |
| 0 | Start embedded model, generator3 and floor program |
| 22 | Start owner animation, request SE0x86, release HP gate |
| 50 | Embedded 25-frame model at rate0.5 finishes |
| 160 | Floor hold60/ramp100 completes |
| 380 or later | Owner minimum plus actual model/floor/particle completion releases faint |

The DOL independently confirms runtime timing loads at `0x801DB668` (+0x0C),
`0x801DB678` (+0x14), and normalization addition at `0x801DB82C`. C3WD version2
rejects the earlier importer-indexed version1. The source particle attachment
is damage-model part12, retained native bone11, following its sampled matrix.
Only its basis receives source-to-native scale0.1; translation is already native.

The shared renderer draws original textured quads inside its sole GPU frame.
It preserves original Prim/Env interpolation, trail alpha, mirror wrapping,
depth flags and blend modes. GPU buffers are reused only after nonblocking
FrameBegin succeeds, and borrowed storage survives guarded teardown.

Source camera cuts/orbits and fixed FOV patterns use checked body-map part2
tracks (Umbreon42, Espeon62, Zigzagoon53), rather than guessed native joints.
Damage focus tracks hold for the normalized22 ticks. Camera and particle RNG
are deliberately separate cosmetic streams; original shared-RNG parity is not
claimed. Selected original sound banks are decoded to PCM; see
[the audio qualification](native-battle-audio.md).

## Particle interpreter coverage

Programs retain their original bytes and checked generator references. Reached
unknown instructions fail with generator/PC/op diagnostics. Fixed60Hz updates
retain fractional milliseconds, and large catch-up fails explicitly rather
than discarding elapsed time. Pause consumes no source time.

| Actual program | Executed particles | Peak live | Battle integration |
| --- | ---: | ---: | --- |
| Bite damage3 | 82 | 81 | Implemented |
| Reflect0 and child closure | 2938 | 1369 | Pending sequence/model integration |
| Confusion attack1 | 26 | 24 | Pending |
| Confusion damage7 | 96 | 64 | Pending |
| Confusion damage4 | 512 | 512 | Point/trail rendering unsupported |
| Confusion damage3 | 32 | 28 | Pending |
| Confusion damage2 | 2 | 2 | Pending |

The 1536-particle bound derives from the measured Reflect closure peak. It is
not an emission reduction. Interpreter acceptance does not establish rendered
or original-game visual acceptance. Confusion type12 needs its original model
capture and indirect texture stage, separate from ordinary particle quads.

## Private preparation

Use the admitted disc and pinned importer; outputs remain private:

```powershell
python tools/prepare_wzx_bite.py --disc $disc --parser $parser --output $attack
python tools/prepare_wzx_damage.py --disc $disc --parser $parser --output $damage
python tools/prepare_battle_camera.py --disc $disc --parser $parser --output $camera
python tools/prepare_battle_audio.py --help
```

Stage the generated descriptors and bound packs together in `boot/battle`.
Existing field/title packs are preserved. Present invalid descriptors fail;
absence of optional effects is explicitly incomplete actor-only behavior.

Performance traces separate load wall time, clock/animation sampling, GPU
staging, prior GPU time and full CPU frame time. Memory logs are transition
observations, not continuous high-water measurements. Actual controller,
hardware and original-reference acceptance must retain their own build hashes.
