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
