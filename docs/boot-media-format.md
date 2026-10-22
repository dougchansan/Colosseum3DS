# Native boot movie stream (C3V1)

`tools/prepare_boot_media.py` admits the reviewed GC6E01 revision 0 `main.dol`,
`movie/tpc.thp`, and `movie/gs_logo.thp` by hashes before invoking FFmpeg. It
converts only these two original boot movies. The animated title is a separate
GS/HSD scene; this container does not replace it with rendered video.

```sh
python3 tools/prepare_boot_media.py --disc /path/to/extracted-disc --output /path/to/new-private-media
```

Outputs are private asset files. They must not be committed or distributed with
the homebrew. FFmpeg is needed only for preparation on the host. No frames are
bundled in this repository, and no whole movie is loaded into native RAM.

## Layout

All header fields after the magic are unsigned little-endian 32-bit integers.
The header is exactly 64 bytes (`<4s15I`). There are no alignment gaps.

| Offset | Field | Required value |
|---:|---|---|
| 0 | Magic | `C3V1` |
| 4 | Version | 1 |
| 8 | Total file bytes | End of PCM data, or end of video when absent |
| 12 | Width | 320 |
| 16 | Height | 240 |
| 20 | Frame count | 1–130; reviewed sources use 75 and 130 |
| 24 | FPS numerator | 30000 |
| 28 | FPS denominator | 1001 |
| 32 | Video offset | 64 |
| 36 | Bytes per frame | 153600 |
| 40 | Audio offset | 64 + frame count × 153600 |
| 44 | Audio sample frames | PCM bytes ÷ 4, or zero |
| 48 | Audio sample rate | 32000, or zero when absent |
| 52 | Audio channels | 2, or zero when absent |
| 56 | Pixel format | 1 = RGB565LE |
| 60 | Reserved | Zero |

Video is tightly packed, top row first, pixels left to right. Each pixel is a
little-endian word with red in bits 11–15, green in 5–10, and blue in 0–4. The
native framebuffer adapter is responsible for the 3DS display orientation and
centering the 320×240 picture in the 400×240 upper screen. Keeping the original
4:3 aspect ratio preserves the movie's composition.

PCM follows the last video frame, without a WAV header: signed 16-bit
little-endian samples, left then right for every sample frame. Readers may hold
separate file handles for sequential video and audio reads. Silent movies have
zero sample count/rate/channels and their audio offset equals total file size.

The host uses Lanczos downsampling from 640×480 to 320×240, RGB565 quantization,
and nominal 30000/1001 cadence. Retail stores 29.969999313 as a float in its THP
header; normalization to the NTSC rational differs by about one part per
million. The original float is recorded in the manifest. No frames are dropped.
`tpc` has a 32 kHz
stereo THP ADPCM stream, decoded to PCM; `gs_logo` has no audio stream. The
manifest records source and output hashes, dimensions, exact frame count, and
sample count. The two movies require about 32 MB on SD, with only one 153600-byte
video frame and bounded PCM buffers needed at a time.

## Validation boundaries

Both source movie hashes are checked before starting either decoder. Output
frame byte counts must exactly equal the source THP frame count. Extra frames,
truncation, an odd PCM sample frame, decoder failure, and PCM exceeding five
seconds are rejected. All conversion takes place in a temporary directory; the
destination directory appears only after both movies and the manifest succeed.
Existing output directories are never overwritten.

These are offline conversion and format checks. They do not establish 3DS SD
bandwidth, audiovisual synchronization, or hardware playback performance; those
require a native run.
