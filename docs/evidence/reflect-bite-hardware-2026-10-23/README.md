# Physical Bite and Reflect checkpoint

Normal SD-launched build `749708b3bd332bc013a15e69a3cb3f4183276a9d`
(`749708b3bd33` on screen), tested October 23, 2026 HST / October 24 UTC.
The installed executable was retrieved after the run: 494,868 bytes,
SHA-256 `e70b5093b000abb6247552526355fbf40962f651794df9d124bb7722af76f04d`.
CI [40055623883](https://github.com/dougchansan/Colosseum3DS/actions/runs/40055623883)
passed for this source. Later changes are not covered by this hardware run.

The route was saved WES Continue → shop exit → Willie challenge → nonpersistent
preview → Bite/Reflect → Bite/Confusion → WIN → pending encounter → re-entry and
cancel → field → START+SELECT → Homebrew Launcher → HOME → FTPD.
NTR video stayed active; no reboot or NTR reload was needed during this route.
Confusion in this binary uses the explicitly incomplete fallback presentation.

The silent MP4 is a 160-second excerpt beginning at encoded recording second 320.
It contains actual top and bottom NTR images sampled at 10 fps, including repeated
frames. It is neither an audio recording nor a measurement of game frame rate.
The original recording contains 6,903 samples over 691.844 seconds, one initial
stale sample, zero decode errors, and a successful encoder exit. Its SHA-256 is
`0b7f1db088ec4e45ed4d20a070e72f1952bd986336e537e901d62ac4ca2d7e4f`.
Both original and excerpt passed full FFmpeg decode validation.

| Image | Original sample | UTC | Received top / bottom counters |
| --- | ---: | --- | --- |
| reflect-edge.png | 3270 | 03:28:46.021107 | 5393 / 5396 |
| reflect-ring.png | 3310 | 03:28:50.021112 | 5464 / 5468 |
| bite-fangs.png | 3360 | 03:28:55.021621 | 5550 / 5554 |
| bite-damage.png | 3370 | 03:28:56.028629 | 5568 / 5571 |
| preview-win.png | 4680 | 03:31:07.914740 | 7784 / 7793 |
| field-return.png | 5000 | 03:31:40.559534 | 8326 / 8336 |
| clean-exit.png | 6120 | 03:33:32.555761 | 10164 / 10175 |

All selected samples report both screens fresh. Sample numbers count the recorder's
output, not game-render frames. The first Reflect burst is clipped at the lower
screen edge; these images alone do not identify both targets or qualify complete
effect geometry. The trace records sequential completed instances for owner 1
and owner 0, with distinct sound-544 cue identities 1 and 2. The second cue
reaches actual NDSP sample 115886, wave status 3, before completion.

Both Bite defender sequences complete at source tick 380 with 82 emitted
particles, peak 81 and 325 cosmetic RNG draws. Each HP/sound gate fires at tick 22.
The two preview guards cover 122,832 live-body bytes, report unchanged hashes and
zero play-time advancement. The retrieved GCI remains SHA-256
`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`.
The trace ends `boot_end result=1 error=`. Trace: 591,220 bytes, SHA-256
`b0143fbb0a80bada2afeb3717a6b16787a6785875266463c8591f2a9ff4aae67`.

Logged maxima: 142,948 command words out of 262,144; 123,868 particle command
words; 1,369 live particles; 18,369,528 GPU buffer bytes; 98,410 µs draw CPU and
193,796 µs sampled full CPU frame time. Minimum logged linear free memory is
12,059,520 bytes. These are sampled diagnostics, not exhaustive frame maxima.
The recording briefly shows GPU-busy recovery during the field transition;
the trace confirms subsequent queue drain and successful return/re-entry/exit.
Performance and retail visual parity remain unaccepted.

Raw trace, complete recording, per-sample freshness metadata, readback manifests,
private source assets and the original save remain private. `manifest.json`
hashes the published media and records their source identities.
