#!/usr/bin/env python3
"""Re-encode a finished C3T pack's textures for the 3DS GPU.

The prepare tools write every texture as top-down RGBA8 rows, which the
renderer swizzles at load into 4-byte PICA texels with a single level. The
field's large GameCube images (1024x1024 CMPR, 0.5 byte/texel on the
GameCube) then cost 4 MiB each and are always sampled from the full-size level,
even when a wall covers a few dozen pixels of the 400x240 top screen.

This pass rewrites only the texture section and sets header byte 112
("texture encoding") to 1. Each descriptor's wrap_s word then also carries
the PICA texture format (bits 8..15) and mip level count minus one
(bits 16..23), and the pixel bytes are the exact GPU image: 8x8 Morton tiles,
rows bottom-up, levels contiguous from level 0, as citro3d lays out a
mipmapped C3D_Tex. The renderer copies them unchanged.

* Opaque images become RGB565 (2 bytes/texel) with a full box-filtered mip
  chain down to 8 texels. CMPR's endpoints are themselves RGB565, so the
  colour loss is limited to its interpolated midpoints.
* Images with any alpha below 255 keep RGBA8 and a single level: alpha-tested
  cutouts would erode under averaged mips, and translucent ramps keep their
  8-bit alpha.
* --max-dimension drops leading mip levels so no level 0 exceeds that size
  (a pack-time downscale, made from the same box filter).

Everything else in the pack is copied byte for byte; the result is
deterministic.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import sys

import numpy as np

HEADER_BYTES = 128
ENCODING_BYTE = 112
GPU_RGBA8, GPU_RGB565 = 0x0, 0x3
BYTES_PER_TEXEL = {GPU_RGBA8: 4, GPU_RGB565: 2}


def morton_table():
    table = np.zeros((8, 8), dtype=np.int64)
    for y in range(8):
        for x in range(8):
            table[y, x] = ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
                           ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))
    return table


MORTON = morton_table()


def tile_order(width, height):
    """Index into a bottom-up flattened image for each GPU texel slot."""
    ys, xs = np.mgrid[0:height, 0:width]
    slot = ((ys >> 3) * (width >> 3) + (xs >> 3)) * 64 + MORTON[ys & 7, xs & 7]
    order = np.empty(width * height, dtype=np.int64)
    order[slot.ravel()] = (ys * width + xs).ravel()
    return order


def tiled(level_rgba, fmt):
    """level_rgba is (h, w, 4) uint8 top row first; returns PICA bytes."""
    height, width = level_rgba.shape[:2]
    flat = level_rgba[::-1].reshape(-1, 4)[tile_order(width, height)]
    if fmt == GPU_RGBA8:
        return flat[:, ::-1].astype(np.uint8).tobytes()  # A, B, G, R in memory
    r = (flat[:, 0].astype(np.uint32) * 31 + 127) // 255
    g = (flat[:, 1].astype(np.uint32) * 63 + 127) // 255
    b = (flat[:, 2].astype(np.uint32) * 31 + 127) // 255
    return ((r << 11) | (g << 5) | b).astype("<u2").tobytes()


def mip_chain(rgba, count):
    levels = [rgba]
    for _ in range(count - 1):
        prior = levels[-1].astype(np.uint32)
        half = (prior[0::2, 0::2] + prior[1::2, 0::2] + prior[0::2, 1::2] + prior[1::2, 1::2] + 2) // 4
        levels.append(half.astype(np.uint8))
    return levels


def max_levels(width, height):
    return (min(width, height).bit_length() - 1) - 3 + 1


def encode_texture(width, height, rgba_bytes, max_dimension):
    rgba = np.frombuffer(rgba_bytes, dtype=np.uint8).reshape(height, width, 4)
    opaque = bool((rgba[:, :, 3] == 255).all())
    fmt = GPU_RGB565 if opaque else GPU_RGBA8
    drop = 0
    while (width >> drop) > max_dimension or (height >> drop) > max_dimension:
        drop += 1
    if drop and (not opaque or min(width, height) >> drop < 8):
        drop = 0  # cutouts and tiny images are never downscaled
    chain = mip_chain(rgba, max_levels(width, height) if opaque else 1)[drop:]
    data = b"".join(tiled(level, fmt) for level in chain)
    return chain[0].shape[1], chain[0].shape[0], fmt, len(chain), data


def repack(pack, max_dimension=1024):
    if len(pack) < HEADER_BYTES or pack[:3] != b"C3T" or pack[3:4] not in b"1234":
        raise ValueError("Not a C3T1..C3T4 pack")
    if any(pack[ENCODING_BYTE:HEADER_BYTES]):
        raise ValueError("Pack textures are already GPU-encoded")
    total, = struct.unpack_from("<I", pack, 8)
    counts = struct.unpack_from("<7I", pack, 12)
    offsets = list(struct.unpack_from("<7I", pack, 44))
    pixel_bytes, = struct.unpack_from("<I", pack, 76)
    if total != len(pack) or offsets[6] + pixel_bytes != total:
        raise ValueError("Pack extents are inconsistent")
    descriptors, pixels, report = bytearray(), bytearray(), []
    cursor = offsets[6]
    for i in range(counts[3]):
        width, height, offset, size, wrap_s, wrap_t = struct.unpack_from("<6I", pack, offsets[2] + i * 24)
        if offset != cursor or size != width * height * 4 or wrap_s > 2 or wrap_t > 2:
            raise ValueError("Noncanonical texture %d" % i)
        cursor += size
        w, h, fmt, levels, data = encode_texture(width, height, pack[offset:offset + size], max_dimension)
        descriptors.extend(struct.pack("<6I", w, h, offsets[6] + len(pixels), len(data),
                                       wrap_s | fmt << 8 | (levels - 1) << 16, wrap_t))
        pixels.extend(data)
        report.append({"index": i, "source": [width, height], "packed": [w, h],
                       "format": {GPU_RGBA8: "rgba8", GPU_RGB565: "rgb565"}[fmt],
                       "levels": levels, "bytes_before": size, "bytes_after": len(data)})
    if cursor != total:
        raise ValueError("Trailing texture bytes")
    header = bytearray(pack[:HEADER_BYTES])
    struct.pack_into("<I", header, 8, offsets[6] + len(pixels))
    struct.pack_into("<I", header, 76, len(pixels))
    header[ENCODING_BYTE] = 1
    body = pack[HEADER_BYTES:offsets[2]] + bytes(descriptors) + pack[offsets[2] + counts[3] * 24:offsets[6]]
    return bytes(header) + body + bytes(pixels), report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--max-dimension", type=int, default=1024, choices=(128, 256, 512, 1024))
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args(argv)
    source = args.input.read_bytes()
    packed, report = repack(source, args.max_dimension)
    args.output.write_bytes(packed)
    summary = {"input": args.input.name, "input_sha256": hashlib.sha256(source).hexdigest(),
               "output_sha256": hashlib.sha256(packed).hexdigest(), "max_dimension": args.max_dimension,
               "bytes_before": len(source), "bytes_after": len(packed), "textures": report}
    if args.report:
        args.report.write_text(json.dumps(summary, indent=1) + "\n", newline="\n")
    print("%s: %d -> %d bytes" % (args.input.name, len(source), len(packed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
