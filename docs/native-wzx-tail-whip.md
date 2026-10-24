# Native Tail Whip source contract

The optional native path executes the admitted encounter-10 Tail Whip model,
yaw controller, center-null attachment, procedural camera and selected dry sound.
Host fixtures qualify decoding and stage coordination, including both Zigzagoon.
Normal build `5539f43fa3a6` has a [physical checkpoint](evidence/tail-whip-hardware-2026-10-23/README.md):
Tail Whip on B, source sound completion, WIN, return/re-entry/cancel and clean
exit with NTR active. Save and live-body guards are unchanged. Retail
audiovisual parity and full-speed performance remain unqualified. No game
bytes belong in Git. Existing actor/arena packs are reused.

`tools/prepare_wzx_tail_whip.py` checks the GC6E01 disc, parser and source pins
in `config/wzx-tail-whip-source-files.json`. It admits kind 1 group `0x3A9`,
member `0x07332000` from `wzx_shippowofuru_attack.fsys`: archive 51,616 bytes,
SHA-256 `821c575cf7a6c12d67238f80fb139e7d810052ed721aaefabab3350f08255ea1`;
decompressed member 139,068 bytes, SHA-256
`f3dabf6dfd082f2c10f333dcc1afed34f1ea54c454e850f04fe8429a0f6030dc`.
The source dispatcher is `fightWazaWzxTypeFuncAtt` at `0x8020FAC0`, with
variant-zero callback `0x8020FB30`. It loads only kind 1 and executes when its
flag argument is zero. There is no default defender/special sequence.

The root header is `[0,4,0x78,2,5,0]`. Its three entries all start at source
tick zero, without a negative-start normalization:

| Node / offset | Original resource and binding |
| --- | --- |
| 1 / `0xA0` | Model DAT `0x140`, 683 bytes; two joints, no mesh; clip ends at authored frame 31. Owner follows controller joint 1 continuously, taking position and rotation, with scale retained. |
| 2 / `0x400` | Model DAT `0x4A0`, 137,747 bytes; six joints, two meshes, one material; clip ends at frame 45. Selector 16 requests a newly created owner bound-center null, then position-only attachment. |
| 3 / `0x21EC0` | Sound request `0x1DB`, payload flags 3. The admitted common REL maps it to battle-bank FX `0x24E`, macro `0x402`. Selected dry audio is prepared separately. |

`_wazaSequenceModelEntryStart` (`0x801D91EC`) interprets node 1's payload
`[0,0,0,0,1,1,1,683]`. `GSmodelAttachToGSpart` (`0x800E4AC0`) sets attachment
mode and resets the attachment offsets. `modelUpdateAttachments`
(`0x800ED4D4`) defines mode 4 as position plus rotation, mode 1 as position
only. The controller receives the original owner position/yaw and class scale
0.75. All 63 controller samples have zero translation, unit scale and yaw only:
approximately -0.209412 to +0.264909 radians. Pitch/roll are checked as zero;
the exported resource retains complete matrices and rejects another SRT branch.

Selector 16 is **not bone 16**. `fn_801D97F0` calls `GSmodelCenterNull`
(`0x800E6BC8`), which appends a null at the midpoint of the cached original
model bounds. The original frame-zero bound calculation is reused from the
checked geometry path. The admitted Zigzagoon center in native units is
approximately `(0.007567314, 0.276132728, -0.108336426)`. Its position must
follow the controller-driven owner transform. Node 2 keeps its own original
owner rotation and class scale; its position attachment does not inherit the
controller's yaw. `wazaSequenceEntryStop` (`0x801D7E58`) detaches/restores the
owner and removes the temporary center null as the corresponding nodes stop.

Model animation uses source rate 0.5. The model loader fills zero timing slots
with authored frame counts 31/45, but the selected one-shot entries use actual
animation-ended checks. `wazaSequenceSysGetWazaTime` (`0x801DB3F8`) and
`wazaSequenceUpdate` (`0x801DB988`) also impose owner entry-0 timing `[0,98]`,
conditional early termination and sound-running checks. The descriptor records
98 as the owner gate and zero as “no asserted fixed total”; it does not invent
a 90-tick total or equate dry PCM completion with original DSP completion.
The admitted owner idle clip ends at authored frame 49. The loader inserts a
reserved track prefix with kind 1/value 0; `fn_801DD100` writes normalization
zero to runtime entry `field_90`. Motion start therefore takes its duration-zero
branch into `fn_801DF070`, which explicitly sets frame zero and rate 0.5.
The 98-tick owner gate is a separate timing value, not this start-duration field.

The root has no external camera data. Raw flags `0x78` admit motion modes
3/0/1/2; derived camera parameter flags are zero, so FOV uses the original
weighted short-eight table. Move-39 camera preparation is separate. The
controller transform must also be supplied to camera actor/target tracking.

## Private preparation

```powershell
python tools/prepare_wzx_tail_whip.py `
  --disc G:/gamecube-decomp/disc-extract-dump `
  --parser vendor/hsd-importer `
  --output C:/Colosseum3DS-pr7-captures/tail-whip-preparation-v2
```

An explicit `--source-root` can point to a separately pinned source checkout.
The output contains a non-renderable controller motion pack, the actual
drawable C3T, a checked binding descriptor and a JSON source report. The
controller has zero mesh, so no fake vertices or drawable C3T are generated
for it. No original camera, floor, particle or capture effect is invented.

## Binary formats

All fields below are little-endian; reserved bytes are zero. SHA-1 is an
integrity/binding check, not a signature.

`C3TM` version 1 has a 128-byte header and 63 frame records, each containing
24 float32 values: original joint 0's row-major 3x4 matrix followed by joint 1.
Header uint32 fields at offsets 4..28 are version, total bytes, move 39, joint
count 2, sample count 63, source rate 60 and attached joint 1. The source
commit occupies 32..51, controller DAT SHA-256 52..83, payload SHA-1 84..103,
and whole-file SHA-1 104..123 (computed with that field zero). Matrix
translations use the established 0.1 native-unit conversion. These are local
original matrices, without an arena/owner transform already applied.

`C3TW` version 1 is 512 bytes. Header uint32 fields at 4..28 are version,
length, move 39, species 288, owner entry 0, source rate 60 and node count 3.
It binds the source commit at 32, WZX SHA-256 at 52, controller SHA-1 at 84,
drawable SHA-1 at 104 and extracted owner HSD SHA-1 at 124. Offsets 144..160
hold raw flags, body-map selector, SE ID, owner gate and fixed-end sentinel.
The native bound center is three floats at 164, model rate/class scale at
176/180. Three 96-byte node records begin at 192. Each holds 16 uint32 values
(key, kind, attachment selector, position mode, link, source timing index,
target timing index, duration timing index, flags, part, source offset,
normalized start, model end frame, resource role, zero, zero) and eight
original resource payload words. Whole-file SHA-1 is at 480..499, computed
with that field zero. The remaining bytes are reserved.

Synthetic fixtures reject changed headers, payloads, timing, attachments,
nonfinite/non-yaw matrices and source drift. Optional private fixtures check
the resource hashes and load/sample all 91 frames of the actual drawable
through the native C3T decoder, with strict compiler and sanitizer checks.
These conversion checks remain separate from native stage and physical evidence.

## Native stage and completion

Stage admission reads `tail_whip.c3w`, `tail_whip_controller.c3m` and
`tail_whip_effect.c3t`; present invalid files fail closed. An absent
`tailwhip_camera.c3c` is explicitly incomplete. Prepare the camera with
`tools/prepare_battle_camera.py --tailwhip` and the eight-cue audio bundle with
`tools/prepare_battle_audio.py --confusion --tail-whip`. Keep all outputs private.

One typed move-39 event starts one attacker sequence for Zigzagoon slot 2 or 3;
spread-target status events do not duplicate it. The owner retains its scale,
follows the controller yaw and returns to its saved root when the controller
stops. The visible effect follows the current bound-center position, while
retaining its initial yaw and independent .75 class scale. Owner idle starts
at source frame zero and runs at .5, matching the reviewed duration-multiple
branch of `wazaSequencePokemonMotionStart`. The camera uses a separate cosmetic
stream; it cannot consume combat or accepted Bite/Confusion camera RNG.

The stage submits one uniquely identified sound-0x1DB cue. It waits for both
model endpoints, the 98-tick owner gate and actual sound completion; it does
not use a guessed fixed total duration. Native C3T endpoint sampling is the
bounded adapter for model-ended signals; original GSmodel epsilon/update-order
parity remains unqualified. Pause retains clocks. Cancellation restores actor
and camera transforms before resource teardown; repeated/partially loaded
cleanup preserves borrowed-track lifetime.

`tests/test_wzx_tail_whip.c` covers exact descriptors, invalid inputs, retained
elapsed time, attachment, explicit completion and restoration. The optional
actual-resource `tests/test_wzx_tail_whip_stage.c` exercises production stage
loading and both owners with mocked GPU submission and audio completion. It
checks one cue per spread move, delayed completion, pause, transform restoration,
GPU-busy cleanup and repeated close. These mocks do not establish NDSP playback,
PICA raster correctness, hardware lifecycle or retail visual acceptance.

Run the optional stage fixture with `C3_TAIL_WHIP_STAGE_FIXTURE` set to the
private battle directory, then `python -m unittest discover -s tests -p
test_tail_whip_stage.py -v`. No fixture is generated or copied to the console
by that test.
