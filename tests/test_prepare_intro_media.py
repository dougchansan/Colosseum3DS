import importlib.util
from fractions import Fraction
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import prepare_boot_media as media
import prepare_intro_media as intro


class IntroMediaTests(unittest.TestCase):
    def test_intro_has_separate_explicit_limits(self):
        with self.assertRaises(ValueError):
            media.encode_header(1926, Fraction(30000, 1001), 0)
        data = media.encode_header(1926, Fraction(30000, 1001), 8233148,
                                   max_frames=intro.LIMITS["max_frames"],
                                   max_audio_bytes=intro.LIMITS["max_audio_bytes"])
        fields = struct.unpack("<4s15I", data)
        self.assertEqual(fields[2], 304066812)
        self.assertEqual(fields[10], 295833664)
        self.assertEqual(fields[11:14], (2058287, 32000, 2))
        with self.assertRaises(ValueError):
            media.encode_header(1927, Fraction(30000, 1001), 0,
                                max_frames=intro.LIMITS["max_frames"])

    def test_admission_limit_cannot_replace_identity_check(self):
        data = bytearray(80)
        struct.pack_into(">4sI", data, 0, b"THP\0", 0x11000)
        struct.pack_into(">fI", data, 16, 29.97, 1926)
        profile = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                   "frames": 1926, "audio": True}
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "openingdemo.thp"
            source.write_bytes(data)
            self.assertEqual(media.admit_movie(source, profile, max_input_bytes=80),
                             (1926, Fraction(30000, 1001)))
            with self.assertRaises(ValueError):
                media.admit_movie(source, profile, max_input_bytes=79)
            data[-1] ^= 1
            source.write_bytes(data)
            with self.assertRaises(ValueError):
                media.admit_movie(source, profile, max_input_bytes=80)

    def test_declared_frame_overrun_fails_before_decoder_or_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "bad.c3v"
            with mock.patch.object(media, "admit_movie", return_value=(1927, Fraction(30000, 1001))), \
                 mock.patch.object(media, "decode_to") as decoder:
                with self.assertRaises(ValueError):
                    media.convert_movie(Path("source"), output, intro.SOURCE, "ffmpeg",
                                        **intro.LIMITS)
                decoder.assert_not_called()
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
