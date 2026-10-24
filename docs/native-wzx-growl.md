# Growl preparation and source contract

This preparer exports the admitted encounter-10 Growl resources. It does not
qualify native Growl stage execution, application-SRT rendering, sound release
parity or hardware presentation. Converted game resources remain private.

The pinned source is `51bfe4314a218dfaf9592fc80723c50498d0ee55`.
`config/wzx-growl-source-files.json` pins the source functions and canonical
DOL slices used for timing, animation start and teardown. The ordinary Att
dispatcher `0x8020FAC0`/variant callback `0x8020FB30` loads kind 1 only and
executes when its flag argument is zero. The admitted group/member is
`0x3AB/0x07352000`, archive `wzx_nakigoe_attack.fsys`:

| Resource | Bytes | SHA-256 |
| --- | ---: | --- |
| Archive | 248,960 | `3f171e90d3725c2e9172f980ee0e5ae234ce00dbf9e6ab4cf251df676ae63af8` |
| Decompressed WZX | 431,540 | `85862ce50519fc694b179761e5038bab74f4bb2ce9c15b7102a0b0937ef8f82e` |
| HSD at `0x140` | 64,851 | `c2c07e0d6cc2a93505386592dd416f0bf3730c926ec49bb31fc4600610019ac8` |
| GPT1 at `0xFF20` | 365,764 | `64867627ce7029307e3064e1af883cafb52995b7e2834980d7722d328ef043da` |

The raw bank ends at `0x693E4`; the following 28 zero bytes align the sound
node at `0x69400` and are not bank bytes. Its original 113 generator IDs and
REF entries are preserved. The explicit `profile='growl'` preparer opt-in
checks this exact bank hash, root 18 and closure `{18,93,111,112}`. It admits
at most 16 reached generators while allowing this specific 113-entry raw
table. Default profiles retain their previous bounds and pack encoding.

Root 18 spawns references 93/111/112 with F0 kind 7. All reached instructions
are in the existing checked instruction subset, including AC's time plus two
floats. Generator types are 0/8. Generators 93 and 111 have original kind
`0x20000`, which creates a PSAppSRT; accepting their metadata is not proof of
native AppSRT execution. Source F0 inheritance and `psDispSubAppSRT`
(`0x8016CE2C`) must be preserved. Dropping this bit or substituting a generic
billboard would lose original behavior.

Only touched texture groups are converted: group 0's six 64x64 I8 frames and
group 1's nineteen 128x128 I8 frames, 25 textures total. The explicit profile
raises only this touched group's frame bound from 16 to 19. Unreached group 2
contains format 6 data; it remains in the original bank and is neither
converted nor claimed rendered. C3PT retains complete touched groups.

## Original schedule and attachment

The root header is `[1,6,0x200858,2,5,0]`, with no external camera data. The
Zigzagoon owner is entry 1, clip 1, authored end 183, with raw timing points
`[0,130,244,366]`. The implicit zero is preserved.

| Node / source offset | Start | Source behavior |
| --- | ---: | --- |
| Model 1 / `0xA0` | 130 | One-shot position attachment to owner body-map 1, original joint 58; owner base yaw/class scale 0.75 remain. Model clip ends at authored frame 39. |
| Particle 2 / `0xFEA0` | 130 | Root 18, mode `0x10A1`, flags 3; one-shot position attachment to linked effect-model original part 9. |
| Sound 3 / `0x69400` | 130 | SE `0x210`, flags 3; selected sound preparation is separate. |
| Control 4 / `0x6947C` | 130 | Control 0, delay 100, mode 3; crosses its stop threshold at 230. |
| Floor 5 / `0x694F8` | 90 | Duration 120; source segments 20/50/50, neutral `0x80808080` to `0x802D2D2D`, hold, return to neutral. |

The HSD exports 4,032 vertices, 22 batches and 23 sampled matrices. Original
part 9 is retained as sampled matrix 22. This is an effect-model part, not
actor bone 9. Its translation is already in native 0.1 units. Position-only
attachment takes this translation, not its rotation or nonuniform scale.
The model's one-shot owner anchor is sampled at owner frame `130*0.5 = 65`.

Owner animation starts at frame zero. The sequence loader inserts reserved
track 0 `{kind=1,value=0}`; `fn_801DD100` writes normalization zero to runtime
entry `field_90`. `wazaSequencePokemonMotionStart` therefore takes the
duration-zero branch into `fn_801DF070`, which explicitly sets frame zero and
rate 0.5. The timing value 244 is not this animation-start duration; do not
derive a frame-61 start from it.

For animation mode 1 and this nonzero/non-hit point, the original
`wazaSequenceSysGetWazaTime` (`0x801DB3F8`) reads normal time from raw `+0x14`
(244) and minimum time from `+0x18` (366), then adds field 0x10 (zero here).
The control changes the whole sequence stopping status at 230.
`wazaSequenceUpdate` (`0x801DB988`) returns that status even while minimum
366 is pending. It does not impose an invented 366-tick terminal wait.

## Full stop differs from isolated generator deletion

`wazaSequenceApplyStop` (`0x801DBB10`) restores camera state and stops
every entry. It then calls `fn_80118874(bank,1)` for owned particle resources
whose state word is zero; Growl's state is zero. The field instance destructor
first calls `psKillFamily` (`0x801695FC`). That removes all matching-family
particles, releases their AppSRT references and calls `psKillGeneratorID`.
F0 children inherit the family's ID. The later isolated `psKillGenerator`
deferred-reference behavior therefore does not establish surviving Growl
particles after this complete WZX teardown. Do not change the isolated API's
semantics to simulate family teardown implicitly.

Sound stop is different: entry stop clears the shared sound thread handle and
calls `fn_80166B18`, then `fn_80167070(id,1)`, `fn_801669E4` and `_sndStopSE`.
The latter calls `sndFXKeyOff` and releases wrapper work. That is an original
keyoff signal, not evidence of instantaneous PCM silence. Original DSP release
tail and scheduler phase remain unqualified by the dry backend.

Camera raw flags derive parameter flags `0x42`, with motion choices 3/0/2.
Motion 0 takes direct FOV mask 2; motion 2/3 take forced short-table entry 5,
mask 2. These paths consume different cosmetic random draws. The source camera
uses owner body-map 2 (part 53), duration 244. Growl camera admission is a
separate profile; Tail Whip's weighted camera profile is not interchangeable.

## Native execution and qualification

The optional `growl.c3w`, `growl_model.c3t`, `growl.c3p` and
`growl_camera.c3c` resources admit move 45 for either encounter Zigzagoon.
The nine-cue audio pack must include sound `0x210`. The renderer registers
five particle banks; Growl alone uses the explicit sparse-ID/AppSRT profile.
Its reached generators preserve original IDs and share the existing bounded
particle pool. Camera-facing application SRT follows the source inverse-view
basis; ordinary particle rendering continues through the existing path.

Control 0 tears down the effect family, floor, model and camera at tick 230.
The owner motion is independent: owner field 75 is one, so ApplyStop bypasses
forced idle. Clip 1 continues at half rate until authored frame 183 (tick
366), then the original animation-ended callback selects idle at frame zero
and half rate. A new actor action supersedes this continuation. It does not
hold the battle event open until tick 366.

Audio keyoff is issued once against the exact cue identity. Explicitly stopped
and naturally finished cues remain distinct. A successful stop with pending
status waits up to ten seconds of active time; paused time is excluded.
Particle/model/floor/camera teardown occurs immediately while that
acknowledgement is pending. The dry native channel clear does not establish
original DSP release-tail parity.

Actual private-resource stage fixtures pass strict GCC and ASan/UBSan for
Bite, Reflect, Confusion, Tail Whip and Growl. Growl regressions exercise both
owners, source timing, retained-part attachment, application-SRT rendering,
owner continuation and overrides, pending audio, errors and repeated cleanup.
These host checks do not qualify PICA execution, audible release or hardware
visual parity. The shared defender stat-down presentation remains incomplete;
caster WZX admission is not complete Growl or Tail Whip presentation.

## Prepare privately

```powershell
python tools/prepare_wzx_growl.py `
  --disc G:/gamecube-decomp/disc-extract-dump `
  --parser vendor/hsd-importer `
  --output C:/Colosseum3DS-pr7-captures/growl-preparation-v2
```

Outputs are the actual model C3T, C3PT, unchanged raw GPT1, a C3WG descriptor
and a JSON source report. Source bytes, generated textures and models are not
repository fixtures. No actor/arena pack is regenerated.

## C3WG version 1

The descriptor is 1,280 bytes, little-endian. Its 320-byte header binds source
commit, WZX SHA-256, model/particle/owner-HSD SHA-1 and original bank SHA-256.
Offsets 176/180 hold model rate/class scale, 184/188/192 the source owner
part, source model part and retained sampled matrix. Offset 196 is owner
normalization. Offsets 200/212 hold the native owner/model anchor vectors.
At 224 are eight uint32 values: normal time 244, minimum 366, stop 230, node
count 5, table offset 320, model end 39, owner authored end 183, sample count
79. Offset 256 contains floor start/duration, model/particle/sound/control
starts, control delay and root ID. Bytes 288..299 are zero. Whole-file SHA-1
is at 300..319, computed with that field zero; it is an integrity check, not
a signature.

Five 192-byte node records follow. Each holds twelve uint32 fields (key,
kind, normalized start, attachment selector, position mode, link, source,
target, timing index, flags, part, state), sixteen source timing words,
nineteen source payload words (zero-padded), and one reserved zero word.
Source colors retain their original packed values. Unknown headers, source
hashes, payloads, IDs, link arithmetic and nonfinite anchors fail preparation.

Synthetic fixtures cover these rejection boundaries. Optional private tests
verify unchanged bank bytes, descriptor bindings and native load/sampling of
every original model frame under strict and sanitizer builds. Native sparse
particle and AppSRT qualification are separate from this conversion contract.
