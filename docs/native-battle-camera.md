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

### RED Attack-down special0x27 camera

`--attack-down` prepares `attack_down_camera.c3c` from original
`wzx_reddown_status.fsys` group49B/member0B6E2000. The checked C3SC header
uses special0x27 and internal tag `C3_BATTLE_CAMERA_RED_DOWN_PROFILE`; load it
with `c3_battle_camera_attack_down_load/decode`, then
`c3_battle_camera_begin_special(...,0x27,affected,opponent,...)`.
GREEN0x29 descriptors and move39 descriptors reject this namespace.

The exact RED header is `[0,4,0x78,2,5,0]`. Bits3/4/5/6 explicitly enable
ordered still/dolly/translation/orbit choices3/0/1/2, with source previous-motion
fallthrough exclusion. GREEN flags0 reaches the same choices through the
CountSheep fallback; the descriptors preserve and check this distinction.
Both affected owners reset original idle entry0/frame0, rate.5, no start delay:
Umbreon sourcebodymap2=part42/duration118; Espeon part62/duration180. Camera
Dolly uses direct middle FOV mask2; the other motions choose weighted short8
initial FOV, with no idle segment interpolation. Near/far/aspect inherit the
incoming camera. Cancellation restores it, and pausing advances no camera or
cosmetic RNG clocks. This camera profile alone does not qualify RED stage
playback, retail shared RNG parity, or hardware visuals.

## Tackle33: separate checked camera profile

`tools/prepare_battle_camera.py --tackle` writes `tackle_camera.c3c` and its
source report. Load with `c3_battle_camera_tackle_load` (or the corresponding
transactional decoder), then use `c3_battle_camera_begin_move(...,33,phase,...)`.
The attack admits Zigzagoon slots2/3 targeting Umbreon/Espeon slots0/1; damage
admits the reverse roles. It cannot enter through the legacy Bite API.

The original `wzx_taiatari_attack/damage` archives and members are pinned by
SHA256 against vendor commit `51bfe4314a218dfaf9592fc80723c50498d0ee55`.
Both headers contain raw flags0x78, entry2/8, targetclass2 and no external camera.
SelectMotion enables bits3/4/5/6 in source order3/0/1/2, with previous-motion
exclusion. CalculateParams derives flags0: default rotation18..63degrees,
height clamp6..20GC units, distance20..52.5 after the original0.875 scale/clamp.
This uses the existing bounded four-motion evaluator; no old RNG path changes.

The owner bindings are original entry2 clip4 for Zigzagoon attack, and entry8
clip3/2 for Umbreon/Espeon damage. Original body_map[2] resolves to JObj53/42/62,
independently sampled at animation frame0 plus0.5frame per source60Hz tick.
Both owner starts and normalization shifts are zero. DoPosition selects the
count-dependent timing field, giving attack130ticks and damage170/100ticks.
The later raw damage minimum298/176 is not the camera motion duration.

C3KC version1 retains the checked512-byte header/actor block, followed by the
exact608-byte short8 FOV table at512, the988-byte long13 table at1120, and
contiguous source part tracks at2108. Both LE table SHA1s and the whole-file
checksum are checked. Runtime admission checks all six source timing/sample
counts; it rejects mismatched phase/owner roles without changing the camera.
The program owns21rows of table storage; existing serialized formats remain
unchanged.

Dolly uses direct FOV mask2. The other motions select weighted short8 for
attack and long13 for damage, preserving the source initial random draw,
initial FOV mix, and two dynamic segments. Their gates are implicit0 then
62/130 for Zigzagoon attack,170/298 for Umbreon damage or100/176 for Espeon.
Source duration modes2/5/12/13, timing placement, and near/mid/far radius use
are evaluated explicitly. All8 short rows and11 reachable long rows are
covered; the long table reaches cumulative weight1 at row10, so its later
fixed-choice rows are not reached by these flags.

Verification used the admitted original disc/importer. All six previous packs
were regenerated and were byte-identical to the5519c82 hardware asset set.
Strict C11 and ASan/UBSan runs with all seven actual camera packs passed
4,116,755 checks, including all four motions, reachable FOV patterns,
chunked/batched clock equivalence, pause/cancel, malformed/checksum-repaired
inputs, profile isolation and actor-role rejection. Preparation tests verify
both exact table hashes and Tackle timing rejection. Private evidence lives in
`tackle-camera-v1` and `tackle-camera-legacy-regression-v1` in the capture root.
The cosmetic RNG remains independent of the retail shared stream; PPC float,
thread/frame-edge and hardware visual parity are not claimed.

The stage handoff also uses `c3_battle_camera_step_units`:1000units per original
tick,60per millisecond. It retains the fractional elapsed after attack tick133
without rounding or advancing the defender camera for a whole extra millisecond.
The old millisecond entry point delegates exactly;20+40units equals1ms,
paused clocks ignore elapsed, and overflow preserves camera state.
