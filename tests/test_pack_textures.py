"""GPU-native texture repacking checks on synthetic C3T packs."""
import importlib.util
from pathlib import Path
import struct
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


try:
    import numpy  # noqa: F401  (pack_textures needs it; the gcc host image has none)
except ImportError:
    numpy = None
TITLE = load("prepare_title")
PACK = load("pack_textures") if numpy is not None else None


def morton8(x, y):
    return ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
            ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))


def runtime_rgba8(width, height, rgba):
    """Byte-for-byte mirror of platform/3ds/title_renderer.c write_texture_pixels."""
    out = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            tile = ((y >> 3) * (width >> 3) + (x >> 3)) * 64
            o = (tile + morton8(x, y)) * 4
            i = ((height - 1 - y) * width + x) * 4
            out[o:o + 4] = bytes((rgba[i + 3], rgba[i + 2], rgba[i + 1], rgba[i]))
    return bytes(out)


def pattern(width, height, alpha=None):
    data = bytearray()
    for y in range(height):
        for x in range(width):
            data += bytes(((x * 37 + y * 11) & 255, (x * 5 + y * 71) & 255, (x ^ y) * 9 & 255,
                           255 if alpha is None else alpha(x, y)))
    return bytes(data)


def pack(textures):
    vertex = (1., 2., 3., 0., 1., 0., .25, .75, 11, 22, 33, 255, 0)
    batch = (0, 3, 0, 1, 4, 3, 0, 0, *([1.] * 9), 50., 1., 1., 0, 0, 7, 7, 0, 3, 0, 0)
    light = (4, 0, 1., 1., 1., 0., 0., 0., 0., 0., 0., 0., 1., 1., 0., 0.)
    frame = struct.pack("<36f", *(1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.),
                        *(1., 1., 0., 0., 1., 1., 0., 0., 0., 1., 1., 1.),
                        *(0., 0., 5., 0., 0., 0., 0., 45., .1, 100., 4 / 3, 0.))
    return TITLE.encode_pack(1, [vertex] * 3, [batch], textures, [(0, 2, 1, 0)], [light], frame * 2, fps=60.)


def descriptors(data):
    offset, count = struct.unpack_from("<I", data, 52)[0], struct.unpack_from("<I", data, 24)[0]
    return [struct.unpack_from("<6I", data, offset + i * 24) for i in range(count)]


@unittest.skipIf(numpy is None, "numpy is unavailable")
class PackTexturesTests(unittest.TestCase):
    def setUp(self):
        self.opaque = pattern(32, 16)
        self.cutout = pattern(8, 8, lambda x, y: 0 if (x + y) & 1 else 255)
        self.source = pack([(32, 16, self.opaque, 1, 0), (8, 8, self.cutout, 2, 1)])

    def test_rest_of_pack_is_unchanged(self):
        out, _ = PACK.repack(self.source)
        texture_offset = struct.unpack_from("<I", self.source, 52)[0]
        pixel_offset = struct.unpack_from("<I", self.source, 68)[0]
        self.assertEqual(out[112], 1)
        self.assertEqual(out[113:128], bytes(15))
        self.assertEqual(out[:8], self.source[:8])
        self.assertEqual(out[12:76], self.source[12:76])
        self.assertEqual(out[80:112], self.source[80:112])
        self.assertEqual(out[128:texture_offset], self.source[128:texture_offset])
        self.assertEqual(out[texture_offset + 48:pixel_offset], self.source[texture_offset + 48:pixel_offset])
        self.assertEqual(struct.unpack_from("<I", out, 8)[0], len(out))
        self.assertEqual(struct.unpack_from("<I", out, 76)[0], len(out) - pixel_offset)

    def test_alpha_texture_keeps_runtime_rgba8_tiling(self):
        out, report = PACK.repack(self.source)
        w, h, offset, size, wrap_s, wrap_t = descriptors(out)[1]
        self.assertEqual((w, h, size, wrap_s, wrap_t), (8, 8, 256, 2, 1))  # RGBA8, one level
        self.assertEqual(out[offset:offset + size], runtime_rgba8(8, 8, self.cutout))
        self.assertEqual(report[1]["format"], "rgba8")

    def test_opaque_texture_is_rgb565_with_box_mips(self):
        out, report = PACK.repack(self.source)
        w, h, offset, size, wrap_s, wrap_t = descriptors(out)[0]
        self.assertEqual((w, h, wrap_s & 0xFF, wrap_s >> 8 & 0xFF, wrap_s >> 16, wrap_t), (32, 16, 1, 3, 1, 0))
        self.assertEqual(size, 32 * 16 * 2 + 16 * 8 * 2)
        self.assertEqual(offset, descriptors(out)[0][2])
        level0 = struct.unpack_from("<%dH" % (32 * 16), out, offset)

        def texel(level, width, height, x, y):
            """Value at top-down (x, y) of a tiled RGB565 level."""
            gy = height - 1 - y
            return level[((gy >> 3) * (width >> 3) + (x >> 3)) * 64 + morton8(x, gy)]

        def q(r, g, b):
            return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | (b * 31 + 127) // 255

        for x, y in ((0, 0), (31, 0), (0, 15), (13, 7), (31, 15)):
            i = (y * 32 + x) * 4
            self.assertEqual(texel(level0, 32, 16, x, y), q(*self.opaque[i:i + 3]))
        level1 = struct.unpack_from("<%dH" % (16 * 8), out, offset + 32 * 16 * 2)
        for x, y in ((0, 0), (15, 7), (6, 3)):
            avg = [(sum(self.opaque[((2 * y + dy) * 32 + 2 * x + dx) * 4 + c]
                        for dy in (0, 1) for dx in (0, 1)) + 2) // 4 for c in range(3)]
            self.assertEqual(texel(level1, 16, 8, x, y), q(*avg))
        self.assertEqual(report[0], {"index": 0, "source": [32, 16], "packed": [32, 16], "format": "rgb565",
                                     "levels": 2, "bytes_before": 2048, "bytes_after": 1280})

    def test_max_dimension_drops_leading_levels(self):
        big = pack([(64, 64, pattern(64, 64), 0, 0)])
        full, _ = PACK.repack(big)
        half, report = PACK.repack(big, max_dimension=32)
        fw, fh, foff, fsize, fwrap, _ = descriptors(full)[0]
        hw, hh, hoff, hsize, hwrap, _ = descriptors(half)[0]
        self.assertEqual((fw, fh, fwrap >> 16, hw, hh, hwrap >> 16), (64, 64, 3, 32, 32, 2))
        self.assertEqual(half[hoff:hoff + hsize], full[foff + 64 * 64 * 2:foff + fsize])
        self.assertEqual(report[0]["packed"], [32, 32])

    def test_cutouts_and_eight_texel_images_are_not_downscaled(self):
        data = pack([(16, 16, pattern(16, 16, lambda x, y: 128), 0, 0), (8, 8, pattern(8, 8), 0, 0)])
        _, report = PACK.repack(data, max_dimension=128)
        out, _ = PACK.repack(data, max_dimension=128)
        self.assertEqual([r["packed"] for r in report], [[16, 16], [8, 8]])
        self.assertEqual([r["levels"] for r in report], [1, 1])

    def test_deterministic_and_refuses_double_encoding(self):
        first, _ = PACK.repack(self.source)
        self.assertEqual(first, PACK.repack(self.source)[0])
        with self.assertRaises(ValueError):
            PACK.repack(first)
        with self.assertRaises(ValueError):
            PACK.repack(self.source[:-1])


if __name__ == "__main__":
    unittest.main()
