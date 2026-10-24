# Source battle cameras

Bite (`C3BC`) and Confusion (`C3CC`) retain their original checked descriptors,
entry points and cosmetic stream behavior. Tail Whip uses a separate `C3TC` v1
pack and explicit move 39 entry point; legacy decoders reject it. Tail Whip damage
phase, unknown source flags and external camera resources fail closed.

## Tail Whip source contract

The admitted `wzx_shippowofuru_attack.fsys` member `0x07332000` has header
`[0,4,0x78,2,5,0]`: idle owner entry 0, original body-map 2, no external camera DAT.
The single-phase pack retains all three admitted species bindings and their
original idle source-part tracks (Umbreon 42, Espeon 62, Zigzagoon 53), sampled
at 120 Hz / .5 authored frames per source 60 Hz tick. Idle tracks loop at their
original endpoint. Zigzagoon camera duration is 98 source ticks: the type 2
implicit point 0 is zero and the source uses timing 0 as the fallback.

Flags bits 3/4/5/6 select motions 3/0/1/2 in that order. Previous-motion exclusion
falls through to the next enabled option on the same random draw; only a repeat
in the final bin requires another draw. Do not replace this with uniform rejection
or an eligible-motion list.

- Motion 0 is Dolly. Its first random value selects inward versus outward motion.
  The next value chooses full duration, shortened duration, or a bounded random
  subtraction. The misleading source `randomDelay` variable reduces the duration;
  motion starts immediately. Two distances, height and yaw follow in source RNG
  order. The bounded helper consumes two 16-bit cosmetic draws.
- Motion 1 translates the eye along signed world X. It draws offset 25..35,
  height 1..10, then distance 20..50 in that order. The initial signed X offset is 10;
  the endpoint adds the signed offset. Yaw is zero in world space: do not rotate
  this path by owner yaw.
- Motions 2/3 retain source orbit/still behavior. Camera yaw parameters capture
  owner yaw at begin; target-offset sampling uses the current owner yaw.

Source distances are converted to native units exactly once by .1. Projection
near/far/aspect are inherited. The source camera origin is captured at begin.
The per-frame target-offset callback subtracts current owner root position from
the world source-part position and renews its .2-second move; this keeps the base
origin captured while allowing the relative part offset to follow current yaw.
The Tail Whip stage supplies the real controller-driven owner yaw and restores
its inherited view after completion/cancellation; it does not automatically
follow root translation.

## FOV and RNG

The source short 8 table is at DOL `0x80371F60`, 608 bytes, big-endian SHA256
`19909cc39508937d9f30391c0a47ebde5078d0738a8cd34b031cc74fe635fe95`.
The descriptor stores the exact checked little-endian table. Idle kind 2 returns
after the initial FOV: no second-stage FOV animation is invented. Dolly bypasses
the pattern table and uses the middle FOV mix; other motions use weighted short 8
selection and its initial mask. Raw WZX flags 0x78 are not the derived FOV flags.

The native stream remains separate from combat and from other move cameras.
This preserves source branch/draw structure but does not claim parity with the
original shared random stream or PPC floating-point bits.

## Preparation and checks

Use `tools/prepare_battle_camera.py --tailwhip` with the admitted disc/importer.
Outputs remain private. `--confusion` and default Bite preparation retain their
existing binary profiles. Host checks cover every motion/selection bin, source
Dolly values and RNG order, world-axis translation, FOV bounds, pause, partition
invariance, cancellation and malformed profile rejection. A private actual pack
can be supplied as the fourth camera-test argument. No stage integration or
hardware visual qualification is established by this camera-only change.

## Growl 45 checked camera

Growl has a separate C3GC v1 pack and `c3_battle_camera_growl_load` API. Its exact
WZX header is `[1,6,0x200858,2,5,0]`, without an external camera resource. Derived
camera flags are 0x42: motion options are source order 3,0,2, excluding translation 1.
The same previous-motion fallthrough and Dolly branch are reused; no generic
camera engine or unrestricted flag acceptance is introduced.

Owner entry 1 starts at frame 0 with .5 authored frames per source 60 Hz tick.
For Zigzagoon the original PKX raw timing table is `[0,130,244,366]`. DoPosition
reads `count*4+4`: count 4 selects raw offset 20, which is timing 1=244. It does not
use 130,366, or a normalized damage delay. The action body-map2 source-part track
clamps at its endpoint; the Tail Whip idle track continues looping as before.

Source distances are 30.625..43.75 GC units, bounded height 6..11, and yaw range
.31415927..62831855 around the initial owner yaw, reversed for source-facing
negative owners. Dolly uses direct static middle FOV mask 2. Orbit/still consume
GetPattern's initial random draw but force short-table row 5, also mask 2; its
segment modes 1/0 retain a constant FOV, so no new FOV keyframe animation is invented.
The native cosmetic stream remains independent of combat and other move cameras.

Growl phase/flags/entry/kind/timing admission is explicit. Its decoder rejects
BC/CC/TC packs and existing decoders reject GC. Camera-only tests cover selection
bins, RNG draw counts, bounds, source duration, pause, cancellation, partition
invariance and rejection without mutation. Hardware Growl rendering and stage
integration are separate work; this section claims neither.

## GREEN stat-down special0x29

GREEN down is special `0x29` (decimal41), not a move ID. The dedicated C3SC v1
loader and `c3_battle_camera_begin_special` admit only that special, affected
Umbreon0/Espeon1 and triggering opponent2/3. Existing move entry points and other
special codes reject it. Only two actor records exist; Zigzagoon is not a target
binding in this pack.

The original `wzx_greendown_status.fsys` member0B6F2000 header is
`[0,4,0,2,5,0]` with no external camera. Target-class2 selects idle body-map2:
source part42 for Umbreon and62 for Espeon. Their source idle durations118/180
and complete119/181-sample tracks are checked explicitly. Native geometry remains
in the existing source-to-native .1 unit basis.

Source SequenceCopy inserts a runtime track at8C/90, DD100 writes normalization0,
and both original idle subanimations have motion_type0. MotionStart therefore
uses DF070, which resets authored frame0 and rate.5; the preserve-idle-phase
DEF0C path does not apply. The camera starts at source0, follows the idle track
at .5 authored frames per source60Hz tick, and loops at its original endpoint.

Raw flags0 select CountSheep's all-four fallback in source order3,0,1,2. Derived
flags0 retain the standard source distance/height/yaw bounds, weighted short8
initial FOV, and direct middle-mask Dolly FOV. The numeric source branches fit
the existing qualified single-phase camera helper, but the API/profile namespace
is separate: this is not a Tail Whip move alias. Each special camera must retain
its own cosmetic stream; original shared RNG and outer target overlap/order
parity remain unqualified. Caller cancellation restores the inherited camera.
No stat-down stage, particle, audio or hardware implementation is claimed here.
