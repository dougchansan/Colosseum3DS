#!/usr/bin/env python3
"""Repack a private C3V1 version 1 movie (raw RGB565) as version 2.

Version 2 stores every frame as one zstd frame, already in the 3DS top-screen
framebuffer order (320 columns of 240 pixels, bottom row first), behind a
frame offset index; the PCM section is copied unchanged. The pixels are
bit-identical to version 1: every packed frame is decompressed again and
compared before the output is renamed into place. The runtime then reads
about a third of the bytes per frame and copies each decoded frame into the
framebuffer without a per-pixel transpose (docs/boot-media-format.md).

    python3 tools/pack_movie.py private/media/openingdemo.c3v private/pt-movie/boot/openingdemo.c3v

Needs the `zstd` command (as tools/compress_packs.py does). An existing
output is never overwritten.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

WIDTH, HEIGHT = 320, 240
HEADER_SIZE = 64
FRAME_BYTES = WIDTH * HEIGHT * 2
MAX_FRAMES = 18000
MAX_FILE_BYTES = 1024 * 1024 * 1024
RAW_FORMAT, PACKED_FORMAT = 1, 2
LEVEL = 19
BATCH = 256


def read_header(data):
    if len(data) != HEADER_SIZE:
        raise ValueError("Truncated C3V1 header")
    words = struct.unpack("<4s15I", data)
    (magic, version, total, width, height, frames, fps_num, fps_den, video_offset,
     frame_bytes, audio_offset, samples, rate, channels, pixel_format, reserved) = words
    if (magic != b"C3V1" or version != 1 or (width, height) != (WIDTH, HEIGHT)
            or pixel_format != RAW_FORMAT or reserved or video_offset != HEADER_SIZE
            or frame_bytes != FRAME_BYTES or not 1 <= frames <= MAX_FRAMES
            or audio_offset != HEADER_SIZE + frames * FRAME_BYTES
            or total != audio_offset + samples * 4
            or (samples and (rate, channels) != (32000, 2))
            or (not samples and (rate, channels) != (0, 0))):
        raise ValueError("Input is not a valid C3V1 version 1 movie")
    return {"total": total, "frames": frames, "fps": (fps_num, fps_den),
            "audio_offset": audio_offset, "samples": samples, "rate": rate,
            "channels": channels}


def to_framebuffer(frame):
    """Row-major RGB565 to framebuffer order: column x holds rows 239..0."""
    pixels = memoryview(frame).cast("H")
    last = (HEIGHT - 1) * WIDTH
    return b"".join(pixels[last + x::-WIDTH].tobytes() for x in range(WIDTH))


def _zstd(arguments, data):
    return subprocess.run(["zstd", "-q", "-c", *arguments], input=data,
                          stdout=subprocess.PIPE, check=True).stdout


def pack_frame(frame, level=LEVEL):
    ordered = to_framebuffer(frame)
    # --stream-size records the decoded size in the frame header; the
    # runtime decodes each frame in one call into a stable buffer.
    packed = _zstd(["-%d" % level, "--no-check", "--stream-size=%d" % FRAME_BYTES], ordered)
    if _zstd(["-d"], packed) != ordered:
        raise ValueError("Packed frame does not round-trip")
    return packed


def encode_header(info, total, audio_offset):
    return struct.pack("<4s15I", b"C3V1", 2, total, WIDTH, HEIGHT, info["frames"],
                       info["fps"][0], info["fps"][1], HEADER_SIZE, FRAME_BYTES, audio_offset,
                       info["samples"], info["rate"], info["channels"], PACKED_FORMAT, 0)


def file_digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pack_movie(source, output, level=LEVEL, jobs=None):
    source, output = Path(source), Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new output path; existing media is never overwritten")
    with source.open("rb") as stream:
        info = read_header(stream.read(HEADER_SIZE))
        stream.seek(0, os.SEEK_END)
        if stream.tell() != info["total"]:
            raise ValueError("Input length differs from its header")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = output.with_name("." + output.name + ".partial")
    frames = info["frames"]
    first = HEADER_SIZE + 4 * (frames + 1)
    offsets, largest = [first], 0
    try:
        with source.open("rb") as stream, stage.open("xb") as out, \
                ThreadPoolExecutor(max_workers=jobs or os.cpu_count() or 4) as pool:
            out.write(bytes(first))
            stream.seek(HEADER_SIZE)
            for start in range(0, frames, BATCH):
                count = min(BATCH, frames - start)
                raw = [stream.read(FRAME_BYTES) for _ in range(count)]
                if any(len(frame) != FRAME_BYTES for frame in raw):
                    raise ValueError("Truncated input frame")
                for packed in pool.map(lambda frame: pack_frame(frame, level), raw):
                    out.write(packed)
                    largest = max(largest, len(packed))
                    offsets.append(offsets[-1] + len(packed))
            audio_offset = offsets[-1]
            remaining = info["samples"] * 4
            while remaining:
                chunk = stream.read(min(1 << 20, remaining))
                if not chunk:
                    raise ValueError("Truncated input audio")
                out.write(chunk)
                remaining -= len(chunk)
            total = out.tell()
            if total > MAX_FILE_BYTES:
                raise ValueError("Packed movie exceeds the runtime size bound")
            out.seek(0)
            out.write(encode_header(info, total, audio_offset))
            out.write(struct.pack("<%dI" % len(offsets), *offsets))
        stage.rename(output)
    finally:
        if stage.exists():
            stage.unlink()
    return {"source": source.name, "source_bytes": info["total"],
            "output": output.name, "bytes": total, "sha256": file_digest(output),
            "format": "C3V1 version 2 (zstd frames, framebuffer order)", "zstd_level": level,
            "frame_count": frames, "largest_frame_bytes": largest,
            "video_bytes": audio_offset - first,
            "video_ratio": round((audio_offset - first) / (frames * FRAME_BYTES), 4),
            "audio_sample_frames": info["samples"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="C3V1 version 1 movie")
    parser.add_argument("output", type=Path, help="new version 2 movie path")
    parser.add_argument("--level", type=int, default=LEVEL, help="zstd level (default 19)")
    parser.add_argument("--jobs", type=int, default=None, help="parallel zstd processes")
    args = parser.parse_args()
    try:
        record = pack_movie(args.source, args.output, args.level, args.jobs)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Movie packing failed: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
