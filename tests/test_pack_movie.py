import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools/pack_movie.py"
SPEC = importlib.util.spec_from_file_location("pack_movie", PATH)
pack = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = pack
SPEC.loader.exec_module(pack)


def pixel(frame, x, y):
    return (x * 131 + y * 7919 + frame * 40503) & 0xFFFF


def raw_frame(frame):
    return struct.pack("<%dH" % (pack.WIDTH * pack.HEIGHT),
                       *(pixel(frame, x, y) for y in range(pack.HEIGHT) for x in range(pack.WIDTH)))


def v1_movie(frames, pcm):
    audio_offset = pack.HEADER_SIZE + frames * pack.FRAME_BYTES
    header = struct.pack("<4s15I", b"C3V1", 1, audio_offset + len(pcm), 320, 240, frames,
                         30000, 1001, 64, pack.FRAME_BYTES, audio_offset, len(pcm) // 4,
                         32000 if pcm else 0, 2 if pcm else 0, 1, 0)
    return header + b"".join(raw_frame(i) for i in range(frames)) + pcm


class PackMovieTests(unittest.TestCase):
    def test_framebuffer_order_is_columns_bottom_row_first(self):
        ordered = struct.unpack("<%dH" % (320 * 240), pack.to_framebuffer(raw_frame(3)))
        for x, y in ((0, 0), (319, 0), (0, 239), (123, 45), (319, 239)):
            self.assertEqual(ordered[x * 240 + 239 - y], pixel(3, x, y))

    def test_rejects_other_versions_and_layouts(self):
        good = v1_movie(1, b"")[:64]
        pack.read_header(good)
        for offset, value in ((4, 2), (56, 2), (36, 1), (40, 65)):
            bad = bytearray(good)
            struct.pack_into("<I", bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pack.read_header(bytes(bad))

    def test_lz4_literal_block_and_overlapping_match_decode(self):
        for size in (1, 14, 15, 16, 269, 270, 1000):
            data = bytes((i * 7) & 0xFF for i in range(size))
            self.assertEqual(pack.lz4_decode(pack.lz4_literal_block(data), size), data)
        # 2 literals, an offset-2 match of 4 + 15 + 3, then a 1-byte literal run.
        block = bytes([(2 << 4) | 15, 0x34, 0x12, 2, 0, 3, 1 << 4, 0x56])
        self.assertEqual(pack.lz4_decode(block, 25), b"4" * 12 + b"V")
        with self.assertRaises(ValueError):
            pack.lz4_decode(bytes([(1 << 4) | 0, 0x11, 5, 0, 0x10, 0x22]), 7)

    @unittest.skipUnless(shutil.which("lz4"), "needs the lz4 command")
    def test_lz4_packed_movie_round_trips_with_index_and_pcm(self):
        pcm = bytes(range(16))
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "in.c3v", Path(temporary) / "out.c3v"
            source.write_bytes(v1_movie(2, pcm))
            pack.pack_movie(source, output, level=9, jobs=2)
            data = output.read_bytes()
            words = struct.unpack_from("<4s15I", data)
            self.assertEqual((words[1], words[14]), (2, 3))
            offsets = struct.unpack_from("<3I", data, 64)
            self.assertEqual(offsets[2], words[10])
            self.assertEqual(data[words[10]:], pcm)
            for frame in range(2):
                self.assertEqual(pack.lz4_decode(data[offsets[frame]:offsets[frame + 1]], pack.FRAME_BYTES),
                                 pack.to_framebuffer(raw_frame(frame)))

    @unittest.skipUnless(shutil.which("zstd"), "needs the zstd command")
    def test_packed_movie_round_trips_with_index_and_pcm(self):
        pcm = bytes(range(16))
        with tempfile.TemporaryDirectory() as temporary:
            source, output = Path(temporary) / "in.c3v", Path(temporary) / "out" / "out.c3v"
            source.write_bytes(v1_movie(2, pcm))
            record = pack.pack_movie(source, output, level=3, jobs=2, codec="zstd")
            data = output.read_bytes()
            words = struct.unpack_from("<4s15I", data)
            self.assertEqual(words[:2], (b"C3V1", 2))
            self.assertEqual(words[2], len(data))
            self.assertEqual(words[14:], (2, 0))
            offsets = struct.unpack_from("<3I", data, 64)
            self.assertEqual(offsets[0], 64 + 12)
            self.assertEqual(offsets[2], words[10])
            self.assertEqual(data[words[10]:], pcm)
            for frame in range(2):
                packed = data[offsets[frame]:offsets[frame + 1]]
                decoded = subprocess.run(["zstd", "-d", "-c", "-q"], input=packed,
                                         stdout=subprocess.PIPE, check=True).stdout
                self.assertEqual(decoded, pack.to_framebuffer(raw_frame(frame)))
            self.assertEqual(record["frame_count"], 2)
            with self.assertRaisesRegex(ValueError, "never overwritten"):
                pack.pack_movie(source, output, level=3, codec="zstd")


if __name__ == "__main__":
    unittest.main()
