#!/usr/bin/env python3
"""Halve a finished C3T pack's sampled animation frames (every other frame).

Packs store each clip as frames sampled at the pack's fps; the runtime
(src/title.c animate) picks the two frames around the current time and
interpolates linearly. Keeping frames 0, 2, 4, ... of every clip (and always
its last frame) and halving fps keeps each clip's timing: a clip with an even
frame count ends one original frame (1/fps s) later, since its last frame is
kept. Loop clips keep their first and last frames.

Measured on the prepared NPC packs (2026-11-13): most clips change too fast
between 60 fps samples for a halfway blend (rotation entries off by 0.28 at
the median pack and up to ~2, none of it from hidden zero-scale parts), so
only about 1 pack in 9 passes at tolerance 0.05. thin() therefore refuses a
pack whose dropped frames leave the tolerance.

Only the clip table, the frame section and the absolute texture pixel
offsets that follow it change; everything else is copied byte for byte.
Packs with few frames (static geometry) or an fps already below the floor
are returned unchanged.
"""
import argparse
from pathlib import Path
import struct

HEADER_BYTES = 128
CLIP_BYTES = 16
TEXTURE_BYTES = 24
MIN_FPS = 12.0


def thin(pack, min_frames=16, tolerance=0.02):
    """Return (pack, changed)."""
    if len(pack) < HEADER_BYTES or pack[:3] != b"C3T" or pack[3:4] not in b"1234":
        raise ValueError("Not a C3T1..C3T4 pack")
    counts = list(struct.unpack_from("<7I", pack, 12))  # bones vertices batches textures clips frames lights
    fps, = struct.unpack_from("<f", pack, 40)
    offsets = list(struct.unpack_from("<7I", pack, 44))
    frame_bytes, pixel_bytes = struct.unpack_from("<2I", pack, 72)
    total, = struct.unpack_from("<I", pack, 8)
    clips, frames = counts[4], counts[5]
    if total != len(pack) or offsets[6] + pixel_bytes != total or offsets[5] + frames * frame_bytes != offsets[6]:
        raise ValueError("Pack extents are inconsistent")
    if frames < min_frames or fps / 2 < MIN_FPS:
        return pack, False
    # A dropped frame is replaced by the halfway blend of its neighbours;
    # where any differs from that blend by more than the tolerance (a snap,
    # a part shown or hidden, a fast turn) the whole pack keeps its rate.
    import numpy as np
    data = np.frombuffer(pack, dtype="<f4", count=frames * frame_bytes // 4,
                         offset=offsets[5]).reshape(frames, frame_bytes // 4)
    worst = 0.0
    for c in range(clips):
        start, count, _loop, _extra = struct.unpack_from("<4I", pack, offsets[3] + c * CLIP_BYTES)
        if count >= 3:
            block = data[start:start + count]
            odd = block[1:count - 1:2]
            blend = (block[0:count - 2:2] + block[2:count:2]) * 0.5
            worst = max(worst, float(np.nanmax(np.abs(odd - blend[:len(odd)]))) if len(odd) else 0.0)
    if not worst <= tolerance:
        return pack, False
    table, kept_frames, first = bytearray(), bytearray(), 0
    for c in range(clips):
        start, count, loop, extra = struct.unpack_from("<4I", pack, offsets[3] + c * CLIP_BYTES)
        keep = list(range(0, count, 2))
        if keep[-1] != count - 1:
            keep.append(count - 1)
        for k in keep:
            at = offsets[5] + (start + k) * frame_bytes
            kept_frames.extend(pack[at:at + frame_bytes])
        table.extend(struct.pack("<4I", first, len(keep), loop, extra))
        first += len(keep)
    delta = len(kept_frames) - frames * frame_bytes
    out = bytearray(pack[:offsets[5]])
    out[offsets[3]:offsets[3] + clips * CLIP_BYTES] = table
    out.extend(kept_frames)
    out.extend(pack[offsets[6]:])
    struct.pack_into("<I", out, 8, total + delta)
    struct.pack_into("<I", out, 32, first)
    struct.pack_into("<f", out, 40, fps / 2)
    struct.pack_into("<I", out, 44 + 6 * 4, offsets[6] + delta)
    for t in range(counts[3]):
        at = offsets[2] + t * TEXTURE_BYTES + 8
        pixel_offset, = struct.unpack_from("<I", out, at)
        struct.pack_into("<I", out, at, pixel_offset + delta)
    return bytes(out), True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    packed, changed = thin(args.input.read_bytes())
    args.output.write_bytes(packed)
    print("thinned" if changed else "unchanged")


if __name__ == "__main__":
    main()
