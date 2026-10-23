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
through game sound table row544 to bank5 FX`0x293`, macro`0x7AD`, with nested
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
