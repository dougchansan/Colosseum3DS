# Native battle effects: current execution boundary

The preview has an executable, bounded GPT1 particle runtime with Bite,
Reflect and a current Confusion integration. This remains a nonpersistent preview, not a completed
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
is damage-model part12, retained native bone11. Its checked POSITION updates
the root emitter; emitted particles remain in source world coordinates. The
renderer applies only the source-to-native scale0.1 with zero translation.

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
| Reflect0 and child closure | 2938 | 1369 | Hardware execution checked on [749708b](evidence/reflect-bite-hardware-2026-10-23/README.md); retail visual parity pending |
| Confusion attack1 | 26 | 24 | Integrated; fresh rendered acceptance pending |
| Confusion damage7 | 96 | 64 | Integrated with overlapping roots |
| Confusion damage4 | 512 | 512 | POINT+TRAIL adapter present; GPU qualification pending |
| Confusion damage3 | 32 | 28 | Integrated with overlapping roots |
| Confusion damage2 | 2 | 2 | Integrated with overlapping roots |
| Confusion damage8 | 96 | 64 | Integrated with overlapping roots |

The 1536-particle bound derives from the measured Reflect closure peak. It is
not an emission reduction. Interpreter acceptance does not establish rendered
or original-game visual acceptance. These counts are isolated interpreter
fixtures, not a measurement of the complete rendered multi-root sequence.
Confusion damage also retains root8 at tick44; resource-sharing state1 does not
mean emitter stop. Its five roots start 2@0, 3@12 and 7/4/8@44 in one system.
The five-root core fixture at seed 123 emits 738 particles, peak 652; root 8's
isolated fixture consumes 576 cosmetic draws. These are distinct workloads from
the line renderer fixture's different launch order and peak 666, not one combined
acceptance measurement.

The [Confusion implementation and contract](native-wzx-confusion.md) now cover
all 15 WZX nodes, `.5` owner animation on a separate 60 Hz event clock, source-part
tracks, scene capture and indirect composite, mixed POINT+TRAIL/quad submission,
and a four-bank particle registry. Attack POSITION is ONESHOT with source flags
`0x0001`; damage POSITION continuously follows with `0x10A1`. Existing emitted
particles remain world-fixed. The new 512×256 capture adds 6,553,600 owned bytes;
source capture was 640×480. Filtering, raster and physical visual parity remain
unqualified.

The seven-cue private audio bundle adds genuine Confusion requests to Bite and
Reflect, with bounded source vibrato/pitch-sweep handling. It remains dry
offline PCM with explicit SRC, mixer, phase and reverb limitations. Build
`749708b3bd33`'s [physical checkpoint](evidence/reflect-bite-hardware-2026-10-23/README.md)
covers its Bite/Reflect route and explicitly incomplete Confusion fallback;
it is not acceptance of the current Confusion implementation or later fixes.

## Private preparation

Use the admitted disc and pinned importer; outputs remain private:

```powershell
python tools/prepare_wzx_bite.py --disc $disc --parser $parser --output $attack
python tools/prepare_wzx_damage.py --disc $disc --parser $parser --output $damage
python tools/prepare_battle_camera.py --disc $disc --parser $parser --output $camera
python tools/prepare_wzx_confusion.py --disc $disc --parser $parser --output $confusion
python tools/prepare_battle_audio.py --disc $disc --confusion --output $audio
python tools/prepare_battle_audio.py --help
```

Stage the generated descriptors and bound packs together in `boot/battle`.
Existing field/title packs are preserved. Present invalid descriptors fail;
absence of optional effects is explicitly incomplete actor-only behavior.

Performance traces separate load wall time, clock/animation sampling, GPU
staging, prior GPU time and full CPU frame time. Memory logs are transition
observations, not continuous high-water measurements. Actual controller,
hardware and original-reference acceptance must retain their own build hashes.

## Corrected native Bite defender binding

The new source-qualified runtime gives the Bite defender model the original
sequence scale `.75`, independently of actor root scale. Particle selector1
uses the linked model part12 POSITION (retained native bone11), converted to
source world units. Only the live root emitter follows that POSITION; previously
emitted particles retain their world positions. Billboard and radius scales are
`.75`, source flags are `0x10A1`, and rendering applies only the `.1` source-to-native
basis with zero translation. Reflect uses the separate `0x18A9` source flags.
The admitted Bite pack proves constant linked POSITION across every baked sample;
a varying pack fails admission until live matrix sampling is implemented.

Evidence labeled `ba428f4` predates this transform correction and cannot
validate the corrected binding. Attack timing/geometry and defender ticks
0/22/380 remain unchanged. Host production fixtures verify the `.75` model scale,
root emitter world POSITION, identity render basis and unchanged 82-particle
closure. Hardware visual acceptance requires a fresh build and capture.

Reflect model position1 follows the same checked live source part POSITION as its
root particle emitter. Its independent sequence scale is `.75`; root yaw is
captured at instance start. Host fixtures compare actual transformed model vertices
against local C3T geometry at two moving source-part times.
