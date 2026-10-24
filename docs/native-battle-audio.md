# Selected battle audio: Bite source cues

The private preparer now produces real decoded PCM for game sound requests
`0x85` and `0x86`. These are selected dry offline conversions, not a MusyX
runtime or proof of retail-matched audio. Original banks, WAVs and PCM packs
remain private.

## Original binding

The admitted revision-0 `main.dol` is checked by `verify_disc`; common REL
SHA-1 is `e816761813900aa0fd4f87d012b3b2a19d2f003e`, source contract
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

`fn_80166AB8` indexes `lbl_80478FAC`, published from common REL data
`0x13FE08`, stride 12. `_snd` dispatch calls `sndFXStartEx` using the row's
16-bit field at +4; a game request ID is therefore different from a MusyX FX
ID. The sample-archive table (`snd_sample_table.c`, entry 5) selects bank group
5, `/sound/snd_se_battle.samp`, and these `common.fsys` members:

| Member | Bytes | SHA-256 |
| --- | ---: | --- |
| pool `0x01B10000` | 165200 | `047718d80e7355f5a9e500401f4333c6481e6bed10b7af3e6697808c8d69a498` |
| project `0x01B20000` | 6700 | `427f88c6427ff86f4f6b2e2d0e2dafff3c3f0bdfa5c08d28d48eb8193809df03` |
| sample directory `0x01B40000` | 4540 | `791acef23f8473d32be6c43b385341a1af81d7ab97ad0a7ba370926b39ddcb00` |

The SAMP file is 1325920 bytes, SHA-256
`caa0ad205c1f33f3310ca7b5137ec619aa514e9bc16c3c6d0d2b59d93c44d062`.

| Game request | MusyX FX | Root macro | Reached child macros | Reached sample IDs |
| --- | --- | --- | --- | --- |
| `0x85` | `0xF8` | `0x5B0` | `0x5B1` | `0x1C`, `0x55` |
| `0x86` | `0xF9` | `0x5A3` | `0x5A4`, `0x5A5`, `0x5A6` | `0x69`, `0x65`, `0x1C`, `0x53` |

The project FX table uses ten-byte records. `s_data.c` resolves pool entries
with relative next offsets and an eight-byte entry header. The bounded macro
conversion follows reached child starts, source keys, centered panning,
controller-volume selection, initial waits, and sample-end waits.
`0x86` creates three nested child macros immediately. Two child sample layers
start immediately, the final child sample starts after its source 80-ms wait,
and the parent sample starts after a source 50-ms wait. Unknown opcodes,
recursion, retriggers, sample loops, offsets and unsupported controller settings
fail with a diagnostic.

`synthdata.c::dataGetSample` resolves 32-byte directory records: sample byte
offset +4, root key/rate +12, format/sample length +16, loop fields +20/+24,
and coefficient offset +28. `hw_dspctrl_exact_8015B250.c` specifies eight
signed coefficient pairs and zero initial histories for this ADPCM format.
The converter decodes each eight-byte DSP frame into fourteen signed samples,
validates predictors, preserves history, rounds and clamps the reconstructed
16-bit sample. The private manifest records each actual sample record,
coefficient set and compressed-byte digest.

## Preparation and byte evidence

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump --output private/battle-audio-v1
```

Stage `audio.c3u` alongside `battle/scene.c3v`. Do not publish prepared audio.
The initial output is 32000-Hz mono PCM16:

| File | Frames | Duration | WAV SHA-256 |
| --- | ---: | ---: | --- |
| `sound_0085.wav` | 73032 | 2.28225 s | `dc90e3a051f1a4d4f83e53e7fa05e8e3023469f90089a530029338c196ac5f20` |
| `sound_0086.wav` | 42904 | 1.34075 s | `353c677f7d9c10425c978599cf6871c8cf6dcaba5882f016f97cecdbbf395e95` |

Initial pack SHA-256:
`55b2b8f812eea2c749572e4b7d9421d3223d94d9b9a528e32856c7ff221aa481`.
Both files contain nonzero decoded samples; these hashes prove preparation,
not a physical speaker or recording. Hardware playback acceptance is separate.

The conversion has explicit substitutions: linear resampling and musical
semitone ratios replace the original DSP polyphase SRC/fixed-point pitch
lookup; reverb is absent; original mixer envelope/output gain is unqualified.
The private manifest reports clipping after dry layer summation. These gaps
must remain visible in coverage claims.

## Native lifetime and cue contract

`battle_audio.c` validates the bounded `C3AU` pack, checksum, immutable PCM
records and unique game sound IDs. `battle_audio_player.c` copies PCM into
linear memory, flushes it for DSP access and uses four independent NDSP
channels (4 through 7). It queues actual PCM; it does not merely log a cue.

Open once for the battle directory. Invoke `cue(sequence, sound_id)` only at
its crossed authored WZX sound start. Exact replay of that pair is a no-op;
an older sequence, unknown sound, or fifth overlapping voice fails explicitly.
The sequence identifies the committed presentation, not the sample or FX ID.
Close after battle GPU drain and before field restoration. Close clears wave
buffers before NDSP release and linear-memory disposal, including allocation
failures and cancellation.

The player's APT hook pauses channels for HOME suspension and lid sleep,
tracks nested pause reasons, then unpauses without clearing or requeuing the
wave. The existing libctru NDSP layer preserves DSP sleep state. Manual pause
is independent. A balanced `ndspInit`/`ndspExit` pair is compatible with the
[libctru reference-counted service](https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/ndsp/ndsp.c);
movie audio still closes its own reference after the boot movie. No competing
movie playback is supported during battle.

`c3_battle_audio_player_trace` reports each channel's request/sequence, NDSP
playing/paused state, hardware sample cursor and wave status. This is advisory
playback telemetry; do not equate it with verified audible capture.

Synthetic regressions execute production pack and NDSP lifetime code with SDK
stubs, exercising replay rejection, overlap bounds, failure cleanup, nested
HOME/lid pause without restarting buffers, telemetry and ADPCM bounds. They
also cover signed nibbles, predictor history, saturation and child timing.

## Reflect sound extension

Reflect's actual WZX sound payload is hexadecimal `0x220` (544 decimal), not
220 decimal. The sound node at member `0x07632000` offset `0x16FE0` resolves
through game sound table row544 to bank 5 FX`0x293`, macro`0x7AD`, with nested
children`0x7AE`, `0x7AF`, `0x7B0`. Source sample IDs are`0x55`, `0x79`, `0x56`,
and `0x60`. The final child waits600ms before its sample. All bytes still come
from the admitted battle bank; no synthetic tone or substituted waveform is used.

The selected macro adds centered surround setup and opcode`0x1D` pitch sweep.
`synthmacros.c::mcmdPitchSweep` installs count20 and +500-Hz frequency converted
through `hardware.c::hwFrq2Pitch` at32000-Hz mix rate. `synth.c` low-precision
jobs recur after`0xF00` time units(15ms), decrement the source counter, add the
fixed-point pitch offset, and reset at counter expiry. The offline converter
executes this integer recurrence with its first update aligned to the sample's
first frame. Original within-job/DSP phase is unqualified; this is an explicit
additional substitution alongside linear SRC, dry reverb and mixer-gain gaps.
Unknown sweep profiles fail rather than becoming no-ops.

The preparer's current default output includes all three cues. Initial
`private/battle-audio-v2/sound_0220.wav` contains115886 mono frames at32000Hz
(3.6214375s), WAV SHA-256
`237950685a1fdc0fb2017e3ef6c894f33a26eb2b574b1bdca64c348974e46f63`.
The three-cue pack SHA-256 is
`8dd28903aaca77d4fb72b2a89fa79a78833222c2ce0413becb7821d780a73ea2`.
The original85/86 PCM and WAV hashes above remain unchanged. Reflect sound is
prepared/playable through the same cue API; Reflect sequence integration and
physical audible acceptance remain separate gates.

## Optional Tail Whip eighth cue

Tail Whip remains an explicit extension: `--tail-whip` requires `--confusion`
and appends game request `0x1DB` to the seven-cue bundle. Running without either
option still prepares the existing three Bite/Reflect cues; `--confusion`
alone still prepares its existing seven cues. Neither default silently changes.

The admitted Tail Whip attack root is `wzx_shippowofuru_attack.fsys`, group
`0x3A9`, member `0x07332000`. Its kind-5 sound node key 3 is at member offset
`0x21EC0`, payload `0x21F30`, source words `[0x1DB,3,0]`, normalized start 0 and
flags 1. This identifies the request, not a fabricated audio or owner-lifetime
gate. The WZX owner minimum and sound completion remain separate runtime work.

The original common REL sound row at data `0x13FE08 + 0x1DB*12` selects bank 5,
FX `0x24E`, root macro `0x402`. The converter checks that numeric binding,
centered FX pan/key/volume and the full admitted macro-body hashes before
rendering:

| Macro | Pool entry offset | Body bytes | Body SHA-256 |
| --- | ---: | ---: | --- |
| `0x402` root | 123104 | 80 | `9116a23659a4d0e46a9efa1972118cb3dd8c8a8f73697a236e4bee43f1e3c4af` |
| `0x401` child | 123192 | 72 | `1284da98a5ab921666c3a3132abe5d8053a8fc816eb52123ad779f097245018c` |

The root starts the child immediately. Child `0x401` uses source sample `0x59`,
key 40, volume 127 and vibrato of two keys plus 15 cents over 250 ms. Root
`0x402` uses source sample `0x7B`, key 53 and volume 114. Both begin at 0 ms.
The reached closure uses the existing child/controller/key/sample/wait/vibrato
subset; no new opcode or synthetic waveform was introduced. Sample-end waits
bound this dry conversion; it is not execution of the complete MusyX voice
and post-sample macro lifecycle. Unsupported source drift still fails closed.

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump --confusion --tail-whip --output C:/Colosseum3DS-pr7-captures/tail-whip-audio-new
```

Actual private preparation at `tail-whip-audio-v1` produced `audio.c3u`,
1,314,734 bytes, SHA-256
`28fbba0bccf95ae04c9fe76339a10cb73def4f17d24f48ac3a38b770d736a960`
and SHA-1 `b7c2e0ffc4c5c5e5c616d682f0f36801223abad1`.
All seven prior cue PCM payloads are byte-identical to the admitted seven-cue
baseline; adding a descriptor changes pack offsets/checksum, not those samples.

Tail Whip is 92,279 mono frames at 32,000 Hz (2.88371875 seconds), PCM SHA-256
`40a877ee45ee811fc1f8f383bc32679a2f36c3f0078860e1db0c0361b4d2924d`;
`sound_01db.wav` SHA-256 is
`5490b576217db83bd3ead74487a6bc701d5049b846cd06c33d56897259dbdb36`.
The dry summed output clips 1,083 frames. Original mixer gain/envelopes, DSP
SRC/pitch-table precision, within-job vibrato phase, reverb and retail audible
fidelity remain unqualified. These byte identities prove preparation, not
native Tail Whip integration, audible playback or hardware acceptance.

Synthetic tests cover opt-in selection, unchanged defaults, the eight-cue
bound and rejection of altered mapping/macro bodies. Optional private-pack
regressions use `C3_AUDIO_BASELINE_PACK` and `C3_AUDIO_TAIL_WHIP_PACK` to compare
all seven old payloads and the actual new PCM digest; without originals those
checks skip explicitly.

## Growl cue and source key-off boundary

`--growl` explicitly requires both `--confusion --tail-whip` and appends request
`0x210` as cue nine. The original three-, seven- and eight-cue selections remain
unchanged. `C3AU` version 1's existing count/row layout admits nine bounded
records without a byte-layout change; the native limit is now nine prepared
cues, while active NDSP channels remain exactly four. File/frame bounds,
duplicate-ID rejection, transactional parsing and SDK lifetime rules remain.

The admitted bank-5 sound row maps request `0x210` to FX `0x283`, macro `0x39C`.
Its immediate child `0x39B` starts child `0x39A`, whose sample waits 700 ms:

| Macro | Sample / key / start | Body SHA-256 |
| --- | --- | --- |
| `0x39C` | `0x56` / 62 / 0 ms | `2c223c311b9acb58008e81b08130bac46461cb4c43a70df83554529780a230cb` |
| `0x39B` | `0x73` / 62 / 0 ms | `b618be0dc1cca7e0dfda3af7416a581704a575ce01a96216e37ceaa11660244a` |
| `0x39A` | `0x61` / 75 / 700 ms | `09650cfb359f118fbe20acf59e749510997cbc514e0b1f40cd2ca39e1568019f` |

All three use source volume 127 and centered dry output, retaining their 15%
auxiliary-controller selection as metadata. Existing bounded child/wait/sample
handling renders this closure; no new opcode or reverb synthesis is introduced.

The original Growl sound node is at member `0x69400`, key 3, linked to model key
1. Its node flag is 1; payload `[0x210,3,0]` selects format 3. The loader
reads runtime flags from its final word, zero, so playback uses direct SE, not
a shared sound thread. It starts
at normalized tick 130. Control node `0x6947C` carries `[0,100,3]`, also starting
at 130; its stop threshold is 230. The source owner minimum is a separate gate.
Do not replace source stopping with a wait for the entire decoded wave.

`wazaSequenceApplyStop` calls `wazaSequenceEntryStop` with immediate mode for
each entry. Kind 5 conditionally clears the sound-thread global only when
runtime flag bit 0 is set (it is clear here), then unconditionally calls
`fn_80166B18`. Direct-SE running status uses `wazaEntrySoundRunning` statuses
2/3; raw payload selector 3 is not the runtime flag word. The reached chain continues through `fn_80167070(id,1)`,
`fn_801669E4(id,0,0)`, `_sndStopSE` and `sndFXKeyOff(handle)`. This proves
**source key-off**, not an instantaneous retail PCM cut or a qualified release
envelope. The selected dry backend clears the exact cue's NDSP buffer at a
requested stop; original release-envelope/tail timing remains unqualified.

The additive `c3_battle_audio_player_stop(serial,sound_id,...)` checks both
identity components and touches only their matching active channel. Known
finished/stopped pairs are idempotent; unknown/evicted pairs fail without
altering unrelated voices. `c3_battle_audio_player_status` distinguishes
ACTIVE (0), actual SDK FINISHED (1), explicit STOPPED (2) and invalid (-1).
Existing `done` still returns 1 only for SDK DONE/FREE or exactly retained
finished history; a stopped cue returns -1 with a diagnostic. Paused genuine
completion retains the original guards. Trace telemetry adds `explicit_stop`:
SDK FREE after buffer clearing must not be reported as full-wave completion.
The bounded 64-identity history retains the terminal kind across channel reuse.

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump --confusion --tail-whip --growl --output C:/Colosseum3DS-pr7-captures/growl-audio-new
```

Private `growl-audio-v2/audio.c3u` is 1,435,908 bytes (v1 has identical pack bytes), SHA-256
`611286b47e183b8afd4f23e2d2de32183a50fd00b98a56fa66489258785d076a`,
SHA-1 `ca2663fae9abe2fd9c9d9ef479fb3861b5932011`. All eight prior PCM payloads
match the eight-cue baseline byte for byte. Growl contains 60,571 frames at
32,000 Hz (1.89284375 seconds), PCM SHA-256
`51051ad4a42c91ef578e5812d3b51dc3266219193c1a3173187cb455a5bb3ea2`;
WAV SHA-256 `db26297eb16ba49c4a95de98c8a0bd9221251f604b0d58d3e2253292ba867d6f`.
Its dry sum clips 2,081 frames. Starting at tick 130, the full dry wave would
outlast control tick 230 by about 0.2262 seconds. Explicitly stopping that wave
does not qualify the original key-off tail or mean all 60,571 frames played.

Focused mock-NDSP tests cover exact-pair rejection, unrelated channels,
idempotence, no replay resurrection, terminal history/eviction, pause guards,
stopped-versus-finished telemetry, four-voice capacity, close/reopen and a
synthetic nine-cue pack. Optional original-pack tests use `C3_AUDIO_GROWL_PACK`
with the eight-cue fixture to verify all retained PCM and Growl's digest.
These are host/source preparation checks, not stage integration or physical
audible acceptance. Existing dry SRC/mixer/vibrato/reverb limits still apply.

## Green Defense-down opt-in

`--stat-down` requires `--confusion --tail-whip --growl` and appends only
green Defense-down special `0x29`, SE `0x19B`, as cue ten. Existing three-,
seven-, eight- and nine-cue commands retain their prior selections. Red
Attack-down special `0x27` / SE `0x19A` requires the further opt-in below.

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump `
  --output C:/Colosseum3DS-pr7-captures/stat-down-audio-v1 `
  --confusion --tail-whip --growl --stat-down
```

The pinned common REL sound row resolves bank 5 FX `0x20E`, root macro `0x22D`
and child `0x22E`, using samples `0x56` and `0x59`. Preparation checks both
macro hashes and exact dispatch fields before rendering. The child uses the
existing signed pitch sweep (count100, frequency -200, source low-job 15ms).
The newly admitted root25% auxiliary selection and child 15% selection are
retained as dry metadata; no wet reverb or original DSP/release-tail parity
is implied. Original successful-target ordering and native stage use are
separate from this offline audio contract.

C3AU remains version1 with the same layout; this opt-in prepares
ten cues (the current parser bound is thirteen). The active NDSP voice bound remains four. The actual private
pack is1,539,692 bytes, SHA-256
`557550648f3adaa7d31af0aacb52cd0fe4980c47b70c380c4419669dcbcd4d0c`
(SHA-1 `62de1f554d4f0008ec0c2e5c1a6c8c9aa15a6816`). All nine prior PCM
payloads remain byte-identical. SE19B is51,876 mono frames at 32kHz
(1.621125 seconds), PCM SHA-256
`7de486d78f4fb5f6c3987657e2ffb731089b2d248cd7697ab8b653d4b89f3cf9`.
The dry summed output clips142 frames; no source mixer-gain parity is claimed.
Tests cover opt-in dependencies, exact dispatch/macro rejection,25% auxiliary
admission/unknown rejection, native prepared-cue boundary acceptance/transactional
rejection, and actual nine-to-ten PCM identity. No hardware test follows from
this preparation.

## Red Attack-down opt-in

`--attack-down` requires `--confusion --tail-whip --growl --stat-down` and
appends only red Attack-down special `0x27`, SE `0x19A`, as cue eleven. Every
older flag combination retains its selection and byte-identical C3AU output.

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump `
  --output C:/stat-down-red-audio-v1 `
  --confusion --tail-whip --growl --stat-down --attack-down
```

The pinned REL row maps bank 5 FX `0x20D`, macro `0x22B` and child `0x22C`,
using samples `0x56` and `0x5B`. Exact dispatch and both macro body hashes
are checked before rendering. The existing signed pitch sweep covers the child
(count 127, frequency -20, source low-job 15ms). Root 25% and child 15% auxiliary
selections remain explicit dry metadata; no new synthesis opcode, wet reverb
or original DSP/keyoff/release-tail qualification is introduced. Source gameplay
and stage ordering are separate from this preparation.

C3AU version 1/layout is unchanged; the prepared-cue bound is thirteen, with
four active NDSP voices. The actual private eleven-cue pack is 1,641,592 bytes,
SHA-256 `ab998e7f705142757a837a220c884a71e770afbb998e2277c48abb6f18d9f3b4`
(SHA-1 `4790941bb300ccb6901c1f32db7beeeaf0dec74d`). All ten prior PCM
payloads are byte-identical. SE19A is 50,934 mono frames at 32kHz (1.5916875s),
PCM SHA-256 `f8677eec71e71895b72a9983401193f76d90af50da304919ce359567ea16b809`;
its dry sum clips 59 frames. The WAV SHA-256 is
`3cbc4d72835fd340575197070a740e2a65a2579a0a6e1592e3ea63ff76f27621`.

Focused tests cover dependency rejection, dispatch/macro rejection, actual
ten-to-eleven PCM identity, native cue-boundary load and transactional overflow
rejection. Independent preparation with the previous committed converter verifies
all five older flag combinations produce byte-identical pack output. These are
host/source checks and do not qualify physical red rendering or audible parity.


## Tackle attack/damage opt-in

`--tackle` requires all prior options: `--confusion --tail-whip --growl
--stat-down --attack-down`. It appends Tackle attack SE `0x4A2` and damage
SE `0xDE` as cues twelve/thirteen. C3AU stays version 1 with the same layout;
the prepared-cue bound becomes thirteen, while the active NDSP voice bound
remains four. All six earlier flag combinations retain byte-identical packs,
and all eleven earlier PCM payloads remain byte-identical in the new bundle.

```powershell
python tools/prepare_battle_audio.py --disc G:/gamecube-decomp/disc-extract-dump `
  --output C:/Colosseum3DS-pr7-captures/tackle-audio-v1 `
  --confusion --tail-whip --growl --stat-down --attack-down --tackle
```

Pinned source `51bfe4314a218dfaf9592fc80723c50498d0ee55` maps the exact twelve-byte
REL sound rows to bank 5: attack FX `0x33F`, macro `0x260`, sample `0x54`;
damage FX `0x151`, root `0x62F` and children `0x630`–`0x632`, samples
`0x66`, `0x69`, `0x5D`, `0x55`. Preparation checks every dispatch byte,
FX macro/volume/pan/key, all five macro hashes, existing bank-member hashes,
sample archive and source helper pins. Unknown requests, changed rows/macros
and unsupported controller forms fail closed. Report metadata records source
attack tick 130 and defender-local damage tick 0; stage scheduling is separate.

Attack's 20% auxiliary controller already belongs to the dry subset. Damage's
35% controller is admitted only with explicit Tackle opt-in, only for macros
`0x62F`, `0x630`, `0x631`, and only when their entire source bodies match the
pinned hashes. Child `0x632` retains its existing 15% dry selection. No wet
reverb, original DSP/SRC, release-tail, audible or hardware qualification is
introduced. The damage dry sum clips 2629 frames; attack clips zero.

The actual private thirteen-cue pack is 1,804,740 bytes, SHA-256
`958c4947b0b450385b438dbf4c9d228ef6555f2fd0382102fced958c27a46096`.
At 32 kHz, attack has 12609 mono frames (0.39403125 seconds), PCM SHA-256
`5d2ed07df9300c90489470695ab1cebcaf2043eb1fcf9261888524c11a7b89bf`;
damage has 68933 frames (2.15415625 seconds), PCM SHA-256
`fded562149442b48dca093d95be0f664368e5635b879fdad80ac5c5f2ffc7578`.
Strict and ASan/UBSan host tests cover actual thirteen-cue parsing, transactional
fourteen-cue rejection, eleven-payload identity and exact new PCM digests.
These are offline/source and host checks, not original-game audible parity.
