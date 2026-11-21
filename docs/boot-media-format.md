# Native boot movie stream (C3V1)

`tools/prepare_boot_media.py` admits the reviewed GC6E01 revision 0 `main.dol`,
`movie/tpc.thp`, and `movie/gs_logo.thp` by hashes before invoking FFmpeg. It
converts only these two original boot movies. `tools/prepare_intro_media.py`
separately admits and converts `openingdemo.thp` (1,926 frames) using the same
bounded streaming format. The animated title is a separate
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
| 4 | Version | 1 (2: packed frames, below) |
| 8 | Total file bytes | End of PCM data, or end of video when absent |
| 12 | Width | 320 |
| 16 | Height | 240 |
| 20 | Frame count | Runtime bound 1–18,000; reviewed sources use 75, 130, 1,926 and 5,250 (staffroll); total file bound is 1 GiB |
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

## Version 2: packed frames (what the SD card should carry)

Version 1 streams 153,600 bytes per frame, 4.6 MB/s at 29.97 fps, and the 3DS
then transposed every pixel into its column-major framebuffer. On a New 3DS
the opening showed only 1,258 of 1,926 frames (`movie_end ... displayed=1258
skipped=668`). `tools/pack_movie.py` repacks a version 1 file losslessly:

```sh
python3 tools/pack_movie.py private/intro/openingdemo.c3v private/packed/openingdemo.c3v
```

The 64-byte header keeps the version 1 field order with these differences:

| Offset | Field | Version 2 value |
|---:|---|---|
| 4 | Version | 2 |
| 32 | Video offset | 64: the frame index |
| 36 | Bytes per frame | 153600 once decoded |
| 40 | Audio offset | End of the last packed frame |
| 56 | Pixel format | 2 = RGB565LE in framebuffer order, one zstd frame per video frame |

The index at offset 64 holds frame count + 1 little-endian u32 file offsets.
The first is 64 + 4 × (frame count + 1). Offsets strictly increase, so frame
*i* occupies `[index[i], index[i+1])`. Each packed frame is at most
`ZSTD_COMPRESSBOUND(153600)`, and the last offset equals the audio offset. Each
packed frame decodes to exactly 153,600 bytes in the top-screen framebuffer
order: 320 columns of 240 pixels, with image row *y* stored at 239 − *y*. That
is one contiguous copy into the 400×240 framebuffer at column 40. The
PCM section is unchanged. The tool decompresses every packed frame again and
compares it with the transposed source before renaming the output into place,
so the packed pixels are bit-identical to version 1 and there is no visible
loss.

Sizes of the packed reviewed sources (zstd level 19, `--no-check`):

| Movie | Version 1 bytes | Version 2 bytes | Video ratio | Largest frame |
|---|---:|---:|---:|---:|
| `tpc` | 11,840,672 | 1,269,762 | 0.082 | 45,049 |
| `gs_logo` | 19,968,064 | 1,370,370 | 0.069 | 30,635 |
| `openingdemo` | 304,066,812 | 120,015,696 | 0.378 | 99,212 |
| `staffroll` | 806,400,064 | 73,352,725 | 0.091 | 31,628 |

The runtime still accepts version 1. Its player
(`platform/3ds/movie_player.c`) works as a pipeline. A reader thread on the main
core, below the main thread, fills a ring of four stored frames from SD with
unbuffered whole-frame reads. A decode thread unpacks them into three
framebuffer-ordered slots. On a New 3DS that thread runs on the second
application core. The main thread copies the frame that is due and keeps the
audio queue and the vblank pace. Frames that are already late are skipped
without being read or decoded. The clock and the audio start once the first
frame is on screen. Each movie adds `movie_feed` and `movie_timing` trace lines.
They record the pack version, the decoder core, the pre-roll, frames produced
and dropped (before reading and before decoding), vblanks whose frame was late,
average and maximum read/decode/copy microseconds, and bytes read.

## Validation boundaries

Both source movie hashes are checked before starting either decoder. Output
frame byte counts must exactly equal the source THP frame count. Extra frames,
truncation, an odd PCM sample frame, decoder failure, and PCM exceeding five
seconds for the two short logo sources are rejected. The separate opening
converter permits PCM only within its declared movie duration and checks the
exact source profile; its output is 304,066,812 bytes. All conversion takes place in a temporary directory; the
destination directory appears only after both movies and the manifest succeed.
Existing output directories are never overwritten.

These are offline conversion and format checks. They do not establish 3DS SD
bandwidth, audiovisual synchronization, or hardware playback performance; those
require a native run.
