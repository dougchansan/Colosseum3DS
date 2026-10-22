import importlib.util
import io
from fractions import Fraction
from pathlib import Path
import struct
import sys
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools/prepare_boot_media.py"
SPEC = importlib.util.spec_from_file_location("prepare_boot_media", PATH)
media = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = media
SPEC.loader.exec_module(media)


class BootMediaTests(unittest.TestCase):
    def test_header_has_canonical_video_and_audio_boundaries(self):
        data = media.encode_header(75, Fraction(30000, 1001), 320000)
        words = struct.unpack("<4s15I", data)
        self.assertEqual(len(data), 64)
        self.assertEqual(words[:8], (b"C3V1", 1, 11840064, 320, 240, 75, 30000, 1001))
        self.assertEqual(words[8:], (64, 153600, 11520064, 80000, 32000, 2, 1, 0))

    def test_no_audio_has_zero_format_and_video_end_offset(self):
        words = struct.unpack("<4s15I", media.encode_header(130, Fraction(30000, 1001), 0))
        self.assertEqual(words[2], 19968064)
        self.assertEqual(words[10:], (19968064, 0, 0, 0, 1, 0))

    def test_bounds_and_partial_pcm_are_rejected(self):
        for frames, rate, audio in ((0, Fraction(30000, 1001), 0),
                                    (131, Fraction(30000, 1001), 0),
                                    (75, Fraction(30), 0),
                                    (75, Fraction(30000, 1001), 3),
                                    (75, Fraction(30000, 1001), 640004)):
            with self.subTest(frames=frames, rate=rate, audio=audio):
                with self.assertRaises(ValueError):
                    media.encode_header(frames, rate, audio)

    def test_decoder_overrun_is_not_written(self):
        output = io.BytesIO()
        with self.assertRaisesRegex(ValueError, "bound"):
            media.copy_bounded(io.BytesIO(b"abcde"), output, 4)
        self.assertEqual(output.getvalue(), b"")

    def test_decoder_truncation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "frame count"):
            media.copy_bounded(io.BytesIO(b"abc"), io.BytesIO(), 4, expected=4)

    def test_stream_copy_preserves_chunk_boundaries(self):
        source = bytes(range(256)) * 300
        output = io.BytesIO()
        self.assertEqual(media.copy_bounded(io.BytesIO(source), output, len(source), len(source)), len(source))
        self.assertEqual(output.getvalue(), source)


if __name__ == "__main__":
    unittest.main()
