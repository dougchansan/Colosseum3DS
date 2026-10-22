#!/usr/bin/env python3
"""Convert verified private GC6E01 boot THPs to bounded native C3V1 streams.

The runtime reads RGB565 frames and optional stereo PCM; FFmpeg is an offline
dependency only. This tool admits only the two reviewed boot movies. It does
not replace the animated title scene, opening demo, or attract mode.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN_DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
WIDTH, HEIGHT = 320, 240
HEADER_SIZE = 64
FRAME_BYTES = WIDTH * HEIGHT * 2
MAX_FRAMES = 130
MAX_AUDIO_BYTES = 4 * 32000 * 5
MAX_INPUT_BYTES = 3 * 1024 * 1024
SOURCES = {
    "tpc": {
        "sha256": "711c7cc495e189f1aa72e2751852d956d632d391b660f21efc8a60b6f4d6a483",
        "bytes": 1953856, "frames": 75, "audio": True,
    },
    "gs_logo": {
        "sha256": "5455a6edec0188329f75e4ce12135f5462218bd95bf5d2311fdc6c39787d8556",
        "bytes": 2375888, "frames": 130, "audio": False,
    },
}


def file_digest(path, algorithm="sha256"):
    h = hashlib.new(algorithm)
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_disc(disc):
    disc = Path(disc)
    for relative in ("", "sys", "sys/boot.bin", "sys/main.dol", "files", "files/movie"):
        if (disc / relative).is_symlink():
            raise ValueError("Symbolic links are not accepted in boot-media inputs")
    with (disc / "sys/boot.bin").open("rb") as stream:
        if stream.read(8) != b"GC6E01\0\0":
            raise ValueError("Expected GC6E01 disc 1 revision 0")
    if (disc / "sys/main.dol").stat().st_size != 3779808:
        raise ValueError("Unexpected main.dol size")
    if file_digest(disc / "sys/main.dol", "sha1") != MAIN_DOL_SHA1:
        raise ValueError("main.dol differs from the pinned original")
    return disc.resolve(strict=True)


def admit_movie(path, profile, *, max_input_bytes=MAX_INPUT_BYTES):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size != profile["bytes"]:
        raise ValueError("Movie input size or path differs from the reviewed source")
    if path.stat().st_size > max_input_bytes or file_digest(path) != profile["sha256"]:
        raise ValueError("Movie differs from the reviewed GC6E01 revision 0 source")
    with path.open("rb") as stream:
        header = stream.read(80)
    if len(header) != 80 or header[:4] != b"THP\0":
        raise ValueError("Invalid THP header")
    version = struct.unpack_from(">I", header, 4)[0]
    fps = struct.unpack_from(">f", header, 16)[0]
    frames = struct.unpack_from(">I", header, 20)[0]
    if version != 0x11000 or frames != profile["frames"] or not math.isfinite(fps):
        raise ValueError("Unexpected THP version, frame count, or timing")
    # Retail writes the float spelling of 29.97, not an exact NTSC rational.
    # Use the agreed nominal NTSC cadence and record the original float below.
    rate = Fraction(30000, 1001)
    if abs(fps - 29.97) > 0.00001:
        raise ValueError("Unsupported source movie frame rate")
    return frames, rate


def encode_header(frame_count, fps, audio_bytes, *, max_frames=MAX_FRAMES,
                  max_audio_bytes=MAX_AUDIO_BYTES):
    if not isinstance(frame_count, int) or not 1 <= frame_count <= max_frames:
        raise ValueError("Movie frame count exceeds the supported bound")
    if fps != Fraction(30000, 1001):
        raise ValueError("Unsupported movie timing")
    if not isinstance(audio_bytes, int) or not 0 <= audio_bytes <= max_audio_bytes or audio_bytes % 4:
        raise ValueError("Invalid stereo PCM length")
    audio_offset = HEADER_SIZE + frame_count * FRAME_BYTES
    return struct.pack(
        "<4s15I", b"C3V1", 1, audio_offset + audio_bytes,
        WIDTH, HEIGHT, frame_count, fps.numerator, fps.denominator,
        HEADER_SIZE, FRAME_BYTES, audio_offset, audio_bytes // 4,
        32000 if audio_bytes else 0, 2 if audio_bytes else 0, 1, 0,
    )


def copy_bounded(stream, output, limit, expected=None):
    """Copy a finite decoder stream without buffering the movie in RAM."""
    count = 0
    while True:
        chunk = stream.read(min(65536, limit - count + 1))
        if not chunk:
            break
        count += len(chunk)
        if count > limit:
            raise ValueError("Decoded media exceeds its declared bound")
        output.write(chunk)
    if expected is not None and count != expected:
        raise ValueError("Decoded video frame count differs from its THP header")
    return count


def decode_to(command, output, limit, expected=None):
    # stderr goes to a temporary file so a decoder error cannot deadlock a full
    # pipe while stdout is being drained. It is never included in asset data.
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors)
        try:
            count = copy_bounded(process.stdout, output, limit, expected)
            return_code = process.wait()
            if return_code:
                errors.seek(0)
                message = errors.read(4096).decode("utf-8", "replace").strip()
                raise ValueError("FFmpeg decode failed: " + message)
            return count
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()


def convert_movie(source, output, profile, ffmpeg, *, max_input_bytes=MAX_INPUT_BYTES,
                  max_frames=MAX_FRAMES, max_audio_bytes=MAX_AUDIO_BYTES):
    frame_count, fps = admit_movie(source, profile, max_input_bytes=max_input_bytes)
    # Validate the declared extent before opening an output or starting FFmpeg.
    encode_header(frame_count, fps, 0, max_frames=max_frames,
                  max_audio_bytes=max_audio_bytes)
    base = [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
            "-threads", "1", "-filter_threads", "1", "-i", str(source)]
    video = base + ["-map", "0:v:0", "-an", "-vf", "scale=320:240:flags=lanczos",
                    "-fps_mode", "passthrough", "-pix_fmt", "rgb565le",
                    "-f", "rawvideo", "pipe:1"]
    with Path(output).open("xb") as out:
        out.write(bytes(HEADER_SIZE))
        video_bytes = frame_count * FRAME_BYTES
        decode_to(video, out, video_bytes, expected=video_bytes)
        audio_bytes = 0
        if profile["audio"]:
            audio = base + ["-map", "0:a:0", "-vn", "-acodec", "pcm_s16le",
                            "-ar", "32000", "-ac", "2", "-f", "s16le", "pipe:1"]
            audio_bytes = decode_to(audio, out, max_audio_bytes)
            if not audio_bytes:
                raise ValueError("The source movie's audio decoded to an empty stream")
        out.seek(0)
        out.write(encode_header(frame_count, fps, audio_bytes, max_frames=max_frames,
                                max_audio_bytes=max_audio_bytes))
    return {
        "source": source.name, "source_sha256": profile["sha256"],
        "output": Path(output).name, "sha256": file_digest(output),
        "bytes": Path(output).stat().st_size, "width": WIDTH, "height": HEIGHT,
        "frame_count": frame_count, "fps_num": fps.numerator,
        "fps_den": fps.denominator, "pixel_format": "RGB565LE",
        "source_thp_fps_float": 29.969999313354492,
        "audio_sample_frames": audio_bytes // 4,
        "sample_rate": 32000 if audio_bytes else 0,
        "channels": 2 if audio_bytes else 0,
    }


def prepare(disc, output, ffmpeg):
    disc = verify_disc(disc)
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new output directory; existing media is never overwritten")
    resolved = output.resolve()
    if ROOT == resolved or ROOT in resolved.parents:
        if ROOT / "private" not in resolved.parents:
            raise ValueError("Repository media outputs must be under ignored private/")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Verify both inputs before starting either potentially expensive decoder.
    for name, profile in SOURCES.items():
        admit_movie(disc / "files/movie" / (name + ".thp"), profile)
    with tempfile.TemporaryDirectory(prefix=".c3-media-", dir=output.parent) as temporary:
        stage = Path(temporary) / "media"
        stage.mkdir()
        records = [convert_movie(disc / "files/movie" / (name + ".thp"),
                                 stage / (name + ".c3v"), profile, ffmpeg)
                   for name, profile in SOURCES.items()]
        manifest = {
            "format": "C3V1", "disc": "GC6E01", "revision": 0,
            "main_dol_sha1": MAIN_DOL_SHA1,
            "source_commit": "51bfe4314a218dfaf9592fc80723c50498d0ee55",
            "sequence": ["tpc.c3v", "gs_logo.c3v", "native animated title"],
            "conversion": "Lanczos 640x480 to 320x240, RGB565LE, nominal NTSC 30000/1001 cadence; PCM16LE stereo where present",
            "movies": records,
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="new private output directory")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error("FFmpeg is required on the host")
    try:
        result = prepare(args.disc, args.output, args.ffmpeg)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Boot media preparation failed: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
